## MODIFIED Requirements

### 需求:SnapshotStore key SHALL use "db:table:tag" format
OnWrite key SHALL change from `tag` to `db:table:tag`.

#### 场景:OnWrite with database context
- **当** `store->OnWrite("factory_a", "boiler_data", "BOILER-001", point)`
- **那么** 内部存储 key 为 `"factory_a:boiler_data:BOILER-001"`
