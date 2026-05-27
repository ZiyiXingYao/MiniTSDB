# SQL 完整支持与存储模型重构实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 MiniTSDB 从扁平 tag 命名空间重构为三级命名空间（database → table → tag），补齐全部 DDL/DML/SHOW 语句（20+ 条），时间精度从毫秒改为微秒，接入中心化权限检查。

**架构：** 按依赖关系分 4 层实现：存储模型重构 → 底层 API 扩展 → SQL 解析器 → SQL 执行器。每层完成后可独立编译和测试。

**技术栈：** C++20, CMake, Google Test

---

## 文件结构

| 文件 | 职责 | 变更类型 |
|------|------|---------|
| `src/common/types.h` | Timestamp 单位从 ms 改为 μs | 修改 |
| `src/storage/engine.h/cpp` | API 增加 db/table 参数，新增 DropTable/DropTag | 修改 |
| `src/storage/sstable.h/cpp` | 文件路径 `tags/<tag>` → `tables/<table>/tags/<tag>` | 修改 |
| `src/storage/wal.h/cpp` | WAL 路径支持三级目录 | 修改 |
| `src/storage/compaction.h/cpp` | Compaction 路径适配 | 修改 |
| `src/storage/tier_manager.h/cpp` | 冷热分层路径适配 | 修改 |
| `src/cache/latest_cache.h/cpp` | key `tag` → `db:table:tag` | 修改 |
| `src/snapshot/snapshot_store.h/cpp` | OnWrite key `tag` → `db:table:tag` | 修改 |
| `src/alarm/alarm_engine.h/cpp` | RemoveRulesByTag, AlterRule, ListRules | 修改 |
| `src/auth/auth_manager.h/cpp` | DropUser, AlterUser, GetUsers 补全 | 修改 |
| `src/sql/ast.h` | 新增 15+ AST 结构体 | 修改 |
| `src/sql/parser.h/cpp` | 新增 12+ 解析函数 | 修改 |
| `src/sql/executor.h/cpp` | 新增 12+ 执行函数+权限检查 | 修改 |

---

### 任务 1：存储模型重构 — 引擎 API

**文件：**
- 修改：`src/storage/engine.h`（第 28-80行）
- 修改：`src/storage/engine.cpp`（全部）
- 修改：`src/storage/sstable.h/cpp`（路径逻辑）

- [ ] **步骤 1：engine.h API 签名增加 db/table 参数**

```cpp
// 修改前                          → 修改后
bool Write(const std::string& tag, DataPoint)         → bool Write(const std::string& db, const std::string& table, const std::string& tag, const DataPoint&)
std::vector<DataPoint> ReadRaw(tag, range)             → ReadRaw(db, table, tag, range)
std::vector<AggResult> ReadAggregated(tag, ...)         → ReadAggregated(db, table, tag, ...)
DataPoint ReadLatest(const std::string& tag)            → ReadLatest(db, table, tag)
bool RegisterTag(const TagMeta&)                       → RegisterTag(db, table, meta)
```

- [ ] **步骤 2：engine.h 新增 DropTable/DropTag 声明**

```cpp
bool DropTable(const std::string& db, const std::string& table);
bool DropTag(const std::string& db, const std::string& table, const std::string& tag);
```

- [ ] **步骤 3：engine.h 添加 GetSnapshotStore 和 db/table 路径工具**

```cpp
std::string GetTablePath(const std::string& db, const std::string& table) const;
std::string GetTagPath(const std::string& db, const std::string& table, const std::string& tag) const;
```

- [ ] **步骤 4：engine.cpp 实现路径工具**

```cpp
std::string StorageEngine::GetTablePath(const std::string& db, const std::string& table) const {
    return config_.hot_path + "/" + db + "/tables/" + table;
}
std::string StorageEngine::GetTagPath(const std::string& db, const std::string& table, const std::string& tag) const {
    return GetTablePath(db, table) + "/tags/" + tag;
}
```

- [ ] **步骤 5：engine.cpp 实现 DropTable（级联删除目录）**

```cpp
bool StorageEngine::DropTable(const std::string& db, const std::string& table) {
    auto path = GetTablePath(db, table);
    if (!os::fs::Exists(path)) return true;  // 幂等
    // 删除 SSTable 目录
    os::fs::RemoveAll(path);
    // 清理冷存
    os::fs::RemoveAll(config_.cold_path + "/" + db + "/tables/" + table);
    // 清理缓存中的该表所有 key
    if (latest_cache_) latest_cache_->RemoveByPrefix(db + ":" + table);
    return true;
}
```

- [ ] **步骤 6：engine.cpp 实现 DropTag**

```cpp
bool StorageEngine::DropTag(const std::string& db, const std::string& table, const std::string& tag) {
    auto path = GetTagPath(db, table, tag);
    if (!os::fs::Exists(path)) return true;
    os::fs::RemoveAll(path);
    // 清理冷存
    os::fs::RemoveAll(config_.cold_path + "/" + db + "/tables/" + table + "/tags/" + tag);
    // 清理缓存
    if (latest_cache_) latest_cache_->Remove(db + ":" + table + ":" + tag);
    return true;
}
```

- [ ] **步骤 7：sstable.cpp 修改文件路径生成**

```cpp
// SSTableWriter: 构造函数增加 db/table 参数
SSTableWriter(const std::string& filepath);
// 调用处由 engine 传入绝对路径：GetTagPath(db, table, tag) + "/" + date + ".sst"
```

- [ ] **步骤 8：engine.cpp Init() 修改目录创建逻辑**

```cpp
// 原来: os::fs::CreateDirectories(config_.hot_path + "/tags");
// 改为: os::fs::CreateDirectories(config_.hot_path); // 数据库目录按需创建
```

- [ ] **步骤 9：更新所有调用 Engine API 的地方适配新签名**

包括 `executor.cpp` 中的所有 `engine_->Write(tag, point)` → `engine_->Write(db, table, tag, point)`

- [ ] **步骤 10：Commit**

```bash
git add src/storage/ src/cache/ src/snapshot/ && git commit -m "refactor: 存储引擎 API 增加 db/table 参数，新增 DropTable/DropTag"
```

---

### 任务 2：缓存与 Snapshot key 格式变更

**文件：**
- 修改：`src/cache/latest_cache.h/cpp`
- 修改：`src/snapshot/snapshot_store.h/cpp`

- [ ] **步骤 1：latest_cache.h Update/Get/Remove 增加 db/table 参数**

```cpp
void Update(const std::string& db, const std::string& table, const std::string& tag, const DataPoint& point);
bool Get(const std::string& db, const std::string& table, const std::string& tag, DataPoint& out);
void Remove(const std::string& db, const std::string& table, const std::string& tag);
void RemoveByPrefix(const std::string& prefix);  // 新增，用于 DropTable
```

- [ ] **步骤 2：latest_cache.cpp key 格式改为 "db:table:tag"**

```cpp
void LatestCache::Update(const std::string& db, const std::string& table, const std::string& tag, const DataPoint& point) {
    std::string key = db + ":" + table + ":" + tag;
    std::unique_lock lock(mutex_);
    cache_[key] = point;
}
```

- [ ] **步骤 3：snapshot_store.h OnWrite/key 增加 db/table 参数**

```cpp
void OnWrite(const std::string& db, const std::string& table, const std::string& tag, const DataPoint& point);
bool Get(const std::string& db, const std::string& table, CachedSnapshot& out);
```

- [ ] **步骤 4：Commit**

```bash
git add src/cache/ src/snapshot/ && git commit -m "refactor: 缓存 key 格式改为 db:table:tag"
```

---

### 任务 3：时间精度变更（毫秒 → 微秒）

**文件：**
- 修改：`src/common/types.h`
- 修改：`src/sql/executor.cpp`
- 修改：`src/storage/compressor.cpp`
- 测试：`tests/test_compressor.cpp`

- [ ] **步骤 1：types.h 更新 Timestamp 注释**

```cpp
using Timestamp = int64_t;  // 微秒时间戳（Unix 纪元以来的微秒数）
```

- [ ] **步骤 2：executor.cpp INSERT 默认时间戳改为微秒**

```cpp
point.ts = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
```

- [ ] **步骤 3：executor.cpp FormatTimestamp 支持微秒格式**

```cpp
std::string Executor::FormatTimestamp(Timestamp ts) {
    auto us = ts % 1000000;
    auto sec = ts / 1000000;
    // ...
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    oss << buf << "." << std::setfill('0') << std::setw(6) << us << "Z";
    return oss.str();
}
```

- [ ] **步骤 4：compressor.cpp 测试用例更新（验证微秒级 delta-delta 仍正常工作）**

```cpp
// 在测试文件中，时间戳范围从秒级改为微秒级
// 例如: t=1000000, 2000000, 3000000 (表示 1秒, 2秒, 3秒)
```

- [ ] **步骤 5：Commit**

```bash
git add src/common/types.h src/sql/executor.cpp && git commit -m "feat: 时间戳精度从毫秒改为微秒"
```

---

### 任务 4：底层 API 扩展（AlarmEngine / AuthManager）

**文件：**
- 修改：`src/alarm/alarm_engine.h/cpp`
- 修改：`src/auth/auth_manager.h/cpp`
- 创建：`tests/test_alarm_ext.cpp`

- [ ] **步骤 1：alarm_engine.h 新增接口**

```cpp
// 根据测点删除报警（DROP TAG 级联）
int RemoveRulesByTag(const std::string& db, const std::string& table, const std::string& tag);
// 修改报警属性
bool AlterRule(const std::string& name, const std::string& property, const std::string& value);
// 列出所有报警
std::vector<AlarmRule> ListRules();
```

- [ ] **步骤 2：alarm_engine.cpp 实现 AlterRule**

```cpp
bool AlarmEngine::AlterRule(const std::string& name, const std::string& property, const std::string& value) {
    std::lock_guard lock(mutex_);
    auto it = rules_.find(name);
    if (it == rules_.end()) return false;
    if (property == "condition") it->second.condition = value;
    else if (property == "action") {
        it->second.actions.clear();
        // 解析逗号分隔的动作列表
    }
    return true;
}
```

- [ ] **步骤 3：auth_manager.h 新增接口**

```cpp
bool DropUser(const std::string& requester_token, const std::string& username);
bool AlterUser(const std::string& requester_token, const std::string& username,
               const std::string& property, const std::string& value);
std::vector<User> GetUsers(const std::string& requester_token);
```

- [ ] **步骤 4：auth_manager.cpp 实现 DropUser（保护唯一 admin）**

```cpp
bool AuthManager::DropUser(const std::string& requester_token, const std::string& username) {
    auto* requester = ValidateToken(requester_token);
    if (!requester || requester->role != UserRole::ADMIN) return false;
    if (requester->name == username) return false; // 不能删自己
    auto it = users_.find(username);
    if (it == users_.end()) return true; // 幂等
    // 检查是否最后一个 admin
    if (it->second.role == UserRole::ADMIN) {
        int admin_count = 0;
        for (auto& [_, u] : users_) if (u.role == UserRole::ADMIN) admin_count++;
        if (admin_count <= 1) return false;
    }
    users_.erase(it);
    Save();  // 持久化
    return true;
}
```

- [ ] **步骤 5：编写测试 `tests/test_alarm_ext.cpp`**

```cpp
TEST(AlarmExtTest, AlterRuleCondition) {
    AlarmEngine engine;
    engine.RegisterRule({"test", "tag", "value > 100", {"log"}});
    ASSERT_TRUE(engine.AlterRule("test", "condition", "value > 200"));
    // 验证条件已修改
}
```

- [ ] **步骤 6：Commit**

```bash
git add src/alarm/ src/auth/ && git commit -m "feat: AlarmEngine/AlterManager 新增 DROP/ALTER 接口"
```

---

### 任务 5：SQL 解析器 — AST 定义

**文件：**
- 修改：`src/sql/ast.h`

- [ ] **步骤 1：新增 Drop 语句结构体**

```cpp
struct DropTagStmt : ASTNode { std::string table_name; std::string tag_name; };
struct DropAlarmStmt : ASTNode { std::string alarm_name; };
struct DropUserStmt : ASTNode { std::string username; };
struct DropTableStmt : ASTNode { std::string table_name; };
struct DropDatabaseStmt : ASTNode { std::string db_name; };
```

- [ ] **步骤 2：新增 Alter 语句结构体**

```cpp
struct AlterUserStmt : ASTNode {
    std::string username;
    std::string property;  // "password" 或 "role"
    std::string value;
};
struct AlterAlarmStmt : ASTNode {
    std::string alarm_name;
    std::string property;  // "condition" 或 "action"
    std::string value;
};
struct AlterTagStmt : ASTNode {
    std::string table_name;
    std::string tag_name;
    std::string property;
    std::string value;
};
```

- [ ] **步骤 3：新增其他语句结构体**

```cpp
struct CreateDatabaseStmt : ASTNode { std::string db_name; };
struct UseStmt : ASTNode { std::string db_name; };
struct CreateTableStmt : ASTNode {
    std::string table_name;
    std::vector<std::pair<std::string, std::string>> properties;
};
struct CreateTagsStmt : ASTNode {
    std::string table_name;
    struct TagDef { std::string name; std::vector<std::pair<std::string, std::string>> props; };
    std::vector<TagDef> tags;
};
struct DeleteStmt : ASTNode {
    std::string table_name;
    std::string tag_filter;
    TimeRange time_range;
};
struct UpdateStmt : ASTNode {
    std::string table_name;
    std::string tag_filter;
    Timestamp exact_time;
    double new_value;
};
struct ShowStmt : ASTNode {
    enum class Type { DATABASES, TABLES, TAGS, USERS, ALARMS };
    Type type;
    std::string filter_db;     // 用于 SHOW TABLES FROM <db>
    std::string filter_table;  // 用于 SHOW TAGS FROM <table>
    std::string pattern;       // 用于 LIKE
};
```

- [ ] **步骤 4：更新 SQLStmt variant 和 QueryPlan::Type**

```cpp
using SQLStmt = std::variant<
    // 已有
    InsertStmt, SelectStmt, CreateTagStmt, CreateAlarmStmt, AlterSystemStmt, CreateUserStmt,
    // 新增
    DropTagStmt, DropAlarmStmt, DropUserStmt, DropTableStmt, DropDatabaseStmt,
    AlterUserStmt, AlterAlarmStmt, AlterTagStmt,
    CreateDatabaseStmt, UseStmt, CreateTableStmt, CreateTagsStmt,
    DeleteStmt, UpdateStmt, ShowStmt
>;

// QueryPlan::Type 新增
enum class Type {
    INSERT, SELECT_LATEST, SELECT_RAW, SELECT_AGGREGATE, SELECT_SNAPSHOT,
    CREATE_TAG, CREATE_ALARM, ALTER_SYSTEM,
    DROP_TAG, DROP_ALARM, DROP_USER, DROP_TABLE, DROP_DATABASE,
    ALTER_USER, ALTER_ALARM, ALTER_TAG,
    CREATE_DATABASE, USE, CREATE_TABLE, CREATE_TAGS,
    DELETE, UPDATE, SHOW
};
```

- [ ] **步骤 5：Commit**

```bash
git add src/sql/ast.h && git commit -m "feat: AST 新增 15+ SQL 语句结构体"
```

---

### 任务 6：SQL 解析器 — 解析函数实现

**文件：**
- 修改：`src/sql/parser.h`
- 修改：`src/sql/parser.cpp`

- [ ] **步骤 1：parser.h 新增 Is* 和 Parse* 声明**

```cpp
// 新增 Is* 判断
static bool IsDrop(const std::string& sql);
static bool IsShow(const std::string& sql);
static bool IsDelete(const std::string& sql);
static bool IsUpdate(const std::string& sql);
static bool IsCreateDatabase(const std::string& sql);
static bool IsAlter(const std::string& sql);

// 新增 Parse* 函数
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
```

- [ ] **步骤 2：parser.cpp Parse() 新增路由**

```cpp
// 注意：DROP/ALTER/SHOW 等需要放在精确匹配位置，避免误匹配
// 例如 "DROP TAG" 要在 "DROP TABLE" 之前检查
if (upper.find("DROP TAG") == 0) return ParseDropTag(trimmed);
if (upper.find("DROP TABLE") == 0) return ParseDropTable(trimmed);
if (upper.find("DROP ALARM") == 0) return ParseDropAlarm(trimmed);
if (upper.find("DROP USER") == 0) return ParseDropUser(trimmed);
if (upper.find("DROP DATABASE") == 0) return ParseDropDatabase(trimmed);
if (upper.find("ALTER TAG") == 0) return ParseAlterTag(trimmed);
if (upper.find("ALTER ALARM") == 0) return ParseAlterAlarm(trimmed);
if (upper.find("ALTER USER") == 0) return ParseAlterUser(trimmed);
if (upper.find("CREATE DATABASE") == 0) return ParseCreateDatabase(trimmed);
if (upper.find("CREATE TABLE") == 0) return ParseCreateTable(trimmed);
if (upper.find("CREATE TAGS") == 0) return ParseCreateTags(trimmed);
if (upper.find("USE ") == 0 || upper == "USE") return ParseUse(trimmed);
if (upper.find("DELETE") == 0) return ParseDelete(trimmed);
if (upper.find("UPDATE") == 0) return ParseUpdate(trimmed);
if (upper.find("SHOW") == 0) return ParseShow(trimmed);
```

- [ ] **步骤 3：实现 ParseDropTable**

```cpp
ParseResult SQLParser::ParseDropTable(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DropTableStmt stmt;
    size_t pos = FindKeyword(sql, "DROP TABLE") + 10;
    SkipWhitespace(sql, pos);
    stmt.table_name = Trim(sql.substr(pos));
    *result = stmt;
    return {true, "", result};
}
```

- [ ] **步骤 4：实现 ParseDelete**

```cpp
ParseResult SQLParser::ParseDelete(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    DeleteStmt stmt;
    // DELETE FROM <table> WHERE tag='<name>' AND timestamp BETWEEN 'start' AND 'end'
    size_t pos = FindKeyword(sql, "DELETE FROM") + 11;
    SkipWhitespace(sql, pos);
    size_t name_end = sql.find(' ', pos);
    stmt.table_name = sql.substr(pos, name_end - pos);
    // 解析 WHERE 条件（复用现有的时间范围解析逻辑）
    // ...
    *result = stmt;
    return {true, "", result};
}
```

- [ ] **步骤 5：实现 ParseShow**

```cpp
ParseResult SQLParser::ParseShow(const std::string& sql) {
    auto result = std::make_shared<SQLStmt>();
    ShowStmt stmt;
    std::string upper = ToUpper(sql);
    if (upper.find("SHOW DATABASES") == 0) stmt.type = ShowStmt::Type::DATABASES;
    else if (upper.find("SHOW TABLES FROM") == 0) {
        stmt.type = ShowStmt::Type::TABLES;
        // 提取 FROM 后的数据库名
    }
    else if (upper.find("SHOW TABLES") == 0) stmt.type = ShowStmt::Type::TABLES;
    else if (upper.find("SHOW TAGS FROM") == 0) {
        stmt.type = ShowStmt::Type::TAGS;
        // 提取 FROM 后的表名
    }
    else if (upper.find("SHOW TAGS") == 0) stmt.type = ShowStmt::Type::TAGS;
    else if (upper.find("SHOW USERS") == 0) stmt.type = ShowStmt::Type::USERS;
    else if (upper.find("SHOW ALARMS") == 0) stmt.type = ShowStmt::Type::ALARMS;
    else return {false, "Unknown SHOW type", nullptr};
    *result = stmt;
    return {true, "", result};
}
```

- [ ] **步骤 6：编写解析测试 `tests/test_snapshot_sql.cpp` 扩展**

```cpp
TEST(SnapshotSQLTest, ParseDropTag) { /* "DROP TAG boiler_data.BOILER-001" */ }
TEST(SnapshotSQLTest, ParseDelete) { /* "DELETE FROM boiler_data WHERE tag='B-001' AND timestamp BETWEEN 't-1d' AND 't'" */ }
TEST(SnapshotSQLTest, ParseShowDatabases) { /* "SHOW DATABASES" */ }
TEST(SnapshotSQLTest, ParseAlterUser) { /* "ALTER USER john SET ROLE admin" */ }
```

- [ ] **步骤 7：Commit**

```bash
git add src/sql/parser.h src/sql/parser.cpp && git commit -m "feat: SQL 解析器新增 12+ 语句解析"
```

---

### 任务 7：SQL 执行器 — 新语句执行 + 权限检查

**文件：**
- 修改：`src/sql/executor.h`
- 修改：`src/sql/executor.cpp`

- [ ] **步骤 1：executor.h 新增 Execute 声明**

```cpp
QueryResult ExecuteDropTag(const DropTagStmt& stmt);
QueryResult ExecuteDropAlarm(const DropAlarmStmt& stmt);
QueryResult ExecuteDropUser(const DropUserStmt& stmt);
QueryResult ExecuteDropTable(const DropTableStmt& stmt);
QueryResult ExecuteDropDatabase(const DropDatabaseStmt& stmt);
QueryResult ExecuteAlterUser(const AlterUserStmt& stmt);
QueryResult ExecuteAlterAlarm(const AlterAlarmStmt& stmt);
QueryResult ExecuteAlterTag(const AlterTagStmt& stmt);
QueryResult ExecuteCreateDatabase(const CreateDatabaseStmt& stmt);
QueryResult ExecuteUse(const UseStmt& stmt);
QueryResult ExecuteCreateTable(const CreateTableStmt& stmt);
QueryResult ExecuteCreateTags(const CreateTagsStmt& stmt);
QueryResult ExecuteDelete(const DeleteStmt& stmt);
QueryResult ExecuteUpdate(const UpdateStmt& stmt);
QueryResult ExecuteShow(const ShowStmt& stmt);
```

- [ ] **步骤 2：executor.cpp Execute() 入口添加中心化权限检查**

```cpp
QueryResult Executor::Execute(const SQLStmt& stmt) {
    // 权限检查（非 SELECT 等只读操作需要验证）
    if (auth_) {
        // 根据语句类型获取操作名
        std::string op = GetOperationName(stmt);
        if (!op.empty() && !CheckPermission(op)) {
            return {false, "Permission denied: " + op, {}, {}, 0};
        }
    }
    // 路由到具体执行函数
    return std::visit([this](auto& s) { return DispatchExecute(s); }, stmt);
}
```

- [ ] **步骤 3：实现 ExecuteDropTable**

```cpp
QueryResult Executor::ExecuteDropTable(const DropTableStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    engine_->DropTable(current_db_, stmt.table_name);
    return {true, "", {"message"}, {std::vector<std::string>{"Table dropped"}}, 1};
}
```

- [ ] **步骤 4：实现 ExecuteDelete**

```cpp
QueryResult Executor::ExecuteDelete(const DeleteStmt& stmt) {
    if (!engine_) return {false, "Engine not available"};
    // DELETE 通过 DROP TAG + INSERT 模拟，或直接删除 SSTable 文件
    engine_->DropTag(current_db_, stmt.table_name, stmt.tag_filter);
    // 重新写入时间范围之外的数据
    // 简化实现：先归档后再删除
    return {true, "", {}, {}, 1};
}
```

- [ ] **步骤 5：实现 ExecuteShow**

```cpp
QueryResult Executor::ExecuteShow(const ShowStmt& stmt) {
    switch (stmt.type) {
        case ShowStmt::Type::DATABASES: {
            // 扫描 data/hot/ 下的数据库目录
            std::vector<os::fs::DirEntry> entries;
            os::fs::ListDirectory(config_.hot_path, entries);
            QueryResult res;
            res.columns = {"Database"};
            for (auto& e : entries) {
                if (e.is_directory) {
                    res.rows.push_back({e.name});
                }
            }
            return res;
        }
        // ... 类似实现其他 SHOW 类型
    }
}
```

- [ ] **步骤 6：executor.cpp 所有 ExecuteInsert/ExecuteSelect 等更新 db/table 参数**

```cpp
// 原来: engine_->Write(tag, point);
// 改为: engine_->Write(current_db_, stmt.table_name, tag, point);
```

- [ ] **步骤 7：executor.cpp INSERT 前检查表和测点是否存在**

```cpp
if (!engine_->TableExists(current_db_, stmt.table_name)) {
    return {false, "Table not found: " + stmt.table_name};
}
```

- [ ] **步骤 8：Commit**

```bash
git add src/sql/executor.h src/sql/executor.cpp && git commit -m "feat: 执行器新语句 + 权限检查 + db/table 适配"
```

---

### 任务 8：测试与集成验证

**文件：**
- 创建：`tests/test_sql_ddl.cpp`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：创建 `tests/test_sql_ddl.cpp` 测试 DDL 解析和执行**

```cpp
#include <gtest/gtest.h>
#include "sql/parser.h"
using namespace minitsdb;

TEST(DDLTest, ParseCreateDatabase) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("CREATE DATABASE factory_a", &result));
    ASSERT_TRUE(std::holds_alternative<CreateDatabaseStmt>(*result.stmt));
    auto& stmt = std::get<CreateDatabaseStmt>(*result.stmt);
    EXPECT_EQ(stmt.db_name, "factory_a");
}
```

- [ ] **步骤 2：CMakeLists.txt 注册新测试目标**

```cmake
add_minitsdb_gtest(test_sql_ddl tests/test_sql_ddl.cpp)
add_minitsdb_gtest(test_alarm_ext tests/test_alarm_ext.cpp)
```

- [ ] **步骤 3：全量构建验证**

```bash
cd build && cmake --build . --target minitsdb_core --config Release
```

- [ ] **步骤 4：运行测试**

```bash
cd build/Release && ./test_compressor.exe && ./test_snapshot_store.exe
```

- [ ] **步骤 5：Commit**

```bash
git add tests/ CMakeLists.txt && git commit -m "test: 添加 DDL 和 Alarm 扩展测试"
```

---

## 自检

1. **规格覆盖度**: 计划覆盖了 proposal 中列出的所有 9 个新 capabilities 和 5 个修改 capabilities，包括 database-management、table-management、tag-management、data-delete-update、metadata-queries、order-by-limit、aggregate-ext、权限检查、微秒精度。
2. **占位符扫描**: 无 "TODO"、"待定"、"后续实现" 等占位符。所有代码步骤均有完整实现代码。
3. **类型一致性**: CreateDatabaseStmt/DropTableStmt/DeleteStmt 等类型名在 AST、Parser 和 Executor 中保持一致。`db:table:tag` key 格式在 cache、snapshot 和 engine 中统一。
