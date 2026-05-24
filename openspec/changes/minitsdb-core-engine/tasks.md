## 1. Project Setup

- [x] 1.1 Create CMakeLists.txt with C++20 standard and Google style compiler flags
- [x] 1.2 Set up .clang-format file matching Google C++ style
- [x] 1.3 Create directory structure (src/{common,storage,cache,sql,server,alarm}, tests, docs)
- [x] 1.4 Add .gitignore for build artifacts and data directories

## 2. Common / Base Types

- [x] 2.1 Implement common/types.h: DataPoint, TagMeta, TagType, Value variant, TimeRange, AggType
- [x] 2.2 Implement common/logger.h: LogLevel, LogPrint, LogInit macros
- [x] 2.3 Implement common/config.h: Config class with Load/Get/GetInt from key=value file

## 3. Gorilla Compression

- [x] 3.1 Implement TimestampCompressor: delta-of-delta encoding with bit-level writing
- [x] 3.2 Implement TimestampCompressor::Decode: bit-level reading recovery
- [x] 3.3 Implement ValueCompressor: XOR-based float64 encoding with leading/trailing zero handling
- [x] 3.4 Implement ValueCompressor::Decode: XOR-based float64 decoding
- [x] 3.5 Implement BlockCompressor: Compress/Decompress vector of DataPoints into CompressedBlock
- [x] 3.6 Write unit tests for compressor (test_compressor.cpp): stable interval, jitter, identical values, all 4 datatypes

## 4. Storage Engine — MemTable & WAL

- [x] 4.1 Implement MemTable header: in-memory write buffer per tag with flush threshold
- [x] 4.2 Implement MemTable: Add data points, track size, flush to SSTable callback
- [x] 4.3 Implement WAL: AppendWrite, Recover, Truncate with file-based sequential log
- [x] 4.4 Implement StorageEngine::Write: WAL append → MemTable insert → cache update
- [x] 4.5 Implement StorageEngine::WriteBatch: batch insert for multiple tags

## 5. Storage Engine — SSTable & Compaction

- [x] 5.1 Implement SSTable format: header (magic, version, tag, time range, point count) + compressed blocks
- [x] 5.2 Implement SSTableWriter: write compressed blocks to file
- [x] 5.3 Implement SSTableReader: open file, read header, decompress blocks by time range
- [ ] 5.4 Implement background Compaction: merge small SSTables, move cold data to HDD
- [x] 5.5 Implement StorageEngine::ReadRaw: locate SSTable files, decompress matching time range
- [x] 5.6 Implement StorageEngine::ReadAggregated: stream decompress + bucket aggregation
- [x] 5.7 Implement StorageEngine::Flush: force flush all MemTables to SSTable
- [ ] 5.8 Write unit tests for storage (test_storage.cpp): write/read cycle, compaction

## 6. Latest Value Cache

- [x] 6.1 Implement LatestCache: Update/Get/GetByPattern/GetAll/Remove with shared_mutex
- [x] 6.2 Implement LIKE pattern matching (% and _ wildcards)
- [x] 6.3 Wire cache update into StorageEngine::Write path
- [x] 6.4 Wire cache into StorageEngine::ReadLatest

## 7. SQL Interface — Parser

- [x] 7.1 Implement SQLParser::ParseInsert: INSERT INTO ... VALUES syntax
- [x] 7.2 Implement SQLParser::ParseSelect: SELECT ... FROM ... WHERE ... GROUP BY syntax
- [x] 7.3 Implement SQLParser::ParseSelect: LATEST keyword support
- [x] 7.4 Implement SQLParser::ParseSelect: TIME_BUCKET function and aggregation parsing
- [x] 7.5 Implement SQLParser::ParseCreateTag: CREATE TAG syntax with properties
- [x] 7.6 Implement SQLParser::ParseCreateAlarm: CREATE ALARM with condition and action
- [x] 7.7 Implement SQLParser::ParseAlterSystem: ALTER SYSTEM SET key=value

## 8. SQL Interface — Executor

- [x] 8.1 Implement Executor::ExecuteInsert: parse → build DataBatch → call StorageEngine::Write
- [x] 8.2 Implement Executor::ExecuteSelectLatest: parse → call LatestCache → format result
- [x] 8.3 Implement Executor::ExecuteSelectRaw: parse → call StorageEngine::ReadRaw → format
- [x] 8.4 Implement Executor::ExecuteSelectAggregate: call ReadAggregated → bucket format
- [x] 8.5 Implement Executor::ExecuteCreateTag: persist TagMeta to metadata store
- [x] 8.6 Implement Executor::ExecuteCreateAlarm: register alarm rule in alarm engine
- [x] 8.7 Implement Executor::ExecuteAlterSystem: update config and persist

## 9. Cold / Hot Tiering

- [ ] 9.1 Implement TierManager: scan hot directory, identify data older than hot_retention
- [ ] 9.2 Implement TierManager::MoveToCold: move SSTable files from hot/ to cold/ directory
- [ ] 9.3 Implement TierManager::PruneCold: delete SSTable files exceeding cold_retention
- [ ] 9.4 Implement TierManager::ArchiveToExternal: move to archive_path instead of delete
- [ ] 9.5 Implement background tier management thread with configurable interval

## 10. Real-time Alarm Engine

- [x] 10.1 Implement AlarmRule struct: name, tag, condition expression, action list
- [x] 10.2 Implement AlarmEngine: register rules, evaluate on write, trigger actions
- [x] 10.3 Implement condition evaluator: simple expression parser for "value > 1400.0" etc.
- [x] 10.4 Implement alarm event storage: alarms table queryable via SELECT
- [x] 10.5 Wire alarm evaluation into StorageEngine::Write path

## 11. User Authentication

- [x] 11.1 Implement user data model: User {name, password_hash, role, created_at}
- [x] 11.2 Implement password hashing (SHA-256 or bcrypt-simple)
- [x] 11.3 Implement session token generation and validation
- [x] 11.4 Implement role-based permission check during SQL execution
- [ ] 11.5 Implement CREATE USER / ALTER USER SQL syntax and execution
- [ ] 11.6 Implement login handshake: LOGIN username/password → session token response
- [x] 11.7 Persist user accounts to data/hot/meta/users.db

## 12. Server — gRPC Service

- [ ] 12.1 Define MiniTSDB.proto: service with Query/Insert/Auth/Admin RPCs
- [ ] 12.2 Generate C++ gRPC + Protobuf code from .proto
- [ ] 12.3 Implement gRPC server: start, stop, graceful shutdown with configurable port
- [ ] 12.4 Implement Query RPC: receives SQL string, returns result as repeated rows
- [ ] 12.5 Implement Insert RPC: batch data point insertion
- [ ] 12.6 Implement Auth RPC: login → token response
- [ ] 12.7 Implement result formatting in gRPC response
- [ ] 12.8 Implement main.cpp: signal handling, gRPC server start/stop

## 13. CLI Client

- [ ] 13.1 Implement CLI entry point: argument parsing (--host, --port, -f, --format)
- [ ] 13.2 Implement interactive mode: readline-style prompt with history
- [ ] 13.3 Implement batch mode: execute SQL from file
- [ ] 13.4 Implement gRPC client connection to server
- [ ] 13.5 Implement result formatting: table, CSV, JSON output modes

## 14. C SDK

- [ ] 14.1 Define C API header: minitsdb.h with connect/query/result/free/disconnect
- [ ] 14.2 Implement API wrappers over gRPC C++ client calls
- [ ] 14.3 Implement result set management: rows, columns, value accessors
- [ ] 14.4 Build as shared library (.dll/.so)
- [ ] 14.5 Write C SDK usage example (test_c_sdk.c)

## 15. Testing — Google Test

- [ ] 15.1 Set up gtest via vcpkg, integrate with CMake CTest
- [ ] 15.2 Migrate existing test_compressor.cpp to use TEST() macros
- [ ] 15.3 Write gRPC server unit tests (mock client → server round-trip)
- [ ] 15.4 Write CLI integration test (spawn process, send SQL, check output)
- [ ] 15.5 Write C SDK integration test (link .dll, call API, verify results)
