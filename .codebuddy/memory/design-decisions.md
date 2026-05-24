---
name: design-decisions
description: MiniTSDB 核心设计原则、关键设计决策、SSTable/WAL 格式
type: project
---

# MiniTSDB 设计决策

## 核心设计原则
1. **SQL 全接口** — 所有操作（写入、查询、管理）通过 SQL 完成
2. **单机优先** — 先做好单机，稳定后再考虑分布式
3. **冷热分层** — 最近 3 个月 SSD 热存，历史数据 HDD 冷存
4. **高压缩率** — Gorilla 算法，支持 2 年数据保留

## 存储目录结构
```
data/
├── hot/                    # SSD 热存（3 个月）
│   ├── meta/tags.db        # 测点元数据
│   ├── meta/users.db       # 用户数据
│   ├── wal/wal.log         # 预写日志
│   └── tags/{tag-name}/    # 按 Tag 分目录
│       └── {date}.sst      # 按日期命名 SSTable
├── cold/                   # HDD 冷存（2 年）
│   └── {tag-name}/*.sst
└── archive/                # 外部归档（可选）
```

## SSTable 二进制格式
```
[MAGIC: 8 bytes]   "MINITSDB"
[Version: 4 bytes] uint32 (2 = CRC enabled)
[Tag name: N bytes]
[Block count: 4 bytes]
[Blocks...]
  [Min ts: 8 bytes]
  [Max ts: 8 bytes]
  [Ts len: 4 bytes]
  [Timestamp data: TsLen bytes]  (Gorilla delta-of-delta 压缩)
  [Val len: 4 bytes]
  [Value data: ValLen bytes]     (Gorilla XOR + 2-bit type tag)
[CRC32: 4 bytes]
```

## WAL 条目格式
```
[CRC: 4 bytes]     -> 数据部分 CRC32
[Type: 1 byte]     -> DATA_POINT / TAG_META / CHECKPOINT
[Data len: 4 bytes]
[Data: N bytes]    -> tag_len(2) + tag(N) + ts(8) + value(8)
```

## Gorilla 压缩
- **时间戳**：Delta-of-delta 编码，各级变长编码（0→1bit, 01→7bits, 011→9bits, 0111→12bits, 1111→32bits）
- **值编码**：2-bit type tag（00=double XOR, 01=int64 XOR, 10=string 直接存储）
- **压缩率估算**：时间戳 1-2 字节/点，浮点值 1-2 字节/点

## 关键设计决策
1. **SQL 手写解析器**：不依赖外部 SQL 解析库，减少依赖
2. **SHA-256 自实现**：认证模块不依赖 OpenSSL，减少外部依赖
3. **写前日志（WAL）**：先写 WAL 再写 MemTable，确保崩溃恢复
4. **冷热分层**：超过 hot_retention_days 自动迁移到冷存，由 tier_manager 管理
5. **Compaction**：后台合并小文件，减少文件碎片，默认 5 分钟间隔
6. **报警去重**：60 秒内相同规则不重复触发，最多保留 10000 条事件
7. **Token 认证**：登录返回 Token，有效期 8 小时，存储盐值密码
8. **测试自动化**：TestServer 辅助类管理服务端生命周期，ctest 一键全自动
