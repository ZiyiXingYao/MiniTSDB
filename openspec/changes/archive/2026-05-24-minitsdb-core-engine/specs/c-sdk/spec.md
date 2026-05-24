## ADDED Requirements

### Requirement: C SDK SHALL expose connect/disconnect API
The C SDK SHALL provide minitsdb_connect() and minitsdb_disconnect() functions that wrap gRPC channel management.

#### Scenario: Connect to server
- **WHEN** calling `MinitsdbConn* conn = minitsdb_connect("127.0.0.1", 8086, "admin", "pass123")`
- **THEN** the SDK SHALL establish a gRPC channel and authenticate

#### Scenario: Disconnect
- **WHEN** calling `minitsdb_disconnect(conn)`
- **THEN** the SDK SHALL close the gRPC channel and free resources

### Requirement: C SDK SHALL expose query API
The SDK SHALL provide minitsdb_query() that sends SQL and returns a result set.

#### Scenario: Execute SELECT and read results
- **WHEN** calling `MinitsdbResult* res = minitsdb_query(conn, "SELECT * FROM boiler_temp LATEST")`
- **THEN** the SDK SHALL return a result set with rows and columns accessible via minitsdb_result_rows() and minitsdb_result_value()

### Requirement: C SDK SHALL be built as a shared library
The SDK SHALL be compiled as a shared library (.dll on Windows, .so on Linux) with a C-compatible header.

#### Scenario: Link SDK into C program
- **WHEN** a C program includes minitsdb.h and links against libminitsdb
- **THEN** the program SHALL successfully call all SDK functions
