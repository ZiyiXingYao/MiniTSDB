# test-automation Specification

## Purpose
Define the test infrastructure for automated integration testing, ensuring all tests can run via `ctest` without manual server setup.

## Requirements
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
