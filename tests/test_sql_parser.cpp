#include <gtest/gtest.h>
#include "sql/parser.h"
#include "sql/ast.h"

using namespace minitsdb;

TEST(SQLParserTest, InsertSingle) {
    SQLParser p; auto r = p.Parse("INSERT INTO t VALUES ('x', 1)");
    ASSERT_TRUE(r.ok) << r.error_msg;
    ASSERT_TRUE(std::holds_alternative<InsertStmt>(*r.stmt));
    EXPECT_EQ(std::get<InsertStmt>(*r.stmt).table_name, "t");
    EXPECT_EQ(std::get<InsertStmt>(*r.stmt).rows.size(), 1);
}

TEST(SQLParserTest, InsertBatch) {
    SQLParser p; auto r = p.Parse("INSERT INTO t (tag, val) VALUES ('a',1), ('b',2)");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<InsertStmt>(*r.stmt));
    EXPECT_EQ(std::get<InsertStmt>(*r.stmt).rows.size(), 2);
}

TEST(SQLParserTest, SelectLatest) {
    SQLParser p; auto r = p.Parse("SELECT * FROM t WHERE tag='x' LATEST");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*r.stmt));
    EXPECT_TRUE(std::get<SelectStmt>(*r.stmt).latest);
}

TEST(SQLParserTest, SelectPattern) {
    SQLParser p; auto r = p.Parse("SELECT * FROM t WHERE tag LIKE 'BOILER-%' LATEST");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*r.stmt));
    EXPECT_EQ(std::get<SelectStmt>(*r.stmt).where.tag_pattern, "BOILER-%");
}

TEST(SQLParserTest, CreateTag) {
    SQLParser p; auto r = p.Parse("CREATE TAG t1 (type='analog', unit='celsius')");
    ASSERT_TRUE(r.ok) << r.error_msg;
    ASSERT_TRUE(std::holds_alternative<CreateTagStmt>(*r.stmt));
    EXPECT_EQ(std::get<CreateTagStmt>(*r.stmt).tag_name, "t1");
    EXPECT_EQ(std::get<CreateTagStmt>(*r.stmt).properties.size(), 2);
}

TEST(SQLParserTest, CreateAlarm) {
    SQLParser p; auto r = p.Parse("CREATE ALARM a ON t WHEN v>10 THEN ACTION('log')");
    ASSERT_TRUE(r.ok) << r.error_msg;
    ASSERT_TRUE(std::holds_alternative<CreateAlarmStmt>(*r.stmt));
    EXPECT_EQ(std::get<CreateAlarmStmt>(*r.stmt).alarm_name, "a");
}

TEST(SQLParserTest, AlterSystem) {
    SQLParser p; auto r = p.Parse("ALTER SYSTEM SET k = v");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<AlterSystemStmt>(*r.stmt));
    EXPECT_EQ(std::get<AlterSystemStmt>(*r.stmt).key, "k");
}

TEST(SQLParserTest, EmptySQL) {
    SQLParser p; EXPECT_FALSE(p.Parse("").ok);
}

TEST(SQLParserTest, UnknownStatement) {
    SQLParser p; EXPECT_FALSE(p.Parse("DROP TABLE foo").ok);
}

TEST(SQLParserTest, CreateUser) {
    SQLParser p;
    auto r = p.Parse("CREATE USER engineer1 WITH PASSWORD 'pass123' ROLE 'operator'");
    ASSERT_TRUE(r.ok) << r.error_msg;
    ASSERT_TRUE(std::holds_alternative<CreateUserStmt>(*r.stmt));
    auto& s = std::get<CreateUserStmt>(*r.stmt);
    EXPECT_EQ(s.username, "engineer1");
    EXPECT_EQ(s.password, "pass123");
    EXPECT_EQ(s.role, "operator");
}

TEST(SQLParserTest, CreateUserDefaultRole) {
    SQLParser p;
    auto r = p.Parse("CREATE USER viewer1 WITH PASSWORD 'view'");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<CreateUserStmt>(*r.stmt));
    auto& s = std::get<CreateUserStmt>(*r.stmt);
    EXPECT_EQ(s.username, "viewer1");
    EXPECT_EQ(s.role, "viewer");
}
