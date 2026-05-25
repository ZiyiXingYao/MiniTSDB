# snapshot-store Specification

## Purpose
Dedicated in-memory snapshot storage for real-time tag values, with Protobuf-based disk persistence. Independent from LatestCache and StorageEngine.

## ADDED Requirements

### 需求:SnapshotStore 必须缓存所有 tag 的最新值
SnapshotStore 必须在内存中维护所有已注册 tag 的最新值，支持单个 tag 查询、LIKE 模式批量查询和全量导出。查询延迟必须 < 100μs。

#### 场景:单点查询
- **当** 查询已存在 tag 的最新值
- **那么** 返回该 tag 的最近一次 DataPoint（时间戳和值）

#### 场景:查询不存在的 tag
- **当** 查询未注册的 tag
- **那么** 返回空结果，不报错

#### 场景:LIKE 模式批量查询
- **当** 使用 `BOILER-%` 进行模式匹配查询
- **那么** 返回所有匹配 tag 的最新值列表

#### 场景:全量导出
- **当** 查询所有 tag 的最新值
- **那么** 返回全部 tag 的快照数据

### 需求:SnapshotStore 必须支持磁盘快照持久化
SnapshotStore 必须支持将内存快照保存到 `data/hot/snapshot.json`，并在启动时从该文件加载恢复。

#### 场景:启动时加载快照
- **当** 服务启动且 `snapshot.json` 存在
- **那么** 所有 tag 的实时值被加载到内存中

#### 场景:关闭时保存快照
- **当** 服务正常关闭
- **那么** 当前所有 tag 的最新值被写入 `snapshot.json`

#### 场景:定时自动保存快照
- **当** 快照保存间隔（默认 10s）到达
- **那么** 内存快照被异步写入磁盘
