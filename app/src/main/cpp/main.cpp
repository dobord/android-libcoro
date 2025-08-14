#include <android/log.h>
#include <coro/coro.hpp>

#include <iostream>

// Logging helpers
#ifndef LOG_TAG
#    define LOG_TAG "coroTest"
#endif

#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)

using namespace coro;

// VS Code IntelliSense fallback: if the build system hasn't injected the
// feature macros yet, define them so the guarded code isn't greyed out.
#ifdef __INTELLISENSE__
#    ifndef LIBCORO_FEATURE_NETWORKING
#        define LIBCORO_FEATURE_NETWORKING
#    endif
#    ifndef LIBCORO_FEATURE_TLS
#        define LIBCORO_FEATURE_TLS
#    endif
#endif

coro::task<void> test_networking_tls()
{
#if defined(LIBCORO_FEATURE_NETWORKING) && defined(LIBCORO_FEATURE_TLS)
    try {
        auto scheduler = default_executor::io_executor();
        co_await scheduler->schedule(); // Ensure execution on shared io_scheduler before any I/O
        auto tls_ctx = std::make_shared<net::tls::context>();

        net::dns::resolver resolver{scheduler, std::chrono::seconds{5}};
        auto dns_result = co_await resolver.host_by_name(net::hostname{"www.google.com"});
        if (dns_result->status() != net::dns::status::complete || dns_result->ip_addresses().empty()) {
            LOGE("DNS resolve failed");
            co_return;
        }
        auto address = dns_result->ip_addresses().front();

        net::tls::client::options opts{.address = address, .port = 443};
        net::tls::client client{scheduler, tls_ctx, opts};
        auto cstatus = co_await client.connect(std::chrono::seconds{10});
        if (cstatus != net::tls::connection_status::connected) {
            LOGE("TLS connect failed status=%d", (int)cstatus);
            co_return;
        }

        std::string request = "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n";
        auto send_result = co_await client.send(request);
        if (send_result.first != net::tls::send_status::ok) {
            LOGE("Send failed status=%d", (int)send_result.first);
        }

        std::array<char, 2048> buf{};
        auto recv_result = co_await client.recv(buf);
        if (recv_result.first == net::tls::recv_status::ok) {
            LOGI("Recv %zu bytes", recv_result.second.size());
        } else {
            LOGE("Recv failed status=%d", (int)recv_result.first);
        }
    } catch (const std::exception &e) {
        LOGE("Exception: %s", e.what());
    }
#else
    LOGI("Networking/TLS disabled at compile time");
#endif
    co_return;
}

extern "C" JNIEXPORT void JNICALL Java_com_example_libcorotest_MainActivity_runTest(JNIEnv *, jobject)
{
    // Single-shot invocation
    try {
        coro::sync_wait(test_networking_tls());
    } catch (const std::exception &e) {
        LOGE("sync_wait exception: %s", e.what());
    }
}
