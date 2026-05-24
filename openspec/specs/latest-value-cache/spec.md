# latest-value-cache Specification

## Purpose
TBD - created by archiving change minitsdb-core-engine. Update Purpose after archive.
## Requirements
### Requirement: Latest value cache SHALL be updated on every write
Every successful write to the storage engine SHALL also update the in-memory latest value cache.

#### Scenario: Update cache after write
- **WHEN** a DataPoint is written for tag BOILER-001 at timestamp T with value 523.7
- **THEN** the latest cache SHALL reflect (BOILER-001, 523.7, T)

#### Scenario: Overwrite with newer value
- **WHEN** a DataPoint with a newer timestamp is written for BOILER-001
- **THEN** the cache SHALL contain the newer value

#### Scenario: Ignore out-of-order old data
- **WHEN** a DataPoint with an older timestamp is written after a newer one already exists
- **THEN** the cache SHALL retain the newer value (not be overwritten)

### Requirement: LATEST queries SHALL read from cache only
SELECT ... LATEST queries SHALL NOT read from disk. They SHALL return results from the in-memory cache.

#### Scenario: LATEST query returns immediately
- **WHEN** a LATEST query selects 1000 matching tags
- **THEN** the query SHALL complete in under 1 millisecond

### Requirement: Cache SHALL support LIKE pattern matching
The cache SHALL support SQL LIKE-style pattern matching on tag names. Supported patterns: `%` (any sequence), `_` (single character).

#### Scenario: LIKE with prefix wildcard
- **WHEN** querying `WHERE tag LIKE 'BOILER-%'`
- **THEN** all tags starting with "BOILER-" SHALL be returned

