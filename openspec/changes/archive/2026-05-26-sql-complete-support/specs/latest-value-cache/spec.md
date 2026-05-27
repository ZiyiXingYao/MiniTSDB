## MODIFIED Requirements

### 需求:Cache key SHALL use "db:table:tag" format
The LatestCache key SHALL change from `tag` to `db:table:tag` to support three-level naming.

#### 场景:Update and query with database context
- **当** `cache->Update("factory_a", "boiler_data", "BOILER-001", point)`
- **那么** 内部 key 为 `"factory_a:boiler_data:BOILER-001"`
