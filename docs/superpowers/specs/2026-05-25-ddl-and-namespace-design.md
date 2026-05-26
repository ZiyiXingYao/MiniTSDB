# MiniTSDB 存储模型重构与 DDL 完善设计

## 背景

当前 MiniTSDB 使用扁平化 tag 命名空间（`data/hot/tags/<tag>/`），SQL 语法中有 `table_name` 概念但存储层未使用。同时 DDL 语句严重缺失：有 CREATE 无 DROP、不支持 ALTER USER 和 SHOW USERS。

作为对标 PI System 的工业时序数据库，需要预注册测点、两级命名空间（表→测点）、完整的 DDL 生命周期管理。

## 目标 / 非目标

**目标：**
- 三级命名空间：database → table → tag（`data/hot/<db>/tables/<table>/tags/<tag>/`）
- 所有存储 API 增加 database/table 参数
- 数据库管理：CREATE DATABASE / DROP DATABASE / USE
- 表与测点管理：CREATE TABLE / DROP TABLE / CREATE TAG(s) / DROP TAG / ALTER TAG
- 数据操作：DELETE FROM / UPDATE（按时间范围精确操作）
- 用户管理：DROP USER / ALTER USER / SHOW USERS
- 报警管理：DROP ALARM / SHOW ALARMS
- 元数据查询：SHOW DATABASES / SHOW TABLES / SHOW TAGS
- 时间精度：微秒（Timestamp 从毫秒改为微秒）
- 写入时自动创建表（默认描述，tag 可选预注册）

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

### 5. 时间精度
**选择**: 微秒（μs），`Timestamp` 类型保持 `int64_t`，单位从毫秒改为微秒
**理由**: 工业采集常见 100ms 间隔，微秒足够。毫秒提升到微秒不会影响 Gorilla 压缩效果。

### 6. ALTER TAG 约束
**选择**: 禁止修改 `type` 属性，其他属性（unit、precision、description 等）可修改
**理由**: 修改数据类型会导致已存储数据被错误解释（如 double 当 int64 读）。

### 7. 元数据查询
**选择**: 使用 `SHOW` 命令系列替代系统表模式（如 PI 的 `pipoint..classic`）
**理由**: 更简洁，与 MySQL 的 SHOW 系列语义一致。

### 8. DELETE / UPDATE
**选择**: 两者都支持
- `DELETE FROM <table> WHERE tag='x' AND timestamp BETWEEN ...` — 按时间范围删除
- `UPDATE <table> SET value = ... WHERE tag='x' AND timestamp = ...` — 修改指定时间点的值

### 9. 等间隔插值
**选择**: 不支持
**理由**: Gorilla 压缩无损，原始数据完整可查，无需插值估算中间值。

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

## SQL 语法参考

### DDL — 数据定义

```sql
-- 数据库管理
CREATE DATABASE <name>
DROP DATABASE <name>
USE <name>

-- 表管理
CREATE TABLE <name> (
    description='All boiler measurements',
    location='Unit 1'
)
DROP TABLE <name>

-- 测点注册（单个）
CREATE TAG BOILER-TEMP IN TABLE boiler_data (
    type='analog',          -- analog/digital/string/accumulator
    unit='celsius',
    precision=1
)

-- 测点注册（批量）
CREATE TAGS IN TABLE boiler_data (
    BOILER-TEMP  (type='analog', unit='celsius', precision=1),
    BOILER-PRESS (type='analog', unit='kPa', precision=2),
    BOILER-STAT  (type='digital'),
    BOILER-ALARM (type='string')
)

-- 测点管理
DROP TAG <table>.<name>
ALTER TAG <table>.<name> SET <property>='<value>'
-- 注意：ALTER TAG 禁止修改 type 属性

-- 报警/用户
DROP ALARM <name>
DROP USER <name>
ALTER USER <name> SET PASSWORD '<new_pwd>'
ALTER USER <name> SET ROLE admin|operator|viewer
```

### DML — 数据操作

```sql
-- 写入
INSERT INTO <table> (tag, value, timestamp?) VALUES (...), (...), ...

-- 查询（原始数据）
SELECT <columns> FROM <table> WHERE tag = '<name>'
    AND timestamp BETWEEN '<start>' AND '<end>'

-- 查询（最新值）
SELECT <columns> FROM <table> WHERE tag = '<name>' LATEST

-- 查询（快照 - 系统表）
SELECT tag, value, timestamp FROM SNAPSHOT WHERE tag LIKE 'BOILER-%'

-- 聚合查询
SELECT TIME_BUCKET('5m', timestamp) AS bucket, AVG(value)
    FROM <table> WHERE tag = '<name>' AND timestamp BETWEEN ... GROUP BY bucket
-- 支持的聚合函数：AVG, MAX, MIN, SUM, COUNT, FIRST, LAST, STDDEV

-- 删除（按时间范围）
DELETE FROM <table> WHERE tag = '<name>' AND timestamp BETWEEN '<start>' AND '<end>'

-- 更新（修改指定时间点的值）
UPDATE <table> SET value = <new_val> WHERE tag = '<name>' AND timestamp = '<exact_time>'
```

### SHOW — 元数据查询

```sql
SHOW DATABASES
SHOW TABLES                    -- 当前数据库
SHOW TABLES FROM <database>    -- 指定数据库
SHOW TAGS FROM <table>         -- 表下所有测点
SHOW TAGS FROM <table> LIKE '<pattern>'  -- 模式匹配
SHOW USERS
SHOW ALARMS
```

## 测点数据类型

测点类型（`type` 属性）决定了该测点在存储和查询时的物理数据类型和行为语义：

| 类型 | 含义 | 物理类型 | 工业场景举例 |
|------|------|---------|-------------|
| `analog` | 模拟量，连续数值 | `double` | 温度 523.7°C、压力 12.5 kPa、转速 3000 rpm |
| `digital` | 数字量/开关量 | `int64` | 阀门 0=关/1=开、设备停机/运行 |
| `string` | 字符串量 | `std::string` | 报警码 "E-001"、操作记录 |
| `accumulator` | 累加量，单调递增 | `int64` | 累计发电量 12345 kWh、运行时长 9876 h |

## 风险 / 权衡

- **[风险] 路径变长**: `data/hot/<db>/tables/<table>/tags/<tag>/` 嵌套深，文件路径可能超限
  → **缓解**: 数据库名和表名限制长度（建议 ≤ 64 字符）
- **[风险] USE 状态管理**: gRPC 无状态连接，USE 只对当前 SQL 序列有效
  → **缓解**: 通过 token 绑定当前数据库（在 SessionToken 中增加 current_db 字段）
- **[风险] 自动建表与显式建表冲突**: 写入时自动建表的元数据为默认值，后续显式建表可能不一致
  → **缓解**: 自动建表仅在没有同名表时生效；CREATE TABLE 覆盖已存在的表会报错
