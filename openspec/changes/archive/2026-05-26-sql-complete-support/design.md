## 上下文

当前 MiniTSDB 的 SQL 实现支持 6 种语句（INSERT / SELECT / CREATE TAG / CREATE ALARM / CREATE USER / ALTER SYSTEM），存储模型为扁平 tag 命名空间。需要在保留现有功能的基础上，重构为三级命名空间并补齐全部 DDL/DML 语句。

详细设计见 `docs/superpowers/specs/2026-05-25-ddl-and-namespace-design.md`。

## 目标 / 非目标

**目标：**
- 三级命名空间：database → table → tag，存储路径对应调整
- 20+ 条 SQL 语句的解析、执行、测试
- 严格预注册：写入不存在的表/测点返回错误
- 时间精度从毫秒改为微秒
- 所有存储 API 增加 database/table 参数
- 中心化权限检查接入 Executor

**非目标：**
- 不做跨库查询
- 不做列级权限
- 不做事务支持

## 决策

### 1. 实现顺序：从底层往上
**选择**: 按依赖关系分 4 层实现：
1. 存储模型重构（路径、API、缓存 key）
2. 底层 API 扩展（DropTag、DropUser、AlterUser 等）
3. SQL 解析器扩展（20+ 语句）
4. SQL 执行器扩展（权限检查 + 新语句路由）

### 2. 命名限制
**选择**: 数据库名 ≤ 64、表名 ≤ 64、测点名 ≤ 128 字符。只允许字母/数字/下划线/连字符。

### 3. 用户管理
**选择**: 所有 DDL 语句（CREATE/DROP/ALTER USER）仅 admin 可执行。

### 4. USE 的会话状态
**选择**: Executor 持有 `current_db_`，USE 切换后 INSERT/SELECT/CREATE TABLE 等自动应用。

### 5. 缓存 key 格式
**选择**: `"<database>:<table>:<tag>"` 三级分隔，用冒号分隔。

### 6. ORDER BY 默认排序
**选择**: 不指定时为 `ORDER BY timestamp ASC`。

## 风险 / 权衡

- **[风险] 存储路径变长**: `data/hot/<db>/tables/<table>/tags/<tag>/<date>.sst`
  → **缓解**: db≤64, table≤64, tag≤128，超出 MAX_PATH 用 `\\?\` 前缀
- **[风险] 权限检查引入后现有客户端失效**: 升级后未传 token 的请求被拒绝
  → **缓解**: 默认 admin 用户存在于初始配置中
- **[风险] 微秒精度放大数据量**: 时间戳值放大 1000 倍，Gorilla delta-delta 差值更大
  → **缓解**: 算法不关心单位，仅影响压缩率（预计 < 5% 差异）
