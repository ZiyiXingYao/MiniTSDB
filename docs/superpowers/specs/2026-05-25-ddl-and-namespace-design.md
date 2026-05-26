# MiniTSDB 存储模型重构与 DDL 完善设计

## 背景

当前 MiniTSDB 使用扁平化 tag 命名空间（`data/hot/tags/<tag>/`），SQL 语法中有 `table_name` 概念但存储层未使用。同时 DDL 语句严重缺失：有 CREATE 无 DROP、不支持 ALTER USER 和 SHOW USERS。

作为对标 PI System 的工业时序数据库，需要预注册测点、两级命名空间（表→测点）、完整的 DDL 生命周期管理。

## 目标 / 非目标

**目标：**
- 三级命名空间：database → table → tag（`data/hot/<db>/tables/<table>/tags/<tag>/`）
- 所有存储 API 增加 database/table 参数
- 数据库管理：CREATE DATABASE / DROP DATABASE / USE
- 表管理：CREATE TABLE / DROP TABLE / CREATE TAG (单个/批量) / DROP TAG
- 用户/报警管理：DROP ALARM / DROP USER / ALTER USER / SHOW USERS
- 写入时自动创建表（兼容简化使用，但 tag 仍需预注册或自动创建）

**非目标：**
- 不做跨库查询
- 不做列级权限
- 不做事务支持（单语句自动提交）

## 决策

### 1. 数据模型：三级命名空间
**选择**: database → table → tag
**存储路径**: `data/hot/<database>/tables/<table>/tags/<tag>/<date>.sst`
**理由**: PI System 使用点（tag）+ 点所在的集合（类似于 table），工业现场天然按工厂/装置/测点分层。

### 2. 写入时自动创建 vs 预注册
**选择**: 两者都支持
- 首次 `INSERT INTO <table>` 若表不存在，自动创建（默认描述）
- `CREATE TABLE` 可显式定义表的描述信息（description, location 等）
- `CREATE TAG` / `CREATE TAGS` 用于预注册测点及其元数据
- 类型、单位、精度等元数据是**测点级**的属性，不是表级的
**理由**: 一张表可以包含不同数据类型的测点（温度 analog、状态 digital、报警码 string），类型约束在测点级别而非表级别。工业场景需要显式元数据管理，但简化场景下"写了就有"更友好。

### 3. DROP TAG 语法
**选择**: `DROP TAG <table>.<tag>` 以限定表范围
**理由**: 两级命名下，同一 tag 名可能在不同表中存在。

### 4. USE 的实现
**选择**: 在 Executor 中维护 `current_db_` 状态，`USE` 切换后后续 SQL 自动应用
**理由**: 与 MySQL `USE database` 语义一致，避免每条 SQL 都带数据库前缀。

## 存储模型变更

### API 签名变化

```cpp
// StorageEngine
Write(db, table, tag, point)           // 新增 db/table 参数
ReadRaw(db, table, tag, range)
ReadAggregated(db, table, tag, range, bucket_ms, agg_type)
ReadLatest(db, table, tag)
DropTable(db, table)
DropTag(db, table, tag)

// LatestCache / SnapshotStore
Update(db, table, tag, point)
// key 格式: "db:table:tag"

// AuthManager / AlarmEngine 不涉及数据库和表，保持不变
```

### 存储路径映射

```
SQL: INSERT INTO boiler_temp (tag, value) VALUES ('BOILER-001', 523.7)
当前数据库: factory_a

→ 存储路径: data/hot/factory_a/tables/boiler_temp/tags/BOILER-001/2026-05-25.sst
→ 缓存 key: "factory_a:boiler_temp:BOILER-001"
```

## DDL 语法

```sql
-- 数据库管理
CREATE DATABASE <name>
DROP DATABASE <name>
USE <name>

-- 建表：仅定义分组信息，不限制测点类型
CREATE TABLE <name> (
    description='All boiler measurements',  -- 描述
    location='Unit 1'                        -- 位置（可选）
)

-- 注册测点（单个，带元数据）
CREATE TAG BOILER-TEMP IN TABLE boiler_data (
    type='analog',          -- 数据类型：analog/digital/string/accumulator
    unit='celsius',         -- 工程单位
    precision=1             -- 小数位
)

-- 注册测点（批量，每个测点独立指定类型）
CREATE TAGS IN TABLE boiler_data (
    BOILER-TEMP  (type='analog', unit='celsius', precision=1),
    BOILER-PRESS (type='analog', unit='kPa', precision=2),
    BOILER-STAT  (type='digital'),
    BOILER-ALARM (type='string')
)

-- 删除
DROP TABLE <name>
DROP TAG <table>.<tag>
DROP ALARM <name>
DROP USER <name>

-- 用户管理
ALTER USER <name> SET PASSWORD '<new_pwd>'
ALTER USER <name> SET ROLE admin|operator|viewer
SHOW USERS
```

## 风险 / 权衡

- **[风险] 路径变长**: `data/hot/<db>/tables/<table>/tags/<tag>/` 嵌套深，文件路径可能超限
  → **缓解**: 数据库名和表名限制长度（建议 ≤ 64 字符）
- **[风险] USE 状态管理**: gRPC 无状态连接，USE 只对当前 SQL 序列有效
  → **缓解**: 通过 token 绑定当前数据库（在 SessionToken 中增加 current_db 字段）
- **[风险] 自动建表与显式建表冲突**: 写入时自动建表的元数据为默认值，后续显式建表可能不一致
  → **缓解**: 自动建表仅在没有同名表时生效；CREATE TABLE 覆盖已存在的表会报错
