## ADDED Requirements

### Requirement: Server SHALL expose gRPC service for all operations
All external communication SHALL use gRPC. The protobuf service definition SHALL include Query, Insert, Auth, and Admin RPCs.

#### Scenario: Client connects via gRPC
- **WHEN** a client opens a gRPC channel to the server at host:port
- **THEN** the server SHALL accept the connection and respond to RPCs

### Requirement: Query RPC SHALL accept SQL string and return tabular results
The Query RPC SHALL accept a SQL string, execute it, and return column names + row values.

#### Scenario: Execute SELECT and get results
- **WHEN** a client calls QueryRPC with "SELECT tag, value, ts FROM boiler_temp WHERE tag='BOILER-001' LATEST"
- **THEN** the server SHALL return column names ["tag", "value", "ts"] and one row of data

### Requirement: Insert RPC SHALL support batch data point insertion
The Insert RPC SHALL accept a batch of DataPoint messages for efficient bulk writing.

#### Scenario: Batch insert 1000 data points
- **WHEN** a client calls InsertRPC with 1000 data points
- **THEN** the server SHALL write all 1000 points and return success with count

### Requirement: Auth RPC SHALL handle login and token management
The Auth RPC SHALL accept username/password and return a session token with expiry.

#### Scenario: Login credentials accepted
- **WHEN** a client calls AuthRPC with valid credentials
- **THEN** the server SHALL return a token valid for 8 hours
