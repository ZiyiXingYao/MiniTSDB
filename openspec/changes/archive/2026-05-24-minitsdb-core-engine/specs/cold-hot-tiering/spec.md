## ADDED Requirements

### Requirement: System SHALL maintain hot and cold storage tiers with configurable retention
The hot tier (SSD) SHALL store recent data for fast queries. The cold tier (HDD) SHALL store older compressed data. Retention periods SHALL be configurable.

#### Scenario: Hot retention default 90 days
- **WHEN** the system writes data newer than 90 days
- **THEN** it SHALL be stored in `data/hot/` on the hot storage path

#### Scenario: Data older than hot_retention SHALL be moved to cold
- **WHEN** a background tier check runs and finds data aged 91 days in the hot tier
- **THEN** the data SHALL be moved to `data/cold/<year>/<month>/<tag>.sst`

### Requirement: System SHALL auto-delete data exceeding cold retention
Data exceeding the cold retention period SHALL be automatically deleted.

#### Scenario: Delete data older than 2 years
- **WHEN** the system finds a cold SSTable with data older than 730 days
- **THEN** that SSTable file SHALL be deleted

### Requirement: System SHALL support external archive path
If configured, data SHALL be moved to an archive path (external large disk) instead of being deleted.

#### Scenario: Archive instead of delete
- **WHEN** archive_path is configured and data exceeds cold retention
- **THEN** the system SHALL move the SSTable file to `archive_path/<year>/<month>/` instead of deleting

### Requirement: Retention periods and paths SHALL be configurable via SQL
Users SHALL configure tier settings via ALTER SYSTEM SET statements.

#### Scenario: Configure hot retention
- **WHEN** receiving `ALTER SYSTEM SET hot_retention = 180`
- **THEN** the system SHALL update its hot retention to 180 days and write the config to disk
