# snapshot-query-rpc Specification

## Purpose
gRPC Snapshot RPC for real-time value queries (GET/GET_MANY/GET_ALL/COUNT) and SQL SELECT FROM SNAPSHOT syntax support.

## ADDED Requirements

### 需求:gRPC 必须提供 Snapshot RPC
服务端必须提供统一的 `Snapshot` RPC，支持 GET（单点）、GET_MANY（模式匹配）、GET_ALL（全量）、COUNT（统计）四种查询类型。

#### 场景:单 tag 实时值查询
- **当** 客户端发送 `Snapshot {type: GET, tag: "BOILER-001"}`
- **那么** 服务端返回该 tag 的最新值

#### 场景:模式匹配批量查询
- **当** 客户端发送 `Snapshot {type: GET_MANY, pattern: "BOILER-%"}`
- **那么** 服务端返回所有匹配 tag 的最新值列表

#### 场景:全量快照导出
- **当** 客户端发送 `Snapshot {type: GET_ALL}`
- **那么** 服务端返回全部 tag 的最新值

#### 场景:未认证请求拒绝
- **当** 客户端发送无效 token 的 Snapshot 请求
- **那么** 服务端返回认证错误

### 需求:系统必须支持 SELECT FROM SNAPSHOT SQL 语法
SQL 解析器必须将 `SNAPSHOT` 识别为虚拟系统表，SELECT FROM SNAPSHOT 直接查询 SnapshotStore。

#### 场景:SELECT * 全量快照查询
- **当** 执行 `SELECT * FROM SNAPSHOT`
- **那么** 返回全部 tag 的最新值（tag, value, ts）

#### 场景:SELECT 列选择
- **当** 执行 `SELECT tag, value FROM SNAPSHOT WHERE tag = 'BOILER-001'`
- **那么** 仅返回选中的列

#### 场景:LIKE 模式过滤
- **当** 执行 `SELECT * FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'`
- **那么** 返回所有匹配 tag 的最新值

#### 场景:COUNT 统计
- **当** 执行 `SELECT COUNT(*) FROM SNAPSHOT`
- **那么** 返回快照中的 tag 总数
