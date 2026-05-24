# MiniTSDB

轻量级时序数据库，面向工业 SIS（厂级监控信息系统）场景设计，替代 PI（OSIsoft PI System）等传统实时数据库。

## 特性

- **Gorilla 压缩** — Delta-of-delta 时间戳 + XOR 浮点值编码，压缩比 3:1 ~ 30:1
- **SQL 全接口** — 写入、查询、管理全部通过 SQL 完成
- **LSM-Tree 存储** — MemTable + WAL + SSTable 分层存储
- **冷热分层** — SSD 热存 + HDD 冷存 + 外部归档，自动迁移
- **实时报警** — 基于 SQL 规则表达式，写入时自动评估
- **用户权限** — 多角色（admin/operator/viewer）+ Token 鉴权
- **gRPC 服务** — Protobuf 定义的 RPC 接口
- **CLI 客户端** — 交互式/批处理/JSON/CSV 多种输出
- **C SDK** — 纯 C 接口的客户端动态库

## 快速开始

### 构建

```bash
# 依赖：Clang、Ninja、vcpkg（安装 gtest）
# 编译 gtest 参考：https://github.com/google/googletest

cmake -B build/release -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/release -j8
```

### 启动服务端

```bash
./build/release/minitsdb.exe -c minitsdb.conf
```

### CLI 客户端

```bash
# 交互模式
./build/release/minitsdb_cli.exe

# 单条 SQL
./build/release/minitsdb_cli.exe -e "SELECT tag, value FROM sensors LATEST"

# JSON 输出
./build/release/minitsdb_cli.exe --format json -e "SELECT * FROM sensors LATEST"
```

### 使用 C SDK

```c
#include "minitsdb.h"

minitsdb_conn* conn = minitsdb_connect("127.0.0.1", 8086, "admin", "admin123");
minitsdb_result* res = minitsdb_query(conn, "SELECT tag, value FROM sensors LATEST");

for (int r = 0; r < minitsdb_result_rows(res); r++) {
    printf("%s = %s\n",
           minitsdb_result_value(res, r, 0),
           minitsdb_result_value(res, r, 1));
}

minitsdb_result_free(res);
minitsdb_disconnect(conn);
```

## 配置文件

```ini
[log]
directory = ./logs         # 日志目录
level = info               # 日志级别
keep_files = 1000          # 保留日志个数

[storage]
data_dir = ./data          # 数据存储根目录
hot_retention_days = 90    # 热数据保留天数
cold_retention_days = 730  # 冷数据保留天数

[server]
port = 8086                # gRPC 端口
```

## 测试

```bash
cmake --build build/release --target test_compressor test_wal test_sql_parser \
      test_alarm test_auth test_storage test_grpc_server

# 运行全部 45 个测试
for t in test_compressor test_wal test_sql_parser test_alarm \
         test_auth test_storage test_grpc_server; do
    ./build/release/$t.exe
done
```

## 项目结构

```
src/
├── alarm/          # 实时报警引擎
├── auth/           # 用户认证管理 (SHA-256)
├── cache/          # LatestCache 最新值缓存
├── cli/            # CLI 客户端
├── common/         # 基础类型、配置、日志
├── sdk/            # C SDK
├── server/         # gRPC 服务端
├── sql/            # SQL 解析器 + 执行器
└── storage/        # 存储引擎 (MemTable/WAL/SSTable/Compaction/TierManager)
```

## 架构

```
┌─────────────────────────────────────────────┐
│               SQL Interface                  │
│    Parser → Planner → Executor              │
├──────────┬──────────┬───────────┬───────────┤
│ 写入路径  │ 实时查询  │ 历史查询   │ 报警引擎   │
│  WAL     │ 最新值    │ 存储引擎   │ 规则匹配   │
│  MemTable│ 缓存      │ SSTable   │           │
└──────────┴──────────┴───────────┴───────────┘
     │          │           │           │
     ▼          ▼           ▼           ▼
┌─────────────────────────────────────────────┐
│         冷热分层存储 (SSD / HDD)              │
└─────────────────────────────────────────────┘
```

## 许可

MIT
