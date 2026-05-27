## Why

工业 SIS 场景中，DCS/SCADA 大屏需要毫秒级响应查询所有测点的最新值。当前 MiniTSDB 的 LatestCache 仅有基础内存缓存，缺乏持久化、快照加载和独立查询接口。每次查实时值仍需走 StorageEngine 链路，无法满足大规模（20 万点）实时刷新需求。PI System 提供了独立的 Snapshot 子系统专门处理这类需求，MiniTSDB 也需要类似机制。

## What Changes

- 新增 `SnapshotStore` 模块：独立于 LatestCache 的专用实时值存储，支持内存存储、磁盘快照加载/保存
- 新增 `SnapshotQuery` RPC：gRPC 接口中增加专门的实时值查询，不经过 SQL 解析和执行链路
- 新增 `snapshot.json` 快照文件：定期保存所有 tag 的最新值至磁盘，服务启动时快速加载
- CLI 增加 `LATEST` 子命令：绕过 SQL 解析器直接调用 Snapshot RPC
- C SDK 增加 `minitsdb_snapshot()` 函数：直接查询实时快照

## Capabilities

### 新增功能
- `snapshot-store`: 专用实时值快照存储，独立于历史引擎，支持内存快速读写和磁盘持久化
- `snapshot-query-rpc`: gRPC 新增 SnapshotQuery RPC，绕过 SQL 引擎直接返回 tag 实时值
- `snapshot-cli`: CLI 增加快照查询子命令，毫秒级返回结果
- `snapshot-c-sdk`: C SDK 增加快照查询接口

### 修改功能

<!-- 无现有规范的修改 -->

None

## Impact

- **代码**: 新增 `src/snapshot/` 模块，修改 `protos/minitsdb.proto`、`src/server/grpc_server.cpp`、`src/cli/main.cpp`、`src/sdk/`
- **存储**: 新增 `data/hot/snapshot.json` 快照文件（约 20 万点 × 80 字节 ≈ 16 MB）
- **协议**: gRPC proto 新增 `SnapshotQuery` RPC
