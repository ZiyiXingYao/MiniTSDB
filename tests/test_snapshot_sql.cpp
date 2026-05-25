#include <gtest/gtest.h>
#include "sql/parser.h"

// SQL SELECT FROM SNAPSHOT 解析测试
// 验证解析器正确识别 SNAPSHOT 虚拟表名，不需要运行服务端

TEST(SnapshotSQLTest, ParseSelectAll) {
    minitsdb::SQLParser parser;
    minitsdb::ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM SNAPSHOT", &result));
    ASSERT_TRUE(std::holds_alternative<minitsdb::SelectStmt>(*result.stmt));
    auto& stmt = std::get<minitsdb::SelectStmt>(*result.stmt);
    EXPECT_TRUE(stmt.latest);
    EXPECT_EQ(stmt.columns.size(), 1);
}

TEST(SnapshotSQLTest, ParseSelectColumns) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT tag, value, ts FROM SNAPSHOT WHERE tag = 'BOILER-001'", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    EXPECT_TRUE(stmt.latest);
    EXPECT_EQ(stmt.where.tag_filter, "BOILER-001");
}

TEST(SnapshotSQLTest, ParseSelectPattern) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    EXPECT_TRUE(stmt.latest);
    EXPECT_EQ(stmt.where.tag_pattern, "BOILER-%");
}

TEST(SnapshotSQLTest, ParseSelectCount) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT COUNT(*) FROM SNAPSHOT", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    EXPECT_TRUE(stmt.latest);
}

TEST(SnapshotSQLTest, ParseLowercaseSnapshot) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("select * from snapshot where tag = 'test'", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    EXPECT_TRUE(stmt.latest);
}

TEST(SnapshotSQLTest, ParseExistingLatestStillWorks) {
    // 验证原有的 SELECT ... LATEST 仍然正常工作（表名不是 SNAPSHOT）
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM boiler_temp WHERE tag = 'BOILER-001' LATEST", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    // SNAPSHOT 查询会设置 latest=true，普通 LATEST 查询也设置 latest=true
    // 区别是 table_name 不同（"boiler_temp" vs "SNAPSHOT"）
    EXPECT_NE(stmt.table_name, "SNAPSHOT");
    EXPECT_EQ(stmt.latest, true);  // LATEST 关键字也设 latest=true
}
