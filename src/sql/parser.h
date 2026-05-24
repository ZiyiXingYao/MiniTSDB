#pragma once

#include "ast.h"
#include <string>

namespace minitsdb {

// SQL 解析器
// 将 SQL 字符串解析为 AST
class SQLParser {
public:
    SQLParser() = default;

    // 解析一条 SQL 语句
    // 返回 ParseResult，包含 AST 或错误信息
    ParseResult Parse(const std::string& sql);

    // 快速判断语句类型（不生成完整 AST）
    static bool IsInsert(const std::string& sql);
    static bool IsSelect(const std::string& sql);
    static bool IsCreateTag(const std::string& sql);
    static bool IsCreateAlarm(const std::string& sql);
    static bool IsAlterSystem(const std::string& sql);
    static bool IsCreateUser(const std::string& sql);

private:
    // 内部解析函数（后续扩展为基于 SQLite parser 改造）
    ParseResult ParseInsert(const std::string& sql);
    ParseResult ParseSelect(const std::string& sql);
    ParseResult ParseCreateTag(const std::string& sql);
    ParseResult ParseCreateAlarm(const std::string& sql);
    ParseResult ParseAlterSystem(const std::string& sql);
    ParseResult ParseCreateUser(const std::string& sql);

    // 解析时间范围
    TimeRange ParseTimeRange(const std::string& clause);

    // 解析聚合函数
    std::pair<AggType, std::string> ParseAggFunction(const std::string& expr);
};

} // namespace minitsdb
