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

#include "../../../../external/libcoro/test/catch_amalgamated.hpp"

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

static void ui_append_line(const char* line) {
    std::lock_guard<std::mutex> lk(g_sink_mutex);
    if (g_log_file) {
        std::fputs(line, g_log_file);
        std::fputc('\n', g_log_file);
        std::fflush(g_log_file);
    }
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

// In-process runner for libcoro tests using Catch2 main. We cannot include test main TU, so we emulate CLI.
// We link libcoro test object files via CMake and then call Catch2 session API.

// Forward declare Catch2 Session to avoid including huge amalgamation here again; tests already include it.
namespace Catch { class Session; }

// Contract: run_all_tests executes Catch2 test session and returns exit code; it must not throw.
static int run_all_tests_with_output(const std::string& files_dir) noexcept {
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

    int code = 2; // non-zero by default
    try {
        // Build args: verbose, no colour, timestamps
    std::vector<const char*> argv;
    argv.push_back("coroTest");
    argv.push_back("-r"); argv.push_back("console");
    argv.push_back("--colour"); argv.push_back("no");
    argv.push_back("-s"); // show successful
    argv.push_back("--durations=yes");
    // Exclude TLS server tests that require external openssl binary
    argv.push_back("*~[tls_server]");

        Catch::Session session; // uses global registry linked from tests
        session.configData().benchmarkNoAnalysis = true; // speed on mobile
        session.configData().shardCount = 1; session.configData().shardIndex = 0;
        code = session.applyCommandLine(static_cast<int>(argv.size()), argv.data());
        if (code == 0) {
            code = session.run();
        }
    } catch (const std::exception& e) {
        ui_append_line((std::string("Exception: ") + e.what()).c_str());
        code = 3;
    } catch (...) {
        ui_append_line("Unknown exception");
        code = 4;
    }

    out.flush_to_ui();
    err.flush_to_ui();

    {
        std::lock_guard<std::mutex> lk(g_sink_mutex);
        if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
    }
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
    return static_cast<jint>(rc);
}
