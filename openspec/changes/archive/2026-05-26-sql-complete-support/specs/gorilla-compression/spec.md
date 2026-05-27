## ADDED Requirements

### 需求:Timestamp precision SHALL be microsecond
All timestamps SHALL use microsecond precision (int64_t, unit changed from ms to μs).

#### 场景:Gorilla compression with microsecond timestamps
- **当** 压缩微秒精度的时间戳序列
- **那么** compression SHALL handle larger delta-delta values correctly
