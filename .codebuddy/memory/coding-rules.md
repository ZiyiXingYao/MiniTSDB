---
name: coding-rules
description: MiniTSDB 编码规范、命名约定、代码风格、工程规则
type: project
---

# MiniTSDB 编码规则

## 代码风格
- 遵循 Google C++ Style Guide
- 使用 `.clang-format` 配置 Google 风格
- 所有代码符合 C++20 标准

## 命名约定
| 类别 | 风格 | 示例 |
|------|------|------|
| 类/结构体 | PascalCase | StorageEngine, LatestCache |
| 变量/函数 | snake_case | write_data(), hot_retention_days |
| 常量 | kPascalCase | kDefaultPort |
| 枚举 | PascalCase | TagType::ANALOG |
| 文件 | snake_case | compressor.h, grpc_server.cpp |
| 宏 | UPPER_SNAKE | MINITSDB_MAGIC |

## 项目规则
1. **文件分离**：头文件中的实现必须移到源文件（.cpp），头文件只保留声明
2. **依赖管理**：使用 vcpkg，不直接引入第三方源码
3. **跨平台**：OS 操作必须通过 `src/common/os/` 抽象层，不直接使用平台 API
4. **IO 操作**：文件 IO 使用 `os::File` RAII 封装，文件系统操作使用 `os::fs` 命名空间
5. **测试**：所有核心功能必须有 Google Test 测试，集成测试可通过 TestServer 自动化
6. **OpenSpec 工作流**：遵循 brainstorming → writing-plans → 实现 → code-review → verification 流程
7. **头文件自包含**：每个头文件必须能独立编译（包含其所有依赖）
8. **无 using namespace std**：避免全局 using 声明
9. **异常**：项目中使用错误码返回而非异常

## OpenSpec 规范
- 主规范路径：`openspec/specs/{module}/spec.md`
- 变更路径：`openspec/changes/{change-name}/`
- 变更产出物：proposal.md → design.md → specs/ → tasks.md
- 归档路径：`openspec/changes/archive/{date}-{change}/`

## 测试规则
- 测试目标命名：test_{module}（如 test_compressor, test_wal）
- 测试文件位置：`tests/test_{module}.cpp`
- 集成测试需确保服务端自动启停
- 纯 C 测试使用 `.c` 扩展名
