---
name: code-architecture
description: MiniTSDB 模块架构、目录结构、数据流、各模块职责
type: project
---

# MiniTSDB 代码架构

## 整体架构

```
                      SQL Interface（Parser → Executor）
                     /           |           |        \
             写入路径       实时查询       历史查询       报警引擎
              WAL          最新值         存储引擎        规则匹配
              MemTable     缓存           SSTable
              SSTable                     Gorilla 解码
                     \           |           |        /
                     冷热分层存储（Hot SSD / Cold HDD）
```

## 目录结构
```
src/
├── main.cpp                  # 服务端入口（信号处理、初始化、启动各组件）
├── common/                   # 公共基础层
│   ├── types.h               # 核心类型定义（Timestamp, Value, DataPoint, TagMeta, AggType 等）
│   ├── config.h/cpp          # 配置管理（INI 解析）
│   ├── logger.h/cpp          # 日志系统（spdlog + 滚动文件 + gzip）
│   └── os/                   # 操作系统抽象层
│       ├── file.h/cpp        # 文件 IO 抽象（Win32 CreateFile / POSIX open）
│       └── fs.h/cpp          # 文件系统操作（目录、删除、重命名、列表等）
├── storage/                  # 存储引擎层
│   ├── engine.h/cpp          # 存储引擎入口
│   ├── memtable.h/cpp        # 内存写入缓冲区（按 Tag 分组、64KB 阈值刷新）
│   ├── wal.h/cpp             # 预写日志（CRC32 校验、OS 级刷盘）
│   ├── sstable.h/cpp         # SSTable 读写器（自定义二进制格式、CRC32）
│   ├── compressor.h/cpp      # Gorilla 压缩算法（时间戳 delta-of-delta + 值 XOR）
│   ├── compaction.h/cpp      # 后台合并压缩（小文件合并）
│   └── tier_manager.h/cpp    # 冷热分层管理器（热→冷迁移、过期清理、外部归档）
├── cache/
│   └── latest_cache.h/cpp    # 最新值缓存（shared_mutex + unordered_map + LIKE 匹配）
├── sql/
│   ├── ast.h                 # 抽象语法树定义
│   ├── parser.h/cpp          # SQL 解析器（手写，6 种语句类型）
│   └── executor.h/cpp        # SQL 执行器（插入、查询、聚合、管理）
├── server/
│   └── grpc_server.h/cpp     # gRPC 服务端（4 个 RPC）
├── alarm/
│   └── alarm_engine.h/cpp    # 实时报警引擎（条件评估、60s 防重复）
├── auth/
│   └── auth_manager.h/cpp    # 用户认证（SHA-256 自实现、Token、角色权限）
├── sdk/
│   ├── minitsdb.h             # C SDK 公共头文件
│   └── minitsdb_sdk.cpp       # C SDK 实现（基于 gRPC 客户端）
└── cli/
    └── main.cpp               # CLI 客户端（交互/批处理/JSON/CSV）
```

## 各模块职责

### common/ 公共层
- **types.h**：Timestamp(int64_t), Value(variant<double,int64_t,string>), TagType(ANALOG/DIGITAL/STRING/ACCUMULATOR), DataPoint, TagMeta, DataBatch, TimeRange, AggType, StorageTier
- **config.h/cpp**：INI 配置文件解析
- **logger.h/cpp**：基于 spdlog 的日志，支持控制台输出、滚动文件、gzip 压缩
- **os/file.h/cpp**：RAII 文件句柄封装，跨平台 Win32/POSIX，支持移动语义
- **os/fs.h/cpp**：跨平台文件系统操作（CreateDirectories, Remove, Rename, Copy, Exists, ListDirectory, RemoveAll）

### storage/ 存储引擎
- **engine.h/cpp**：存储入口，管理 Init/Write/WriteBatch/ReadRaw/ReadAggregated/ReadLatest
- **memtable.h/cpp**：按 Tag 分组缓存写入数据，64KB 阈值触发 flush 回调
- **wal.h/cpp**：先写 WAL 再写 MemTable，CRC32 校验，确保崩溃恢复
- **sstable.h/cpp**：自定义二进制格式（MAGIC+Version+Tag+Blocks+CRC32），分块 CRC 计算防 OOM
- **compressor.h/cpp**：Gorilla 时间戳 delta-of-delta + 值 XOR 编码，支持 double/int64/string
- **compaction.h/cpp**：后台线程合并小 SSTable，默认 5 分钟间隔
- **tier_manager.h/cpp**：热->冷->归档三层管理，自动过期清理

### cache/ 缓存
- **latest_cache.h/cpp**：最新值缓存，shared_mutex 保护，支持按模式匹配（LIKE %_），按 Tag 查询

### sql/ 查询引擎
- **parser.h/cpp**：手写 SQL 解析器，支持 INSERT/SELECT/CREATE TAG/ALARM/USER/ALTER SYSTEM
- **executor.h/cpp**：执行计划生成和执行，调用 storage/cache/alarm 组件

### server/ 服务端
- **grpc_server.h/cpp**：4 个 RPC：Query(SQL), Insert(点), Auth(登录), Admin(SQL 命令)

### alarm/ 报警
- **alarm_engine.h/cpp**：条件评估（>,<,>=,<=,==,!=），60s 防重复，最多 10000 条事件

### auth/ 认证
- **auth_manager.h/cpp**：SHA-256 自实现，密码加盐，Token 生成，角色权限（admin/operator/viewer）

### sdk/ & cli/ 客户端
- **minitsdb_sdk.cpp**：纯 C 接口，基于 gRPC 客户端
- **cli/main.cpp**：交互模式、-e 单条 SQL、-f 批处理，TABLE/CSV/JSON 输出

## Protobuf/gRPC 定义
```protobuf
service MiniTSDB {
    rpc Query(QueryRequest) returns (QueryResponse);
    rpc Insert(InsertRequest) returns (InsertResponse);
    rpc Auth(AuthRequest) returns (AuthResponse);
    rpc Admin(AdminRequest) returns (AdminResponse);
}
```
