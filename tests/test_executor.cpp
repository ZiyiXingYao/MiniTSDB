#include <gtest/gtest.h>
#include "sql/executor.h"
#include "sql/parser.h"
#include "storage/engine.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include <filesystem>

using namespace minitsdb;

class ExecutorTest : public ::testing::Test {
protected:
    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    std::unique_ptr<Executor> executor_;
    SQLParser parser_;

    void SetUp() override {
        StorageConfig sc;
        sc.hot_path = "./test_exec_data/hot";
        sc.cold_path = "./test_exec_data/cold";
        engine_ = std::make_shared<StorageEngine>(sc);
        engine_->Init();
        cache_ = std::make_shared<LatestCache>();
        auth_ = std::make_shared<AuthManager>();
        auth_->Init("./test_exec_data/hot");
        executor_ = std::make_unique<Executor>(engine_, cache_, auth_);
    }

    void TearDown() override {
        engine_->Close();
        std::error_code ec;
        std::filesystem::remove_all("./test_exec_data", ec);
    }
};

TEST_F(ExecutorTest, InsertAndLatest) {
    auto res = executor_->ExecuteSQL("INSERT INTO test (tag, value) VALUES ('demo', 42.0)");
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.affected_rows, 1);

    res = executor_->ExecuteSQL("SELECT * FROM test WHERE tag='demo' LATEST");
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.rows.size(), 1);
    if (!res.rows.empty()) {
        EXPECT_EQ(res.rows[0][0], "demo");
    }
}

TEST_F(ExecutorTest, InsertBatch) {
    auto res = executor_->ExecuteSQL(
        "INSERT INTO sensors (tag, value) VALUES ('s1', 1.0), ('s2', 2.0)");
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.affected_rows, 2);
}

TEST_F(ExecutorTest, CreateTag) {
    auto res = executor_->ExecuteSQL(
        "CREATE TAG boiler_temp (type='analog', unit='celsius')");
    ASSERT_TRUE(res.ok);
}

TEST_F(ExecutorTest, InvalidSQL) {
    auto res = executor_->ExecuteSQL("INVALID SQL");
    EXPECT_FALSE(res.ok);
}

TEST_F(ExecutorTest, CreateUser) {
    auto res = executor_->ExecuteSQL(
        "CREATE USER engineer1 WITH PASSWORD 'pass123' ROLE 'operator'");
    ASSERT_TRUE(res.ok) << res.error_msg;
    EXPECT_EQ(res.affected_rows, 1);
}

TEST_F(ExecutorTest, EmptyQueryReturnsNone) {
    auto res = executor_->ExecuteSQL("SELECT * FROM empty WHERE tag='x' LATEST");
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.rows.size(), 0);
}
