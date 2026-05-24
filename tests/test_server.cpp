#include "test_server.h"

#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#endif

#include <cstdio>

TestServer::TestServer() = default;

TestServer::~TestServer() {
    Stop();
}

std::string TestServer::FindServerPath() const {
    // 从构建目录的二进制文件查找
    // 优先从测试 exe 所在目录推导
    std::string paths[] = {
        "D:/Code/MiniTSDB/build/release/minitsdb.exe",
        "./minitsdb.exe",
        "../build/release/minitsdb.exe",
        "./minitsdb",
        "../build/release/minitsdb",
    };

    for (const auto& p : paths) {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return p;
#else
        if (access(p.c_str(), X_OK) == 0)
            return p;
#endif
    }
    return "minitsdb"; // fallback: rely on PATH
}

int TestServer::FindFreePort() const {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 0;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // 系统自动分配

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        WSACleanup();
        return 0;
    }

    int len = sizeof(addr);
    getsockname(s, (sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);
    closesocket(s);
    return port;
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(s);
        return 0;
    }

    socklen_t len = sizeof(addr);
    getsockname(s, (sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);
    close(s);
    return port;
#endif
}

bool TestServer::WaitForReady(int timeout_ms) const {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(static_cast<u_short>(port_));

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            closesocket(s);
            return true;
        }
        closesocket(s);
#else
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(static_cast<uint16_t>(port_));

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            close(s);
            return true;
        }
        close(s);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool TestServer::Start(int port, int timeout_ms) {
    if (running_) return false;

    // 确定端口
    if (port <= 0) port = FindFreePort();
    if (port <= 0) port = 9180; // fallback
    port_ = port;

    std::string exe_path = FindServerPath();

    // 写入临时配置文件（服务端通过 -c 读取配置）
    std::string config_path = "./test_server_port.conf";
    std::string port_str = std::to_string(port_);
    {
        std::string conf = "server.port=" + port_str + "\n";
        FILE* f = fopen(config_path.c_str(), "w");
        if (f) { fwrite(conf.c_str(), 1, conf.size(), f); fclose(f); }
    }

    std::string cmdline = "\"" + exe_path + "\" -c \"" + config_path + "\"";
    std::fprintf(stderr, "TestServer: Starting %s\n", cmdline.c_str());

#ifdef _WIN32
    STARTUPINFOA si = {};
    si.cb = sizeof(si);

    // 创建进程时不显示控制台窗口
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // CreateProcess 需要可修改的 cmdline
    char* cmd_buf = _strdup(cmdline.c_str());

    BOOL ok = CreateProcessA(
        nullptr,           // 应用程序名
        cmd_buf,           // 命令行
        nullptr,           // 进程安全属性
        nullptr,           // 线程安全属性
        FALSE,             // 不继承句柄
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,  // 创建标志
        nullptr,           // 环境变量
        nullptr,           // 当前目录
        &si,
        &proc_info_
    );

    free(cmd_buf);

    if (!ok) {
        std::fprintf(stderr, "TestServer: Failed to start server (CreateProcess error %lu)\n",
                     GetLastError());
        return false;
    }
#else
    child_pid_ = fork();
    if (child_pid_ < 0) {
        std::fprintf(stderr, "TestServer: fork failed\n");
        return false;
    }

    if (child_pid_ == 0) {
        // 子进程：启动服务端
        // 重定向 stdout/stderr 到 /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        execl(exe_path.c_str(), exe_path.c_str(), "-c", config_path.c_str(), nullptr);
        // 如果 execl 返回，说明失败
        _exit(1);
    }
#endif

    // 等待服务端就绪
    if (!WaitForReady(timeout_ms)) {
        DWORD exit_code = 0;
        GetExitCodeProcess(proc_info_.hProcess, &exit_code);
        std::fprintf(stderr, "TestServer: Server exit code: %lu\n", exit_code);
        Stop();
        return false;
    }

    running_ = true;
    return true;
}

void TestServer::Stop() {
    if (!running_) return;
    running_ = false;

#ifdef _WIN32
    if (proc_info_.hProcess) {
        // 发送 Ctrl+Break 信号给进程组
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, proc_info_.dwProcessId);

        // 等待最多 3 秒让进程优雅退出
        DWORD wait_result = WaitForSingleObject(proc_info_.hProcess, 3000);

        if (wait_result == WAIT_TIMEOUT) {
            // 强制终止
            TerminateProcess(proc_info_.hProcess, 1);
            WaitForSingleObject(proc_info_.hProcess, 1000);
        }

        CloseHandle(proc_info_.hProcess);
        CloseHandle(proc_info_.hThread);
        proc_info_.hProcess = nullptr;
        proc_info_.hThread = nullptr;
    }
#else
    if (child_pid_ > 0) {
        // 发送 SIGTERM
        kill(child_pid_, SIGTERM);

        // 等待最多 3 秒
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            // 未退出，再等 2 秒
            for (int i = 0; i < 20; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                result = waitpid(child_pid_, &status, WNOHANG);
                if (result != 0) break;
            }
            if (result == 0) {
                // 强制终止
                kill(child_pid_, SIGKILL);
                waitpid(child_pid_, &status, 0);
            }
        }
        child_pid_ = -1;
    }
#endif

    std::fprintf(stderr, "TestServer: Server stopped\n");
}

// ============================================================
// C 语言包装函数（供 test_c_sdk.c 等纯 C 文件使用）
// ============================================================

TestServer& GetGlobalTestServer() {
    static TestServer s_instance;
    return s_instance;
}

int test_server_start(int port_hint, int timeout_ms) {
    auto& srv = GetGlobalTestServer();
    if (srv.Start(port_hint, timeout_ms)) {
        return srv.Port();
    }
    return 0;
}

void test_server_stop() {
    auto& srv = GetGlobalTestServer();
    srv.Stop();
}
