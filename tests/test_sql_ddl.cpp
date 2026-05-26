#include <gtest/gtest.h>
#include "sql/parser.h"

using namespace minitsdb;

// ── DROP 语句解析 ──

TEST(DDLParseTest, DropTag) {
    SQLParser parser;
    auto r = parser.Parse("DROP TAG boiler_data.BOILER-001");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<DropTagStmt>(*r.stmt));
    auto& s = std::get<DropTagStmt>(*r.stmt);
    EXPECT_EQ(s.table_name, "boiler_data");
    EXPECT_EQ(s.tag_name, "BOILER-001");
}

TEST(DDLParseTest, DropAlarm) {
    SQLParser parser;
    auto r = parser.Parse("DROP ALARM boiler_high_temp");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<DropAlarmStmt>(*r.stmt));
    EXPECT_EQ(std::get<DropAlarmStmt>(*r.stmt).alarm_name, "boiler_high_temp");
}

TEST(DDLParseTest, DropUser) {
    SQLParser parser;
    auto r = parser.Parse("DROP USER john");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<DropUserStmt>(*r.stmt));
    EXPECT_EQ(std::get<DropUserStmt>(*r.stmt).username, "john");
}

TEST(DDLParseTest, DropTable) {
    SQLParser parser;
    auto r = parser.Parse("DROP TABLE boiler_data");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<DropTableStmt>(*r.stmt));
    EXPECT_EQ(std::get<DropTableStmt>(*r.stmt).table_name, "boiler_data");
}

TEST(DDLParseTest, DropDatabase) {
    SQLParser parser;
    auto r = parser.Parse("DROP DATABASE factory_a");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<DropDatabaseStmt>(*r.stmt));
    EXPECT_EQ(std::get<DropDatabaseStmt>(*r.stmt).db_name, "factory_a");
}

// ── ALTER 语句解析 ──

TEST(DDLParseTest, AlterUserPassword) {
    SQLParser parser;
    auto r = parser.Parse("ALTER USER john SET PASSWORD 'new_pass'");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<AlterUserStmt>(*r.stmt));
    auto& s = std::get<AlterUserStmt>(*r.stmt);
    EXPECT_EQ(s.username, "john");
    EXPECT_EQ(s.property, "PASSWORD");
    EXPECT_EQ(s.value, "new_pass");
}

TEST(DDLParseTest, AlterUserRole) {
    SQLParser parser;
    auto r = parser.Parse("ALTER USER john SET ROLE admin");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<AlterUserStmt>(*r.stmt));
    auto& s = std::get<AlterUserStmt>(*r.stmt);
    EXPECT_EQ(s.username, "john");
    EXPECT_EQ(s.property, "ROLE");
    EXPECT_EQ(s.value, "admin");
}

// ── CREATE 语句解析 ──

TEST(DDLParseTest, CreateDatabase) {
    SQLParser parser;
    auto r = parser.Parse("CREATE DATABASE factory_b");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<CreateDatabaseStmt>(*r.stmt));
    EXPECT_EQ(std::get<CreateDatabaseStmt>(*r.stmt).db_name, "factory_b");
}

TEST(DDLParseTest, Use) {
    SQLParser parser;
    auto r = parser.Parse("USE factory_a");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<UseStmt>(*r.stmt));
    EXPECT_EQ(std::get<UseStmt>(*r.stmt).db_name, "factory_a");
}

TEST(DDLParseTest, CreateTable) {
    SQLParser parser;
    auto r = parser.Parse("CREATE TABLE boiler_data (description='Boiler data')");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<CreateTableStmt>(*r.stmt));
    auto& s = std::get<CreateTableStmt>(*r.stmt);
    EXPECT_EQ(s.table_name, "boiler_data");
    ASSERT_EQ(s.properties.size(), 1);
    EXPECT_EQ(s.properties[0].first, "description");
    EXPECT_EQ(s.properties[0].second, "Boiler data");
}

TEST(DDLParseTest, CreateTags) {
    SQLParser parser;
    auto r = parser.Parse(
        "CREATE TAGS IN TABLE boiler_data (BOILER-TEMP (type='analog'), BOILER-STAT (type='digital'))");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<CreateTagsStmt>(*r.stmt));
    auto& s = std::get<CreateTagsStmt>(*r.stmt);
    EXPECT_EQ(s.table_name, "boiler_data");
    ASSERT_EQ(s.tags.size(), 2);
    EXPECT_EQ(s.tags[0].name, "BOILER-TEMP");
    EXPECT_EQ(s.tags[1].name, "BOILER-STAT");
}

// ── DML 语句解析 ──

TEST(DDLParseTest, ShowDatabases) {
    SQLParser parser;
    auto r = parser.Parse("SHOW DATABASES");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<ShowStmt>(*r.stmt));
    EXPECT_EQ(std::get<ShowStmt>(*r.stmt).type, ShowStmt::Type::DATABASES);
}

TEST(DDLParseTest, ShowUsers) {
    SQLParser parser;
    auto r = parser.Parse("SHOW USERS");
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(std::holds_alternative<ShowStmt>(*r.stmt));
    EXPECT_EQ(std::get<ShowStmt>(*r.stmt).type, ShowStmt::Type::USERS);
}

// ── 路由测试 ──

TEST(DDLParseTest, RouterSelect) {
    SQLParser parser;
    EXPECT_TRUE(SQLParser::IsSelect("SELECT * FROM boiler"));
    EXPECT_FALSE(SQLParser::IsInsert("SELECT * FROM boiler"));
    EXPECT_FALSE(SQLParser::IsShow("SELECT * FROM boiler"));
}

TEST(DDLParseTest, RouterShow) {
    SQLParser parser;
    EXPECT_TRUE(SQLParser::IsShow("SHOW DATABASES"));
    EXPECT_FALSE(SQLParser::IsSelect("SHOW DATABASES"));
}
