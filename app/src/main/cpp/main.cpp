#include <coro/coro.hpp>
#include <iostream>
#include <android/log.h>

using namespace coro;

coro::task<void> test_networking_tls() {
#if defined(LIBCORO_FEATURE_NETWORKING) && defined(LIBCORO_FEATURE_TLS)
    try {
        auto scheduler = io_scheduler::make_shared();
        auto tls_ctx   = std::make_shared<net::tls::context>();

        net::dns::resolver resolver{scheduler, std::chrono::seconds{5}};
        auto dns_result = co_await resolver.host_by_name(net::hostname{"www.google.com"});
        if (dns_result->status() != net::dns::status::complete || dns_result->ip_addresses().empty()) {
            __android_log_print(ANDROID_LOG_ERROR, "coroTest", "DNS resolve failed");
            co_return;
        }
        auto address = dns_result->ip_addresses().front();

    net::tls::client::options opts{ .address = address, .port = 443 };
        net::tls::client client{scheduler, tls_ctx, opts};
        auto cstatus = co_await client.connect(std::chrono::seconds{10});
        if (cstatus != net::tls::connection_status::connected) {
            __android_log_print(ANDROID_LOG_ERROR, "coroTest", "TLS connect failed status=%d", (int)cstatus);
            co_return;
        }

        std::string request = "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n";
        auto send_result = co_await client.send(request);
        if (send_result.first != net::tls::send_status::ok) {
            __android_log_print(ANDROID_LOG_ERROR, "coroTest", "Send failed status=%d", (int)send_result.first);
        }

        std::array<char, 2048> buf{};
        auto recv_result = co_await client.recv(buf);
        if (recv_result.first == net::tls::recv_status::ok) {
            __android_log_print(ANDROID_LOG_INFO, "coroTest", "Recv %zu bytes", recv_result.second.size());
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "coroTest", "Recv failed status=%d", (int)recv_result.first);
        }
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "coroTest", "Exception: %s", e.what());
    }
#else
    __android_log_print(ANDROID_LOG_INFO, "coroTest", "Networking/TLS disabled at compile time");
#endif
    co_return; }

extern "C" JNIEXPORT void JNICALL
Java_com_example_libcorotest_MainActivity_runTest(JNIEnv*, jobject) {
    // Одноразовый запуск
    try {
        coro::sync_wait(test_networking_tls());
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "coroTest", "sync_wait exception: %s", e.what());
    }
}
