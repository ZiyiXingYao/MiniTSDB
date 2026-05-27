#pragma once

#include "common/types.h"
#include "storage/compressor.h"
#include "storage/wal.h"
#include "cache/latest_cache.h"
#include "alarm/alarm_engine.h"
#include "snapshot/snapshot_store.h"
#include <string>
#include <memory>
#include <vector>

namespace minitsdb {

// 存储引擎配置
struct StorageConfig {
    std::string hot_path = "./data/hot";
    std::string cold_path = "./data/cold";
    std::string archive_path = "./data/archive";
    int32_t hot_retention_days = 90;   // 热数据保留 90 天
    int32_t cold_retention_days = 730; // 冷数据保留 730 天
    size_t memtable_size = 64 * 1024;  // 64KB 刷盘
    int32_t flush_interval_ms = 100;   // 100ms 刷盘
};

// 存储引擎入口
class StorageEngine {
public:
    StorageEngine(const StorageConfig& config);
    ~StorageEngine();

    // 初始化（创建目录、加载元数据、恢复 WAL）
    bool Init();

    // 写入数据点
    bool Write(const std::string& tag, const DataPoint& point);
    bool WriteBatch(const std::vector<DataBatch>& batches);

    // 注册测点元数据
    bool RegisterTag(const TagMeta& meta);
    TagMeta GetTagMeta(const std::string& tag_name);

    // 读取原始数据
    std::vector<DataPoint> ReadRaw(const std::string& tag,
                                   const TimeRange& range);

    // 读取聚合数据
    struct AggResult {
        Timestamp bucket_ts;
        double avg = 0;
        double max = 0;
        double min = 0;
        double sum = 0;
        int64_t count = 0;
    };
    std::vector<AggResult> ReadAggregated(const std::string& tag,
                                          const TimeRange& range,
                                          int64_t bucket_ms,
                                          AggType agg_type);

    // 查询最新值
    DataPoint ReadLatest(const std::string& tag);

    // 刷新内存到磁盘
    void Flush();

    // 获取报警引擎
    AlarmEngine* GetAlarmEngine() { return alarm_engine_.get(); }

    // 获取快照存储
    SnapshotStore* GetSnapshotStore() { return snapshot_store_.get(); }

    // ==== 三级命名空间 ====
    // 获取表和测点的存储路径
    std::string GetTablePath(const std::string& db, const std::string& table) const;
    std::string GetTagPath(const std::string& db, const std::string& table,
                           const std::string& tag) const;

    // 检查表/测点是否存在
    bool TableExists(const std::string& db, const std::string& table) const;
    bool TagExists(const std::string& db, const std::string& table,
                   const std::string& tag) const;

    // 删除整表（级联删除所有测点数据）
    bool DropTable(const std::string& db, const std::string& table);

    // 删除单个测点
    bool DropTag(const std::string& db, const std::string& table,
                 const std::string& tag);

    // 关闭
    void Close();

private:
    StorageConfig config_;
    std::unique_ptr<class MemTable> mem_table_;
    std::unique_ptr<WalWriter> wal_;
    std::shared_ptr<LatestCache> latest_cache_;
    std::unique_ptr<AlarmEngine> alarm_engine_;
    std::unique_ptr<SnapshotStore> snapshot_store_;
    bool initialized_ = false;
};

} // namespace minitsdb
