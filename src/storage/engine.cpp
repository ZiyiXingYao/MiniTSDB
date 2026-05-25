#include "storage/engine.h"
#include "storage/memtable.h"
#include "storage/wal.h"
#include "storage/sstable.h"
#include "cache/latest_cache.h"
#include "common/logger.h"
#include "common/os/fs.h"
#include <algorithm>
#include <unordered_map>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace minitsdb {

// 获取当前日期字符串 YYYY-MM-DD
static std::string GetCurrentDateStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

StorageEngine::StorageEngine(const StorageConfig& config)
    : config_(config) {}

StorageEngine::~StorageEngine() {
    Close();
}

bool StorageEngine::Init() {
    // 创建数据目录
    try {
        os::fs::CreateDirectories(config_.hot_path + "/meta");
        os::fs::CreateDirectories(config_.hot_path + "/tags");
        os::fs::CreateDirectories(config_.hot_path + "/wal");
        os::fs::CreateDirectories(config_.cold_path);
    } catch (...) {
        return false;
    }

    // 初始化 MemTable
    mem_table_ = std::make_unique<MemTable>(config_.memtable_size);

    // 设置 flush 回调：MemTable 触发刷盘时写入 SSTable
    mem_table_->SetFlushCallback([this](const std::string& tag,
                                         std::vector<DataPoint>&& points) {
        if (points.empty()) return;
        auto date_str = GetCurrentDateStr();
        std::string tag_dir = config_.hot_path + "/tags/" + tag;
        os::fs::CreateDirectories(tag_dir);
        auto ts_str = std::to_string(points.front().ts);
        auto sstable = std::make_unique<SSTableWriter>(
            tag_dir + "/" + date_str + "_" + ts_str + ".sst");
        if (sstable->Open()) {
            BlockCompressor compressor;
            auto block = compressor.Compress(points);
            sstable->AddBlock(block);
            sstable->Close();
            LOG_DEBUG("Flushed {} points for tag '{}' to SSTable",
                      points.size(), tag);
        }
    });

    // 初始化 WAL
    wal_ = std::make_unique<WalWriter>(config_.hot_path + "/wal/wal.log");
    if (!wal_->Open()) {
        LOG_INFO("WAL not found, creating new at {}/wal/wal.log", config_.hot_path);
    } else {
        LOG_INFO("WAL opened at {}/wal/wal.log", config_.hot_path);
    }

    // WAL 恢复：从 WAL 回放未刷盘的数据（不截断，截断在 Flush 后执行）
    {
        std::string wal_path = config_.hot_path + "/wal/wal.log";
        if (os::fs::Exists(wal_path)) {
            WalReader reader(wal_path);
            if (reader.Open()) {
                const auto& entries = reader.Entries();
                for (const auto& entry : entries) {
                    if (entry.type == WalEntryType::DATA_POINT &&
                        !entry.points.empty()) {
                        for (const auto& p : entry.points) {
                            mem_table_->Add(entry.tag_name, p);
                        }
                    }
                }
                LOG_INFO("WAL recovery: {} entries replayed", entries.size());
                reader.Close();
                // 不在这里 truncate WAL — 延迟到 Flush() 成功后执行
                // 避免 Init → truncate → crash 时丢失恢复的数据
            }
        }
    }

    // 初始化 LatestCache
    latest_cache_ = std::make_shared<LatestCache>();
    LOG_DEBUG("LatestCache initialized");

    // 初始化报警引擎
    alarm_engine_ = std::make_unique<AlarmEngine>();
    LOG_DEBUG("AlarmEngine initialized");

    // 初始化快照存储
    snapshot_store_ = std::make_unique<SnapshotStore>();
    snapshot_store_->Init(config_.hot_path + "/snapshot");
    LOG_DEBUG("SnapshotStore initialized");

    initialized_ = true;
    LOG_INFO("StorageEngine initialized (hot={}, cold={}, retention={}d/{})",
             config_.hot_path, config_.cold_path,
             config_.hot_retention_days, config_.cold_retention_days);
    return true;
}

bool StorageEngine::Write(const std::string& tag, const DataPoint& point) {
    if (!initialized_) return false;

    // 写入 WAL 并立即刷盘确保持久化
    if (wal_) {
        wal_->AppendWrite(tag, point);
        wal_->Flush();
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

    // 更新快照存储
    if (snapshot_store_) {
        snapshot_store_->OnWrite(tag, point);
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
    TagMeta meta;
    meta.name = tag_name;
    meta.description = "";
    meta.unit = "";
    return meta;
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
        if (!os::fs::Exists(tag_dir)) return result;

        std::vector<os::fs::DirEntry> entries;
        if (os::fs::ListDirectory(tag_dir, entries)) {
            for (const auto& entry : entries) {
                if (entry.name.size() < 4 ||
                    entry.name.substr(entry.name.size() - 4) != ".sst") continue;

                SSTableReader reader(entry.path);
                if (!reader.Open()) continue;

                auto points = reader.ReadRange(range);
                result.insert(result.end(), points.begin(), points.end());
                reader.Close();
            }
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
        // 数据已刷入 SSTable 后，截断 WAL
        if (wal_) {
            WalReader::Truncate(wal_->Path());
            LOG_DEBUG("WAL truncated after flush");
        }
        LOG_DEBUG("StorageEngine flush completed");
    }
}

void StorageEngine::Close() {
    if (initialized_) {
        LOG_INFO("StorageEngine shutting down...");
        Flush();

        // 关闭快照存储
        if (snapshot_store_) {
            snapshot_store_->Shutdown();
            LOG_DEBUG("SnapshotStore shut down");
        }

        if (wal_) {
            wal_->Close();
            LOG_DEBUG("WAL closed");
        }
        initialized_ = false;
        LOG_INFO("StorageEngine shut down");
    }
}

} // namespace minitsdb
