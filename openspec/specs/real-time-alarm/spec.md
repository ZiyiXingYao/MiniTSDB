# real-time-alarm Specification

## Purpose
TBD - created by archiving change minitsdb-core-engine. Update Purpose after archive.
## Requirements
### Requirement: System SHALL support CREATE ALARM for defining alarm rules
The CREATE ALARM statement SHALL define an alarm rule on a specific tag with a condition expression and actions.

#### Scenario: Create high temperature alarm
- **WHEN** receiving `CREATE ALARM high_temp ON boiler_temp WHEN value > 1400.0 THEN ACTION('log', 'notify')`
- **THEN** the system SHALL store the alarm rule and activate it for all future writes to this tag

### Requirement: Alarm rules SHALL be evaluated on every write
On each INSERT for a tag with an active alarm rule, the condition expression SHALL be evaluated against the new value.

#### Scenario: Alarm triggers on threshold crossing
- **WHEN** a write to BOILER-001 with value 1420.0 arrives and an alarm rule has `value > 1400.0`
- **THEN** the system SHALL evaluate the condition as true and execute the configured actions

#### Scenario: Alarm does not trigger below threshold
- **WHEN** a write to BOILER-001 with value 1200.0 arrives
- **THEN** the system SHALL evaluate the condition as false and take no action

### Requirement: Alarm actions SHALL include logging
The 'log' action SHALL write the alarm event to a persistent alarms table.

#### Scenario: Log alarm event
- **WHEN** an alarm triggers with action 'log'
- **THEN** the system SHALL append a record with (alarm_name, tag, value, timestamp, condition) to the alarms table

### Requirement: Alarms SHALL be queryable via SELECT
Users SHALL query recent alarm events using standard SELECT syntax.

#### Scenario: Query recent alarms
- **WHEN** receiving `SELECT * FROM alarms WHERE tag = 'BOILER-001' AND ts > NOW() - INTERVAL '1 hour'`
- **THEN** the system SHALL return alarm events for BOILER-001 within the last hour

