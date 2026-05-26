#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <utility>

namespace minitsdb {

// ============================================================
// 抽象语法树节点定义
// 顶层：一条 SQL 语句可以是以下几种类型
// ============================================================

struct ASTNode {
    virtual ~ASTNode() = default;
};

// ---------- 写入：INSERT INTO tag (tag, value, ts?) VALUES ... ----------
struct InsertStmt : ASTNode {
    std::string table_name;     // 表名（对应 Tag 名或通配）
    std::vector<std::string> columns;  // 列名
    // 每行值
    struct RowValue {
        std::vector<std::string> values;  // 字符串形式，执行时解析
    };
    std::vector<RowValue> rows;
};

// ---------- 查询：SELECT ... FROM ... WHERE ... ----------
struct SelectStmt : ASTNode {
    struct SelectExpr {
        std::string expr;          // "value", "AVG(value)" 等
        std::string alias;         // AS 别名
        bool is_aggregate = false;
        AggType agg_type = AggType::NONE;
    };

    std::vector<SelectExpr> columns;
    std::string table_name;

    // WHERE 条件
    struct WhereClause {
        std::string tag_filter;        // tag = 'BOILER-001'
        std::string tag_pattern;       // tag LIKE 'BOILER-%'
        TimeRange time_range;          // ts BETWEEN ... AND ...
    };
    WhereClause where;

    bool latest = false;               // 是否 LATEST 查询
    std::string order_by;
    bool order_asc = true;
    int32_t limit = -1;

    // GROUP BY time bucket
    struct GroupBy {
        std::string bucket;            // "5m", "1h", "1d"
    };
    std::shared_ptr<GroupBy> group_by;
};

// ---------- 创建测点：CREATE TAG ... ----------
struct CreateTagStmt : ASTNode {
    std::string tag_name;
    struct TagProperty {
        std::string key;
        std::string value;
    };
    std::vector<TagProperty> properties;
};

// ---------- 创建报警：CREATE ALARM ... ----------
struct CreateAlarmStmt : ASTNode {
    std::string alarm_name;
    std::string tag_name;
    std::string condition;            // "value > 1400.0"
    std::vector<std::string> actions; // "log", "notify"
};

// ---------- 系统配置：ALTER SYSTEM SET ... ----------
struct AlterSystemStmt : ASTNode {
    std::string key;
    std::string value;
};

// ---------- 创建用户：CREATE USER ... ----------
struct CreateUserStmt : ASTNode {
    std::string username;
    std::string password;
    std::string role;  // admin, operator, viewer
};

// ── 新增 DDL 语句 ──

// DROP TAG <table>.<tag>
struct DropTagStmt : ASTNode {
    std::string table_name;
    std::string tag_name;
};

// DROP ALARM <name>
struct DropAlarmStmt : ASTNode {
    std::string alarm_name;
};

// DROP USER <name>
struct DropUserStmt : ASTNode {
    std::string username;
};

// DROP TABLE <name>
struct DropTableStmt : ASTNode {
    std::string table_name;
};

// CREATE DATABASE <name>
struct CreateDatabaseStmt : ASTNode {
    std::string db_name;
};

// DROP DATABASE <name>
struct DropDatabaseStmt : ASTNode {
    std::string db_name;
};

// USE <name>
struct UseStmt : ASTNode {
    std::string db_name;
};

// CREATE TABLE <name> (properties...)
struct CreateTableStmt : ASTNode {
    std::string table_name;
    std::vector<std::pair<std::string, std::string>> properties;
};

// CREATE TAG <tag> IN TABLE <table> (...)  / CREATE TAGS IN TABLE <table> (...)
struct CreateTagsStmt : ASTNode {
    std::string table_name;
    struct TagDef {
        std::string name;
        std::vector<std::pair<std::string, std::string>> props;
    };
    std::vector<TagDef> tags;
};

// ALTER TAG <table>.<tag> SET <prop>='<value>'
struct AlterTagStmt : ASTNode {
    std::string table_name;
    std::string tag_name;
    std::string property;
    std::string value;
};

// ALTER ALARM <name> SET <prop>='<value>'
struct AlterAlarmStmt : ASTNode {
    std::string alarm_name;
    std::string property;
    std::string value;
};

// ALTER USER <name> SET <prop>='<value>'
struct AlterUserStmt : ASTNode {
    std::string username;
    std::string property;
    std::string value;
};

// DELETE FROM <table> WHERE tag='<name>' AND timestamp BETWEEN ...
struct DeleteStmt : ASTNode {
    std::string table_name;
    std::string tag_filter;
    TimeRange time_range;
};

// UPDATE <table> SET value=<new> WHERE tag='<name>' AND timestamp='<ts>'
struct UpdateStmt : ASTNode {
    std::string table_name;
    std::string tag_filter;
    Timestamp exact_time = 0;
    double new_value = 0.0;
};

// SHOW DATABASES / TABLES / TAGS / USERS / ALARMS
struct ShowStmt : ASTNode {
    enum class Type {
        DATABASES, TABLES, TAGS, USERS, ALARMS
    };
    Type type;
    std::string filter_db;
    std::string filter_table;
    std::string pattern;       // LIKE 模式匹配
};

// ---------- 顶层语句变体 ----------
using SQLStmt = std::variant<
    InsertStmt, SelectStmt,
    CreateTagStmt, CreateAlarmStmt, CreateUserStmt,
    AlterSystemStmt,
    DropTagStmt, DropAlarmStmt, DropUserStmt,
    DropTableStmt, DropDatabaseStmt,
    CreateDatabaseStmt, UseStmt, CreateTableStmt,
    CreateTagsStmt, AlterTagStmt, AlterAlarmStmt, AlterUserStmt,
    DeleteStmt, UpdateStmt, ShowStmt
>;

// ---------- 解析结果 ----------
struct ParseResult {
    bool ok = false;
    std::string error_msg;
    std::shared_ptr<SQLStmt> stmt;
};

// ---------- 查询计划 ----------
struct QueryPlan {
    enum class Type {
        INSERT, SELECT_LATEST, SELECT_RAW, SELECT_AGGREGATE, SELECT_SNAPSHOT,
        CREATE_TAG, CREATE_ALARM, ALTER_SYSTEM,
        DROP_TAG, DROP_ALARM, DROP_USER, DROP_TABLE, DROP_DATABASE,
        CREATE_DATABASE, USE, CREATE_TABLE, CREATE_TAGS,
        ALTER_TAG, ALTER_ALARM, ALTER_USER,
        DELETE_STMT, UPDATE_STMT, SHOW_INFO
    };

    Type type;
    std::vector<std::string> involved_tags;
    TimeRange time_range;
    StorageTier target_tier = StorageTier::HOT;  // 默认查热区
};

} // namespace minitsdb
