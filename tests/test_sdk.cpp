#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "proto_gen/minitsdb.grpc.pb.h"
#include "sdk/minitsdb.h"
#include "server/grpc_server.h"
#include "storage/engine.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace minitsdb;

// C SDK 集成测试：进程内启动服务端，用 C API 连接测试
class SdkTest : public ::testing::Test {
protected:
    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    std::unique_ptr<GrpcServer> server_;
    int port_;
    minitsdb_conn* conn_ = nullptr;

    void SetUp() override {
        static int next_port = 9196;
        port_ = next_port++;

        StorageConfig sc;
        sc.hot_path = "./test_sdk_data/hot";
        sc.cold_path = "./test_sdk_data/cold";
        engine_ = std::make_shared<StorageEngine>(sc);
        engine_->Init();

        cache_ = std::make_shared<LatestCache>();
        auth_ = std::make_shared<AuthManager>();
        auth_->Init("./test_sdk_data/hot");

        GrpcServerConfig srv_cfg;
        srv_cfg.port = port_;
        server_ = std::make_unique<GrpcServer>(srv_cfg, engine_, cache_, auth_);
        ASSERT_TRUE(server_->Start());

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::string host = "127.0.0.1";
        conn_ = minitsdb_connect(host.c_str(), port_, "admin", "admin123");
        ASSERT_NE(conn_, nullptr);
    }

    void TearDown() override {
        if (conn_) minitsdb_disconnect(conn_);
        if (server_) server_->Stop();
        engine_->Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::error_code ec;
        std::filesystem::remove_all("./test_sdk_data", ec);
    }
};

TEST_F(SdkTest, ConnectAndQuery) {
    minitsdb_result* res = minitsdb_query(conn_, "SELECT tag FROM test LATEST");
    ASSERT_NE(res, nullptr);
    // 检查错误字符串为空（表示成功）
    const char* err = minitsdb_result_error(res);
    EXPECT_TRUE(err == nullptr || err[0] == '\0') << (err ? err : "");
    EXPECT_GE(minitsdb_result_cols(res), 0);
    EXPECT_GE(minitsdb_result_rows(res), 0);
    minitsdb_result_free(res);
}

TEST_F(SdkTest, InsertCheckResult) {
    // 写入数据
    minitsdb_result* res = minitsdb_query(conn_,
        "INSERT INTO sdk_test (tag, value) VALUES ('sdk-demo', 99.5)");
    ASSERT_NE(res, nullptr);
    const char* err = minitsdb_result_error(res);
    EXPECT_TRUE(err == nullptr || err[0] == '\0') << (err ? err : "");
    minitsdb_result_free(res);

    // 查询
    res = minitsdb_query(conn_,
        "SELECT tag, value FROM sdk_test WHERE tag='sdk-demo' LATEST");
    ASSERT_NE(res, nullptr);
    err = minitsdb_result_error(res);
    EXPECT_TRUE(err == nullptr || err[0] == '\0') << (err ? err : "");

    EXPECT_EQ(minitsdb_result_rows(res), 1);
    EXPECT_EQ(minitsdb_result_cols(res), 3);  // tag, value, ts

    // 验证值
    const char* tag = minitsdb_result_value(res, 0, 0);
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(std::string(tag), "sdk-demo");

    const char* val = minitsdb_result_value(res, 0, 1);
    ASSERT_NE(val, nullptr);

    minitsdb_result_free(res);
}

TEST_F(SdkTest, AccessorFunctions) {
    minitsdb_result* res = minitsdb_query(conn_,
        "INSERT INTO test_abc (tag, value) VALUES ('a', 1.0), ('b', 2.0)");
    ASSERT_NE(res, nullptr);
    const char* err = minitsdb_result_error(res);
    EXPECT_TRUE(err == nullptr || err[0] == '\0') << (err ? err : "");
    minitsdb_result_free(res);

    res = minitsdb_query(conn_,
        "SELECT tag, value FROM test_abc WHERE tag='a' LATEST");
    ASSERT_NE(res, nullptr);
    err = minitsdb_result_error(res);
    EXPECT_TRUE(err == nullptr || err[0] == '\0') << (err ? err : "");

    EXPECT_GE(minitsdb_result_cols(res), 0);
    EXPECT_GE(minitsdb_result_rows(res), 0);

    const char* col = minitsdb_result_col_name(res, 0);
    EXPECT_NE(col, nullptr);
    EXPECT_EQ(std::string(col), "tag");

    err = minitsdb_result_error(res);
    EXPECT_NE(err, nullptr);

    minitsdb_result_free(res);
}

TEST_F(SdkTest, FailedConnect) {
    minitsdb_conn* bad_conn = minitsdb_connect("127.0.0.1", 1, "admin", "admin123");
    EXPECT_EQ(bad_conn, nullptr);
}
