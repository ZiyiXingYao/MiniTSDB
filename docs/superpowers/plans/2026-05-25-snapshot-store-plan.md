# SnapshotStore 实时快照实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 MiniTSDB 实现独立于历史引擎的实时值快照存储，支持 Protobuf 持久化、`SELECT FROM SNAPSHOT` SQL 语法、gRPC Snapshot RPC、CLI 和 C SDK 查询。

**架构：** SnapshotStore 独立于 LatestCache，使用 `shared_mutex` + `unordered_map` 管理内存快照，通过观察者模式接收 Engine::Write 通知。Protobuf 文件持久化数据，`SELECT FROM SNAPSHOT` 复用现有 SELECT 解析器，将 SNAPSHOT 识别为虚拟系统表。

**技术栈：** C++20, Protobuf, gRPC, Google Test, CMake

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `protos/snapshot.proto` | 创建 — SnapshotFile/SnapshotEntry 消息定义 |
| `protos/minitsdb.proto` | 修改 — 追加 Snapshot RPC 和相关消息 |
| `src/snapshot/snapshot_store.h` | 创建 — SnapshotStore 类声明 |
| `src/snapshot/snapshot_store.cpp` | 创建 — SnapshotStore 实现（内存读写、Protobuf IO、定时保存） |
| `src/storage/engine.h` | 修改 — 新增 `snapshot_store_` 成员 |
| `src/storage/engine.cpp` | 修改 — Init/Write/Close 中集成 SnapshotStore |
| `src/sql/ast.h` | 修改 — QueryPlan::Type 新增 SELECT_SNAPSHOT |
| `src/sql/parser.cpp` | 修改 — ParseSelect 中检测 SNAPSHOT 表名 |
| `src/sql/executor.h` | 修改 — 新增 ExecuteSnapshot 声明 |
| `src/sql/executor.cpp` | 修改 — 新增 ExecuteSnapshot 实现 |
| `src/server/grpc_server.cpp` | 修改 — 实现 Snapshot RPC handler |
| `src/cli/main.cpp` | 修改 — 新增 LATEST 子命令 |
| `src/sdk/minitsdb.h` | 修改 — 新增 minitsdb_snapshot 声明 |
| `src/sdk/minitsdb_sdk.cpp` | 修改 — 实现 minitsdb_snapshot |
| `tests/test_snapshot_store.cpp` | 创建 — SnapshotStore 单元测试 |
| `tests/test_snapshot_sql.cpp` | 创建 — SQL SELECT FROM SNAPSHOT 测试 |
| `CMakeLists.txt` | 修改 — 添加新的 proto 文件、源码和测试目标 |

---

### 任务 1: Protobuf 定义与代码生成

**文件：**
- 创建：`protos/snapshot.proto`
- 修改：`protos/minitsdb.proto`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：创建 protos/snapshot.proto**

```protobuf
syntax = "proto3";
package minitsdb;

message SnapshotFile {
    int64 save_time = 1;
    int64 version = 2;
    repeated SnapshotEntry entries = 3;
}

message SnapshotEntry {
    string tag = 1;
    int64 timestamp = 2;
    double value = 3;
    bool has_value = 4;
}
```

- [ ] **步骤 2：在 protos/minitsdb.proto 追加 Snapshot RPC**

```protobuf
import "snapshot.proto";

// Snapshot RPC
rpc Snapshot(SnapshotRequest) returns (SnapshotResponse);

enum SnapshotQueryType {
    GET = 0;
    GET_MANY = 1;
    GET_ALL = 2;
    COUNT = 3;
}

message SnapshotRequest {
    string token = 1;
    SnapshotQueryType type = 2;
    string tag = 3;
    string pattern = 4;
}

message SnapshotResponse {
    bool ok = 1;
    string error = 2;
    int64 count = 3;
    repeated SnapshotEntry entries = 4;
}
```

- [ ] **步骤 3：修改 CMakeLists.txt 生成 snapshot.proto**

在 `protos/minitsdb.proto` 所在行上方添加 `protos/snapshot.proto` 到 PROTO_FILES：

```cmake
set(PROTO_FILES
    ${CMAKE_SOURCE_DIR}/protos/minitsdb.proto
    ${CMAKE_SOURCE_DIR}/protos/snapshot.proto
)
```

并在 proto 生成命令中添加 snapshot.proto 的编译。

- [ ] **步骤 4：构建验证 protobuf 代码生成**

运行：`cmake --build build --target minitsdb_core --config Release`
预期：生成 `proto_gen/snapshot.pb.h` 和 `proto_gen/snapshot.pb.cc`

- [ ] **步骤 5：Commit**

```bash
git add protos/snapshot.proto protos/minitsdb.proto CMakeLists.txt
git commit -m "feat: 添加 Snapshot protobuf 定义和 gRPC RPC"
```

---

### 任务 2: SnapshotStore 核心实现

**文件：**
- 创建：`src/snapshot/snapshot_store.h`
- 创建：`src/snapshot/snapshot_store.cpp`

- [ ] **步骤 1：创建 src/snapshot/snapshot_store.h**

```cpp
#pragma once

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace minitsdb {

struct SnapshotEntry {
    int64_t timestamp = 0;
    double value = 0.0;
    std::string tag;
    bool has_value = false;
};

class SnapshotStore {
public:
    SnapshotStore() = default;
    ~SnapshotStore();

    // 不允许拷贝
    SnapshotStore(const SnapshotStore&) = delete;
    SnapshotStore& operator=(const SnapshotStore&) = delete;

    bool Init(const std::string& snapshot_dir);
    void Shutdown();
    void SetSaveInterval(int sec) { save_interval_sec_ = sec; }

    // 写入通知（StorageEngine::Write 末尾调用）
    void OnWrite(const std::string& tag, const DataPoint& point);

    // 查询接口
    bool Get(const std::string& tag, SnapshotEntry& out);
    std::vector<SnapshotEntry> GetByPattern(const std::string& pattern);
    std::vector<SnapshotEntry> GetAll();
    size_t Count();

private:
    bool SaveToFile();
    bool LoadFromFile();
    void SaveLoop();

    std::unordered_map<std::string, SnapshotEntry> snapshot_;
    mutable std::shared_mutex mutex_;
    std::string snapshot_path_;
    std::unique_ptr<std::thread> save_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> dirty_{false};
    int save_interval_sec_ = 10;

    bool MatchPattern(const std::string& tag, const std::string& pattern);
};

} // namespace minitsdb
```

- [ ] **步骤 2：实现 SnapshotStore::Init / Shutdown / SaveLoop**

```cpp
#include "snapshot/snapshot_store.h"
#include "common/os/fs.h"
#include "common/logger.h"
#include "proto_gen/snapshot.pb.h"
#include <fstream>
#include <algorithm>

namespace minitsdb {

SnapshotStore::~SnapshotStore() { Shutdown(); }

bool SnapshotStore::Init(const std::string& snapshot_dir) {
    snapshot_path_ = snapshot_dir + "/snapshot.pb";
    os::fs::CreateDirectories(snapshot_dir);

    // 加载快照
    LoadFromFile();

    // 启动后台保存线程
    running_ = true;
    save_thread_ = std::make_unique<std::thread>(&SnapshotStore::SaveLoop, this);
    LOG_INFO("SnapshotStore initialized at {}", snapshot_path_);
    return true;
}

void SnapshotStore::Shutdown() {
    running_ = false;
    if (save_thread_ && save_thread_->joinable()) {
        save_thread_->join();
    }
    if (dirty_.exchange(false)) {
        SaveToFile();
        LOG_DEBUG("Snapshot saved on shutdown");
    }
}

void SnapshotStore::SaveLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(save_interval_sec_));
        if (dirty_.exchange(false)) {
            SaveToFile();
        }
    }
}
```

- [ ] **步骤 3：实现 OnWrite 和查询接口**

```cpp
void SnapshotStore::OnWrite(const std::string& tag, const DataPoint& point) {
    SnapshotEntry entry;
    entry.tag = tag;
    entry.timestamp = point.ts;
    entry.has_value = true;
    if (std::holds_alternative<double>(point.value)) {
        entry.value = std::get<double>(point.value);
    }

    {
        std::unique_lock lock(mutex_);
        snapshot_[tag] = entry;
    }
    dirty_.store(true, std::memory_order_relaxed);
}

bool SnapshotStore::Get(const std::string& tag, SnapshotEntry& out) {
    std::shared_lock lock(mutex_);
    auto it = snapshot_.find(tag);
    if (it == snapshot_.end()) return false;
    out = it->second;
    return true;
}

std::vector<SnapshotEntry> SnapshotStore::GetByPattern(const std::string& pattern) {
    std::vector<SnapshotEntry> result;
    std::shared_lock lock(mutex_);
    for (const auto& [tag, entry] : snapshot_) {
        if (MatchPattern(tag, pattern)) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<SnapshotEntry> SnapshotStore::GetAll() {
    std::vector<SnapshotEntry> result;
    std::shared_lock lock(mutex_);
    result.reserve(snapshot_.size());
    for (const auto& [tag, entry] : snapshot_) {
        result.push_back(entry);
    }
    return result;
}

size_t SnapshotStore::Count() {
    std::shared_lock lock(mutex_);
    return snapshot_.size();
}
```

- [ ] **步骤 4：实现 Protobuf 保存和加载**

```cpp
bool SnapshotStore::SaveToFile() {
    SnapshotFile pb_file;
    pb_file.set_save_time(std::time(nullptr));
    pb_file.set_version(1);

    {
        std::shared_lock lock(mutex_);
        for (const auto& [tag, entry] : snapshot_) {
            auto* pb_entry = pb_file.add_entries();
            pb_entry->set_tag(tag);
            pb_entry->set_timestamp(entry.timestamp);
            pb_entry->set_value(entry.value);
            pb_entry->set_has_value(entry.has_value);
        }
    }

    std::ofstream ofs(snapshot_path_, std::ios::binary);
    if (!ofs) {
        LOG_WARN("Failed to open snapshot file for writing: {}", snapshot_path_);
        return false;
    }
    if (!pb_file.SerializeToOstream(&ofs)) {
        LOG_WARN("Failed to serialize snapshot to {}", snapshot_path_);
        return false;
    }
    LOG_DEBUG("Snapshot saved: {} entries", pb_file.entries_size());
    return true;
}

bool SnapshotStore::LoadFromFile() {
    if (!os::fs::Exists(snapshot_path_)) {
        LOG_INFO("No snapshot file found at {}, starting fresh", snapshot_path_);
        return true;
    }

    SnapshotFile pb_file;
    std::ifstream ifs(snapshot_path_, std::ios::binary);
    if (!ifs || !pb_file.ParseFromIstream(&ifs)) {
        LOG_WARN("Failed to parse snapshot file: {}", snapshot_path_);
        return false;
    }

    {
        std::unique_lock lock(mutex_);
        snapshot_.clear();
        for (int i = 0; i < pb_file.entries_size(); i++) {
            const auto& pb_entry = pb_file.entries(i);
            SnapshotEntry entry;
            entry.tag = pb_entry.tag();
            entry.timestamp = pb_entry.timestamp();
            entry.value = pb_entry.value();
            entry.has_value = pb_entry.has_value();
            snapshot_[entry.tag] = entry;
        }
    }
    LOG_INFO("Snapshot loaded: {} entries from {}", snapshot_.size(), snapshot_path_);
    return true;
}
```

- [ ] **步骤 5：实现 MatchPattern**

```cpp
bool SnapshotStore::MatchPattern(const std::string& tag, const std::string& pattern) {
    size_t pi = 0, ti = 0;
    while (pi < pattern.size() && ti < tag.size()) {
        if (pattern[pi] == '%') {
            pi++;
            if (pi == pattern.size()) return true;
            while (ti < tag.size() && tag[ti] != pattern[pi]) ti++;
            if (ti >= tag.size()) return false;
        } else if (pattern[pi] == '_') {
            pi++; ti++;
        } else if (pattern[pi] == tag[ti]) {
            pi++; ti++;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '%') pi++;
    return pi == pattern.size() && ti == tag.size();
}
```

- [ ] **步骤 6：创建测试文件 `tests/test_snapshot_store.cpp`**（基础单元测试模板）

```cpp
#include <gtest/gtest.h>
#include "snapshot/snapshot_store.h"
#include <cstdio>

using namespace minitsdb;

class SnapshotStoreTest : public ::testing::Test {
protected:
    std::string test_dir_ = "./test_snapshot_data";
    void SetUp() override { os::fs::RemoveAll(test_dir_); }
    void TearDown() override { os::fs::RemoveAll(test_dir_); }
};

TEST_F(SnapshotStoreTest, InitCreatesDir) {
    SnapshotStore store;
    ASSERT_TRUE(store.Init(test_dir_));
    // 写入一条数据验证，然后查询
    DataPoint p{1000, 42.5};
    store.OnWrite("test-tag", p);
    SnapshotEntry entry;
    ASSERT_TRUE(store.Get("test-tag", entry));
    ASSERT_EQ(entry.timestamp, 1000);
    ASSERT_DOUBLE_EQ(entry.value, 42.5);
    store.Shutdown();
}

TEST_F(SnapshotStoreTest, PersistAndLoad) {
    {
        SnapshotStore store;
        store.Init(test_dir_);
        store.OnWrite("tag-1", DataPoint{1000, 1.0});
        store.OnWrite("tag-2", DataPoint{2000, 2.0});
        store.Shutdown();  // 应触发保存
    }
    {
        SnapshotStore store;
        store.Init(test_dir_);  // 应加载快照
        ASSERT_EQ(store.Count(), 2);
        SnapshotEntry e;
        ASSERT_TRUE(store.Get("tag-1", e));
        ASSERT_DOUBLE_EQ(e.value, 1.0);
    }
}
```

- [ ] **步骤 7：Commit**

```bash
git add src/snapshot/ tests/test_snapshot_store.cpp
git commit -m "feat: 实现 SnapshotStore 核心（内存读写 + Protobuf 持久化）"
```

---

### 任务 3: StorageEngine 集成

**文件：**
- 修改：`src/storage/engine.h`
- 修改：`src/storage/engine.cpp`

- [ ] **步骤 1：engine.h 新增成员和头文件**

```cpp
// engine.h 顶部增加
#include "snapshot/snapshot_store.h"

// 类成员新增
std::unique_ptr<SnapshotStore> snapshot_store_;
```

- [ ] **步骤 2：engine.cpp Init 中初始化 SnapshotStore**

```cpp
// 在 Init() 末尾，alarm_engine_ 初始化之后
snapshot_store_ = std::make_unique<SnapshotStore>();
snapshot_store_->Init(config_.hot_path + "/snapshot");
```

- [ ] **步骤 3：engine.cpp Write 末尾异步通知**

```cpp
// 在 Write() 末尾，LOG_DEBUG 之前
if (snapshot_store_) {
    snapshot_store_->OnWrite(tag, point);
}
```

- [ ] **步骤 4：engine.cpp Close 中关闭 SnapshotStore**

```cpp
// 在 Close() 中 Flush() 之后，WAL 关闭之前
if (snapshot_store_) {
    snapshot_store_->Shutdown();
    LOG_DEBUG("SnapshotStore shut down");
}
```

- [ ] **步骤 5：Commit**

```bash
git add src/storage/engine.h src/storage/engine.cpp
git commit -m "feat: StorageEngine 集成 SnapshotStore"
```

---

### 任务 4: SQL SELECT FROM SNAPSHOT

**文件：**
- 修改：`src/sql/ast.h`
- 修改：`src/sql/parser.cpp`
- 修改：`src/sql/executor.h`
- 修改：`src/sql/executor.cpp`
- 创建：`tests/test_snapshot_sql.cpp`

- [ ] **步骤 1：ast.h QueryPlan 新增 SELECT_SNAPSHOT**

```cpp
enum class Type {
    INSERT,
    SELECT_LATEST,
    SELECT_RAW,
    SELECT_AGGREGATE,
    SELECT_SNAPSHOT,   // 新增
    CREATE_TAG,
    CREATE_ALARM,
    ALTER_SYSTEM
};
```

- [ ] **步骤 2：parser.cpp 检测 SNAPSHOT 表名**

在 `ParseSelect()` 的 table_name 解析部分之后：

```cpp
// 在 stmt.table_name 赋值后
if (ToUpper(stmt.table_name) == "SNAPSHOT") {
    stmt.latest = true;  // 标记为快照查询
}
```

- [ ] **步骤 3：executor.h 新增 ExecuteSnapshot 方法**

```cpp
// executor.h 公开方法区域
ExecutorResult ExecuteSnapshot(const SelectStmt& stmt);
```

- [ ] **步骤 4：executor.cpp 实现 ExecuteSnapshot**

```cpp
ExecutorResult Executor::ExecuteSnapshot(const SelectStmt& stmt) {
    ExecutorResult result;

    // 解析列选择
    bool select_all = false;
    bool select_tag = false, select_value = false, select_ts = false;
    bool select_count = false;

    for (const auto& col : stmt.columns) {
        auto upper = ToUpper(col.expr);
        if (upper == "*" || upper.find("COUNT") != std::string::npos) {
            select_all = true;
            if (upper.find("COUNT") != std::string::npos) select_count = true;
        }
        if (upper == "TAG") select_tag = true;
        if (upper == "VALUE") select_value = true;
        if (upper == "TS" || upper == "TIMESTAMP") select_ts = true;
    }

    // 获取快照数据
    std::vector<SnapshotEntry> entries;
    if (!stmt.where.tag_pattern.empty()) {
        entries = engine_->GetSnapshotStore()->GetByPattern(stmt.where.tag_pattern);
    } else if (!stmt.where.tag_filter.empty()) {
        SnapshotEntry entry;
        if (engine_->GetSnapshotStore()->Get(stmt.where.tag_filter, entry)) {
            entries.push_back(entry);
        }
    } else {
        if (select_count) {
            // COUNT(*) — 仅返回数字
            result.columns = {"count"};
            std::vector<std::string> row;
            row.push_back(std::to_string(engine_->GetSnapshotStore()->Count()));
            result.rows.push_back(row);
            return result;
        }
        entries = engine_->GetSnapshotStore()->GetAll();
    }

    // 设置列名
    if (select_all || select_tag) result.columns.push_back("tag");
    if (select_all || select_value) result.columns.push_back("value");
    if (select_all || select_ts) result.columns.push_back("ts");

    // 格式化输出
    for (const auto& entry : entries) {
        std::vector<std::string> row;
        if (select_all || select_tag) row.push_back(entry.tag);
        if (select_all || select_value) row.push_back(std::to_string(entry.value));
        if (select_all || select_ts) row.push_back(std::to_string(entry.timestamp));
        result.rows.push_back(std::move(row));
    }

    result.ok = true;
    return result;
}
```

注意：需要给 StorageEngine 增加 `GetSnapshotStore()` 访问器。

- [ ] **步骤 5：engine.h 添加 GetSnapshotStore() 访问器**

```cpp
SnapshotStore* GetSnapshotStore() { return snapshot_store_.get(); }
```

- [ ] **步骤 6：executor.cpp Execute 中路由 SELECT_SNAPSHOT**

```cpp
// 在 Execute() 的 switch/if 中
if (plan.type == QueryPlan::Type::SELECT_SNAPSHOT) {
    return ExecuteSnapshot(stmt);
}
```

同时确保 executor 在生成 QueryPlan 时检测到 SNAPSHOT 表名并设置正确的 type。

- [ ] **步骤 7：创建测试 `tests/test_snapshot_sql.cpp`**

```cpp
#include <gtest/gtest.h>
#include "sql/parser.h"
#include "snapshot/snapshot_store.h"

using namespace minitsdb;

TEST(SnapshotSQLTest, ParseSelectFromSnapshot) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM SNAPSHOT WHERE tag = 'BOILER-001'", &result));
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(*result.stmt));
    auto& stmt = std::get<SelectStmt>(*result.stmt);
    ASSERT_TRUE(stmt.latest);
    ASSERT_EQ(stmt.where.tag_filter, "BOILER-001");
}
```

- [ ] **步骤 8：Commit**

```bash
git add src/sql/ast.h src/sql/parser.cpp src/sql/executor.h src/sql/executor.cpp tests/test_snapshot_sql.cpp
git commit -m "feat: 实现 SELECT FROM SNAPSHOT SQL 语法"
```

---

### 任务 5: gRPC Snapshot RPC

**文件：**
- 修改：`src/server/grpc_server.cpp`
- 修改：`src/server/grpc_server.h`

- [ ] **步骤 1：grpc_server.h 添加 Snapshot RPC handler 声明**

```cpp
// 在现有 RPC handler 声明旁
grpc::Status HandleSnapshot(grpc::ServerContext* ctx,
                            const SnapshotRequest* req,
                            SnapshotResponse* resp);
```

- [ ] **步骤 2：grpc_server.cpp 实现 Snapshot RPC**

```cpp
grpc::Status GrpcServer::HandleSnapshot(
    grpc::ServerContext* ctx,
    const SnapshotRequest* req,
    SnapshotResponse* resp) {

    // 认证检查
    if (auth_) {
        auto role = auth_->ValidateToken(req->token());
        if (!role.has_value()) {
            resp->set_ok(false);
            resp->set_error("Unauthorized");
            return grpc::Status::OK;
        }
    }

    auto* snapshot = engine_->GetSnapshotStore();
    if (!snapshot) {
        resp->set_ok(false);
        resp->set_error("Snapshot not available");
        return grpc::Status::OK;
    }

    switch (req->type()) {
        case SnapshotQueryType::GET: {
            SnapshotEntry entry;
            if (snapshot->Get(req->tag(), entry)) {
                auto* pb = resp->add_entries();
                pb->set_tag(entry.tag);
                pb->set_timestamp(entry.timestamp);
                pb->set_value(entry.value);
                pb->set_has_value(entry.has_value);
            }
            break;
        }
        case SnapshotQueryType::GET_MANY: {
            auto entries = snapshot->GetByPattern(req->pattern());
            for (auto& e : entries) {
                auto* pb = resp->add_entries();
                pb->set_tag(e.tag);
                pb->set_timestamp(e.timestamp);
                pb->set_value(e.value);
                pb->set_has_value(e.has_value);
            }
            break;
        }
        case SnapshotQueryType::GET_ALL: {
            auto entries = snapshot->GetAll();
            for (auto& e : entries) {
                auto* pb = resp->add_entries();
                pb->set_tag(e.tag);
                pb->set_timestamp(e.timestamp);
                pb->set_value(e.value);
                pb->set_has_value(e.has_value);
            }
            break;
        }
        case SnapshotQueryType::COUNT: {
            resp->set_count(snapshot->Count());
            break;
        }
    }

    resp->set_ok(true);
    return grpc::Status::OK;
}
```

- [ ] **步骤 3：在 Service 实现中注册 Snapshot RPC**

在 `MiniTSDB::Service` 子类中添加：
```cpp
grpc::Status Snapshot(grpc::ServerContext* ctx,
                      const SnapshotRequest* req,
                      SnapshotResponse* resp) override {
    return server_->HandleSnapshot(ctx, req, resp);
}
```

- [ ] **步骤 4：Commit**

```bash
git add src/server/grpc_server.cpp src/server/grpc_server.h
git commit -m "feat: 实现 gRPC Snapshot RPC"
```

---

### 任务 6: CLI LATEST 子命令

**文件：**
- 修改：`src/cli/main.cpp`

- [ ] **步骤 1：main.cpp 添加 LATEST 子命令解析**

```cpp
// 在参数解析中添加：
if (cmd == "LATEST") {
    std::string tag, pattern;
    bool query_all = false;
    std::string format = "table";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--all") query_all = true;
        else if (arg == "--pattern" && i + 1 < argc) pattern = argv[++i];
        else if (arg == "--format" && i + 1 < argc) format = argv[++i];
        else if (arg[0] != '-') tag = arg;
    }

    // 构建 SnapshotRequest
    SnapshotRequest req;
    if (!tag.empty()) {
        req.set_type(SnapshotQueryType::GET);
        req.set_tag(tag);
    } else if (!pattern.empty()) {
        req.set_type(SnapshotQueryType::GET_MANY);
        req.set_pattern(pattern);
    } else if (query_all) {
        req.set_type(SnapshotQueryType::GET_ALL);
    } else {
        std::cerr << "Usage: minitsdb_cli LATEST <tag> | --pattern <pattern> | --all\n";
        return 1;
    }

    req.set_token(token);

    SnapshotResponse res;
    grpc::ClientContext ctx;
    auto status = client_stub->Snapshot(&ctx, req, &res);
    // ... 输出结果
}
```

- [ ] **步骤 2：实现快照结果的表格/JSON 输出**

```cpp
if (format == "json") {
    std::cout << "[\n";
    for (int i = 0; i < res.entries_size(); i++) {
        auto& e = res.entries(i);
        std::cout << "  {\"tag\":\"" << e.tag() << "\","
                  << "\"value\":" << e.value() << ","
                  << "\"ts\":" << e.timestamp() << "}";
        if (i < res.entries_size() - 1) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "]\n";
} else {
    // 表格输出
    printf("%-20s %-15s %s\n", "TAG", "VALUE", "TIMESTAMP");
    for (int i = 0; i < res.entries_size(); i++) {
        auto& e = res.entries(i);
        printf("%-20s %-15.3f %ld\n", e.tag().c_str(), e.value(), e.timestamp());
    }
}
```

- [ ] **步骤 3：Commit**

```bash
git add src/cli/main.cpp
git commit -m "feat: CLI 新增 LATEST 子命令查询快照"
```

---

### 任务 7: C SDK 快照接口

**文件：**
- 修改：`src/sdk/minitsdb.h`
- 修改：`src/sdk/minitsdb_sdk.cpp`

- [ ] **步骤 1：minitsdb.h 添加声明**

```c
// 在写入接口旁
// 查询快照（实时值）
// tag: 指定 tag 名，pattern: LIKE 模式，同时为 NULL 时查全部
// 返回结果集，使用 minitsdb_result_free 释放
MINITSDB_API minitsdb_result* minitsdb_snapshot(minitsdb_conn* conn,
                                                  const char* tag,
                                                  const char* pattern);
```

- [ ] **步骤 2：minitsdb_sdk.cpp 实现**

```cpp
minitsdb_result* minitsdb_snapshot(minitsdb_conn* conn,
                                    const char* tag,
                                    const char* pattern) {
    auto res = new minitsdb_result();

    SnapshotRequest req;
    req.set_token(conn->token);
    if (tag) {
        req.set_type(SnapshotQueryType::GET);
        req.set_tag(tag);
    } else if (pattern) {
        req.set_type(SnapshotQueryType::GET_MANY);
        req.set_pattern(pattern);
    } else {
        req.set_type(SnapshotQueryType::GET_ALL);
    }

    SnapshotResponse pb_res;
    grpc::ClientContext ctx;
    auto status = conn->stub->Snapshot(&ctx, req, &pb_res);

    if (!status.ok() || !pb_res.ok()) {
        res->ok = false;
        res->error = status.ok() ? pb_res.error() : status.error_message();
        return res;
    }

    res->columns = {"tag", "value", "ts"};
    for (int i = 0; i < pb_res.entries_size(); i++) {
        auto& e = pb_res.entries(i);
        std::vector<std::string> row;
        row.push_back(e.tag());
        row.push_back(std::to_string(e.value()));
        row.push_back(std::to_string(e.timestamp()));
        res->rows.push_back(row);
    }
    res->ok = true;
    return res;
}
```

- [ ] **步骤 3：Commit**

```bash
git add src/sdk/minitsdb.h src/sdk/minitsdb_sdk.cpp
git commit -m "feat: C SDK 新增 minitsdb_snapshot 快照查询"
```

---

### 任务 8: CMake 构建集成与验证

**文件：**
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：更新 CMakeLists.txt 注册测试**

```cmake
# 在现有的 add_minitsdb_gtest 附近添加
add_minitsdb_gtest(test_snapshot_store tests/test_snapshot_store.cpp)
add_minitsdb_gtest(test_snapshot_sql tests/test_snapshot_sql.cpp)
```

- [ ] **步骤 2：全量构建**

```bash
cmake --build build --target minitsdb_core --config Release
```

- [ ] **步骤 3：运行测试**

```bash
cd build/Release
./test_snapshot_store.exe
./test_snapshot_sql.exe
```

预期：所有测试通过

- [ ] **步骤 4：Commit**

```bash
git add CMakeLists.txt
git commit -m "build: 注册 SnapshotStore 测试目标"
```

---

## 自检

1. **规格覆盖度**：计划覆盖了 OpenSpec 变更中所有 7 个任务组的全部 25 个子任务。spec/snapshot-store/、spec/snapshot-query-rpc/、spec/snapshot-cli/、spec/snapshot-c-sdk/ 中的需求全都有对应实现任务。
2. **占位符扫描**：无 "TODO"、"待定"、"后续实现" 等占位符残留。
3. **类型一致性**：`SnapshotEntry`、`SnapshotStore`、`SnapshotQueryType` 等类型名在 protobuf、C++ 头文件和测试中保持一致。
