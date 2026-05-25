## ADDED Requirements

### 需求:CLI 必须支持快照查询子命令
CLI 客户端必须支持 `LATEST` 子命令，绕过 SQL 解析器直接调用 Snapshot RPC，返回毫秒级实时值。

#### 场景:单 tag 快照查询
- **当** 执行 `minitsdb_cli LATEST BOILER-001`
- **那么** 返回该 tag 的最新时间和值

#### 场景:模式匹配查询
- **当** 执行 `minitsdb_cli LATEST --pattern "BOILER-%"`
- **那么** 返回所有匹配 tag 的最新值列表

#### 场景:全量快照导出
- **当** 执行 `minitsdb_cli LATEST --all`
- **那么** 返回所有 tag 的最新值

#### 场景:JSON 格式输出
- **当** 执行 `minitsdb_cli LATEST --all --format json`
- **那么** 输出格式为 JSON 数组
