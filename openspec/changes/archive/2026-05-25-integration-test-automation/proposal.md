## Why

当前 C SDK 测试 (`test_c_sdk.c`) 和 CLI 测试 (`test_cli`) 依赖手动启动服务端后才可运行，无法集成到 `ctest` 自动化流程。每次提交前需要额外步骤，容易遗漏。需要让测试自动启动服务端进程，执行测试，然后关闭服务端。

## What Changes

- 创建测试辅助模块，在测试启动时自动启动 `minitsdb` 服务端进程
- 修改 `test_c_sdk.c` 使用测试辅助模块自动管理服务端生命周期
- 修改 `test_cli` 使用测试辅助模块自动管理服务端生命周期
- 确保所有测试可一键 `ctest` 执行

## Capabilities

### New Capabilities
- `test-server-automation`: 测试服务端自动化启动/停止辅助模块
- `sdk-test-automation`: C SDK 测试改造为自动化
- `cli-test-automation`: CLI 测试改造为自动化

### Modified Capabilities

无（主规范中的 `c-sdk` 和 `cli-client` 仅涉及测试方式，不修改接口定义）

## Impact

- **代码**: 新增测试辅助源文件，修改现有测试文件
- **依赖**: 无新增外部依赖
- **测试**: test_c_sdk 从跳过变为自动执行，15/15 测试全通过
