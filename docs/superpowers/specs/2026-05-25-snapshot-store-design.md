# SnapshotStore 实时快照功能设计

## 背景

MiniTSDB 当前已有 `LatestCache` 模块提供基础内存缓存，但缺乏持久化、快照加载和独立查询接口。工业 SIS 场景中 DCS/SCADA 大屏需要毫秒级响应查询所有测点的最新值。参照 PI System 的 Snapshot 子系统，设计独立的实时值快照存储。

约束：C++20、Protobuf、gRPC、独立于历史引擎。

## 目标 / 非目标

**目标：**
- 独立于 LatestCache 的 SnapshotStore，自包含数据结构和持久化
- Protobuf 格式快照文件（`snapshot.pb`），支持启动加载/定时保存/关闭保存
- gRPC 新增 `Snapshot` RPC（GET/GET_MANY/GET_ALL/COUNT）
- CLI 新增 `LATEST` 子命令直接查询快照
- C SDK 新增 `minitsdb_snapshot()` 函数
- 单点查询 < 100μs，20 万点全量导出 < 500ms

**非目标：**
- 不涉及 WebSocket/HTTP 接口
- 不做历史数据查询
- 不做分布式一致性

## 决策

### 1. 独立实现 vs 封装 LatestCache
**选择**: 独立实现（用户选择）
**理由**: LatestCache 耦合在 StorageEngine 内部，独立实现使 SnapshotStore 可以独立测试、独立启停、未来可独立部署。

### 2. 快照文件格式：JSON vs Protobuf
**选择**: Protobuf（用户选择）
**理由**: Protobuf 占用空间小（20 万点约 10MB vs JSON 约 25MB），解析速度快，与 gRPC 生态一致。

### 3. 观察者模式异步通知
**选择**: StorageEngine::Write 末尾异步调用 SnapshotStore::OnWrite
**理由**: 不阻塞写路径，松耦合，SnapshotStore 可独立启停。

### 4. 快照保存策略
**选择**: 定时保存（默认 10s）+ 关闭时保存，仅 dirty 标志为 true 时执行
**理由**: 避免无变更时频繁 IO，宕机时最多丢失 10s 数据。

### 5. Snapshot RPC 设计
**选择**: 统一的 Snapshot RPC，通过 SnapshotQueryType 区分查询模式
**理由**: 减少 proto RPC 数量，客户端和服务器端都清晰。

## 架构

```
StorageEngine::Write(tag, point)
  ├── WAL → MemTable → Cache → Alarm
  └── (异步) SnapshotStore::OnWrite(tag, point)
                       │
                       ↓
                 unordered_map<string, SnapshotEntry>
                       │
                  ┌────┴────┐
                  │         │
            SaveLoop()   Query(path)
                  │         │
                  ↓         ↓
            snapshot.pb   gRPC Snapshot RPC
                              │
                    ┌─────────┼─────────┐
                    ↓         ↓         ↓
                 CLI LATEST  C SDK   HTTP(未来)
```

## 数据模型

```cpp
struct SnapshotEntry {
    int64_t timestamp;
    double value;
    std::string tag;
    bool has_value;
};

class SnapshotStore {
public:
    bool Init(const std::string& snapshot_path);
    void Shutdown();
    void SetSaveInterval(int interval_ms);
    void OnWrite(const std::string& tag, const DataPoint& point);
    bool Get(const std::string& tag, SnapshotEntry& out);
    std::vector<SnapshotEntry> GetByPattern(const std::string& pattern);
    std::vector<SnapshotEntry> GetAll();
    size_t Count();
private:
    std::unordered_map<std::string, SnapshotEntry> snapshot_;
    mutable std::shared_mutex mutex_;
    std::string snapshot_path_;
    std::thread save_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> dirty_{false};
    int save_interval_sec_ = 10;
};
```

## Protobuf 定义

```protobuf
// protos/snapshot.proto
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

```protobuf
// protos/minitsdb.proto 追加
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

## SQL 语法：SELECT FROM SNAPSHOT

复用现有 SELECT 解析器，将 SNAPSHOT 视为虚拟系统表。

**支持的语法**：

```sql
-- 全部实时值
SELECT * FROM SNAPSHOT
SELECT tag, value, ts FROM SNAPSHOT

-- 单点查询
SELECT * FROM SNAPSHOT WHERE tag = 'BOILER-001'
SELECT tag, value FROM SNAPSHOT WHERE tag = 'BOILER-001'

-- 模式匹配
SELECT * FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'
SELECT tag, ts FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'

-- 统计
SELECT COUNT(*) FROM SNAPSHOT
```

**解析器改动**（`parser.cpp`）：
- `ParseSelect` 解析出 `table_name` 后，检查 `ToUpper(stmt.table_name) == "SNAPSHOT"`
- 设置 `stmt.latest = true` 标记为快照查询
- 忽略 `time_range`、`group_by`、`order_by`、`limit` 等不适用子句

**AST 改动**（`ast.h`）：
- `SelectStmt` 已有 `latest` 字段，无需新增结构体
- `QueryPlan::Type` 新增 `SELECT_SNAPSHOT`

**执行器改动**（`executor.cpp`）：
- `Execute()` 中检测 `SELECT_SNAPSHOT` 类型
- 调用 `ExecuteSnapshot()` 直接查询 SnapshotStore
- 列选择映射：`*` → 全部字段，`tag`/`value`/`ts` → 对应 SnapshotEntry 字段
- WHERE 过滤：`tag =` → `Get()`，`tag LIKE` → `GetByPattern()`，无条件 → `GetAll()`

## 文件结构

```
data/hot/snapshot/
  └── snapshot.pb    ← Protobuf 快照文件

src/snapshot/
  ├── snapshot_store.h
  ├── snapshot_store.cpp

protos/
  ├── minitsdb.proto     ← 追加 Snapshot RPC
  └── snapshot.proto     ← SnapshotFile/SnapshotEntry 定义
```

## 风险 / 权衡

- **[风险] 宕机丢失数据**: 最多丢失 10s 的实时值变化
  → **缓解**: 可通过调小 save_interval 降低风险
- **[风险] 内存占用**: 20 万点 × ~150 字节（map+string 开销）≈ 30MB
  → **缓解**: 当前可控，未来可用 flat_hash_map 优化
- **[风险] Protobuf 序列化性能**: 20 万点序列化约 100-200ms
  → **缓解**: 异步后台线程保存，不阻塞主路径
