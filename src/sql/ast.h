#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>

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

// ---------- 顶层语句变体 ----------
using SQLStmt = std::variant<
    InsertStmt,
    SelectStmt,
    CreateTagStmt,
    CreateAlarmStmt,
    AlterSystemStmt,
    CreateUserStmt
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
        INSERT,
        SELECT_LATEST,        // 走最新值缓存
        SELECT_RAW,           // 原始数据查询
        SELECT_AGGREGATE,     // 聚合查询
        CREATE_TAG,
        CREATE_ALARM,
        ALTER_SYSTEM
    };

    Type type;
    std::vector<std::string> involved_tags;
    TimeRange time_range;
    StorageTier target_tier = StorageTier::HOT;  // 默认查热区
};

} // namespace minitsdb
