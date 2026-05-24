## ADDED Requirements

### Requirement: TestServer SHALL start and stop the server process
A TestServer helper class SHALL manage the full lifecycle of the minitsdb server process: start, wait for readiness, and graceful shutdown.

#### Scenario: Server starts and becomes ready
- **WHEN** TestServer::Start() is called with a valid executable path and port
- **THEN** the server SHALL be running and listening on the specified port within 10 seconds

#### Scenario: Server stops gracefully
- **WHEN** TestServer::Stop() is called
- **THEN** the server process SHALL exit cleanly within 5 seconds

#### Scenario: Port conflict is detected
- **WHEN** TestServer::Start() is called on a port already in use
- **THEN** the method SHALL return false and log a warning

### Requirement: C SDK test SHALL auto-start the server
test_c_sdk SHALL no longer require manual server startup. The test SHALL use CTest fixtures or a wrapper to automatically start/stop the server.

#### Scenario: SDK test passes with auto-started server
- **WHEN** running `ctest -R test_c_sdk` without any prior server running
- **THEN** the test SHALL pass (connect, query, disconnect)

### Requirement: CLI integration tests SHALL auto-start the server
test_cli SHALL use TestServer in SetUpTestSuite to start the server and enable the currently DISABLED integration tests.

#### Scenario: CLI insert and query test
- **WHEN** running `ctest -R test_cli`
- **THEN** the test SHALL pass, including InsertAndQuery, FormatTable, and FormatJson sub-tests
