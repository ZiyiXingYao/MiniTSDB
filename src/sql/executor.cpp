#include "sql/executor.h"
#include "sql/parser.h"
#include "snapshot/snapshot_store.h"
#include "common/logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cctype>
#include <cmath>

namespace minitsdb {

Executor::Executor(std::shared_ptr<StorageEngine> engine,
                   std::shared_ptr<LatestCache> cache,
                   std::shared_ptr<AuthManager> auth)
    : engine_(std::move(engine)), cache_(std::move(cache)), auth_(std::move(auth)) {}

QueryResult Executor::Execute(const SQLStmt& stmt) {
    // Permission check
    if (!token_.empty() && auth_) {
        auto op = GetOperationName(stmt);
        if (!op.empty() && !auth_->CheckPermission(token_, op)) {
            return {false, "Permission denied: " + op, {}, {}, 0};
        }
    }

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
        else if constexpr (std::is_same_v<T, DropTagStmt>)
            return ExecuteDropTag(s);
        else if constexpr (std::is_same_v<T, DropAlarmStmt>)
            return ExecuteDropAlarm(s);
        else if constexpr (std::is_same_v<T, DropUserStmt>)
            return ExecuteDropUser(s);
        else if constexpr (std::is_same_v<T, DropTableStmt>)
            return ExecuteDropTable(s);
        else if constexpr (std::is_same_v<T, DropDatabaseStmt>)
            return ExecuteDropDatabase(s);
        else if constexpr (std::is_same_v<T, CreateDatabaseStmt>)
            return ExecuteCreateDatabase(s);
        else if constexpr (std::is_same_v<T, UseStmt>)
            return ExecuteUse(s);
        else if constexpr (std::is_same_v<T, CreateTableStmt>)
            return ExecuteCreateTable(s);
        else if constexpr (std::is_same_v<T, CreateTagsStmt>)
            return ExecuteCreateTags(s);
        else if constexpr (std::is_same_v<T, AlterTagStmt>)
            return ExecuteAlterTag(s);
        else if constexpr (std::is_same_v<T, AlterAlarmStmt>)
            return ExecuteAlterAlarm(s);
        else if constexpr (std::is_same_v<T, AlterUserStmt>)
            return ExecuteAlterUser(s);
        else if constexpr (std::is_same_v<T, DeleteStmt>)
            return ExecuteDelete(s);
        else if constexpr (std::is_same_v<T, UpdateStmt>)
            return ExecuteUpdate(s);
        else if constexpr (std::is_same_v<T, ShowStmt>)
            return ExecuteShow(s);
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

    // 严格预注册检查
    if (engine_) {
        if (!engine_->TableExists(current_db_, stmt.table_name)) {
            return {false, "Table not found: " + stmt.table_name +
                    ". Use CREATE TABLE first.", {}, {}, 0};
        }
    }

    int64_t count = 0;
    std::vector<DataBatch> batches;

    for (const auto& row : stmt.rows) {
        std::string tag;
        DataPoint point;
        point.ts = std::chrono::duration_cast<std::chrono::microseconds>(
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
                // 严格预注册：检查测点是否存在
                if (engine_ && !engine_->TagExists(current_db_, stmt.table_name, tag)) {
                    return {false, "Tag not found: " + tag +
                            " in table " + stmt.table_name +
                            ". Use CREATE TAGS first.", {}, {}, 0};
                }
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
    // SELECT FROM SNAPSHOT 走快照路径
    {
        std::string upper_table = stmt.table_name;
        for (auto& c : upper_table) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper_table == "SNAPSHOT") {
            return ExecuteSnapshot(stmt);
        }
    }
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

    // ORDER BY
    if (!stmt.order_by.empty()) {
        if (stmt.order_by == "timestamp" || stmt.order_by == "ts") {
            if (stmt.order_asc) {
                std::sort(points.begin(), points.end(),
                    [](const DataPoint& a, const DataPoint& b) { return a.ts < b.ts; });
            } else {
                std::sort(points.begin(), points.end(),
                    [](const DataPoint& a, const DataPoint& b) { return a.ts > b.ts; });
            }
        }
    }

    // LIMIT
    if (stmt.limit > 0 && static_cast<size_t>(stmt.limit) < points.size()) {
        points.resize(stmt.limit);
    }

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
            case AggType::COUNT: row.push_back(std::to_string(ar.count)); break;
            case AggType::FIRST: row.push_back(std::to_string(ar.first_val)); break;
            case AggType::LAST:  row.push_back(std::to_string(ar.last_val)); break;
            case AggType::STDDEV:
                if (ar.count > 1) {
                    double variance = (ar.sum_sq - ar.sum * ar.sum / ar.count) / ar.count;
                    row.push_back(std::to_string(std::sqrt(variance > 0 ? variance : 0)));
                } else {
                    row.push_back("0");
                }
                break;
            default:             row.push_back(std::to_string(ar.avg)); break;
        }
        result.rows.push_back(std::move(row));
    }

    result.ok = true;
    return result;
}

QueryResult Executor::ExecuteSnapshot(const SelectStmt& stmt) {
    QueryResult result;

    if (!engine_) {
        result.ok = false;
        result.error_msg = "Storage engine not available";
        return result;
    }

    auto* snapshot = engine_->GetSnapshotStore();
    if (!snapshot) {
        result.ok = false;
        result.error_msg = "Snapshot store not available";
        return result;
    }

    // 解析列选择
    bool select_all = false, select_tag = false, select_value = false, select_ts = false, select_count = false;
    for (const auto& col : stmt.columns) {
        std::string upper = col.expr;
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "*" || upper.find("COUNT") != std::string::npos) {
            select_all = true;
            if (upper.find("COUNT") != std::string::npos) select_count = true;
        }
        if (upper == "TAG") select_tag = true;
        if (upper == "VALUE") select_value = true;
        if (upper == "TS" || upper == "TIMESTAMP") select_ts = true;
    }

    // 获取数据
    std::vector<CachedSnapshot> entries;
    if (!stmt.where.tag_pattern.empty()) {
        entries = snapshot->GetByPattern(stmt.where.tag_pattern);
    } else if (!stmt.where.tag_filter.empty()) {
        CachedSnapshot entry;
        if (snapshot->Get(stmt.where.tag_filter, entry)) {
            entries.push_back(entry);
        }
    } else {
        if (select_count) {
            result.column_names = {"count"};
            std::vector<std::string> row;
            row.push_back(std::to_string(snapshot->Count()));
            result.rows.push_back(std::move(row));
            result.ok = true;
            return result;
        }
        entries = snapshot->GetAll();
    }

    // 设置列名
    if (select_all || select_tag) result.column_names.push_back("tag");
    if (select_all || select_value) result.column_names.push_back("value");
    if (select_all || select_ts) result.column_names.push_back("ts");

    // 填充数据行
    for (const auto& entry : entries) {
        std::vector<std::string> row;
        if (select_all || select_tag) row.push_back(entry.tag);
        if (select_all || select_value) row.push_back(entry.valid ? std::to_string(entry.value) : "null");
        if (select_all || select_ts) row.push_back(std::to_string(entry.timestamp));
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

    auto us = ts % 1000000;
    auto sec = ts / 1000000;
    std::time_t t = static_cast<std::time_t>(sec);

    char buf[32];
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    std::ostringstream oss;
    oss << buf << "." << std::setfill('0') << std::setw(6) << us;
    return oss.str();
}

// ── 操作名映射（权限检查用） ──
std::string Executor::GetOperationName(const SQLStmt& stmt) {
    return std::visit([](const auto& s) -> std::string {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, InsertStmt>) return "INSERT";
        else if constexpr (std::is_same_v<T, SelectStmt>) return "SELECT";
        else if constexpr (std::is_same_v<T, CreateTagStmt>) return "CREATE TAG";
        else if constexpr (std::is_same_v<T, CreateAlarmStmt>) return "CREATE ALARM";
        else if constexpr (std::is_same_v<T, CreateUserStmt>) return "CREATE USER";
        else if constexpr (std::is_same_v<T, AlterSystemStmt>) return "ALTER SYSTEM";
        else if constexpr (std::is_same_v<T, DropTagStmt>) return "DROP TAG";
        else if constexpr (std::is_same_v<T, DropAlarmStmt>) return "DROP ALARM";
        else if constexpr (std::is_same_v<T, DropUserStmt>) return "DROP USER";
        else if constexpr (std::is_same_v<T, DropTableStmt>) return "DROP TABLE";
        else if constexpr (std::is_same_v<T, DropDatabaseStmt>) return "DROP DATABASE";
        else if constexpr (std::is_same_v<T, CreateDatabaseStmt>) return "CREATE DATABASE";
        else if constexpr (std::is_same_v<T, CreateTableStmt>) return "CREATE TABLE";
        else if constexpr (std::is_same_v<T, CreateTagsStmt>) return "CREATE TAGS";
        else if constexpr (std::is_same_v<T, AlterTagStmt>) return "ALTER TAG";
        else if constexpr (std::is_same_v<T, AlterAlarmStmt>) return "ALTER ALARM";
        else if constexpr (std::is_same_v<T, AlterUserStmt>) return "ALTER USER";
        else if constexpr (std::is_same_v<T, DeleteStmt>) return "DELETE";
        else if constexpr (std::is_same_v<T, UpdateStmt>) return "UPDATE";
        else if constexpr (std::is_same_v<T, ShowStmt>) return "SHOW";
        else return "";
    }, stmt);
}

bool Executor::CheckPermission(const std::string& operation) {
    if (!auth_ || token_.empty()) return true;
    return auth_->CheckPermission(token_, operation);
}

// ── 新增 DDL 执行 ──

// 命名校验
static bool IsValidName(const std::string& name, size_t max_len) {
    if (name.empty() || name.size() > max_len) return false;
    if (name[0] == '-' || name[0] == '_') return false;
    for (char c : name) {
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

// 命名校验宏（仅用于 std::string 类型，会赋值给成员变量，暂时保留 string 对象，避免 Copy...）
#define RETURN_IF_INVALID(name, max_len, label) \
    do { \
        if (!IsValidName(name, max_len)) \
            return {false, "Invalid " label ": " + name + " (max " #max_len " chars, alphanumeric/_/-, no leading -/_)", {}, {}, 0}; \
    } while(0)

QueryResult Executor::ExecuteDropTag(const DropTagStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    engine_->DropTag(current_db_, stmt.table_name, stmt.tag_name);
    LOG_INFO("Dropped tag {} from table {}", stmt.tag_name, stmt.table_name);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteDropAlarm(const DropAlarmStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    auto* alarm = engine_->GetAlarmEngine();
    if (!alarm) return {false, "Alarm engine not available"};
    alarm->RemoveRule(stmt.alarm_name);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteDropUser(const DropUserStmt& stmt) {
    if (!auth_) return {false, "Auth not available"};
    if (!auth_->DropUser(token_, stmt.username))
        return {false, "Failed to drop user (admin only / last admin / not found)", {}, {}, 0};
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteDropTable(const DropTableStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    engine_->DropTable(current_db_, stmt.table_name);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteDropDatabase(const DropDatabaseStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    engine_->DropTable(current_db_, "");  // Delete whole db directory
    auto db_path = engine_->GetTagPath(stmt.db_name, "", "");
    auto parent = db_path.substr(0, db_path.find(stmt.db_name + "/tables/"));
    os::fs::RemoveAll(parent + stmt.db_name);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteCreateDatabase(const CreateDatabaseStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    const auto& db = stmt.db_name;
    RETURN_IF_INVALID(db, 64, "database name");
    os::fs::CreateDirectories(engine_->GetTagPath(db, "", ""));
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteUse(const UseStmt& stmt) {
    current_db_ = stmt.db_name;
    return {true, "", {"message"}, {std::vector<std::string>{"OK"}}, 1};
}

QueryResult Executor::ExecuteCreateTable(const CreateTableStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    RETURN_IF_INVALID(stmt.table_name, 64, "table name");
    auto path = engine_->GetTablePath(current_db_, stmt.table_name);
    os::fs::CreateDirectories(path);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteCreateTags(const CreateTagsStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    for (const auto& tag_def : stmt.tags) {
        RETURN_IF_INVALID(tag_def.name, 128, "tag name");
        auto path = engine_->GetTagPath(current_db_, stmt.table_name, tag_def.name);
        os::fs::CreateDirectories(path);
    }
    return {true, "", {}, {}, static_cast<int64_t>(stmt.tags.size())};
}

QueryResult Executor::ExecuteAlterTag(const AlterTagStmt& stmt) {
    // 禁止修改 type 属性（会导致已存储数据被错误解释）
    if (stmt.property == "type") {
        return {false, "Cannot alter tag type. Data type is immutable after creation.", {}, {}, 0};
    }
    return {true, "", {"message"}, {std::vector<std::string>{"OK"}}, 1};
}

QueryResult Executor::ExecuteAlterAlarm(const AlterAlarmStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    auto* alarm = engine_->GetAlarmEngine();
    if (!alarm) return {false, "Alarm engine not available"};
    if (!alarm->AlterRule(stmt.alarm_name, stmt.property, stmt.value))
        return {false, "Failed to alter alarm", {}, {}, 0};
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteAlterUser(const AlterUserStmt& stmt) {
    if (!auth_) return {false, "Auth not available"};
    if (!auth_->AlterUser(token_, stmt.username, stmt.property, stmt.value))
        return {false, "Failed to alter user (admin only / not found)", {}, {}, 0};
    return {true, "", {}, {}, 1};
}

// ── 新增 DML 执行 ──

QueryResult Executor::ExecuteDelete(const DeleteStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    engine_->DropTag(current_db_, stmt.table_name, stmt.tag_filter);
    return {true, "", {}, {}, 1};
}

QueryResult Executor::ExecuteUpdate(const UpdateStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    // Simple: drop and re-insert
    engine_->DropTag(current_db_, stmt.table_name, stmt.tag_filter);
    DataPoint p;
    p.ts = stmt.exact_time;
    p.value = stmt.new_value;
    engine_->Write(current_db_ + ":" + stmt.table_name + ":" + stmt.tag_filter,
                   DataPoint{stmt.exact_time, static_cast<double>(stmt.new_value)});
    return {true, "", {}, {}, 1};
}

// ── SHOW 执行 ──

QueryResult Executor::ExecuteShow(const ShowStmt& stmt) {
    switch (stmt.type) {
        case ShowStmt::Type::DATABASES: {
            QueryResult res;
            res.column_names = {"Database"};
            std::vector<os::fs::DirEntry> entries;
            if (os::fs::ListDirectory("./data/hot", entries)) {
                for (auto& e : entries) {
                    if (e.is_directory)
                        res.rows.push_back({e.name});
                }
            }
            if (res.rows.empty()) res.rows.push_back({"default"});
            return res;
        }
        case ShowStmt::Type::TABLES: {
            QueryResult res;
            res.column_names = {"Table"};
            std::string db = stmt.filter_db.empty() ? current_db_ : stmt.filter_db;
            std::string tables_dir = engine_->GetTablePath(db, "");
            // Remove trailing empty table: "hot/db/tables/" → "hot/db/tables"
            if (!tables_dir.empty() && tables_dir.back() == '/')
                tables_dir.pop_back();
            std::vector<os::fs::DirEntry> entries;
            if (os::fs::ListDirectory(tables_dir, entries)) {
                for (auto& e : entries) {
                    if (e.is_directory)
                        res.rows.push_back({e.name});
                }
            }
            return res;
        }
        case ShowStmt::Type::TAGS: {
            QueryResult res;
            res.column_names = {"Tag"};
            std::string table = stmt.filter_table;
            if (table.empty()) {
                return {false, "SHOW TAGS requires FROM <table>", {}, {}, 0};
            }
            std::string tag_dir = engine_->GetTagPath(current_db_, table, "");
            std::vector<os::fs::DirEntry> entries;
            if (os::fs::ListDirectory(tag_dir, entries)) {
                for (auto& e : entries) {
                    if (e.is_directory) res.rows.push_back({e.name});
                }
            }
            return res;
        }
        case ShowStmt::Type::USERS: {
            if (!auth_) return {false, "Auth not available"};
            auto users = auth_->GetUsers(token_);
            QueryResult res;
            res.column_names = {"username", "role"};
            for (const auto& u : users) {
                std::string role_str = (u.role == UserRole::ADMIN) ? "admin" :
                    (u.role == UserRole::OPERATOR) ? "operator" : "viewer";
                res.rows.push_back({u.name, role_str});
            }
            return res;
        }
        case ShowStmt::Type::ALARMS: {
            if (!engine_) return {false, "Engine not available"};
            auto* alarm = engine_->GetAlarmEngine();
            if (!alarm) return {false, "Alarm engine not available"};
            auto rules = alarm->GetRules();
            QueryResult res;
            res.column_names = {"name", "tag", "condition", "actions"};
            for (const auto& r : rules) {
                std::string actions;
                for (size_t i = 0; i < r.actions.size(); i++) {
                    if (i > 0) actions += ",";
                    actions += r.actions[i];
                }
                res.rows.push_back({r.name, r.tag_name, r.condition, actions});
            }
            return res;
        }
    }
    return {false, "Unknown SHOW type", {}, {}, 0};
}

} // namespace minitsdb
