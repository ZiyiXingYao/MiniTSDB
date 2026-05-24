## Context

MiniTSDB 是一个面向工业 SIS（厂级监控信息系统）场景的单机时序数据库，替代 PI 等传统实时数据库。典型部署 10-30 万测点，采集频率以 1s 为主。所有对外接口通过 SQL 完成。

现有基础：已完成类的接口定义（types.h、compressor.h、engine.h、ast.h 等），代码框架已搭好。Gorilla 压缩模块已实现并通过测试。

约束：C++20、Google C++ 编码规范、vcpkg 管理依赖（gRPC、Protobuf、Google Test）、gRPC 通信协议。

## Goals / Non-Goals

**Goals:**
- 实现 Gorilla 压缩（Delta-of-delta + XOR），压缩比 3-4:1
- 实现 LSM-Tree 存储引擎（MemTable → SSTable → Compaction）
- 实现完整 SQL 接口（INSERT/SELECT LATEST/聚合查询/CREATE TAG/CREATE ALARM）
- 实现冷热分层存储（SSD 3 个月热存 / HDD 2 年冷存 / 归档）
- 实现最新值缓存，微秒级 LATEST 查询
- 实现实时报警引擎
- 实现用户权限管理，支持多用户、角色（admin/operator/viewer）、SQL 级权限控制
- 单机支持 20 万点，35 万条/秒写入

**Non-Goals:**
- 不支持分布式集群（后续版本考虑）
- 不支持跨表 JOIN 等复杂 SQL
- 不支持 HTTPS/TLS（TCP 明文，后续版本支持）
- 不支持 LDAP/OAuth 等外部认证集成（使用内置用户系统）

## Decisions

### 1. 存储结构：LSM-Tree vs B-Tree
**Chosen**: LSM-Tree
**Rationale**: 时序数据库写入密集、顺序写入为主。LSM-Tree 的批量顺序写性能优于 B-Tree 的随机写。且 Gorilla 压缩算法天然适配 LSM 的分层结构。
**Alternatives**: B-Tree 读性能好但写入放大明显，不适合 35万/秒的写入量。

### 2. SQL 解析器：复用 SQLite parser 改造 vs 手写
**Chosen**: 阶段一用简易手写解析器，阶段二替换为 SQLite parser
**Rationale**: 初期 SQL 语法有限（INSERT/SELECT/CREATE TAG/CREATE ALARM），手写解析器即可覆盖，避免引入外部依赖的构建复杂度。
**Migration**: 后续需要子查询、UNION 等复杂语法时再引入 SQLite parser。

### 3. 存储分层：按 Tag × 天分文件 vs 全局分片
**Chosen**: 按 Tag × 天分文件
**Rationale**: 工业现场查询模式固定（查某个 Tag 某段时间），按 Tag 分文件直接定位到目标文件，无需全局索引。按天分文件便于冷热迁移（直接移动目录）。
**Trade-off**: Tag 数量多时文件数多（20 万 Tag × 730 天 = 1.46 亿文件），实际不可能每个 Tag 每天都有数据。通过 Compaction 合并为更粗粒度文件（不活跃的 Tag 按月合并）。

### 4. 压缩算法：Gorilla vs Protobuf vs 自定义
**Chosen**: Gorilla
**Rationale**: Facebook 在 Beringer Systems 论文中验证的算法，对时序数据压缩率极高（时间戳可压缩到 1-2 字节/点，浮点值 1-2 字节/点）。比 Protobuf 轻量，无需编解码框架。
**Trade-off**: 解压需要按顺序解码，无法随机跳转。可通过布隆过滤器 + 时间索引加速。

### 5. 文件格式：自定义 SSTable vs SQLite page
**Chosen**: 自定义 SSTable
**Rationale**: SQLite page 面向通用 OLTP 场景，对时序扫描和压缩支持不足。自定义 SSTable 可直接存储 Gorilla 压缩块，减少序列化开销。

### 6. 最新值缓存：ConcurrentHashMap
**Chosen**: ConcurrentHashMap（std::shared_mutex + std::unordered_map）
**Rationale**: 20 万点 × 80 字节 ≈ 16 MB，全量常驻内存。读写锁分离，写入不阻塞读取。

### 7. 用户权限模型：Token 认证 + 角色权限
**Chosen**: 客户端连接时用用户名/密码认证，服务端下发 Token。后续请求携带 Token 鉴权。
**Role 设计**：
- **admin**: 全部权限（CREATE/ALTER/DROP TAG、CREATE ALARM、用户管理）
- **operator**: 读写权限（INSERT、SELECT、查询报警），可查看所有数据
- **viewer**: 只读权限（SELECT LATEST、聚合查询），不可写入和修改
**SQL 权限粒度**：解析 AST 后，根据语句类型 + 操作对象检查用户角色是否有对应操作权限。
**存储**：用户信息存储在独立的 `minitsdb_users` 系统表（内存表 + 持久化文件）。
**Alternatives**: LDAP/OAuth 集成 → 当前不必要，增加部署复杂度。工业现场通常局域网环境，内置用户系统足够。

### 8. 通信协议：gRPC vs 原始 TCP
**Chosen**: gRPC（基于 HTTP/2 + Protobuf）
**Rationale**: 
- Protobuf 提供强类型接口定义，客户端和服务端契约清晰
- gRPC 原生支持 SSL/TLS，解决 Token 明文传输风险
- 流式 RPC 适合大批量数据写入和查询
- gRPC 跨语言支持，C SDK 可直接复用 `.proto` 定义
**接口定义**：一个 `.proto` 文件定义所有 RPC（Query/Insert/Auth/Admin）

### 9. CLI 客户端模块
**Chosen**: 独立可执行文件，通过 gRPC 连接服务端
**功能**：
- 交互式模式（`minitsdb>` 提示符）：输入 SQL 直接执行
- 批处理模式（`minitsdb -f script.sql`）：执行文件中的 SQL
- 连接管理：`minitsdb --host 127.0.0.1 --port 8086`
- 结果格式化：表格/CSV/JSON 三种输出格式

### 10. C SDK
**Chosen**: 封装 gRPC 调用的 C 接口动态链接库
**API 设计**：
```c
// 连接
MinitsdbConn* minitsdb_connect(const char* host, int port,
                               const char* user, const char* pass);
// 执行 SQL
MinitsdbResult* minitsdb_query(MinitsdbConn* conn, const char* sql);
// 读取结果
int minitsdb_result_rows(MinitsdbResult* res);
const char* minitsdb_result_value(MinitsdbResult* res, int row, int col);
// 清理
void minitsdb_result_free(MinitsdbResult* res);
void minitsdb_disconnect(MinitsdbConn* conn);
```
**用途**：外部 C/C++ 程序（如 DCS 采集器）直接链接，无需启动子进程

### 11. 测试框架：Google Test
**Chosen**: Google Test（gtest）
**Rationale**: C++ 项目事实标准的测试框架，与 CMake + vcpkg 集成良好。支持 TEST/TEST_F 宏、ASSERT_EQ/EXPECT_TRUE 等断言。

### 12. 依赖管理：vcpkg
**Chosen**: vcpkg manifest 模式（vcpkg.json)
**Rationale**:
- 声明式依赖管理，`vcpkg.json` 列出所有依赖
- CMake 通过 `CMAKE_TOOLCHAIN_FILE` 集成，无需手动配置路径
- gRPC、Protobuf、gtest 均可通过 vcpkg 安装

## Risks / Trade-offs

- **[风险] 文件数过多**: 按 Tag 分文件在 Tag 数量大时文件数太多
  → **缓解**: Compaction 合并为更高粒度，不活跃 Tag 按月存储

- **[风险] Gorilla 解压性能**: 历史数据查询需全量解压
  → **缓解**: 每个 SSTable 内部按小时分段，加时间索引，只解压相关段

- **[风险] WAL 写放大**: 35 万/秒写入导致 WAL 成为瓶颈
  → **缓解**: 批量写 WAL（每次 100ms 刷盘），使用异步 IO

- **[风险] 数据丢失**: 内存数据未刷盘时宕机
  → **缓解**: WAL + 定期 checkpoint

- **[风险] Token 泄露**: Token 在传输中被截获
  → **缓解**: gRPC 支持 SSL/TLS 加密传输。Token 设置有效期（默认 8 小时）
