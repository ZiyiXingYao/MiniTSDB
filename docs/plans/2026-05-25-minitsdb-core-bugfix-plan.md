# MiniTSDB 核心 Bug 修复实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 修复验证发现的 25 个问题（11 CRITICAL + 8 WARNING + 6 SUGGESTION），覆盖存储引擎、认证、压缩器、告警、SQL、gRPC、CLI、C SDK 8 个模块。

**Architecture:** 按依赖拓扑分 3 批修复：底层模块（存储引擎/认证/压缩器/告警）先行，SQL 解析器中层，上层模块（gRPC/CLI/SDK）最后。每个 Bug 修复前先写失败测试，验证通过后提交。

**Tech Stack:** C++20, Google Test, gRPC, Protobuf, CMake

---

### Task 1: WAL 目录创建 + Flush 回调 + WAL 恢复 (C3, C1, C2)

**Files:** `src/storage/engine.cpp`, `src/storage/wal.h`

**Step 1: 写失败测试 — flus回调 不连接时 WAL 目录不存在 + Flush 不生效**

在 `tests/test_storage.cpp` 添加测试：

```cpp
TEST(StorageEngineTest, WALDirectoryCreated) {
    TempDir tmp;
    StorageConfig cfg{tmp.path + "/data/hot", tmp.path + "/data/cold", 64 * 1024, 90, 730};
    StorageEngine engine(cfg);
    ASSERT_TRUE(engine.Init());
    ASSERT_TRUE(os::fs::Exists(tmp.path + "/data/hot/wal"));
}
```

运行: `cmake --build build --target test_storage && ctest -R test_storage`
预期: FAIL — WAL 目录不存在

**Step 2: 写失败测试 — MemTable flush 回调和数据持久化**

```cpp
TEST(StorageEngineTest, MemTableFlushCreatesSSTable) {
    TempDir tmp;
    StorageConfig cfg{tmp.path + "/data/hot", tmp.path + "/data/cold", 100 /* small memtable */, 90, 730};
    StorageEngine engine(cfg);
    ASSERT_TRUE(engine.Init());
    // 写入超过 memtable 大小
    for (int i = 0; i < 1000; i++) {
        engine.Write("test-tag", DataPoint{static_cast<int64_t>(i) * 1000, 42.0 + i});
    }
    engine.Flush();
    // 验证 SSTable 文件存在
    auto entries = os::fs::ListDirectory(tmp.path + "/data/hot/tags/test-tag");
    ASSERT_GT(entries.size(), 0);
}
```

运行: `ctest -R test_storage -V`
预期: FAIL — 没有 SSTable 文件生成

**Step 3: 写失败测试 — WAL 恢复**

```cpp
TEST(StorageEngineTest, WALRecoveryAfterRestart) {
    TempDir tmp;
    {
        StorageConfig cfg{tmp.path + "/data/hot", tmp.path + "/data/cold", 64 * 1024, 90, 730};
        StorageEngine engine(cfg);
        engine.Init();
        engine.Write("test-tag", DataPoint{1000, 42.0});
    }
    // 模拟重启
    {
        StorageConfig cfg{tmp.path + "/data/hot", tmp.path + "/data/cold", 64 * 1024, 90, 730};
        StorageEngine engine(cfg);
        engine.Init();
        auto result = engine.ReadRaw("test-tag", TimeRange{0, UINT64_MAX});
        ASSERT_GT(result.size(), 0);
        ASSERT_DOUBLE_EQ(result[0].value, 42.0);
    }
}
```

运行: `ctest -R test_storage -V`
预期: FAIL — 重启后读取不到数据

**Step 4: engine.cpp:22 添加 WAL 目录创建**

```cpp
os::fs::CreateDirectories(config_.hot_path + "/wal");
```

**Step 5: engine.cpp:31 在 Init() 中添加 SetFlushCallback**

```cpp
mem_table_->SetFlushCallback([this](const std::string& tag, std::vector<DataPoint> points) {
    auto sstable = std::make_unique<SSTableWriter>(
        config_.hot_path + "/tags/" + tag + "/" + GetCurrentDateStr() + ".sst");
    if (sstable->Open()) {
        CompressedBlock block;
        BlockCompressor compressor;
        block = compressor.Compress(points);
        sstable->AddBlock(block);
        sstable->Close();
        LOG_DEBUG("Flushed {} points for tag '{}'", points.size(), tag);
    }
});
```

**Step 6: engine.cpp:34 在 Init() 中添加 WAL 恢复**

```cpp
// WAL 恢复
if (config_.hot_path + "/wal/wal.log") {
    WalReader reader;
    if (reader.Open(config_.hot_path + "/wal/wal.log")) {
        auto entries = reader.ReadAll();
        for (const auto& entry : entries) {
            mem_table_->Add(entry.tag, {entry.point});
        }
        LOG_INFO("WAL recovery: {} entries replayed", entries.size());
        WalReader::Truncate(config_.hot_path + "/wal/wal.log");
    }
}
```

**Step 7: 运行测试验证通过**

运行: `cmake --build build --target test_storage && ctest -R test_storage -V`
预期: PASS

**Step 8: 提交**

```bash
git add src/storage/engine.cpp tests/test_storage.cpp
git commit -m "fix: 添加 WAL 目录创建、flush 回调和 WAL 恢复机制"
```

---

### Task 2: SSTable 范围追踪 + Header 格式 + Compaction 损坏处理 (C11, W2, W3, S1)

**Files:** `src/storage/sstable.h`, `src/storage/sstable.cpp`, `src/storage/compaction.cpp`

**Step 1: 修改 SSTable 格式注释 — sstable.h:13-26 修复为 uint16 注释**

```cpp
// SSTable 文件格式:
// [Magic: 8 bytes] "MINITSDB"
// [Version: 4 bytes] uint32 (2 = CRC enabled)
// [Tag name len: 2 bytes] uint16    <-- 改为 uint16
// [Tag name: N bytes]
// [Block count: 4 bytes] uint32
```

**Step 2: sstable.cpp:120 修改 range_.start 初始化为 UINT64_MAX**

```cpp
if (range_.start == UINT64_MAX || block.range.start < range_.start)
    range_.start = block.range.start;
```

同时修改 `sstable.h:62` 中 `TimeRange range_` 成员的默认值声明。

**Step 3: sstable.cpp:135-138 修改 tag_len 写入为 uint16**

```cpp
uint16_t tag_len = static_cast<uint16_t>(tag_name_.size());
file_.Seek(12, SEEK_SET);
file_.Write(&tag_len, sizeof(tag_len));
if (tag_len > 0) file_.Write(tag_name_.data(), tag_len);
file_.Write(&block_count_, sizeof(block_count_));
```

**Step 4: sstable.cpp:211 修改 tag_len 读取为 uint16**

```cpp
uint16_t tag_len;
if (!file_.Read(&tag_len, sizeof(tag_len), &bytes_read) || bytes_read != sizeof(tag_len)) return false;
```

**Step 5: sstable.h:14-26 更新注释为 |uint16| + 实际 tag 名**

**Step 6: compaction.cpp 修复损坏文件处理**

```cpp
// 在添加文件到 small_files 前：
SSTableReader reader(entry.path);
if (!reader.Open()) {
    LOG_WARN("Compaction: skipping corrupt SSTable '{}'", entry.path);
    continue;
}
```

**Step 7: 删除 sstable.cpp:167-169 的 CalculateFileCrc() 死代码**

同时删除 `sstable.h:64` 中的 `uint32_t CalculateFileCrc();` 声明。

**Step 8: 写测试 — SSTable 范围**

```cpp
TEST(SSTableTest, RangeTracking) {
    TempDir tmp;
    auto path = tmp.path + "/test.sst";
    {
        SSTableWriter writer(path);
        writer.Open();
        CompressedBlock block;
        block.range = TimeRange{1000, 2000};
        writer.AddBlock(block);
        writer.Close();
    }
    {
        SSTableReader reader(path);
        ASSERT_TRUE(reader.Open());
        auto range = reader.GetTimeRange();
        ASSERT_EQ(range.start, 1000);
        ASSERT_EQ(range.end, 2000);
    }
}
```

**Step 9: 运行测试并提交**

```bash
git add src/storage/sstable.h src/storage/sstable.cpp src/storage/compaction.cpp tests/
git commit -m "fix: SSTable 范围追踪、header 格式、compaction 损坏处理"
```

---

### Task 3: WAL Flush 写入后立即刷盘 (W1)

**Files:** `src/storage/engine.cpp`

**Step 1: engine.cpp:61 在 AppendWrite 后添加 Flush()**

```cpp
if (wal_) {
    wal_->AppendWrite(tag, point);
    wal_->Flush();  // 立即刷盘
}
```

**Step 2: 提交**

```bash
git add src/storage/engine.cpp
git commit -m "fix: WAL 写入后立即 Flush 确保持久化"
```

---

### Task 4: SHA-256 total_bits_ 修复 (C8)

**Files:** `src/auth/auth_manager.cpp`

**Step 1: 写失败测试 — SHA-256 与标准实现对齐**

```cpp
TEST(AuthTest, SHA256CompatibleWithOpenSSL) {
    // "admin123" 的标准 SHA-256 摘要 (hex)
    const std::string expected = "240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9";
    // 假设 AuthManager::HashPassword 对 "admin123" 返回 hex 字符串
    // 需要暴露 HashPassword 或通过 AuthManager 内部测试
    ASSERT_EQ(expected, AuthManager::Sha256Hex("admin123"));
}
```

运行: `ctest -R test_auth -V`
预期: FAIL — 哈希值不匹配

**Step 2: 修复 auth_manager.cpp 中 SHA-256 实现**

```cpp
// 移除 total_bits_ 成员
// 改为 tracks message_bytes_
void ProcessBlock(const uint8_t* block, size_t len) {
    message_bytes_ += len;
    // ... 原有块处理逻辑
}
// Digest() 中
uint64_t bits = message_bytes_ * 8;
// 写入 64 位长度编码
```

**Step 3: 验证测试通过并提交**

```bash
git add src/auth/auth_manager.cpp tests/test_auth.cpp
git commit -m "fix: 修复 SHA-256 total_bits_ 计算错误，与标准实现对齐"
```

---

### Task 5: 压缩器注释 + 告警事件列表限制 (S2, S6)

**Files:** `src/storage/compressor.cpp`, `src/alarm/alarm_engine.cpp`

**Step 1: compressor.cpp:49 添加 32 位限制注释**

在 delta-delta overflow 处理代码上方添加：
```cpp
// Note: Gorilla paper uses 64 bits for overflow, but we use 32 bits.
// This limits the max delta-delta to ±INT32_MAX (~24 days).
// Acceptable for typical time-series blocks (< 1 hour range).
```

**Step 2: alarm_engine.cpp:78 先检查再 push_back**

```cpp
if (events_.size() < max_alarm_events_) {
    events_.push_back(event);
}
// 移除原有的 pop_front 逻辑
```

**Step 3: 告警测试**

```cpp
TEST(AlarmTest, MaxEventsNotExceeded) {
    AlarmEngine engine;
    AlarmRule rule{"test", "test-tag", "value > 0", {}};
    engine.RegisterRule(rule);
    for (int i = 0; i < 20000; i++) {
        engine.Evaluate("test-tag", DataPoint{1000, 1.0});
    }
    ASSERT_LE(engine.EventCount(), 10000);
}
```

**Step 4: 提交**

```bash
git add src/storage/compressor.cpp src/alarm/alarm_engine.cpp tests/test_alarm.cpp
git commit -m "fix: 压缩器添加限制注释，告警事件列表限制修复"
```

---

### Task 6: SQL 解析器修复 (C4, C5, C6, S3)

**Files:** `src/sql/parser.cpp`

**Step 1: 写失败测试 — 无 WHERE 的 SELECT**

```cpp
TEST(SQLParserTest, SelectWithoutWhere) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM boiler_temp", &result));
    ASSERT_EQ(result.type, StatementType::SELECT);
    ASSERT_EQ(result.select_stmt.table_name, "boiler_temp");
}
```

**Step 2: parser.cpp:267 添加 `*result = stmt;`**

```cpp
// 在 return {true, "", result} 之前：
*result = stmt;
return {true, "", result};
```

**Step 3: 写失败测试 — 时间范围存储**

```cpp
TEST(SQLParserTest, SelectWithTimeRange) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT * FROM t WHERE ts BETWEEN '2026-05-23' AND '2026-05-24'", &result));
    ASSERT_NE(result.select_stmt.where.time_range.start, 0);
    ASSERT_NE(result.select_stmt.where.time_range.end, 0);
}
```

**Step 4: parser.cpp:352 赋值时间范围**

```cpp
w.time_range = {ts_start, ts_end};
```

**Step 5: 写失败测试 — 聚合类型**

```cpp
TEST(SQLParserTest, AggregateType) {
    SQLParser parser;
    ParseResult result;
    ASSERT_TRUE(parser.Parse("SELECT MAX(value) FROM t WHERE tag='x' GROUP BY TIME_BUCKET('5m', ts)", &result));
    ASSERT_EQ(result.select_stmt.columns[0].agg_type, AggType::MAX);
}
```

**Step 6: parser.cpp:239-245 解析聚合函数名**

```cpp
if (upper_name == "AVG") expr.agg_type = AggType::AVG;
else if (upper_name == "MAX") expr.agg_type = AggType::MAX;
else if (upper_name == "MIN") expr.agg_type = AggType::MIN;
else if (upper_name == "SUM") expr.agg_type = AggType::SUM;
else if (upper_name == "COUNT") expr.agg_type = AggType::COUNT;
```

**Step 7: 删除 parser.cpp:567-588 的 ParseTimeRange() 死代码**

**Step 8: 运行测试并提交**

```bash
git add src/sql/parser.cpp tests/test_sql_parser.cpp
git commit -m "fix: SQL 解析器 - 空 AST、时间范围、聚合类型、死代码"
```

---

### Task 7: SQL 执行器修复 (C7, W4, W5)

**Files:** `src/sql/executor.cpp`, `src/sql/executor.h`

**Step 1: 写失败测试 — 聚合输出**

```cpp
TEST(ExecutorTest, SelectMaxReturnsMax) {
    // 设置测试数据: values = [1.0, 5.0, 3.0]
    // SELECT MAX(value) FROM t WHERE tag='x' AND ts BETWEEN 0 AND 1000
    // GROUP BY TIME_BUCKET('1m', ts)
    // 期望返回 5.0，不是 avg = 3.0
}
```

**Step 2: executor.cpp:223 改为 switch**

```cpp
switch (agg_type) {
    case AggType::AVG:   row.push_back(std::to_string(ar.avg)); break;
    case AggType::MAX:   row.push_back(std::to_string(ar.max)); break;
    case AggType::MIN:   row.push_back(std::to_string(ar.min)); break;
    case AggType::SUM:   row.push_back(std::to_string(ar.sum)); break;
    case AggType::COUNT: row.push_back(std::to_string(ar.cnt)); break;
    default:             row.push_back(std::to_string(ar.avg)); break;
}
```

**Step 3: executor.cpp:307 替换硬编码密码**

```cpp
std::string admin_token = auth_->Login("admin", config_->Get("auth.admin_password", "admin123"));
```

**Step 4: executor.cpp:65-68 列名转小写比较**

```cpp
auto lower_col = ToLower(col);
if (lower_col == "tag") { col_index.tag = i; }
else if (lower_col == "value") { col_index.value = i; }
else if (lower_col == "ts") { col_index.ts = i; }
```

注：需在 `executor.cpp` 中添加 `ToLower()` 工具函数。

**Step 5: 运行测试并提交**

```bash
git add src/sql/executor.cpp tests/test_executor.cpp
git commit -m "fix: SQL 执行器 - 聚合输出、硬编码密码、列名大小写"
```

---

### Task 8: gRPC 服务器修复 (C9, W7)

**Files:** `src/server/grpc_server.cpp`

**Step 1: 写失败测试 — 启动/停止 100 次无泄漏**

```cpp
TEST(GrpcServerTest, StartStopNoLeak) {
    for (int i = 0; i < 100; i++) {
        GrpcServer server("0.0.0.0:0", nullptr);
        ASSERT_TRUE(server.Start());
        server.Stop();
    }
}
```

**Step 2: grpc_server.cpp:177 Server 使用 unique_ptr**

```cpp
auto server_ptr = builder.BuildAndStart();
server_ = server_ptr.get();
server_ptr.release();  // 所有权转移到成员
```

**Step 3: grpc_server.cpp:198 Stop() 中正确删除**

```cpp
void GrpcServer::Stop() {
    if (server_) {
        static_cast<Server*>(server_)->Shutdown();
        if (server_thread_.joinable()) server_thread_.join();
        delete service_;
        service_ = nullptr;
        delete static_cast<Server*>(server_);
        server_ = nullptr;
    }
}
```

**Step 4: 运行测试并提交**

```bash
git add src/server/grpc_server.cpp tests/test_grpc_server.cpp
git commit -m "fix: gRPC server 内存泄漏和析构时序修复"
```

---

### Task 9: CLI 客户端修复 (W6, S4, S5)

**Files:** `src/cli/main.cpp`

**Step 1: cli/main.cpp:244 脚本模式支持分号分割**

```cpp
std::string content;
// 读取文件内容
std::string line;
while (std::getline(file, line)) {
    content += line + "\n";
}
// 按分号分割
size_t pos = 0, next;
while ((next = content.find(';', pos)) != std::string::npos) {
    std::string stmt = Trim(content.substr(pos, next - pos));
    if (!stmt.empty()) {
        auto result = client.Query(stmt);
        PrintResult(result, opts.format);
    }
    pos = next + 1;
}
```

**Step 2: cli/main.cpp:111 修复 PrintTable 分隔线对齐**

```cpp
void PrintSeparator(const std::vector<size_t>& widths) {
    std::cout << "+";
    for (size_t w : widths) {
        std::cout << std::string(w + 2, '-') << "+";
    }
    std::cout << "\n";
}
```

**Step 3: cli/main.cpp:219 登录失败显示错误详情**

```cpp
if (!client.Login(opts.user, opts.password)) {
    std::cerr << "ERROR: Login failed: " << client.LastError() << "\n";
    return 1;
}
```

**Step 4: 提交**

```bash
git add src/cli/main.cpp
git commit -m "fix: CLI 脚本分号分割、表格对齐、登录错误详情"
```

---

### Task 10: C SDK 修复 (C10, W8)

**Files:** `src/sdk/minitsdb.c`, `src/sdk/minitsdb_sdk.cpp`, `src/sdk/minitsdb.h`

**Step 1: 填充 minitsdb.c**

```c
#include "minitsdb.h"

// C SDK 函数通过 extern "C" 桥接到 C++ 实现
// 实际实现在 minitsdb_sdk.cpp 中
// 这个文件提供 C 编译器的符号引用

// 声明外部 C++ 实现
extern void* minitsdb_connect_impl(const char* host, int port,
                                    const char* user, const char* pass);
extern int minitsdb_query_impl(void* conn, const char* sql,
                                void** out_result);
extern int minitsdb_insert_impl(void* conn, const char* tag,
                                 int64_t timestamp, double value);
extern void minitsdb_result_free_impl(void* result);
extern void minitsdb_disconnect_impl(void* conn);

MinitsdbConn* minitsdb_connect(const char* host, int port,
                               const char* user, const char* pass) {
    return (MinitsdbConn*)minitsdb_connect_impl(host, port, user, pass);
}

int minitsdb_query(MinitsdbConn* conn, const char* sql,
                   MinitsdbResult** out_result) {
    return minitsdb_query_impl((void*)conn, sql, (void**)out_result);
}

int minitsdb_result_rows(MinitsdbResult* res) { return res->row_count; }
int minitsdb_result_cols(MinitsdbResult* res) { return res->col_count; }
const char* minitsdb_result_value(MinitsdbResult* res, int row, int col) {
    if (row < res->row_count && col < res->col_count)
        return res->data[row * res->col_count + col].c_str();
    return NULL;
}
void minitsdb_result_free(MinitsdbResult* res) { minitsdb_result_free_impl((void*)res); }
void minitsdb_disconnect(MinitsdbConn* conn) { minitsdb_disconnect_impl((void*)conn); }

int minitsdb_insert(MinitsdbConn* conn, const char* tag,
                    int64_t timestamp, double value) {
    return minitsdb_insert_impl((void*)conn, tag, timestamp, value);
}
```

**Step 2: minitsdb_sdk.cpp 添加 extern "C" 实现**

```cpp
extern "C" {
    MinitsdbConn* minitsdb_connect_impl(const char* host, int port,
                                         const char* user, const char* pass) {
        auto ctx = new SdkContext();
        // ... 原有 minitsdb_connect 逻辑
        return reinterpret_cast<MinitsdbConn*>(ctx);
    }

    int minitsdb_insert_impl(void* conn, const char* tag,
                              int64_t timestamp, double value) {
        auto ctx = reinterpret_cast<SdkContext*>(conn);
        InsertRequest req;
        req.set_token(ctx->token);
        auto* pv = req.add_points();
        pv->set_tag(tag);
        pv->set_timestamp(timestamp);
        pv->set_value(value);
        InsertResponse res;
        auto status = ctx->stub->Insert(&ctx->ctx, req, &res);
        return status.ok() ? static_cast<int>(res.count()) : -1;
    }
}
```

**Step 3: minitsdb.h 添加 minitsdb_insert 声明**

```c
int minitsdb_insert(MinitsdbConn* conn, const char* tag,
                    int64_t timestamp, double value);
```

**Step 4: 运行测试并提交**

```bash
cd build
cmake --build . --target test_c_sdk
ctest -R test_c_sdk -V
```

```bash
git add src/sdk/minitsdb.c src/sdk/minitsdb_sdk.cpp src/sdk/minitsdb.h
git commit -m "fix: C SDK 空文件填充和 minitsdb_insert 添加"
```

---

## 汇总

| 任务 | 模块 | 问题数 | 步数 |
|------|------|--------|------|
| 1 | 存储引擎(WAL+Flush) | C3, C1, C2 | 8 |
| 2 | 存储引擎(SSTable+Compaction) | C11, W2, W3, S1 | 9 |
| 3 | 存储引擎(WAL Flush) | W1 | 2 |
| 4 | 认证 | C8 | 3 |
| 5 | 压缩器+告警 | S2, S6 | 4 |
| 6 | SQL解析器 | C4, C5, C6, S3 | 8 |
| 7 | SQL执行器 | C7, W4, W5 | 5 |
| 8 | gRPC服务器 | C9, W7 | 4 |
| 9 | CLI客户端 | W6, S4, S5 | 4 |
| 10 | C SDK | C10, W8 | 4 |
| **合计** | **8模块** | **25问题** | **~50步** |
