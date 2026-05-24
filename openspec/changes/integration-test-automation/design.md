## Context

当前 test_c_sdk.c 和 test_cli.cpp 中的集成测试需要手动启动 minitsdb 服务端进程，无法纳入自动化 ctest 流程。ctest 报告显示 15/15 测试中唯一失败的就是 test_c_sdk（因无服务端运行）。

## Goals / Non-Goals

**Goals:**
- 所有测试可一键 `ctest` 执行，无需手动启动服务端
- test_c_sdk 从手动变为自动运行
- test_cli 中 DISABLED 的集成测试启用
- 服务端由测试框架自动启动和停止

**Non-Goals:**
- 不修改服务端本身的代码
- 不修改 C SDK API 或 CLI 接口

## Decisions

### 1. 服务端生命周期管理
**Chosen**: 测试辅助模块在测试套件级别启动/停止服务端进程
**Rationale**: `SetUpTestSuite` / `TearDownTestSuite` 在 Google Test 中保证只执行一次，避免反复启动服务端的开销。每个测试用例单独连接/断开即可。
**Implementation**: 
- 创建一个 `TestServer` 辅助类
- `Start()`: 用 `CreateProcess` (Win) / `fork+exec` (Linux) 启动 `minitsdb.exe`
- `Stop()`: 发送 shutdown 信号 + 等待进程退出
- `WaitForReady()`: 轮询 gRPC 端口直到可用（重试 + 超时）

### 2. test_c_sdk 改造
**Chosen**: 保持 C SDK 测试为独立进程，改为先启动服务端再测试
**Rationale**: test_c_sdk.c 是纯 C 文件，编译为独立 exe。改造方法：在 CMakeLists.txt 中添加 CTest `FIXTURES_REQUIRED` / `FIXTURES_SETUP` 机制，定义 server fixture。

### 3. test_cli 改造
**Chosen**: 在 TestSuite 级别启动服务端，启用 DISABLED 测试
**Rationale**: Google Test 的 `SetUpTestSuite` / `TearDownTestSuite` 静态方法最适合。

## Risks / Trade-offs

- **[风险] 端口冲突**: 多个测试并行运行时端口冲突
  → **缓解**: 使用动态端口（当前已实现 `next_port++`），服务端通过 cmdline 参数指定端口
- **[风险] 服务端启动超时**: 在 CI 环境下启动慢
  → **缓解**: 可配置超时（默认 10 秒），轮询间隔 100ms
- **[风险] 残留进程**: 测试异常退出时服务端进程残留
  → **缓解**: `Stop()` 加 kill fallback，进程句柄 RAII 管理
