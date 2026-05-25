## 上下文

MiniTSDB 当前已有 `LatestCache` 模块（`src/cache/latest_cache.h`），提供基础的内存级 tag 最新值缓存（`shared_mutex` + `unordered_map`）。但存在以下不足：

- **无持久化**: 服务重启后缓存清空，需等待新写入才能恢复实时值
- **无独立查询接口**: 实时值查询需走完整 SQL 解析和执行链路（`SELECT ... LATEST`），延迟较高
- **无批量查询优化**: 大屏刷新 20 万点实时值需逐个查询，网络和解析开销大

PI System 的 Snapshot 子系统提供了参考：保持所有 tag 最新值常驻内存（约 16 MB/20 万点），支持亚毫秒级单点查询和毫秒级全量导出。

## 目标 / 非目标

**目标：**
- 独立于 LatestCache 的 SnapshotStore，自包含数据结构和持久化
- Protobuf 格式快照文件（`snapshot.pb`），启动加载/定时保存/关闭保存
- gRPC 新增 `Snapshot` RPC（GET/GET_MANY/GET_ALL/COUNT）
- SQL 新增 `SELECT ... FROM SNAPSHOT` 语法直接查询快照
- CLI 新增 `LATEST` 子命令直接查询快照
- C SDK 新增 `minitsdb_snapshot()` 函数
- 单点查询延迟 < 100μs，20 万点全量导出 < 500ms

**非目标：**
- 不涉及 WebSocket/HTTP 接口
- 不做历史数据查询（已有 StorageEngine）
- 不做分布式一致性（单机部署）

## 决策

### 1. 独立实现 vs 封装 LatestCache
**选择**: 独立实现
**理由**: LatestCache 耦合在 StorageEngine 内部，独立实现使 SnapshotStore 可独立测试、独立启停、未来可独立部署。

### 2. 快照文件格式：JSON vs Protobuf
**选择**: Protobuf
**理由**: Protobuf 占用空间小（20 万点约 10MB vs JSON 约 25MB），解析速度快，与 gRPC 生态一致。

### 3. 写入路径：同步 vs 异步通知
**选择**: 异步通知（观察者模式）
**理由**: Engine::Write 末尾调用 SnapshotStore::OnWrite，不阻塞写路径，松耦合。

### 4. 快照保存策略
**选择**: 定时保存（默认 10s）+ 关闭时保存，dirty 标志控制
**理由**: 避免无变更时频繁 IO，宕机时最多丢失 10s 数据。

### 5. Snapshot RPC 设计
**选择**: 统一 `Snapshot` RPC，请求中包含查询类型（GET/GET_MANY/GET_ALL/COUNT）
**理由**: 减少 proto 中 RPC 数量

```protobuf
rpc Snapshot(SnapshotRequest) returns (SnapshotResponse);

message SnapshotRequest {
    string token = 1;
    SnapshotQueryType type = 2;
    string tag = 3;
    string pattern = 4;
}
```

### 6. SQL 语法：SELECT FROM SNAPSHOT
**选择**: 复用 SELECT 解析器，将 SNAPSHOT 识别为虚拟系统表
**理由**: 兼容现有 SQL 语法，最小解析器改动

```sql
SELECT * FROM SNAPSHOT WHERE tag = 'BOILER-001'
SELECT tag, value, ts FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'
SELECT COUNT(*) FROM SNAPSHOT
```

## 架构

```
StorageEngine::Write(tag, point)
  ├── WAL → MemTable → Cache → Alarm
  └── (异步) SnapshotStore::OnWrite(tag, point)
                       │
                       ↓
                 SnapshotStore (独立 unordered_map)
                       │
                  ┌────┴────┐
            SaveLoop()   Query(path)
                  │         │
                  ↓         ↓
            snapshot.pb   SnapshotStore::Get/GetByPattern/GetAll
                              │
                    ┌─────────┼─────────┬──────────────┐
                    ↓         ↓         ↓              ↓
            gRPC Snapshot  SQL(SELECT  CLI LATEST   C SDK
                 RPC       FROM SNAPSHOT)            minitsdb_
                                                    snapshot()
```

## 风险 / 权衡

- **[风险] 快照文件写入延迟**: 20 万点 Protobuf 序列化约 100-200ms
  → **缓解**: 异步线程保存，不阻塞写路径
- **[风险] 内存占用**: 20 万点 × 约 150 字节 ≈ 30 MB
  → **缓解**: 当前可控，未来可用 flat_hash_map 优化
- **[风险] 宕机丢失数据**: 最多丢失 10s 的实时值变化
  → **缓解**: 可通过调小 save_interval 降低风险
