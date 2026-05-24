#include <gtest/gtest.h>
#include "test_server.h"
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
    static TestServer s_server;
    std::string cli_path_;
    int port_;

    static void SetUpTestSuite() {
        ASSERT_TRUE(s_server.Start(0, 10000))
            << "Failed to start minitsdb server";
    }

    static void TearDownTestSuite() {
        s_server.Stop();
    }

    void SetUp() override {
        cli_path_ = "D:/Code/MiniTSDB/build/release/minitsdb_cli.exe";
        port_ = s_server.Port();
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

TestServer CliTest::s_server;

TEST_F(CliTest, HelpOutput) {
    std::string out = RunCLI("--help");
    EXPECT_NE(out.find("MiniTSDB CLI Client"), std::string::npos);
    EXPECT_NE(out.find("--host"), std::string::npos);
    EXPECT_NE(out.find("--port"), std::string::npos);
    EXPECT_NE(out.find("--format"), std::string::npos);
}

TEST_F(CliTest, InsertAndQuery) {
    // INSERT 一条记录
    std::string insert_out = RunCLI("-e \"INSERT INTO t (tag, value) VALUES ('x', 123)\"");
    EXPECT_EQ(insert_out.find("ERROR"), std::string::npos) << insert_out;

    // SELECT LATEST 验证
    std::string sel_out = RunCLI("-e \"SELECT * FROM t WHERE tag='x' LATEST\"");
    EXPECT_NE(sel_out.find("x"), std::string::npos) << sel_out;
    EXPECT_NE(sel_out.find("123"), std::string::npos) << sel_out;
}

TEST_F(CliTest, FormatTable) {
    std::string out = RunCLI("--format table -e \"SELECT * FROM t WHERE tag='x' LATEST\"");
    EXPECT_NE(out.find("tag"), std::string::npos) << out;
    EXPECT_NE(out.find("value"), std::string::npos) << out;
    EXPECT_NE(out.find("|"), std::string::npos) << out;  // 表格边框
}

TEST_F(CliTest, FormatJson) {
    std::string out = RunCLI("--format json -e \"SELECT * FROM t WHERE tag='x' LATEST\"");
    EXPECT_NE(out.find("\"tag\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"value\""), std::string::npos) << out;
}
