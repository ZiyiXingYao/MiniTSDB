## 为什么

当前 MiniTSDB 使用扁平化 tag 命名空间（`data/hot/tags/<tag>/`），SQL 语法中有 `table_name` 概念但存储层未使用。同时 DDL 语句严重缺失：有 CREATE 无 DROP、不支持 ALTER USER、SHOW 等元数据查询。作为对标 PI System 的工业时序数据库，需要完整的 SQL 生命周期管理和三级命名空间（database → table → tag）。

## 变更内容

- **BREAKING**: 存储模型从扁平 tag 改为三级命名空间 `database → table → tag`
- **BREAKING**: 时间精度从毫秒改为微秒
- **BREAKING**: 取消自动建表，写入不存在的表/测点返回错误
- 新增：数据库管理（CREATE/DROP/USE DATABASE）
- 新增：表管理（CREATE/DROP TABLE）
- 新增：测点管理（DROP/ALTER TAG，CREATE TAGS 批量注册）
- 新增：报警管理（ALTER/DROP ALARM）
- 新增：用户管理（DROP/ALTER USER，SHOW USERS）
- 新增：数据操作（DELETE FROM，UPDATE）
- 新增：元数据查询（SHOW DATABASES/TABLES/TAGS/ALARMS）
- 新增：ORDER BY / LIMIT 子句
- 新增：聚合函数（FIRST, LAST, STDDEV）
- 修改：现有存储 API 全部增加 database/table 参数

## Capabilities

### 新增功能
- `database-management`: CREATE/DROP/USE DATABASE 语法和存储支持
- `table-management`: CREATE/DROP TABLE 语法和存储支持
- `tag-management`: CREATE TAGS/DROP TAG/ALTER TAG 语法和级联删除
- `alarm-management`: ALTER/DROP ALARM，SHOW ALARMS
- `user-management`: DROP/ALTER USER，SHOW USERS
- `data-delete-update`: DELETE FROM 和 UPDATE 语法和执行
- `metadata-queries`: SHOW DATABASES/TABLES/TAGS 元数据查询
- `order-by-limit`: ORDER BY 和 LIMIT 子句支持
- `aggregate-ext`: FIRST/LAST/STDDEV 聚合函数

### 修改功能
- `lsm-storage-engine`: 存储路径 `tags/<tag>` → `tables/<table>/tags/<tag>`，API 增加 db/table 参数
- `latest-value-cache`: key 格式从 `tag` 改为 `db:table:tag`
- `snapshot-store`: key 格式从 `tag` 改为 `db:table:tag`
- `sql-interface`: 新增 12+ 条 SQL 语句的解析和执行
- `user-auth`: 权限检查接入 Executor
- `gorilla-compression`: 时间戳精度从毫秒改为微秒

## 影响

- **存储**: 所有数据路径从 `tags/<tag>` 改为 `tables/<table>/tags/<tag>`，嵌套一层 database 目录
- **API**: `StorageEngine::Write/ReadRaw/ReadLatest/ReadAggregated` 全部增加 db/table 参数
- **缓存**: `LatestCache` 和 `SnapshotStore` 的 key 格式变更
- **SQL 解析器**: 从 6 种语句扩展到 20+ 种，解析器路由重写
- **SQL 执行器**: 新增 12+ 个 Execute* 函数，中心化权限检查
- **协议**: 无需修改 proto（新 SQL 走 Query RPC）
