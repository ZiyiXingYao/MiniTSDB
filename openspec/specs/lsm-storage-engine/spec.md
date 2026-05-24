# lsm-storage-engine Specification

## Purpose
TBD - created by archiving change minitsdb-core-engine. Update Purpose after archive.
## Requirements
### Requirement: MemTable SHALL buffer incoming writes in memory
Each MemTable SHALL accept writes grouped by Tag name. When the MemTable reaches configurable size (default 64KB) or flush interval (default 100ms), it SHALL be flushed to a new SSTable on disk.

#### Scenario: Write data points until flush threshold
- **WHEN** writing 100 data points to the same tag
- **THEN** the data SHALL remain in MemTable until flush triggers

#### Scenario: Flush creates SSTable file
- **WHEN** MemTable flush interval elapses
- **THEN** a new SSTable file SHALL be created at `data/hot/tags/<tag-name>/<date>.sst`

### Requirement: WAL SHALL persist writes before acknowledging
Every write SHALL be appended to the Write-Ahead Log before being applied to MemTable. On recovery, the WAL SHALL replay to restore MemTable state.

#### Scenario: Crash recovery from WAL
- **WHEN** the system restarts after an unclean shutdown with unflushed data
- **THEN** all acknowledged writes SHALL be recovered from WAL

### Requirement: SSTable SHALL store compressed data blocks
Each SSTable SHALL contain a header (magic number, version, tag name, time range, point count) followed by one or more Gorilla-compressed data blocks. Each block SHALL cover a fixed time window (default 1 hour).

#### Scenario: Read data from SSTable by time range
- **WHEN** querying a tag for data within a specific time range
- **THEN** only the blocks overlapping the query range SHALL be decompressed

### Requirement: Compaction SHALL merge small SSTables
A background compaction thread SHALL periodically merge SSTable files. Small files SHALL be merged into larger ones. Data older than `hot_retention_days` SHALL be moved to the cold tier.

#### Scenario: Merge overlapping SSTables
- **WHEN** two SSTables for the same tag exist with adjacent time ranges
- **THEN** compaction SHALL produce a single merged SSTable with deduplicated timestamps

### Requirement: All C++ code SHALL follow Google C++ Style Guide
Naming, formatting, comments, and file organization SHALL conform to https://google.github.io/styleguide/cppguide.html.

#### Scenario: Code review checks style compliance
- **WHEN** reviewing a pull request for the storage engine module
- **THEN** all identifiers SHALL use Google-style naming (classes: PascalCase, variables/functions: snake_case, constants: kPascalCase)

