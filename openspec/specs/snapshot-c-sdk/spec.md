# snapshot-c-sdk Specification

## Purpose
C SDK minitsdb_snapshot() function for real-time value queries via gRPC Snapshot RPC.

## ADDED Requirements

### 需求:C SDK 必须提供快照查询函数
C SDK 必须提供 `minitsdb_snapshot()` 函数，支持单 tag 查询、模式匹配查询和全量快照导出，不走 SQL 解析路径。

#### 场景:单 tag 快照查询
- **当** 调用 `minitsdb_snapshot(conn, "BOILER-001", NULL, &res)`
- **那么** 返回该 tag 的最新值

#### 场景:模式匹配查询
- **当** 调用 `minitsdb_snapshot(conn, NULL, "BOILER-%", &res)`
- **那么** 返回所有匹配 tag 的最新值列表

#### 场景:全量快照导出
- **当** 调用 `minitsdb_snapshot(conn, NULL, NULL, &res)`（tag 和 pattern 均为 NULL）
- **那么** 返回全部 tag 的最新值
