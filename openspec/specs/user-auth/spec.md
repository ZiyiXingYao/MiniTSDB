# user-auth Specification

## Purpose
TBD - created by archiving change minitsdb-core-engine. Update Purpose after archive.
## Requirements
### Requirement: System SHALL support user authentication via username/password
Clients SHALL authenticate by sending a LOGIN command with username and password. Upon success, the server SHALL return a session token. Subsequent SQL statements SHALL include this token.

#### Scenario: Successful login
- **WHEN** a client sends valid credentials (username='admin', password='admin123')
- **THEN** the server SHALL return a session token valid for 8 hours

#### Scenario: Failed login
- **WHEN** a client sends invalid credentials
- **THEN** the server SHALL reject the connection with an authentication error

### Requirement: System SHALL support three built-in roles: admin, operator, viewer
The system SHALL implement three pre-defined roles with different permission levels:
- **admin**: Full access — CREATE/ALTER/DROP TAG, CREATE ALARM, user management, INSERT, SELECT
- **operator**: Read-write access — INSERT, SELECT, query alarms, view all data
- **viewer**: Read-only access — SELECT LATEST, SELECT with aggregation, no INSERT

#### Scenario: admin creates a tag
- **WHEN** an admin user sends `CREATE TAG boiler_temp (...)`
- **THEN** the tag SHALL be created successfully

#### Scenario: viewer cannot write data
- **WHEN** a viewer user sends `INSERT INTO boiler_temp (tag, value) VALUES ('BOILER-001', 523.7)`
- **THEN** the system SHALL reject the statement with a permission denied error

### Requirement: System SHALL check permissions at SQL execution time
After parsing the SQL statement, the executor SHALL check whether the current user's role has the required permission for the operation.

#### Scenario: operator selects latest value
- **WHEN** an operator user sends `SELECT * FROM boiler_temp WHERE tag = 'BOILER-001' LATEST`
- **THEN** the system SHALL return the latest value

### Requirement: System SHALL persist user accounts to disk
User accounts SHALL be stored in a system file (e.g., `data/hot/meta/users.db`). The admin SHALL manage users via SQL-like commands.

#### Scenario: Create new user
- **WHEN** an admin user sends `CREATE USER 'engineer1' WITH PASSWORD 'pass123' ROLE 'operator'`
- **THEN** the new user SHALL be persisted and immediately usable for authentication

