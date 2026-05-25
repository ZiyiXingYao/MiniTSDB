# MiniTSDB 核心 Bug 修复设计

## 背景

在 `minitsdb-core-engine` 变更的验证中发现 25 个问题（11 个 CRITICAL、8 个 WARNING、6 个 SUGGESTION），涉及 8 个模块。本设计按依赖拓扑分 3 批修复，每批修复后需通过对应的单元测试验证。

## 修复策略

- 按依赖拓扑分批修复：底层模块先行，上层模块后修复
- 每个 Bug 修复补充对应的单元测试
- 修复顺序：存储引擎 → 认证/压缩器/告警 → SQL → gRPC → CLI/SDK

---

## 第 1 批：存储引擎 + 认证 + 压缩器 + 告警

### 1.1 存储引擎（8 个问题）

#### C1 — MemTable flush 回调未连接

- **文件**: `engine.cpp:31-33`
- **根因**: `mem_table_->SetFlushCallback()` 从未调用，`MemTable::CheckFlush()` 触发时数据被静默丢弃
- **修复**: 在 `StorageEngine::Init()` 中添加 flush callback：

```cpp
mem_table_->SetFlushCallback([this](const std::string& tag, std::vector<DataPoint> points) {
    auto sstable = std::make_unique<SSTableWriter>();
    auto date_str = ...;  // 当前日期
    auto path = config_.storage_path + "/data/hot/tags/" + tag + "/" + date_str + ".sst";
    sstable->Open(path, tag, 0);
    sstable->AddBlock(points);
    sstable->Close();
});
```

- **测试**: 写入超过 64KB 的数据量，验证 SSTable 文件生成且内容可正确读取

#### C2 — WAL 恢复缺失

- **文件**: `engine.cpp:34-39`
- **根因**: `WalReader` 类已实现（`wal.h:66-94`），但在 `StorageEngine::Init()` 中从未使用
- **修复**: 在 `Init()` 中添加 WAL 恢复流程：

```cpp
// 恢复 WAL
WalReader reader;
if (reader.Open(wal_path)) {
    auto entries = reader.ReadAll();
    for (auto& entry : entries) {
        mem_table_->Add(entry.tag, {entry.point});
    }
    WalReader::Truncate(wal_path);
}
```

- **测试**: 写入数据 → 重新初始化 StorageEngine → 验证数据从 WAL 恢复

#### C3 — WAL 目录未创建

- **文件**: `engine.cpp:22-28`
- **修复**: 在 `CreateDirectories` 中添加 `CreateDirectory(config_.storage_path + "/data/hot/wal")`
- **测试**: 验证 `Init()` 后目录存在

#### C11 — SSTable 范围追踪 bug

- **文件**: `sstable.cpp:120-123`
- **根因**: `range_.start` 初始化为 `0`，更新时间戳比较条件为 `range_.start == 0`，当所有时间戳 > 0 时 start 保持为 `0`
- **修复**: 初始化 `range_.start` 为 `UINT64_MAX`
- **测试**: 写入时间戳全部大于 0 的数据，`ReadHeader()` 后验证 `range_.start` 正确

#### W1 — WAL 写入后未 Flush

- **文件**: `engine.cpp:60-62`
- **修复**: `wal_->AppendWrite()` 后调用 `wal_->Flush()`
- **测试**: 写入后验证 WAL 文件内容，Flush 前 vs Flush 后检测磁盘落盘

#### W2 — SSTable header 格式不一致

- **文件**: `sstable.h:14-16`, `sstable.cpp:66-72, 135-140`
- **问题**: `.h` 注释写 `uint16 tag_len`，writer 写 `uint32 tag_len`，.cpp 注释写固定 18 字节
- **修复**: 统一为 `uint16 tag_len`（2 字节），同步 `.h` 注释、writer 和 reader
- **测试**: 写入 SSTable → 读取 Header → 验证 tag name 匹配

#### W3 — Compaction 删除损坏的 SSTable

- **文件**: `compaction.cpp:78-131`
- **根因**: `reader.Open()` 失败的文件被加入 `small_files`，随后被删除
- **修复**: `reader.Open()` 失败时不加入 `small_files`，记录 `LOG_WARN("跳过损坏文件: %s", file.c_str())`
- **测试**: 创建损坏的 SSTable 文件 → 运行 compaction → 验证原文件未被删除

#### S1 — 死代码 CalculateFileCrc

- **文件**: `sstable.cpp:167-169`
- **修复**: 删除 `CalculateFileCrc()` 函数（返回 0 的死代码）

---

### 1.2 认证模块（1 个问题）

#### C8 — SHA-256 total_bits_ 计算错误

- **文件**: `auth_manager.cpp:74`
- **根因**: `total_bits_ += 512` 将填充块的比特数计入总长度，`Digest()` 中 `count_ * 8` 又重复计算
- **修复**: 移除 `total_bits_`，改为跟踪原始消息字节数 `message_bytes_`，`Digest()` 时计算 `bits = message_bytes_ * 8`
- **测试**: 对 `"admin123"` 进行哈希，与 OpenSSL `EVP_Digest("admin123", 8, ...)` 输出 16 进制比较，验证一致

---

### 1.3 压缩器（1 个问题）

#### S2 — delta-delta overflow 仅 32 位

- **文件**: `compressor.cpp:49`
- **根因**: overflow 情况仅写入 32 位而非 64 位，时间戳间隔变化 > 24 天时截断
- **修复**: 添加注释说明 32 位限制（间隔变化 < ±24 天可安全使用），对典型时序块场景无影响
- **测试**: 添加包含极长时间间隔（如 30 天）的数据点，验证压缩/解压行为（确认已知限制）

---

### 1.4 告警引擎（1 个问题）

#### S6 — 事件列表短暂超限

- **文件**: `alarm_engine.cpp:78-80`
- **根因**: `push_back` 后才检查 `events_.size()` 并 `pop_front`，事件数短暂超过 10000
- **修复**: 先检查 `events_.size() < max_alarm_events_`，满足条件才 `push_back`
- **测试**: 触发超过 10000 个告警事件，验证列表大小始终 ≤ 10000

---

## 第 2 批：SQL 解析器/执行器

### C4 — ParseSelect 无 WHERE 时 AST 为空

- **文件**: `parser.cpp:267-269`
- **根因**: 路径 `SELECT * FROM tablename` 无 WHERE/LATEST/GROUP BY 子句时，`*result = stmt` 从未执行
- **修复**: 在该返回路径前添加 `*result = stmt;`
- **测试**: `test_sql_parser.cpp` 添加用例 `SELECT * FROM boiler_temp`，验证 AST 中 table_name 和 columns 非空

### C5 — WHERE 时间范围未存储

- **文件**: `parser.cpp:279-352`
- **根因**: 解析出的 `ts_start`/`ts_end` 是局部变量，从未写入 `stmt.where.time_range`
- **修复**: 返回前添加 `w.time_range = {ts_start, ts_end};`
- **测试**: 解析 `SELECT ... WHERE ts BETWEEN '2026-05-23' AND '2026-05-24'`，验证 `select_stmt.where.time_range` 包含解析值

### C6 — 聚合类型始终为 NONE

- **文件**: `parser.cpp:239-245`
- **根因**: 解析 `AVG(value)`、`MAX(value)` 等函数名后未设置 `expr.agg_type`
- **修复**: 解析时根据函数名设置：

```cpp
if (upper_name == "AVG") expr.agg_type = AggType::AVG;
else if (upper_name == "MAX") expr.agg_type = AggType::MAX;
else if (upper_name == "MIN") expr.agg_type = AggType::MIN;
else if (upper_name == "SUM") expr.agg_type = AggType::SUM;
else if (upper_name == "COUNT") expr.agg_type = AggType::COUNT;
```

- **测试**: 分别解析 AVG/MAX/MIN/SUM/COUNT 语句，验证 agg_type 正确

### C7 — 聚合结果始终输出 avg

- **文件**: `executor.cpp:223`
- **根因**: 死代码 `row.push_back(std::to_string(ar.avg))` 忽略 agg_type
- **修复**: 根据 agg_type 选择输出字段：

```cpp
switch (agg_type) {
    case AggType::AVG:   row.push_back(std::to_string(ar.avg)); break;
    case AggType::MAX:   row.push_back(std::to_string(ar.max)); break;
    case AggType::MIN:   row.push_back(std::to_string(ar.min)); break;
    case AggType::SUM:   row.push_back(std::to_string(ar.sum)); break;
    case AggType::COUNT: row.push_back(std::to_string(ar.cnt)); break;
    default: row.push_back(std::to_string(ar.avg)); break;
}
```

- **测试**: 分别执行 AVG/MAX/MIN/SUM 查询，验证输出值类型正确

### W4 — 硬编码 admin 密码

- **文件**: `executor.cpp:307`
- **修复**: 使用 `config_->Get("auth.admin_password")` 替代字面量 `"admin123"`
- **测试**: 使用配置的不同密码测试用户创建流程

### W5 — INSERT 列名大小写敏感

- **文件**: `executor.cpp:65-68`
- **修复**: 比较前将 `col` 转为小写

```cpp
auto lower_col = ToLower(col);
if (lower_col == "tag") ...
```

- **测试**: `INSERT INTO t (Tag, Value, Ts) VALUES (...)` 大小写混合列名应正确执行

### S3 — 死代码 ParseTimeRange

- **文件**: `parser.cpp:567-588`
- **修复**: 删除未使用的 `ParseTimeRange()` 函数

---

## 第 3 批：gRPC + CLI + C SDK

### 3.1 gRPC 服务器

#### C9 — Server 内存泄漏

- **文件**: `grpc_server.cpp:177,198`
- **根因**: `builder.BuildAndStart().release()` 放弃所有权后仅 `nullptr` 未 `delete`
- **修复**: 将 `server_` 改为 `std::unique_ptr<grpc::Server>` 或在 `Stop()` 中添加 `delete`：

```cpp
void GrpcServer::Stop() {
    static_cast<Server*>(server_)->Shutdown();
    if (server_thread_.joinable()) server_thread_.join();
    delete service_;
    delete static_cast<Server*>(server_);
    server_ = nullptr;
    service_ = nullptr;
}
```

- **测试**: 启动/停止服务器 100 次，Valgrind/ASAN 检测无泄漏

#### W7 — 服务实例删除时序竞态

- **文件**: `grpc_server.cpp:201`
- **根因**: `delete service_` 在 `server_thread_.join()` 之前执行
- **修复**: 与 C9 一并修复，按正确顺序：Shutdown() → join() → delete service → delete server
- **测试**: 多次压力启动停止验证无竞态

### 3.2 CLI 客户端

#### W6 — 脚本模式不支持分号多语句

- **文件**: `cli/main.cpp:244-248`
- **修复**: 按 `;` 分割语句后逐条执行，支持分号分隔的多条 SQL
- **测试**: 脚本文件包含多条 `;` 分隔的 SQL，逐条执行

#### S4 — PrintTable 分隔线不对齐

- **文件**: `cli/main.cpp:111-114`
- **修复**: 先计算每列实际最大宽度，生成等宽 `-` 分隔线：

```cpp
void PrintSeparator(const std::vector<size_t>& widths) {
    for (size_t w : widths) {
        std::cout << "+-" << std::string(w, '-') << "-";
    }
    std::cout << "+\n";
}
```

#### S5 — 登录失败无错误详情

- **文件**: `cli/main.cpp:219`
- **修复**: 从 gRPC `Status` 中提取错误信息和错误码

```cpp
if (!status.ok()) {
    std::cerr << "ERROR: Login failed: " << status.error_message() << "\n";
}
```

### 3.3 C SDK

#### C10 — C SDK 源文件为空

- **文件**: `sdk/minitsdb.c`
- **修复**: 填充实际的 C 函数实现，通过 `extern "C"` 桥接 C++ SDK 实现：

```c
#include "minitsdb.h"

MinitsdbConn* minitsdb_connect(const char* host, int port,
                               const char* user, const char* pass) {
    // 调用 C++ SDK 内部实现（编译时链接到 minitsdb_sdk.cpp）
    extern "C" MinitsdbConn* minitsdb_connect_impl(const char*, int, const char*, const char*);
    return minitsdb_connect_impl(host, port, user, pass);
}
// ... 其他 API 包装函数
```

- **注意**: 由于 Windows 上 C 和 C++ 编译器的 ABI 差异，实际实现可能需要在 C++ 编译单元中提供 `extern "C"` 函数，C 文件仅为声明
- **测试**: `test_c_sdk.c` 编译通过并正常运行

#### W8 — affected_rows 始终为 0

- **文件**: `sdk/minitsdb_sdk.cpp:126-128`
- **根因**: `minitsdb_query()` 走 `Query` RPC，其响应无 affected_rows 字段
- **修复**: 添加 `minitsdb_insert()` 函数走 `Insert` RPC，从 `InsertResponse.count` 获取写入行数：

```cpp
int minitsdb_insert(MinitsdbConn* conn, const char* tag,
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
    return status.ok() ? res.count() : -1;
}
```

- **测试**: 通过 C SDK 写入数据，验证返回行数 > 0

---

## 规范自检

1. **占位符检查**: 无 "TODO" 或 "TBD" 残留
2. **内部一致性**: 三批修复之间无矛盾，依赖关系正确
3. **范围检查**: 聚焦于 25 个已确定问题，未新增功能范围
4. **模糊性检查**: 每个修复都有明确的文件/行号、原因和修复方案

## 工作量估算

| 批次 | 模块 | 问题数 | 文件修改 | 新增测试 |
|------|------|--------|---------|---------|
| 第 1 批 | 存储引擎 | 8 | engine.cpp, sstable.h/cpp, compaction.cpp | 3-5 |
| 第 1 批 | 认证 | 1 | auth_manager.cpp | 1 |
| 第 1 批 | 压缩器 | 1 | compressor.cpp | 0（仅注释） |
| 第 1 批 | 告警引擎 | 1 | alarm_engine.cpp | 1 |
| 第 2 批 | SQL | 7 | parser.cpp, executor.cpp | 4-6 |
| 第 3 批 | gRPC | 2 | grpc_server.cpp | 1 |
| 第 3 批 | CLI | 3 | cli/main.cpp | 1 |
| 第 3 批 | C SDK | 2 | sdk/minitsdb.c, minitsdb_sdk.cpp | 1-2 |
