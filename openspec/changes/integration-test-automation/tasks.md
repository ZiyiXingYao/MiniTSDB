## 1. TestServer 辅助类

- [ ] 1.1 创建 `tests/test_server.h` 头文件：`TestServer` 类声明（Start/Stop/WaitForReady）
- [ ] 1.2 创建 `tests/test_server.cpp` 实现
- [ ] 1.3 实现 `TestServer::Start()`: CreateProcess (Win) / fork+exec (Linux) 启动 minitsdb
- [ ] 1.4 实现 `TestServer::Stop()`: 发送关闭信号 + 等待进程退出 + kill fallback
- [ ] 1.5 实现 `TestServer::WaitForReady()`: gRPC 端口轮询（100ms 间隔，10s 超时）
- [ ] 1.6 实现 `TestServer::GetPort()`: 获取服务端端口号

## 2. C SDK 测试改造

- [ ] 2.1 修改 `test_c_sdk.c`: 添加 --host/--port 参数支持
- [ ] 2.2 修改 `CMakeLists.txt`: 为 test_c_sdk 添加 CTest FIXTURES 配置
- [ ] 2.3 验证 `ctest -R test_c_sdk` 自动执行通过

## 3. CLI 测试改造

- [ ] 3.1 修改 `tests/test_cli.cpp`: 添加 TestServer 到 CliTest fixture
- [ ] 3.2 在 CliTest::SetUpTestSuite() 中调用 TestServer::Start() + WaitForReady()
- [ ] 3.3 在 CliTest::TearDownTestSuite() 中调用 TestServer::Stop()
- [ ] 3.4 启用 DISABLED_InsertAndQuery、DISABLED_FormatTable、DISABLED_FormatJson 测试
- [ ] 3.5 实现 InsertAndQuery 测试：INSERT 一条记录 → SELECT LATEST 验证
- [ ] 3.6 实现 FormatTable 测试：--format table 验证表格输出
- [ ] 3.7 实现 FormatJson 测试：--format json 验证 JSON 输出

## 4. CMake 集成

- [ ] 4.1 在 CMakeLists.txt 中将 `tests/test_server.cpp` 加入测试编译
- [ ] 4.2 配置 CTest fixture（FIXTURES_SETUP / FIXTURES_REQUIRED）
- [ ] 4.3 验证 `ctest --output-on-failure -C Release` 全部 15/15 通过
