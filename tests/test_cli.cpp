#include <gtest/gtest.h>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

// CLI 集成测试：启动服务端，用子进程运行 CLI 工具验证输出
class CliTest : public ::testing::Test {
protected:
    std::string cli_path_;
    std::string svr_path_;
    int port_;

    void SetUp() override {
        cli_path_ = "D:/Code/MiniTSDB/build/release/minitsdb_cli.exe";
        svr_path_ = "D:/Code/MiniTSDB/build/release/minitsdb.exe";
        static int next_port = 9180;
        port_ = next_port++;
    }

    std::string RunCLI(const std::string& args) {
        std::string cmd = "\"" + cli_path_ + "\" --port " + std::to_string(port_) + " " + args + " 2>&1";
        std::string result;
        char buf[4096];
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "popen failed";
        while (fgets(buf, sizeof(buf), pipe)) {
            result += buf;
        }
        pclose(pipe);
        return result;
    }
};

TEST_F(CliTest, HelpOutput) {
    std::string out = RunCLI("--help");
    EXPECT_NE(out.find("MiniTSDB CLI Client"), std::string::npos);
    EXPECT_NE(out.find("--host"), std::string::npos);
    EXPECT_NE(out.find("--port"), std::string::npos);
    EXPECT_NE(out.find("--format"), std::string::npos);
}

TEST_F(CliTest, DISABLED_InsertAndQuery) {
    // 需要运行中的服务端，手动启用
    // 启动服务端:
    //   D:/Code/MiniTSDB/build/release/minitsdb.exe &
    // 然后运行:
    //   ./build/release/test_cli.exe --gtest_filter="CliTest.InsertAndQuery"
    //
    // std::string out = RunCLI("-e \"INSERT INTO t (tag, value) VALUES ('x', 1)\"");
    // EXPECT_NE(out.find("ERROR"), std::string::npos) << out;
}

TEST_F(CliTest, DISABLED_FormatTable) {
    // 需要运行中的服务端
}

TEST_F(CliTest, DISABLED_FormatJson) {
    // 需要运行中的服务端
}
