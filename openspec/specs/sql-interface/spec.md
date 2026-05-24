# sql-interface Specification

## Purpose
TBD - created by archiving change minitsdb-core-engine. Update Purpose after archive.
## Requirements
### Requirement: System SHALL accept INSERT statements for data writing
The SQL interface SHALL parse and execute INSERT INTO statements. The parser SHALL support single-point and multi-row batch inserts.

#### Scenario: Single point write
- **WHEN** receiving `INSERT INTO boiler_temp (tag, value) VALUES ('BOILER-001', 523.7)`
- **THEN** the system SHALL write a DataPoint with the current server timestamp

#### Scenario: Batch write with explicit timestamps
- **WHEN** receiving `INSERT INTO boiler_temp (tag, value, ts) VALUES ('P-001', 4.2, '2026-05-24T10:30:00Z'), ('P-002', 0.8, '2026-05-24T10:30:01Z')`
- **THEN** the system SHALL write both points at their specified timestamps

### Requirement: System SHALL support SELECT with LATEST keyword for real-time queries
The LATEST keyword SHALL return the most recent value for each matching tag from the latest value cache.

#### Scenario: Query single tag latest
- **WHEN** receiving `SELECT * FROM boiler_temp WHERE tag = 'BOILER-001' LATEST`
- **THEN** the system SHALL return the single most recent DataPoint for BOILER-001

#### Scenario: Query multiple tags with LIKE
- **WHEN** receiving `SELECT tag, value, ts FROM boiler_temp WHERE tag LIKE 'BOILER-%' LATEST`
- **THEN** the system SHALL return the latest value for every tag matching the LIKE pattern

### Requirement: System SHALL support time-bucketed aggregate queries
The TIME_BUCKET function SHALL group data into fixed time windows and apply aggregate functions (AVG, MAX, MIN, SUM, COUNT).

#### Scenario: 5-minute average aggregation
- **WHEN** receiving `SELECT TIME_BUCKET('5m', ts) AS bucket, AVG(value) FROM boiler_temp WHERE tag = 'BOILER-001' AND ts BETWEEN '2026-05-23' AND '2026-05-24' GROUP BY bucket`
- **THEN** the system SHALL return one row per 5-minute bucket with the average value

### Requirement: System SHALL support CREATE TAG for metadata management
The CREATE TAG statement SHALL register a new measurement tag with its metadata.

#### Scenario: Register analog tag
- **WHEN** receiving `CREATE TAG boiler_temp (type='analog', unit='celsius', precision=1, collect_interval_ms=1000)`
- **THEN** the system SHALL store the tag metadata and make it queryable via INFORMATION_SCHEMA

