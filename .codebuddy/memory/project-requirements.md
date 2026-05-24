---
name: project-requirements
description: MiniTSDB 目标场景、约束条件、功能需求、配置项
type: project
---

# MiniTSDB 需求

## 目标场景
- 电厂/煤场传统 SIS 数据采集
- 替代 PI（OSIsoft PI System）等传统实时数据库
- 典型部署规模：10 万 ~ 30 万测点（Tag）
- 采集频率：100ms ~ 5s，以 1s 为主
- 数据保留：热存 3 个月（SSD），冷存 2 年（HDD）

## 功能需求

### 数据写入
- 支持批量写入（DataBatch）
- 先写 WAL 确保持久化，后写 MemTable
- MemTable 达到 64KB 阈值自动 flush 到 SSTable
- WAL 支持崩溃后恢复

### 数据查询
- 最新值查询（LatestCache，O(1) 复杂度）
- 原始数据查询（按 Tag + 时间范围）
- 聚合查询（AVG/MAX/MIN/SUM/COUNT/FIRST/LAST，时间桶）
- LIKE 模式匹配

### 存储管理
- SSTable 自定义二进制格式，CRC32 校验
- 后台 Compaction 合并小文件
- 冷热分层自动迁移
- 冷数据过期自动清理
- 外部归档

### SQL 接口
6 种语句类型：
- INSERT INTO table(tag,value,ts) VALUES(...)
- SELECT ... FROM ... WHERE ... LATEST / GROUP BY TIME_BUCKET
- CREATE TAG name (type='analog', ...)
- CREATE ALARM name ON tag WHEN condition THEN ACTION(...)
- CREATE USER name WITH PASSWORD '...' ROLE '...'
- ALTER SYSTEM SET key = value

### 报警引擎
- 条件运算符：>, <, >=, <=, ==, !=
- 60 秒防重复触发
- 最多保留 10000 条事件

### 用户认证
- 默认管理员：admin / admin123
- 密码加盐（16 字节随机 salt）
- Token 认证（64 位 hex，8 小时有效期）
- 角色：admin（全部）/ operator（读写）/ viewer（只读）
- 持久化到 meta/users.db

### 通信
- gRPC 协议，4 个 RPC：Query/Insert/Auth/Admin
- C SDK 提供纯 C 接口：connect/query/disconnect
- CLI 客户端：交互/批处理/TABLE/CSV/JSON 格式

## 配置项（minitsdb.conf）
- listen_port, max_tag_count, data_dir
- hot_retention_days（默认 90）
- cold_retention_days（默认 730）
- flush_threshold_bytes（默认 65536）
- flush_interval_ms（默认 100）
- compaction_interval_ms（默认 300000）
- tier_check_interval_ms（默认 3600000）
- auth_token_ttl_hours（默认 8）

## 非功能需求
- 跨平台：Windows（Win32 API）+ Linux（POSIX）
- OS 抽象层分离文件 IO 和文件系统操作
- 高性能：Gorilla 高压缩率，减少 IO
- 可测试：Google Test + ctest 自动化
