#include "storage/engine.h"
#include "storage/memtable.h"
#include "storage/wal.h"
#include "storage/sstable.h"
#include "cache/latest_cache.h"
#include "common/logger.h"
#include <filesystem>
#include <algorithm>
#include <unordered_map>

namespace minitsdb {

StorageEngine::StorageEngine(const StorageConfig& config)
    : config_(config) {}

StorageEngine::~StorageEngine() {
    Close();
}

bool StorageEngine::Init() {
    // 创建数据目录
    try {
        std::filesystem::create_directories(config_.hot_path + "/meta");
        std::filesystem::create_directories(config_.hot_path + "/tags");
        std::filesystem::create_directories(config_.cold_path);
    } catch (...) {
        return false;
    }

    // 初始化 MemTable
    mem_table_ = std::make_unique<MemTable>(config_.memtable_size);

    // 初始化 WAL
    wal_ = std::make_unique<WalWriter>(config_.hot_path + "/wal/wal.log");
    if (!wal_->Open()) {
        LOG_INFO("WAL not found, creating new at {}/wal/wal.log", config_.hot_path);
    } else {
        LOG_INFO("WAL opened at {}/wal/wal.log", config_.hot_path);
    }

    // 初始化 LatestCache
    latest_cache_ = std::make_shared<LatestCache>();
    LOG_DEBUG("LatestCache initialized");

    // 初始化报警引擎
    alarm_engine_ = std::make_unique<AlarmEngine>();
    LOG_DEBUG("AlarmEngine initialized");

    initialized_ = true;
    LOG_INFO("StorageEngine initialized (hot={}, cold={}, retention={}d/{})",
             config_.hot_path, config_.cold_path,
             config_.hot_retention_days, config_.cold_retention_days);
    return true;
}

bool StorageEngine::Write(const std::string& tag, const DataPoint& point) {
    if (!initialized_) return false;

    // 写入 WAL
    if (wal_) {
        wal_->AppendWrite(tag, point);
    }

    // 写入 MemTable
    if (mem_table_) {
        mem_table_->Add(tag, point);
    }

    // 更新缓存
    if (latest_cache_) {
        latest_cache_->Update(tag, point);
    }

    // 报警检查
    if (alarm_engine_) {
        alarm_engine_->Evaluate(tag, point);
    }

    LOG_DEBUG("Written tag={} ts={} value={}", tag, point.ts,
              std::holds_alternative<double>(point.value) ?
              std::to_string(std::get<double>(point.value)) : "string");
    return true;
}

bool StorageEngine::WriteBatch(const std::vector<DataBatch>& batches) {
    if (!initialized_) return false;

    for (const auto& batch : batches) {
        for (const auto& point : batch.points) {
            if (!Write(batch.tag_name, point)) return false;
        }
    }
    LOG_DEBUG("Batch write done: {} batches", batches.size());
    return true;
}

bool StorageEngine::RegisterTag(const TagMeta& meta) {
    if (!initialized_) return false;
    // 简化实现：仅存到内存
    return true;
}

TagMeta StorageEngine::GetTagMeta(const std::string& tag_name) {
    return TagMeta{tag_name};
}

std::vector<DataPoint> StorageEngine::ReadRaw(const std::string& tag,
                                                const TimeRange& range) {
    std::vector<DataPoint> result;

    // 先从 LatestCache 读取
    if (latest_cache_ && !tag.empty()) {
        DataPoint p;
        if (latest_cache_->Get(tag, p) && range.Contains(p.ts)) {
            result.push_back(p);
        }
    }

    // 从 SSTable 文件读取
    try {
        std::string tag_dir = config_.hot_path + "/tags/" + tag;
        if (!std::filesystem::exists(tag_dir)) return result;

        for (const auto& entry : std::filesystem::directory_iterator(tag_dir)) {
            if (entry.path().extension() != ".sst") continue;

            SSTableReader reader(entry.path().string());
            if (!reader.Open()) continue;

            auto points = reader.ReadRange(range);
            result.insert(result.end(), points.begin(), points.end());
            reader.Close();
        }
    } catch (const std::exception& e) {
        LOG_WARN("ReadRaw error for tag '{}': {}", tag, e.what());
    }

    // 去重（缓存可能有重复）
    std::sort(result.begin(), result.end(),
              [](const DataPoint& a, const DataPoint& b) { return a.ts < b.ts; });
    auto last = std::unique(result.begin(), result.end(),
                            [](const DataPoint& a, const DataPoint& b) {
                                return a.ts == b.ts;
                            });
    result.erase(last, result.end());

    return result;
}

std::vector<StorageEngine::AggResult> StorageEngine::ReadAggregated(
    const std::string& tag, const TimeRange& range,
    int64_t bucket_ms, AggType agg_type) {
    std::vector<AggResult> results;
    auto raw = ReadRaw(tag, range);
    if (raw.empty()) return results;

    // 按 bucket 分组聚合
    std::unordered_map<int64_t, AggResult> buckets;

    for (const auto& p : raw) {
        double val = std::holds_alternative<double>(p.value)
            ? std::get<double>(p.value) : 0.0;

        int64_t bucket_ts = (p.ts / bucket_ms) * bucket_ms;

        auto& b = buckets[bucket_ts];
        b.bucket_ts = bucket_ts;
        b.count++;
        b.sum += val;
        if (b.count == 1 || val > b.max) b.max = val;
        if (b.count == 1 || val < b.min) b.min = val;
    }

    for (auto& [ts, b] : buckets) {
        b.avg = b.sum / b.count;
        results.push_back(b);
    }

    std::sort(results.begin(), results.end(),
              [](const AggResult& a, const AggResult& b) {
                  return a.bucket_ts < b.bucket_ts;
              });

    LOG_DEBUG("ReadAggregated: {} buckets from {} raw points", results.size(), raw.size());
    return results;
}

DataPoint StorageEngine::ReadLatest(const std::string& tag) {
    DataPoint p;
    if (latest_cache_) {
        latest_cache_->Get(tag, p);
    }
    return p;
}

void StorageEngine::Flush() {
    if (mem_table_) {
        mem_table_->FlushAll();
        LOG_DEBUG("StorageEngine flush completed");
    }
}

void StorageEngine::Close() {
    if (initialized_) {
        LOG_INFO("StorageEngine shutting down...");
        Flush();
        if (wal_) {
            wal_->Close();
            LOG_DEBUG("WAL closed");
        }
        initialized_ = false;
        LOG_INFO("StorageEngine shut down");
    }
}

} // namespace minitsdb
