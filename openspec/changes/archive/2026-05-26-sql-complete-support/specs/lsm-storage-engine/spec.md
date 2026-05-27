## MODIFIED Requirements

### 需求:系统 SHALL use three-level naming for all storage paths
The storage path SHALL change from `data/hot/tags/<tag>/` to `data/hot/<database>/tables/<table>/tags/<tag>/`.

#### 场景:Write with database context
- **当** 写入数据到表 boiler_data，当前数据库为 factory_a
- **那么** 数据存储路径为 `data/hot/factory_a/tables/boiler_data/tags/BOILER-001/<date>.sst`

### 需求:All storage engine APIs SHALL accept database and table parameters
StorageEngine::Write, ReadRaw, ReadAggregated, ReadLatest SHALL accept `db` and `table` string parameters in addition to `tag`.

#### 场景:Write with three-level path
- **当** 调用 `engine->Write("factory_a", "boiler_data", "BOILER-001", point)`
- **那么** 数据被正确写入三级路径
