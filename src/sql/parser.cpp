#include "sql/parser.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace minitsdb {

// 前向声明
Timestamp ParseTimeString(const std::string& s);

namespace {

// 工具函数：去除首尾空白
std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// 转大写
std::string ToUpper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::toupper(c));
    return r;
}

// 查找不区分大小写的关键字位置
size_t FindKeyword(const std::string& sql, const std::string& kw, size_t pos = 0) {
    auto upper = ToUpper(sql);
    auto kw_upper = ToUpper(kw);
    return upper.find(kw_upper, pos);
}

// 获取引号内的值
std::string ExtractQuoted(const std::string& sql, size_t& pos) {
    if (pos >= sql.size()) return "";
    char quote = sql[pos];
    if (quote != '\'' && quote != '"') return "";

    pos++;  // 跳过起始引号
    std::string result;
    while (pos < sql.size()) {
        if (sql[pos] == quote) {
            // SQL 标准转义：两个连续引号表示一个引号
            if (pos + 1 < sql.size() && sql[pos + 1] == quote) {
                result += quote;
                pos += 2;
                continue;
            }
            pos++;  // 跳过结束引号
            return result;
        }
        result += sql[pos];
        pos++;
    }
    return result;
}

// 跳过空白和逗号
void SkipWhitespace(const std::string& sql, size_t& pos) {
    while (pos < sql.size() && (sql[pos] == ' ' || sql[pos] == '\t' ||
                                 sql[pos] == '\n' || sql[pos] == '\r' || sql[pos] == ',')) {
        pos++;
    }
}

}  // namespace

ParseResult SQLParser::Parse(const std::string& sql) {
    std::string trimmed = Trim(sql);
    if (trimmed.empty()) {
        return {false, "Empty SQL statement", nullptr};
    }

    std::string upper = ToUpper(trimmed);

    if (upper.find("SELECT") == 0) return ParseSelect(trimmed);

    // DDL: 精确匹配从长到短
    if (upper.find("CREATE DATABASE") == 0) return ParseCreateDatabase(trimmed);
    if (upper.find("CREATE TABLE") == 0) return ParseCreateTable(trimmed);
    if (upper.find("CREATE TAGS") == 0) return ParseCreateTags(trimmed);
    if (upper.find("CREATE TAG") == 0) return ParseCreateTag(trimmed);
    if (upper.find("CREATE ALARM") == 0) return ParseCreateAlarm(trimmed);
    if (upper.find("CREATE USER") == 0) return ParseCreateUser(trimmed);

    if (upper.find("DROP TABLE") == 0) return ParseDropTable(trimmed);
    if (upper.find("DROP TAG") == 0) return ParseDropTag(trimmed);
    if (upper.find("DROP ALARM") == 0) return ParseDropAlarm(trimmed);
    if (upper.find("DROP USER") == 0) return ParseDropUser(trimmed);
    if (upper.find("DROP DATABASE") == 0) return ParseDropDatabase(trimmed);

    if (upper.find("ALTER TAG") == 0) return ParseAlterTag(trimmed);
    if (upper.find("ALTER ALARM") == 0) return ParseAlterAlarm(trimmed);
    if (upper.find("ALTER USER") == 0) return ParseAlterUser(trimmed);
    if (upper.find("ALTER SYSTEM") == 0) return ParseAlterSystem(trimmed);
    if (upper.find("ALTER") == 0) return {false, "Unknown ALTER type", nullptr};

    if (upper.find("INSERT") == 0) return ParseInsert(trimmed);
    if (upper.find("DELETE") == 0) return ParseDelete(trimmed);
    if (upper.find("UPDATE") == 0) return ParseUpdate(trimmed);
    if (upper.find("USE ") == 0 || upper == "USE") return ParseUse(trimmed);
    if (upper.find("SHOW") == 0) return ParseShow(trimmed);

    return {false, "Unknown statement type: " + trimmed.substr(0, 30), nullptr};
}

bool SQLParser::IsInsert(const std::string& sql) {
    return ToUpper(Trim(sql)).find("INSERT") == 0;
}

bool SQLParser::IsSelect(const std::string& sql) {
    return ToUpper(Trim(sql)).find("SELECT") == 0;
}

bool SQLParser::IsCreateTag(const std::string& sql) {
    return ToUpper(Trim(sql)).find("CREATE TAG") == 0;
}

bool SQLParser::IsCreateAlarm(const std::string& sql) {
    return ToUpper(Trim(sql)).find("CREATE ALARM") == 0;
}

bool SQLParser::IsAlterSystem(const std::string& sql) {
    return ToUpper(Trim(sql)).find("ALTER SYSTEM") == 0;
}

bool SQLParser::IsCreateUser(const std::string& sql) {
    return ToUpper(Trim(sql)).find("CREATE USER") == 0;
}

bool SQLParser::IsShow(const std::string& sql) {
    return ToUpper(Trim(sql)).find("SHOW") == 0;
}

// ============================================================
//  INSERT 解析
// ============================================================
ParseResult SQLParser::ParseInsert(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    InsertStmt stmt;

    std::string upper = ToUpper(sql);
    size_t pos = 0;

    // INSERT INTO
    pos = FindKeyword(sql, "INSERT INTO", pos);
    if (pos == std::string::npos) {
        return {false, "Expected INSERT INTO", nullptr};
    }
    pos += 11;  // "INSERT INTO".length()

    SkipWhitespace(sql, pos);

    // table name
    size_t table_start = pos;
    while (pos < sql.size() && sql[pos] != '(' && sql[pos] != ' ')
        pos++;
    stmt.table_name = sql.substr(table_start, pos - table_start);
    SkipWhitespace(sql, pos);

    // (col1, col2, ...)
    if (pos < sql.size() && sql[pos] == '(') {
        pos++;  // skip '('
        while (pos < sql.size() && sql[pos] != ')') {
            SkipWhitespace(sql, pos);
            size_t col_start = pos;
            while (pos < sql.size() && sql[pos] != ',' && sql[pos] != ')')
                pos++;
            stmt.columns.push_back(Trim(sql.substr(col_start, pos - col_start)));
            SkipWhitespace(sql, pos);
        }
        if (pos < sql.size()) pos++;  // skip ')'
    }

    // VALUES
    pos = FindKeyword(sql, "VALUES", pos);
    if (pos == std::string::npos) {
        return {false, "Expected VALUES", nullptr};
    }
    pos += 6;
    SkipWhitespace(sql, pos);

    // (val1, val2, ...), (val1, val2, ...)
    while (pos < sql.size() && sql[pos] == '(') {
        pos++;  // skip '('
        InsertStmt::RowValue row;
        while (pos < sql.size() && sql[pos] != ')') {
            SkipWhitespace(sql, pos);
            if (sql[pos] == '\'' || sql[pos] == '"') {
                row.values.push_back(ExtractQuoted(sql, pos));
            } else {
                size_t val_start = pos;
                while (pos < sql.size() && sql[pos] != ',' && sql[pos] != ')')
                    pos++;
                row.values.push_back(Trim(sql.substr(val_start, pos - val_start)));
            }
            SkipWhitespace(sql, pos);
        }
        if (pos < sql.size()) pos++;  // skip ')'
        stmt.rows.push_back(row);
        SkipWhitespace(sql, pos);
    }

    if (stmt.rows.empty()) {
        return {false, "No values provided", nullptr};
    }

    *result = stmt;
    return {true, "", result};
}

// ============================================================
//  SELECT 解析
// ============================================================
ParseResult SQLParser::ParseSelect(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    SelectStmt stmt;

    size_t pos = FindKeyword(sql, "SELECT");
    pos += 6;
    SkipWhitespace(sql, pos);

    // columns
    size_t from_pos = FindKeyword(sql, "FROM", pos);
    if (from_pos == std::string::npos)
        return {false, "Expected FROM", nullptr};

    std::string cols_str = sql.substr(pos, from_pos - pos);
    size_t col_start = 0;
    while (col_start < cols_str.size()) {
        SkipWhitespace(cols_str, col_start);
        if (col_start >= cols_str.size()) break;

        SelectStmt::SelectExpr expr;
        size_t comma = cols_str.find(',', col_start);
        std::string col;
        if (comma == std::string::npos) {
            col = Trim(cols_str.substr(col_start));
            col_start = cols_str.size();
        } else {
            col = Trim(cols_str.substr(col_start, comma - col_start));
            col_start = comma + 1;
        }

        // TIME_BUCKET('5m', ts) AS bucket
        auto tb = FindKeyword(col, "TIME_BUCKET");
        if (tb != std::string::npos) {
            expr.is_aggregate = true;
            // Extract bucket size
            size_t p = col.find('(');
            if (p != std::string::npos) {
                p++;
                auto buck = ExtractQuoted(col, p);
                // Find the column alias after AS
                auto as_pos = FindKeyword(col, "AS");
                if (as_pos != std::string::npos) {
                    expr.alias = Trim(col.substr(as_pos + 2));
                }
            }
        }

        // 检查聚合函数
        for (const auto& agg_name : {"AVG", "MAX", "MIN", "SUM", "COUNT"}) {
            if (FindKeyword(col, agg_name) != std::string::npos) {
                expr.is_aggregate = true;
                expr.expr = col;
                if (std::strcmp(agg_name, "AVG") == 0) expr.agg_type = AggType::AVG;
                else if (std::strcmp(agg_name, "MAX") == 0) expr.agg_type = AggType::MAX;
                else if (std::strcmp(agg_name, "MIN") == 0) expr.agg_type = AggType::MIN;
                else if (std::strcmp(agg_name, "SUM") == 0) expr.agg_type = AggType::SUM;
                else if (std::strcmp(agg_name, "COUNT") == 0) expr.agg_type = AggType::COUNT;
                break;
            }
        }
        if (!expr.is_aggregate) {
            expr.expr = col;
        }

        stmt.columns.push_back(expr);
    }

    pos = from_pos + 4;
    SkipWhitespace(sql, pos);

    // table name
    size_t where_pos = FindKeyword(sql, "WHERE", pos);
    size_t latest_pos = FindKeyword(sql, "LATEST", pos);
    size_t group_pos = FindKeyword(sql, "GROUP BY", pos);
    size_t order_pos = FindKeyword(sql, "ORDER BY", pos);
    size_t limit_pos = FindKeyword(sql, "LIMIT", pos);

    if (where_pos != std::string::npos) {
        stmt.table_name = Trim(sql.substr(pos, where_pos - pos));
    } else if (latest_pos != std::string::npos) {
        stmt.table_name = Trim(sql.substr(pos, latest_pos - pos));
    } else {
        stmt.table_name = Trim(sql.substr(pos));
        *result = stmt;
        return {true, "", result};  // SELECT ... FROM table (no WHERE)
    }

    // 检测 SNAPSHOT 虚拟表
    {
        std::string upper_name = stmt.table_name;
        for (auto& c : upper_name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper_name == "SNAPSHOT") {
            stmt.latest = true;
        }
    }

    // WHERE
    if (where_pos != std::string::npos) {
        pos = where_pos + 5;
        SkipWhitespace(sql, pos);

        // Parse tag conditions
        // tag = 'xxx' or tag IN ('xxx') or tag LIKE 'xxx'
        auto ParseWhereCondition = [&](const std::string& clause) {
            auto& w = stmt.where;
            auto eq = clause.find('=');
            auto like_kw = FindKeyword(clause, "LIKE");
            auto between_pos = FindKeyword(clause, "BETWEEN");

            if (between_pos != std::string::npos) {
                // ts BETWEEN 'start' AND 'end'
                auto p = between_pos + 7;
                SkipWhitespace(clause, p);
                while (p < clause.size() && clause[p] != '\'' && clause[p] != '"')
                    p++;
                std::string ts_start, ts_end;
                if (p < clause.size()) {
                    ts_start = ExtractQuoted(clause, p);
                }
                auto and_pos = FindKeyword(clause, "AND", p);
                if (and_pos != std::string::npos) {
                    p = and_pos + 3;
                    SkipWhitespace(clause, p);
                    if (p < clause.size()) {
                        ts_end = ExtractQuoted(clause, p);
                    }
                }
                w.time_range = {ParseTimeString(ts_start), ParseTimeString(ts_end)};
            }

            if (eq != std::string::npos) {
                std::string field = Trim(clause.substr(0, eq));
                std::string val = Trim(clause.substr(eq + 1));
                // Strip quotes from val
                if (!val.empty() && (val[0] == '\'' || val[0] == '"')) {
                    size_t _unused = 0;
                    val = ExtractQuoted(val, _unused);
                }
                if (ToUpper(field) == "TAG") {
                    w.tag_filter = val;
                }
            } else if (like_kw != std::string::npos) {
                std::string field = Trim(clause.substr(0, like_kw));
                std::string val = Trim(clause.substr(like_kw + 4));
                if (!val.empty() && (val[0] == '\'' || val[0] == '"')) {
                    size_t _u = 0;
                    val = ExtractQuoted(val, _u);
                }
                if (ToUpper(field) == "TAG") {
                    w.tag_pattern = val;
                }
            }
        };

        // Find the end of WHERE clause
        size_t where_end = sql.size();
        if (latest_pos != std::string::npos) where_end = std::min(where_end, latest_pos);
        if (group_pos != std::string::npos) where_end = std::min(where_end, group_pos);
        if (order_pos != std::string::npos) where_end = std::min(where_end, order_pos);
        if (limit_pos != std::string::npos) where_end = std::min(where_end, limit_pos);

        std::string where_clause = Trim(sql.substr(pos, where_end - pos));
        // Split by AND
        size_t and_p = 0;
        while (and_p < where_clause.size()) {
            auto next_and = FindKeyword(where_clause, "AND", and_p);
            std::string cond;
            if (next_and == std::string::npos) {
                cond = Trim(where_clause.substr(and_p));
                and_p = where_clause.size();
            } else {
                cond = Trim(where_clause.substr(and_p, next_and - and_p));
                and_p = next_and + 3;
            }
            ParseWhereCondition(cond);
        }
    }

    // LATEST
    if (latest_pos != std::string::npos) {
        stmt.latest = true;
    }

    // GROUP BY
    if (group_pos != std::string::npos) {
        auto gb = std::make_shared<SelectStmt::GroupBy>();
        pos = group_pos + 8;
        SkipWhitespace(sql, pos);
        // Extract TIME_BUCKET('5m', ts) or just the bucket expression
        size_t p = sql.find('(', pos);
        if (p != std::string::npos) {
            p++;
            gb->bucket = ExtractQuoted(sql, p);
        }
        stmt.group_by = gb;
    }

    *result = stmt;
    return {true, "", result};
}

// ============================================================
//  CREATE TAG 解析
// ============================================================
ParseResult SQLParser::ParseCreateTag(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateTagStmt stmt;

    size_t pos = FindKeyword(sql, "CREATE TAG");
    pos += 10;
    SkipWhitespace(sql, pos);

    // tag name
    size_t name_start = pos;
    while (pos < sql.size() && sql[pos] != ' ' && sql[pos] != '(')
        pos++;
    stmt.tag_name = sql.substr(name_start, pos - name_start);
    SkipWhitespace(sql, pos);

    // properties: (key=val, ...)
    if (pos < sql.size() && sql[pos] == '(') {
        pos++;
        while (pos < sql.size() && sql[pos] != ')') {
            SkipWhitespace(sql, pos);
            size_t eq = sql.find('=', pos);
            if (eq == std::string::npos) break;

            std::string key = Trim(sql.substr(pos, eq - pos));
            pos = eq + 1;
            SkipWhitespace(sql, pos);

            std::string val;
            if (sql[pos] == '\'' || sql[pos] == '"') {
                val = ExtractQuoted(sql, pos);
            } else {
                size_t val_end = sql.find_first_of(",)", pos);
                val = Trim(sql.substr(pos, val_end - pos));
                pos = val_end;
            }
            stmt.properties.push_back({key, val});
            SkipWhitespace(sql, pos);
        }
    }

    *result = stmt;
    return {true, "", result};
}

// ============================================================
//  CREATE ALARM 解析
// ============================================================
ParseResult SQLParser::ParseCreateAlarm(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateAlarmStmt stmt;

    size_t pos = FindKeyword(sql, "CREATE ALARM");
    pos += 12;
    SkipWhitespace(sql, pos);

    // alarm name
    size_t name_start = pos;
    while (pos < sql.size() && sql[pos] != ' ')
        pos++;
    stmt.alarm_name = sql.substr(name_start, pos - name_start);
    SkipWhitespace(sql, pos);

    // ON tag_name
    pos = FindKeyword(sql, "ON", pos);
    if (pos == std::string::npos) return {false, "Expected ON", nullptr};
    pos += 2;
    SkipWhitespace(sql, pos);
    size_t on_end = pos;
    while (on_end < sql.size() && sql[on_end] != ' ')
        on_end++;
    stmt.tag_name = sql.substr(pos, on_end - pos);

    // WHEN condition
    pos = FindKeyword(sql, "WHEN", pos);
    if (pos == std::string::npos) return {false, "Expected WHEN", nullptr};
    pos += 4;
    SkipWhitespace(sql, pos);

    size_t then_pos = FindKeyword(sql, "THEN", pos);
    if (then_pos == std::string::npos) return {false, "Expected THEN", nullptr};
    stmt.condition = Trim(sql.substr(pos, then_pos - pos));

    // THEN ACTION(...)
    pos = FindKeyword(sql, "ACTION", then_pos);
    if (pos != std::string::npos) {
        pos = sql.find('(', pos);
        if (pos != std::string::npos) {
            pos++;
            while (pos < sql.size() && sql[pos] != ')') {
                SkipWhitespace(sql, pos);
                auto action = ExtractQuoted(sql, pos);
                if (!action.empty()) stmt.actions.push_back(action);
                SkipWhitespace(sql, pos);
            }
        }
    }

    *result = stmt;
    return {true, "", result};
}

// ============================================================
//  CREATE USER 解析
// ============================================================
ParseResult SQLParser::ParseCreateUser(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateUserStmt stmt;

    size_t pos = FindKeyword(sql, "CREATE USER");
    pos += 11;
    SkipWhitespace(sql, pos);

    // username
    size_t name_start = pos;
    while (pos < sql.size() && sql[pos] != ' ')
        pos++;
    stmt.username = sql.substr(name_start, pos - name_start);
    SkipWhitespace(sql, pos);

    // WITH PASSWORD 'xxx'
    auto pw_pos = FindKeyword(sql, "PASSWORD", pos);
    if (pw_pos != std::string::npos) {
        pos = pw_pos + 8;
        SkipWhitespace(sql, pos);
        if (pos < sql.size() && (sql[pos] == '\'' || sql[pos] == '"')) {
            stmt.password = ExtractQuoted(sql, pos);
        }
    }

    // ROLE 'admin' | 'operator' | 'viewer'
    auto role_pos = FindKeyword(sql, "ROLE", pos);
    if (role_pos != std::string::npos) {
        pos = role_pos + 4;
        SkipWhitespace(sql, pos);
        if (pos < sql.size() && (sql[pos] == '\'' || sql[pos] == '"')) {
            stmt.role = ExtractQuoted(sql, pos);
        } else {
            size_t r_start = pos;
            while (pos < sql.size() && sql[pos] != ' ')
                pos++;
            stmt.role = sql.substr(r_start, pos - r_start);
        }
    }

    if (stmt.password.empty()) {
        stmt.password = "default123";
    }
    if (stmt.role.empty()) {
        stmt.role = "viewer";
    }

    *result = stmt;
    return {true, "", result};
}

// ============================================================
//  ALTER SYSTEM 解析
// ============================================================
ParseResult SQLParser::ParseAlterSystem(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    AlterSystemStmt stmt;

    size_t pos = FindKeyword(sql, "SET");
    if (pos == std::string::npos)
        return {false, "Expected SET", nullptr};

    pos += 3;
    SkipWhitespace(sql, pos);

    size_t eq = sql.find('=', pos);
    if (eq == std::string::npos)
        return {false, "Expected key=value", nullptr};

    stmt.key = Trim(sql.substr(pos, eq - pos));
    std::string val_str = Trim(sql.substr(eq + 1));
    if (!val_str.empty() && (val_str[0] == '\'' || val_str[0] == '"')) {
        size_t _u = 0;
        stmt.value = ExtractQuoted(val_str, _u);
    }
    else
        stmt.value = val_str;

    *result = stmt;
    return {true, "", result};
}

// 解析时间字符串（ISO 8601 格式）为毫秒时间戳
// 支持格式: "2026-05-23", "2026-05-23T10:30:00Z", "2026-05-23 10:30:00"
Timestamp ParseTimeString(const std::string& s) {
    if (s.empty()) return 0;
    std::tm tm = {};
    int ms = 0;
    std::istringstream iss(s);
    // 尝试 "YYYY-MM-DDTHH:MM:SSZ" 或 "YYYY-MM-DD HH:MM:SS"
    if (s.find('T') != std::string::npos || s.find(' ') != std::string::npos) {
        char sep = (s.find('T') != std::string::npos) ? 'T' : ' ';
        iss >> std::get_time(&tm, "%Y-%m-%d");
        // 跳过 T 或空格
        if (iss && iss.peek() == sep) iss.get();
        if (iss) {
            int h, m, sec;
            char colon;
            iss >> h >> colon >> m >> colon >> sec;
            tm.tm_hour = h;
            tm.tm_min = m;
            tm.tm_sec = sec;
        }
    } else {
        // 仅 "YYYY-MM-DD"
        iss >> std::get_time(&tm, "%Y-%m-%d");
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    auto epoch = std::mktime(&tm);
    if (epoch == -1) return 0;
    return static_cast<Timestamp>(epoch) * 1000 + ms;
}

// ============================================================
//  新增 DDL/DML/SHOW 解析
// ============================================================

ParseResult SQLParser::ParseDropTag(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropTagStmt stmt;
    size_t pos = FindKeyword(sql, "DROP TAG") + 8;
    SkipWhitespace(sql, pos);
    auto rest = Trim(sql.substr(pos));
    auto dot = rest.find('.');
    if (dot != std::string::npos) {
        stmt.table_name = Trim(rest.substr(0, dot));
        stmt.tag_name = Trim(rest.substr(dot + 1));
    } else {
        stmt.tag_name = rest;
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseDropAlarm(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropAlarmStmt stmt;
    size_t pos = FindKeyword(sql, "DROP ALARM") + 10;
    SkipWhitespace(sql, pos);
    stmt.alarm_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseDropUser(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropUserStmt stmt;
    size_t pos = FindKeyword(sql, "DROP USER") + 9;
    SkipWhitespace(sql, pos);
    stmt.username = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseDropTable(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropTableStmt stmt;
    size_t pos = FindKeyword(sql, "DROP TABLE") + 10;
    SkipWhitespace(sql, pos);
    stmt.table_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseDropDatabase(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropDatabaseStmt stmt;
    size_t pos = FindKeyword(sql, "DROP DATABASE") + 13;
    SkipWhitespace(sql, pos);
    stmt.db_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseAlterUser(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    AlterUserStmt stmt;
    // ALTER USER <name> SET <property> '<value>'
    size_t pos = FindKeyword(sql, "ALTER USER") + 10;
    SkipWhitespace(sql, pos);
    size_t name_end = sql.find(" SET ", pos);
    if (name_end == std::string::npos) return {false, "Invalid ALTER USER syntax", nullptr};
    stmt.username = Trim(sql.substr(pos, name_end - pos));
    pos = name_end + 5;
    SkipWhitespace(sql, pos);
    size_t val_pos = sql.find('\'', pos);
    if (val_pos == std::string::npos) {
        // 可能是 role 值不带引号
        stmt.property = Trim(sql.substr(pos));
        size_t space = stmt.property.find(' ');
        if (space != std::string::npos) {
            stmt.value = Trim(stmt.property.substr(space + 1));
            stmt.property = Trim(stmt.property.substr(0, space));
        }
    } else {
        stmt.property = Trim(sql.substr(pos, val_pos - pos));
        stmt.value = ExtractQuoted(sql, val_pos);
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseAlterAlarm(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    AlterAlarmStmt stmt;
    size_t pos = FindKeyword(sql, "ALTER ALARM") + 11;
    SkipWhitespace(sql, pos);
    size_t set_pos = sql.find(" SET ", pos);
    if (set_pos == std::string::npos) return {false, "Invalid ALTER ALARM syntax", nullptr};
    stmt.alarm_name = Trim(sql.substr(pos, set_pos - pos));
    pos = set_pos + 5;
    SkipWhitespace(sql, pos);
    size_t eq = sql.find('=', pos);
    if (eq == std::string::npos) {  // SET action='log','email' 无 = 号
        size_t sp = sql.find(' ', pos);
        if (sp != std::string::npos) {
            size_t q1 = sql.find('\'', sp);
            if (q1 == std::string::npos) return {false, "Invalid ALTER ALARM value", nullptr};
            stmt.property = Trim(sql.substr(pos, sp - pos));
            stmt.value = ExtractQuoted(sql, q1);
        }
    } else {
        stmt.property = Trim(sql.substr(pos, eq - pos));
        pos = eq + 1;
        SkipWhitespace(sql, pos);
        if (sql[pos] == '\'') {
            stmt.value = ExtractQuoted(sql, pos);
        } else {
            size_t end = sql.find_first_of(" \t", pos);
            stmt.value = Trim(sql.substr(pos, end - pos));
        }
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseAlterTag(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    AlterTagStmt stmt;
    size_t pos = FindKeyword(sql, "ALTER TAG") + 9;
    SkipWhitespace(sql, pos);
    auto rest = Trim(sql.substr(pos));
    auto dot = rest.find('.');
    if (dot != std::string::npos) {
        stmt.table_name = Trim(rest.substr(0, dot));
        auto after = Trim(rest.substr(dot + 1));
        auto set_pos = after.find(" SET ");
        if (set_pos == std::string::npos) return {false, "Invalid ALTER TAG syntax", nullptr};
        stmt.tag_name = Trim(after.substr(0, set_pos));
        auto prop_part = Trim(after.substr(set_pos + 5));
        auto eq = prop_part.find('=');
        if (eq == std::string::npos) return {false, "Invalid ALTER TAG property", nullptr};
        stmt.property = Trim(prop_part.substr(0, eq));
        auto val_part = Trim(prop_part.substr(eq + 1));
        if (!val_part.empty() && val_part[0] == '\'')
            val_part = val_part.substr(1, val_part.size() - 2);
        stmt.value = val_part;
    } else {
        return {false, "ALTER TAG requires <table>.<tag>", nullptr};
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseCreateDatabase(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateDatabaseStmt stmt;
    size_t pos = FindKeyword(sql, "CREATE DATABASE") + 15;
    SkipWhitespace(sql, pos);
    stmt.db_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseUse(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    UseStmt stmt;
    auto upper = ToUpper(sql);
    size_t pos = upper.find("USE");
    pos += 3;
    SkipWhitespace(sql, pos);
    stmt.db_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseCreateTable(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateTableStmt stmt;
    size_t pos = FindKeyword(sql, "CREATE TABLE") + 12;
    SkipWhitespace(sql, pos);
    size_t name_end = pos;
    while (name_end < sql.size() && sql[name_end] != ' ' && sql[name_end] != '(')
        name_end++;
    stmt.table_name = Trim(sql.substr(pos, name_end - pos));
    pos = name_end;
    SkipWhitespace(sql, pos);
    if (pos < sql.size() && sql[pos] == '(') {
        pos++;
        while (pos < sql.size() && sql[pos] != ')') {
            SkipWhitespace(sql, pos);
            auto eq = sql.find('=', pos);
            if (eq == std::string::npos || eq > sql.find(')', pos)) break;
            std::string key = Trim(sql.substr(pos, eq - pos));
            pos = eq + 1;
            SkipWhitespace(sql, pos);
            std::string val;
            if (sql[pos] == '\'') val = ExtractQuoted(sql, pos);
            else {
                auto end = sql.find_first_of(",)", pos);
                val = Trim(sql.substr(pos, end - pos));
                pos = end;
            }
            stmt.properties.emplace_back(key, val);
            SkipWhitespace(sql, pos);
        }
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseCreateTags(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    CreateTagsStmt stmt;
    size_t pos = FindKeyword(sql, "CREATE TAGS") + 11;
    SkipWhitespace(sql, pos);
    auto in_pos = ToUpper(sql).find("IN TABLE", pos);
    if (in_pos == std::string::npos) return {false, "CREATE TAGS requires IN TABLE", nullptr};
    pos = in_pos + 8;
    SkipWhitespace(sql, pos);
    size_t name_end = sql.find('(', pos);
    if (name_end == std::string::npos) return {false, "CREATE TAGS needs tag list", nullptr};
    stmt.table_name = Trim(sql.substr(pos, name_end - pos));
    pos = name_end + 1;
    while (pos < sql.size() && sql[pos] != ')') {
        SkipWhitespace(sql, pos);
        size_t tag_start = pos;
        while (pos < sql.size() && sql[pos] != '(' && sql[pos] != ',' && sql[pos] != ')')
            pos++;
        CreateTagsStmt::TagDef def;
        def.name = Trim(sql.substr(tag_start, pos - tag_start));
        SkipWhitespace(sql, pos);
        if (pos < sql.size() && sql[pos] == '(') {
            pos++;
            while (pos < sql.size() && sql[pos] != ')') {
                SkipWhitespace(sql, pos);
                auto eq = sql.find('=', pos);
                if (eq == std::string::npos) break;
                std::string k = Trim(sql.substr(pos, eq - pos));
                pos = eq + 1;
                SkipWhitespace(sql, pos);
                std::string v;
                if (sql[pos] == '\'') v = ExtractQuoted(sql, pos);
                else {
                    auto end = sql.find_first_of(",)", pos);
                    v = Trim(sql.substr(pos, end - pos));
                    pos = end;
                }
                def.props.emplace_back(k, v);
                SkipWhitespace(sql, pos);
            }
            if (pos < sql.size()) pos++;
        }
        stmt.tags.push_back(std::move(def));
        SkipWhitespace(sql, pos);
        if (pos < sql.size() && sql[pos] == ',') pos++;
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseDelete(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DeleteStmt stmt;
    size_t pos = FindKeyword(sql, "DELETE") + 6;
    SkipWhitespace(sql, pos);
    // DELETE FROM <table> WHERE tag='<name>' AND timestamp BETWEEN 'start' AND 'end'
    auto from_pos = ToUpper(sql).find("FROM", pos);
    if (from_pos == std::string::npos) return {false, "DELETE requires FROM", nullptr};
    pos = from_pos + 4;
    SkipWhitespace(sql, pos);
    size_t where_pos = ToUpper(sql).find("WHERE", pos);
    if (where_pos == std::string::npos) return {false, "DELETE requires WHERE", nullptr};
    stmt.table_name = Trim(sql.substr(pos, where_pos - pos));
    // Simplified: just parse tag filter and time range
    pos = where_pos + 5;
    auto tag_eq = sql.find("tag=", pos);
    if (tag_eq != std::string::npos) {
        tag_eq += 4;
        SkipWhitespace(sql, tag_eq);
        if (sql[tag_eq] == '\'') stmt.tag_filter = ExtractQuoted(sql, tag_eq);
    }
    auto between_pos = ToUpper(sql).find("BETWEEN", pos);
    if (between_pos != std::string::npos) {
        auto and_pos = ToUpper(sql).find("AND", between_pos + 7);
        if (and_pos != std::string::npos) {
            auto start_str = Trim(sql.substr(between_pos + 7, and_pos - between_pos - 7));
            auto end_str = Trim(sql.substr(and_pos + 3));
            // Remove quotes
            auto clean = [](const std::string& s) {
                if (s.size() >= 2 && s[0] == '\'') return s.substr(1, s.size() - 2);
                return s;
            };
            stmt.time_range.start = ParseTimeString(clean(start_str));
            stmt.time_range.end = ParseTimeString(clean(end_str));
        }
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseUpdate(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    UpdateStmt stmt;
    size_t pos = FindKeyword(sql, "UPDATE") + 6;
    SkipWhitespace(sql, pos);
    auto set_pos = ToUpper(sql).find("SET", pos);
    if (set_pos == std::string::npos) return {false, "UPDATE requires SET", nullptr};
    stmt.table_name = Trim(sql.substr(pos, set_pos - pos));
    pos = set_pos + 3;
    SkipWhitespace(sql, pos);
    auto where_pos = ToUpper(sql).find("WHERE", pos);
    if (where_pos == std::string::npos) return {false, "UPDATE requires WHERE", nullptr};
    // Parse value
    auto eq = sql.find('=', pos);
    if (eq != std::string::npos && eq < where_pos) {
        auto val_str = Trim(sql.substr(eq + 1, where_pos - eq - 1));
        try { stmt.new_value = std::stod(val_str); } catch (...) {}
    }
    // Parse WHERE
    pos = where_pos + 5;
    auto tag_eq = sql.find("tag=", pos);
    if (tag_eq != std::string::npos) {
        tag_eq += 4;
        SkipWhitespace(sql, tag_eq);
        if (sql[tag_eq] == '\'') stmt.tag_filter = ExtractQuoted(sql, tag_eq);
    }
    auto ts_eq = sql.find("timestamp=", pos);
    if (ts_eq == std::string::npos) ts_eq = sql.find("timestamp =", pos);
    if (ts_eq != std::string::npos) {
        ts_eq = sql.find('=', ts_eq) + 1;
        SkipWhitespace(sql, ts_eq);
        if (sql[ts_eq] == '\'') {
            auto ts_str = ExtractQuoted(sql, ts_eq);
            stmt.exact_time = ParseTimeString(ts_str);
        }
    }
    *result = stmt;
    return {true, "", result};
}

ParseResult SQLParser::ParseShow(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    ShowStmt stmt;
    auto upper = ToUpper(sql);
    if (upper.find("SHOW DATABASES") == 0) {
        stmt.type = ShowStmt::Type::DATABASES;
    } else if (upper.find("SHOW TABLES FROM") == 0) {
        stmt.type = ShowStmt::Type::TABLES;
        size_t pos = upper.find("FROM") + 4;
        stmt.filter_db = Trim(sql.substr(pos));
    } else if (upper.find("SHOW TABLES") == 0) {
        stmt.type = ShowStmt::Type::TABLES;
    } else if (upper.find("SHOW TAGS FROM") == 0) {
        stmt.type = ShowStmt::Type::TAGS;
        size_t pos = upper.find("FROM") + 4;
        auto like_pos = ToUpper(sql).find("LIKE", pos);
        if (like_pos != std::string::npos) {
            stmt.filter_table = Trim(sql.substr(pos, like_pos - pos));
            stmt.pattern = Trim(sql.substr(like_pos + 4));
            // Clean quotes from pattern
            if (stmt.pattern.size() >= 2 && stmt.pattern[0] == '\'')
                stmt.pattern = stmt.pattern.substr(1, stmt.pattern.size() - 2);
        } else {
            stmt.filter_table = Trim(sql.substr(pos));
        }
    } else if (upper.find("SHOW TAGS") == 0) {
        stmt.type = ShowStmt::Type::TAGS;
    } else if (upper.find("SHOW USERS") == 0) {
        stmt.type = ShowStmt::Type::USERS;
    } else if (upper.find("SHOW ALARMS") == 0) {
        stmt.type = ShowStmt::Type::ALARMS;
    } else {
        return {false, "Unknown SHOW type", nullptr};
    }
    *result = stmt;
    return {true, "", result};
}

} // namespace minitsdb
