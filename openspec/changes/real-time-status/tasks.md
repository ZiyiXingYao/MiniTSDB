## 1. Proto 扩展

- [ ] 1.1 创建 `protos/snapshot.proto`：SnapshotFile/SnapshotEntry 消息定义
- [ ] 1.2 在 `protos/minitsdb.proto` 中添加 Snapshot RPC（SnapshotRequest/SnapshotResponse/SnapshotQueryType）
- [ ] 1.3 重新生成 protobuf 和 gRPC 代码

## 2. SnapshotStore 核心

- [ ] 2.1 创建 `src/snapshot/snapshot_store.h`：独立 SnapshotStore 类声明（独立 unordered_map，不依赖 LatestCache）
- [ ] 2.2 实现 `src/snapshot/snapshot_store.cpp`：内存读写（OnWrite/Get/GetByPattern/GetAll/Count）
- [ ] 2.3 实现 Protobuf 快照保存：SnapshotStore::SaveToFile() → `data/hot/snapshot/snapshot.pb`
- [ ] 2.4 实现快照加载：SnapshotStore::LoadFromFile() ← `data/hot/snapshot/snapshot.pb`
- [ ] 2.5 实现定时保存后台线程（默认 10s，dirty 标志控制）
- [ ] 2.6 在 StorageEngine 中集成 SnapshotStore（Init 中启动、Write 末尾通知、Close 中关闭）

## 3. SQL SELECT FROM SNAPSHOT

- [ ] 3.1 `parser.cpp`：ParseSelect 中检测 `table_name == "SNAPSHOT"` 设 latest=true
- [ ] 3.2 `ast.h`：QueryPlan::Type 新增 SELECT_SNAPSHOT
- [ ] 3.3 `executor.cpp`：新增 ExecuteSnapshot() 执行函数，调用 SnapshotStore 查询
- [ ] 3.4 实现列选择和输出格式化（*、tag、value、ts、COUNT(*)）

## 4. gRPC Snapshot RPC

- [ ] 4.1 在 `grpc_server.cpp` 中实现 Snapshot RPC 处理函数（GET/GET_MANY/GET_ALL/COUNT）
- [ ] 4.2 实现认证检查：Snapshot RPC 请求必须携带有效 token
- [ ] 4.3 实现 SnapshotResponse 格式化和返回

## 5. CLI 快照查询

- [ ] 5.1 在 `cli/main.cpp` 中添加 `LATEST` 子命令解析（支持 `--pattern`、`--all`、`--format` 参数）
- [ ] 5.2 实现命令行 gRPC Snapshot 客户端调用
- [ ] 5.3 实现快照结果的表格和 JSON 输出格式

## 6. C SDK 快照接口

- [ ] 6.1 在 `minitsdb.h` 中添加 `minitsdb_snapshot()` 函数声明
- [ ] 6.2 在 `minitsdb_sdk.cpp` 中实现 `minitsdb_snapshot()`（调用 gRPC Snapshot RPC）

## 7. 测试

- [ ] 7.1 编写 SnapshotStore 单元测试（写入→查询→快照保存→加载→验证）
- [ ] 7.2 编写 SQL SELECT FROM SNAPSHOT 解析和执行测试
- [ ] 7.3 编写 gRPC Snapshot RPC 测试（mock 客户端→服务端往返）
- [ ] 7.4 编写 CLI LATEST 子命令集成测试
