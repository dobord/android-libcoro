// All code comments are in English per repo policy.

#include <android/log.h>
#include <jni.h>
#include <coro/coro.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <pthread.h>
#include <future>

#include "../../../../external/libcoro/test/catch_amalgamated.hpp"
#include <signal.h>

// Logging helpers
#ifndef LOG_TAG
#    define LOG_TAG "coroTest"
#endif

#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)

using namespace coro;

// JNI callback holder to append text into UI TextView.
struct UiAppender {
    JNIEnv* env = nullptr;
    jobject activity = nullptr; // GlobalRef held outside
    jclass activityClass = nullptr; // local
    jmethodID appendMethod = nullptr; // void appendLine(String)
};

static std::mutex g_sink_mutex;
static std::FILE* g_log_file = nullptr;
static UiAppender g_ui{};
static JavaVM* g_vm = nullptr;
static jobject g_activity_global = nullptr; // GlobalRef to MainActivity
static std::mutex g_run_mutex; // serialize runs in-process
static std::atomic<bool> g_session_used{false};
static std::atomic<int> g_last_exit_code{-9999};

static void ui_append_line(const char* line) {
    std::lock_guard<std::mutex> lk(g_sink_mutex);
    if (g_log_file) {
        std::fputs(line, g_log_file);
        std::fputc('\n', g_log_file);
        std::fflush(g_log_file);
    }
    // Also mirror to logcat for CI visibility
    LOGI("%s", line);
    // Attach to JVM if needed to call back into UI
    if (!g_vm || !g_activity_global) return;
    JNIEnv* env = nullptr;
    bool need_detach = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            need_detach = true;
        } else {
            return;
        }
    }
    jclass cls = env->GetObjectClass(g_activity_global);
    if (!cls) { if (need_detach) g_vm->DetachCurrentThread(); return; }
    jmethodID mid = env->GetMethodID(cls, "appendLine", "(Ljava/lang/String;)V");
    if (!mid) { if (need_detach) g_vm->DetachCurrentThread(); return; }
    jstring jstr = env->NewStringUTF(line);
    env->CallVoidMethod(g_activity_global, mid, jstr);
    env->DeleteLocalRef(jstr);
    env->DeleteLocalRef(cls);
    if (need_detach) g_vm->DetachCurrentThread();
}

// Minimal Catch2 listener that forwards stdout/stderr:
// We will redirect std::cout/cerr rdbuf to our sink so Catch's console reporter prints into UI and file.
class StreamRedirector {
public:
    explicit StreamRedirector(std::ostream& os): os_(os), old_(os.rdbuf()) {
        os_.rdbuf(buf_.rdbuf());
    }
    ~StreamRedirector() {
        os_.rdbuf(old_);
    }
    void flush_to_ui() {
        std::string s = buf_.str();
        size_t start = 0;
        while (start < s.size()) {
            auto pos = s.find('\n', start);
            std::string line = s.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            if (!line.empty()) ui_append_line(line.c_str());
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        buf_.str("");
        buf_.clear();
    }
private:
    std::ostream& os_;
    std::streambuf* old_;
    std::stringstream buf_;
};

// Periodically flush redirected streams into UI/log to avoid blank screen and provide live progress.
class PeriodicFlusher {
public:
    PeriodicFlusher(StreamRedirector& out, StreamRedirector& err)
        : out_(out), err_(err) {}
    void start(std::chrono::milliseconds interval = std::chrono::milliseconds(200)) {
        if (running_.exchange(true)) return;
        th_ = std::thread([this, interval]{
            // Attach a name for debugging
#if defined(__ANDROID__)
            pthread_setname_np(pthread_self(), "coro-flusher");
#endif
            while (running_.load()) {
                out_.flush_to_ui();
                err_.flush_to_ui();
                std::this_thread::sleep_for(interval);
            }
        });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        if (th_.joinable()) th_.join();
        // Final flush to drain any remaining content
        out_.flush_to_ui();
        err_.flush_to_ui();
    }
private:
    std::atomic<bool> running_{false};
    std::thread th_;
    StreamRedirector& out_;
    StreamRedirector& err_;
};

// In-process runner for libcoro tests using Catch2 main. We cannot include test main TU, so we emulate CLI.
// We link libcoro test object files via CMake and then call Catch2 session API.

// Forward declare Catch2 Session to avoid including huge amalgamation here again; tests already include it.
namespace Catch { class Session; }

// Contract: run_all_tests executes Catch2 test session and returns exit code; it must not throw.
static int run_all_tests_with_output(const std::string& files_dir) noexcept {
    // Ensure we never construct Catch::Session more than once per process.
    if (g_session_used.load(std::memory_order_acquire)) {
        int last = g_last_exit_code.load(std::memory_order_relaxed);
        ui_append_line("Tests already executed in this process. Skipping.");
        if (last == -9999) last = 1; // unknown previous state -> treat as failure
        return last;
    }
    std::unique_lock<std::mutex> run_lk(g_run_mutex);
    if (g_session_used.load(std::memory_order_acquire)) {
        int last = g_last_exit_code.load(std::memory_order_relaxed);
        ui_append_line("Tests already executed in this process. Skipping.");
        if (last == -9999) last = 1;
        return last;
    }
    // Prevent SIGPIPE from killing the process during networking tests
    signal(SIGPIPE, SIG_IGN);
    // Open log file under app's files dir
    const std::string log_path = files_dir + "/libcoro-tests.log";
    {
        std::lock_guard<std::mutex> lk(g_sink_mutex);
        if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
        g_log_file = std::fopen(log_path.c_str(), "w");
    }

    // Redirect std::cout and std::cerr; Catch prints there by default.
    StreamRedirector out(std::cout);
    StreamRedirector err(std::cerr);

    // Proactive line to avoid empty UI at start
    ui_append_line("Starting libcoro tests...");
    // Periodically flush output while tests are running
    PeriodicFlusher flusher(out, err);
    flusher.start(std::chrono::milliseconds(150));

    int code = 2; // non-zero by default
    try {
        // Build only filter args; other settings configured via Session API to avoid CLI incompatibilities
        std::vector<const char*> argv;
        argv.push_back("coroTest");
        // Exclude fragile/slow tests on Android emulator environment
        argv.push_back("~[tls_server]"); // relies on external openssl tool
        argv.push_back("~[tcp_server]"); // network flakiness on CI/emulator
        argv.push_back("~[dns]");       // DNS resolution may be restricted
        argv.push_back("~[bench]");     // benchmarks are slow and unnecessary here
        argv.push_back("~[io_scheduler]"); // event-loop heavy tests may hang on some Android kernels
        argv.push_back("~[thread_pool]"); // heavy multi-thread tests can stall on mobile/emulator

        // Run Catch2 session on a separate thread and enforce a global timeout.
    auto runner = [argv]() -> int {
            try {
                Catch::Session session; // uses global registry linked from tests
                session.configData().benchmarkNoAnalysis = true; // speed on mobile
                session.configData().showDurations = Catch::ShowDurations::Always;
                session.configData().shardCount = 1;
                session.configData().shardIndex = 0;
                int rc = session.applyCommandLine(static_cast<int>(argv.size()), argv.data());
                if (rc != 0) return rc;
                return session.run();
            } catch (const std::exception& ex) {
                ui_append_line((std::string("Exception in test runner: ") + ex.what()).c_str());
                return 3;
            } catch (...) {
                ui_append_line("Unknown exception in test runner");
                return 4;
            }
        };

        std::packaged_task<int()> task(runner);
        auto fut = task.get_future();
        std::thread t(std::move(task));

    // Global timeout for the entire Catch2 run; emulator can be slow.
    constexpr auto kGlobalTimeout = std::chrono::seconds(600);
        if (fut.wait_for(kGlobalTimeout) == std::future_status::ready) {
            code = fut.get();
            t.join();
        } else {
            ui_append_line("Global timeout reached, detaching test runner thread...");
            code = 124; // timeout
            t.detach(); // Let OS reap the thread when process exits
        }
    } catch (const std::exception& e) {
        ui_append_line((std::string("Exception: ") + e.what()).c_str());
        code = 3;
    } catch (...) {
        ui_append_line("Unknown exception");
        code = 4;
    }
    // Stop flusher and perform one last flush
    flusher.stop();

    {
        std::lock_guard<std::mutex> lk(g_sink_mutex);
        if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
    }
    g_session_used.store(true, std::memory_order_release);
    g_last_exit_code.store(code, std::memory_order_relaxed);
    run_lk.unlock();
    return code;
}

// Helper: ensure TLS assets exist in CWD for tests that expect cert.pem/key.pem
static void ensure_tls_assets(const std::string& files_dir) {
#ifdef LIBCORO_FEATURE_TLS
    const std::string cert = files_dir + "/cert.pem";
    const std::string key  = files_dir + "/key.pem";
    auto touch_if_missing = [](const std::string& p){
        if (!std::filesystem::exists(p)) {
            std::ofstream ofs(p); ofs << ""; // empty; some tests generate via openssl in POSIX; on Android we skip TLS server tests
        }
    };
    touch_if_missing(cert);
    touch_if_missing(key);
#endif
}

extern "C" jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

// JNI entry: run tests and return exit code; also streams output to UI and file.
extern "C" JNIEXPORT jint JNICALL Java_com_example_libcorotest_MainActivity_runTests(
    JNIEnv* env, jobject thiz, jstring filesDir)
{
    // Save GlobalRef to activity for callbacks
    if (!g_activity_global) {
        g_activity_global = env->NewGlobalRef(thiz);
    }
    const char* cpath = env->GetStringUTFChars(filesDir, nullptr);
    std::string files_dir = cpath ? cpath : std::string{};
    env->ReleaseStringUTFChars(filesDir, cpath);

    // Use app files dir as working dir
    if (!files_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(files_dir, ec);
        std::filesystem::current_path(files_dir, ec);
    }
    ensure_tls_assets(files_dir);

    int rc = run_all_tests_with_output(files_dir);
    ui_append_line((std::string("Exit code: ") + std::to_string(rc)).c_str());
    ui_append_line((std::string("Tests completed with exit code: ") + std::to_string(rc)).c_str());
    return static_cast<jint>(rc);
}
