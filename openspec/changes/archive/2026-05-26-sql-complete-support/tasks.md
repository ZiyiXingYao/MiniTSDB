## 1. 存储模型重构

- [ ] 1.1 `engine.h/cpp`: 所有 API 增加 db/table 参数（Write/ReadRaw/ReadAggregated/ReadLatest/DropTable/DropTag）
- [ ] 1.2 `sstable.h/cpp`: 文件路径从 `tags/<tag>` 改为 `tables/<table>/tags/<tag>`
- [ ] 1.3 `latest_cache.h/cpp`: key 从 `tag` 改为 `db:table:tag`
- [ ] 1.4 `snapshot_store.h/cpp`: OnWrite key 从 `tag` 改为 `db:table:tag`
- [ ] 1.5 `tier_manager.cpp`: 分层管理路径适配新结构
- [ ] 1.6 `compressor.h/cpp`: 时间戳从毫秒改为微秒，调整 delta-delta 测试用例

## 2. 时间精度变更

- [ ] 2.1 `types.h`: Timestamp 注释和用法从 ms 改为 μs
- [ ] 2.2 `executor.cpp`: INSERT 默认时间戳从 `now_ms` 改为 `now_us`
- [ ] 2.3 `FormatTimestamp`: 支持微秒格式 `%Y-%m-%dT%H:%M:%S.%fZ`

## 3. 底层 API 扩展

- [ ] 3.1 `engine.h/cpp`: 添加 DropTable(db, table)、DropTag(db, table, tag) 级联删除
- [ ] 3.2 `alarm_engine.h/cpp`: 添加 RemoveRulesByTag(db, table, tag)、AlterRule()、ListRules()
- [ ] 3.3 `auth_manager.h/cpp`: 添加 DropUser()、AlterUser()、GetUsers() 完整实现

## 4. SQL 解析器扩展

- [ ] 4.1 `ast.h`: 新增 DropTableStmt/DropTagStmt/DropAlarmStmt/DropUserStmt/AlterUserStmt/AlterAlarmStmt/ShowStmts/DeleteStmt/UpdateStmt/CreateDatabaseStmt/DropDatabaseStmt/UseStmt/CreateTableStmt/CreateTagsStmt/AlterTagStmt
- [ ] 4.2 `parser.cpp Parse()`: 新增所有 DDL/DML/SHOW 的路由（DROP/ALTER/SHOW/DELETE/UPDATE/CREATE DATABASE/USE 等）
- [ ] 4.3 `parser.cpp`: 实现 ParseDropTable/ParseDropTag/ParseDropAlarm/ParseDropUser/ParseAlterUser/ParseAlterAlarm/ParseDelete/ParseUpdate 等
- [ ] 4.4 `parser.cpp`: 实现 ParseShowDatabases/ParseShowTables/ParseShowTags/ParseShowUsers/ParseShowAlarms
- [ ] 4.5 `parser.cpp`: 实现 ParseCreateDatabase/ParseDropDatabase/ParseUse/ParseCreateTable/ParseCreateTags/ParseAlterTag
- [ ] 4.6 `parser.cpp`: ORDER BY 和 LIMIT 子句的完整解析
- [ ] 4.7 `parser.cpp`: 聚合函数 FIRST/LAST/STDDEV 的识别

## 5. SQL 执行器扩展

- [ ] 5.1 `executor.h`: 新增所有 Execute* 声明
- [ ] 5.2 `executor.cpp`: 实现所有新的 Execute* 函数
- [ ] 5.3 `executor.cpp`: 在 Execute() 中添加中心化权限检查（每条 SQL 前调用 CheckPermission）
- [ ] 5.4 `executor.cpp`: DELETE 和 UPDATE 实现（按时间范围）
- [ ] 5.5 `executor.cpp`: SHOW 系列实现（从 Engine/AuthManager/AlarmEngine 获取数据并格式化）
- [ ] 5.6 `executor.cpp`: 实现 FIRST/LAST/STDDEV 聚合计算
- [ ] 5.7 `executor.cpp`: ORDER BY 排序实现
- [ ] 5.8 `executor.cpp`: USE 的 current_db_ 状态管理
- [ ] 5.9 `executor.cpp`: 写入前检查表和测点是否存在（预注册校验）

## 6. 权限检查

- [ ] 6.1 `auth_manager.cpp`: 补全 CheckPermission 的 operation 列表（DROP/ALTER/DELETE/UPDATE/SHOW）
- [ ] 6.2 `executor.cpp`: 所有 Execute* 函数统一在入口处调用 CheckPermission
- [ ] 6.3 `executor.cpp`: CREATE USER/DROP USER/ALTER USER 仅 admin 可执行

## 7. 测试

- [ ] 7.1 存储模型重构测试（三级路径读写验证）
- [ ] 7.2 DDL 解析测试（DROP/ALTER/SHOW/CREATE DATABASE 等全部新语句）
- [ ] 7.3 DDL 执行测试（DROP TAG 级联删除验证）
- [ ] 7.4 DELETE/UPDATE 执行测试
- [ ] 7.5 权限检查测试（各角色对各语句的权限）
- [ ] 7.6 微秒时间戳测试
- [ ] 7.7 ORDER BY/LIMIT 测试
- [ ] 7.8 FIRST/LAST/STDDEV 聚合测试
- [ ] 7.9 命名长度限制和字符校验测试
