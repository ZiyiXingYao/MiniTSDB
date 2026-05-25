#include "sql/executor.h"
#include "sql/parser.h"
#include "common/logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cctype>

namespace minitsdb {

Executor::Executor(std::shared_ptr<StorageEngine> engine,
                   std::shared_ptr<LatestCache> cache,
                   std::shared_ptr<AuthManager> auth)
    : engine_(std::move(engine)), cache_(std::move(cache)), auth_(std::move(auth)) {}

QueryResult Executor::Execute(const SQLStmt& stmt) {
    return std::visit([this](const auto& s) -> QueryResult {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, InsertStmt>)
            return ExecuteInsert(s);
        else if constexpr (std::is_same_v<T, SelectStmt>)
            return ExecuteSelect(s);
        else if constexpr (std::is_same_v<T, CreateTagStmt>)
            return ExecuteCreateTag(s);
        else if constexpr (std::is_same_v<T, CreateAlarmStmt>)
            return ExecuteCreateAlarm(s);
        else if constexpr (std::is_same_v<T, AlterSystemStmt>)
            return ExecuteAlterSystem(s);
        else if constexpr (std::is_same_v<T, CreateUserStmt>)
            return ExecuteCreateUser(s);
        else
            return {false, "Unknown statement type", {}, {}, 0};
    }, stmt);
}

QueryResult Executor::ExecuteSQL(const std::string& sql) {
    auto parse_result = parser_.Parse(sql);
    if (!parse_result.ok) {
        return {false, parse_result.error_msg, {}, {}, 0};
    }
    return Execute(*parse_result.stmt);
}

QueryResult Executor::ExecuteInsert(const InsertStmt& stmt) {
    if (stmt.rows.empty()) {
        return {false, "No values to insert", {}, {}, 0};
    }

    int64_t count = 0;
    std::vector<DataBatch> batches;

    for (const auto& row : stmt.rows) {
        std::string tag;
        DataPoint point;
        point.ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        bool has_tag = false;
        bool has_value = false;

        for (size_t i = 0; i < stmt.columns.size() && i < row.values.size(); i++) {
            std::string col = stmt.columns[i];
            // 列名大小写不敏感
            for (auto& c : col) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const auto& val = row.values[i];

            if (col == "tag") {
                tag = val;
                has_tag = true;
            } else if (col == "value") {
                try {
                    point.value = std::stod(val);
                } catch (...) {
                    point.value = val;  // treat as string
                }
                has_value = true;
            } else if (col == "ts") {
                try {
                    point.ts = std::stoll(val);
                } catch (...) {
                    // keep current timestamp
                }
            }
        }

        if (!has_tag || !has_value) continue;

        // Write to storage engine
        if (engine_) {
            engine_->Write(tag, point);
        }

        // Update cache
        if (cache_) {
            cache_->Update(tag, point);
        }

        count++;
    }

    QueryResult result;
    result.ok = true;
    result.affected_rows = count;
    return result;
}

QueryResult Executor::ExecuteSelect(const SelectStmt& stmt) {
    if (stmt.latest) {
        return ExecuteSelectLatest(stmt);
    }
    if (stmt.group_by) {
        return ExecuteSelectAggregate(stmt);
    }
    return ExecuteSelectRaw(stmt);
}

QueryResult Executor::ExecuteSelectLatest(const SelectStmt& stmt) {
    QueryResult result;
    result.column_names = {"tag", "value", "ts"};

    if (!cache_) {
        result.ok = true;
        return result;
    }

    std::vector<std::pair<std::string, DataPoint>> data;

    if (!stmt.where.tag_filter.empty()) {
        DataPoint point;
        if (cache_->Get(stmt.where.tag_filter, point)) {
            data.emplace_back(stmt.where.tag_filter, point);
        }
    } else if (!stmt.where.tag_pattern.empty()) {
        data = cache_->GetByPattern(stmt.where.tag_pattern);
    } else {
        data = cache_->GetAll();
    }

    for (const auto& [tag, point] : data) {
        std::vector<std::string> row;
        row.push_back(tag);
        row.push_back(FormatValue(point));
        row.push_back(FormatTimestamp(point.ts));
        result.rows.push_back(std::move(row));
    }

    result.ok = true;
    return result;
}

QueryResult Executor::ExecuteSelectRaw(const SelectStmt& stmt) {
    QueryResult result;
    result.column_names = {"ts", "value"};

    if (!engine_) {
        result.ok = true;
        return result;
    }

    std::string tag = stmt.where.tag_filter.empty() ? "" : stmt.where.tag_filter;
    TimeRange range = stmt.where.time_range;

    auto points = engine_->ReadRaw(tag, range);

    for (const auto& p : points) {
        std::vector<std::string> row;
        row.push_back(FormatTimestamp(p.ts));
        row.push_back(FormatValue(p));
        result.rows.push_back(std::move(row));
    }

    result.ok = true;
    return result;
}

QueryResult Executor::ExecuteSelectAggregate(const SelectStmt& stmt) {
    QueryResult result;
    result.column_names = {"bucket", "value"};

    if (!engine_ || !stmt.group_by) {
        result.ok = true;
        return result;
    }

    std::string tag = stmt.where.tag_filter.empty() ? "" : stmt.where.tag_filter;
    TimeRange range = stmt.where.time_range;

    // Parse bucket size
    int64_t bucket_ms = 300000;  // default 5min
    if (stmt.group_by && !stmt.group_by->bucket.empty()) {
        std::string num_part;
        for (char c : stmt.group_by->bucket) {
            if (std::isdigit(c)) num_part += c;
        }
        char unit = stmt.group_by->bucket.back();
        if (std::isalpha(unit) && !num_part.empty()) {
            try {
                int64_t val = std::stoll(num_part);
                switch (unit) {
                    case 's': bucket_ms = val * 1000; break;
                    case 'm': bucket_ms = val * 60000; break;
                    case 'h': bucket_ms = val * 3600000; break;
                    case 'd': bucket_ms = val * 86400000; break;
                    default: bucket_ms = val; break;
                }
            } catch (...) {}
        }
    }
    if (bucket_ms <= 0) bucket_ms = 300000;  // safety

    // 从 stmt 中提取聚合类型
    AggType agg_type = AggType::AVG;
    for (const auto& col : stmt.columns) {
        if (col.is_aggregate && col.agg_type != AggType::NONE) {
            agg_type = col.agg_type;
            break;
        }
    }

    auto agg_results = engine_->ReadAggregated(tag, range, bucket_ms, agg_type);

    for (const auto& ar : agg_results) {
        std::vector<std::string> row;
        row.push_back(FormatTimestamp(ar.bucket_ts));
        switch (agg_type) {
            case AggType::AVG:   row.push_back(std::to_string(ar.avg)); break;
            case AggType::MAX:   row.push_back(std::to_string(ar.max)); break;
            case AggType::MIN:   row.push_back(std::to_string(ar.min)); break;
            case AggType::SUM:   row.push_back(std::to_string(ar.sum)); break;
            case AggType::COUNT: row.push_back(std::to_string(ar.cnt)); break;
            default:             row.push_back(std::to_string(ar.avg)); break;
        }
        result.rows.push_back(std::move(row));
    }

    result.ok = true;
    return result;
}

QueryResult Executor::ExecuteCreateTag(const CreateTagStmt& stmt) {
    TagMeta meta;
    meta.name = stmt.tag_name;

    for (const auto& prop : stmt.properties) {
        if (prop.key == "type") {
            if (prop.value == "analog") meta.type = TagType::ANALOG;
            else if (prop.value == "digital") meta.type = TagType::DIGITAL;
            else if (prop.value == "string") meta.type = TagType::STRING;
            else if (prop.value == "accumulator") meta.type = TagType::ACCUMULATOR;
        } else if (prop.key == "unit") {
            meta.unit = prop.value;
        } else if (prop.key == "description") {
            meta.description = prop.value;
        } else if (prop.key == "precision") {
            try { meta.precision = std::stoi(prop.value); } catch (...) {}
        }
    }

    if (engine_) {
        engine_->RegisterTag(meta);
    }

    QueryResult result;
    result.ok = true;
    result.affected_rows = 1;
    return result;
}

QueryResult Executor::ExecuteCreateAlarm(const CreateAlarmStmt& stmt) {
    if (!engine_) {
        return {false, "Storage engine not available", {}, {}, 0};
    }

    auto* alarm_engine = engine_->GetAlarmEngine();
    if (!alarm_engine) {
        return {false, "Alarm engine not available", {}, {}, 0};
    }

    AlarmRule rule;
    rule.name = stmt.alarm_name;
    rule.tag_name = stmt.tag_name;
    rule.condition = stmt.condition;
    rule.actions = stmt.actions;

    if (!alarm_engine->AddRule(rule)) {
        return {false, "Failed to add alarm rule (may duplicate name)", {}, {}, 0};
    }

    LOG_INFO("Alarm rule '{}' created for tag '{}'", stmt.alarm_name, stmt.tag_name);
    QueryResult result;
    result.ok = true;
    result.affected_rows = 1;
    return result;
}

QueryResult Executor::ExecuteAlterSystem(const AlterSystemStmt& stmt) {
    LOG_INFO("System config changed: {} = {}", stmt.key, stmt.value);
    QueryResult result;
    result.ok = true;
    result.affected_rows = 1;
    return result;
}

QueryResult Executor::ExecuteCreateUser(const CreateUserStmt& stmt) {
    if (!auth_) {
        return {false, "Authentication not enabled", {}, {}, 0};
    }

    UserRole role = UserRole::VIEWER;
    std::string role_lower = stmt.role;
    for (auto& c : role_lower) c = static_cast<char>(std::tolower(c));
    if (role_lower == "admin") role = UserRole::ADMIN;
    else if (role_lower == "operator") role = UserRole::OPERATOR;

    // 使用 admin 内部登录创建用户（无需硬编码密码）
    std::string admin_token = auth_->AdminLogin();
    if (!auth_->CreateUser(admin_token, stmt.username, stmt.password, role)) {
        return {false, "Failed to create user (may already exist)", {}, {}, 0};
    }

    LOG_INFO("Created user '{}' with role '{}'", stmt.username, stmt.role);
    QueryResult result;
    result.ok = true;
    result.affected_rows = 1;
    return result;
}

std::string Executor::FormatValue(const DataPoint& point) {
    if (std::holds_alternative<double>(point.value)) {
        return std::to_string(std::get<double>(point.value));
    } else if (std::holds_alternative<int64_t>(point.value)) {
        return std::to_string(std::get<int64_t>(point.value));
    } else {
        return std::get<std::string>(point.value);
    }
}

std::string Executor::FormatTimestamp(Timestamp ts) {
    if (ts == 0) return "0";

    auto ms = ts % 1000;
    auto sec = ts / 1000;
    std::time_t t = static_cast<std::time_t>(sec);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));

    std::ostringstream oss;
    oss << buf << "." << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
}

} // namespace minitsdb
