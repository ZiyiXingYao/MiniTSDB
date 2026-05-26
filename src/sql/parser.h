#pragma once

#include "ast.h"
#include <string>

namespace minitsdb {

// SQL 解析器
// 将 SQL 字符串解析为 AST
class SQLParser {
public:
    SQLParser() = default;

    ParseResult Parse(const std::string& sql);

    static bool IsInsert(const std::string& sql);
    static bool IsSelect(const std::string& sql);
    static bool IsCreateTag(const std::string& sql);
    static bool IsCreateAlarm(const std::string& sql);
    static bool IsAlterSystem(const std::string& sql);
    static bool IsCreateUser(const std::string& sql);
    static bool IsShow(const std::string& sql);

private:
    ParseResult ParseInsert(const std::string& sql);
    ParseResult ParseSelect(const std::string& sql);
    ParseResult ParseCreateTag(const std::string& sql);
    ParseResult ParseCreateAlarm(const std::string& sql);
    ParseResult ParseAlterSystem(const std::string& sql);
    ParseResult ParseCreateUser(const std::string& sql);

    // 新增解析函数
    ParseResult ParseDropTag(const std::string& sql);
    ParseResult ParseDropAlarm(const std::string& sql);
    ParseResult ParseDropUser(const std::string& sql);
    ParseResult ParseDropTable(const std::string& sql);
    ParseResult ParseDropDatabase(const std::string& sql);
    ParseResult ParseAlterUser(const std::string& sql);
    ParseResult ParseAlterAlarm(const std::string& sql);
    ParseResult ParseAlterTag(const std::string& sql);
    ParseResult ParseCreateDatabase(const std::string& sql);
    ParseResult ParseUse(const std::string& sql);
    ParseResult ParseCreateTable(const std::string& sql);
    ParseResult ParseCreateTags(const std::string& sql);
    ParseResult ParseDelete(const std::string& sql);
    ParseResult ParseUpdate(const std::string& sql);
    ParseResult ParseShow(const std::string& sql);

    std::pair<AggType, std::string> ParseAggFunction(const std::string& expr);
};

} // namespace minitsdb
