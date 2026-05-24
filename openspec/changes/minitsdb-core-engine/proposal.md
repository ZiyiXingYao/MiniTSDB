## Why

传统工业 SIS（厂级监控信息系统）依赖 PI（OSIsoft PI System）等商业实时数据库，架构设计陈旧、扩展成本高。开源时序数据库（如 InfluxDB、TimescaleDB）面向云原生场景设计，对工业现场的单机部署场景不够轻量。MiniTSDB 旨在提供一个专为电厂/煤场等工业现场设计的轻量级时序数据库，通过全 SQL 接口简化使用，支持冷热分层存储以平衡性能和成本。

## What Changes

- 实现 Gorilla 压缩算法（Delta-of-delta 时间戳 + XOR 浮点值），支持高效时序数据存储
- 实现 LSM-Tree 结构的存储引擎（MemTable、SSTable、WAL）
- 实现全 SQL 接口，支持 INSERT、SELECT（含 LATEST 和聚合查询）、CREATE TAG、CREATE ALARM
- 实现冷热分层存储（SSD 热存 3 个月，HDD 冷存 2 年，支持归档）
- 实现最新值内存缓存，支撑大屏实时刷新（微秒级响应）
- 实现基于 SQL 规则的实时报警引擎
- 实现用户权限管理，支持多用户/角色/权限控制
- gRPC 服务端替代 TCP，提供远程调用能力
- CLI 客户端模块，支持命令行交互式查询和管理
- C SDK 封装 gRPC 调用，供外部程序嵌入使用
- 使用 Google Test 框架进行单元测试
- 使用 vcpkg 管理第三方依赖
- 所有代码遵循 **Google C++ 编码规范**

## Capabilities

### New Capabilities
- `gorilla-compression`: Delta-of-delta 时间戳编码和 XOR 浮点值压缩，支持模拟量/数字量/字符串/累加量四种数据类型
- `lsm-storage-engine`: 基于 LSM-Tree 的存储引擎，包含 MemTable 写缓冲、SSTable 分层存储、WAL 预写日志
- `sql-interface`: 完整的 SQL 解析和执行引擎，支持 INSERT/SELECT/CREATE TAG/CREATE ALARM/ALTER SYSTEM 语句
- `cold-hot-tiering`: 冷热分层存储管理，自动将超过 3 个月的数据迁移到冷存，支持 2 年保留和外部归档
- `latest-value-cache`: 最新值内存缓存，支持按 Tag 名称和 LIKE 模式查询，用于大屏实时刷新
- `real-time-alarm`: 基于 SQL 规则的实时报警引擎，写入时自动检查报警条件
- `user-auth`: 用户权限管理，支持多用户登录、角色管理（admin/operator/viewer）、基于 SQL 的权限控制（INSERT/SELECT/CREATE/ALTER 操作授权）
- `grpc-server`: gRPC 服务端，替代原始 TCP，提供 Protocol Buffers 定义的 RPC 接口
- `cli-client`: 命令行客户端，支持交互式 SQL 输入、结果格式化展示、连接管理
- `c-sdk`: C 语言 SDK，封装 gRPC 调用，对外暴露纯 C 接口（minitsdb_connect/send_query/close）

### Modified Capabilities

无（全新项目，无现有规范需要修改）

## Impact

- **代码**: 新建完整的 C++20 项目，约 15 个核心模块，遵循 Google C++ 编码规范
- **依赖**: gRPC、Protobuf、Google Test，通过 vcpkg 管理
- **部署**: 单机部署，gRPC 端口默认 8086
- **存储**: 依赖 SSD + HDD 双硬盘配置（可选）
- **协议**: gRPC (Protobuf)，客户端通过 C SDK 或 CLI 连接
