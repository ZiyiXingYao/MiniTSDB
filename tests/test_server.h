#pragma once

#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

// ============================================================
// C 语言兼容接口（供 test_c_sdk.c 等纯 C 文件使用）
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

// 启动服务端，返回实际端口号（失败返回 0）
int test_server_start(int port_hint, int timeout_ms);

// 停止服务端
void test_server_stop();

#ifdef __cplusplus
}
#endif

// ============================================================
// C++ 接口（供 test_cli.cpp 等 C++ 文件使用）
// ============================================================
#ifdef __cplusplus

#include <string>
#include <atomic>
#include <thread>

class TestServer {
public:
    TestServer();
    ~TestServer();

    bool Start(int port = 0, int timeout_ms = 10000);
    void Stop();
    int Port() const { return port_; }
    bool IsRunning() const { return running_; }

private:
    std::string FindServerPath() const;
    int FindFreePort() const;
    bool WaitForReady(int timeout_ms) const;

#ifdef _WIN32
    PROCESS_INFORMATION proc_info_ = {};
#else
    pid_t child_pid_ = -1;
#endif

    int port_ = 0;
    std::atomic<bool> running_{false};
};

// 全局单例辅助（供 C 包装使用）
TestServer& GetGlobalTestServer();

#endif // __cplusplus
