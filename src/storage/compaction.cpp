#include "storage/compaction.h"
#include "storage/sstable.h"
#include "common/logger.h"
#include <filesystem>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace minitsdb {

Compaction::Compaction(const std::string& hot_path) : hot_path_(hot_path) {}

void Compaction::Start(int32_t interval_sec) {
    if (running_.exchange(true)) return;
    worker_thread_ = std::thread(&Compaction::WorkerLoop, this, interval_sec);
}

void Compaction::Stop() {
    if (running_.exchange(false)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

void Compaction::WorkerLoop(int32_t interval_sec) {
    while (running_) {
        RunOnce();
        for (int i = 0; i < interval_sec && running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void Compaction::RunOnce(size_t threshold_bytes) {
    try {
        std::string tags_path = hot_path_ + "/tags";
        if (!fs::exists(tags_path)) return;

        for (const auto& tag_dir : fs::directory_iterator(tags_path)) {
            if (!tag_dir.is_directory()) continue;
            CompactTag(tag_dir.path().filename().string(),
                       tag_dir.path().string(), threshold_bytes);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Compaction::RunOnce error: {}", e.what());
    }
}

void Compaction::CompactTag(const std::string& tag_name,
                             const std::string& tag_dir,
                             size_t threshold_bytes) {
    // 收集需要合并的小文件
    std::vector<fs::path> small_files;
    size_t total_size = 0;

    for (const auto& entry : fs::directory_iterator(tag_dir)) {
        if (entry.path().extension() != ".sst") continue;
        size_t file_size = static_cast<size_t>(entry.file_size());
        if (file_size < threshold_bytes) {
            small_files.push_back(entry.path());
            total_size += file_size;
        }
    }

    // 如果小文件太少，跳过
    if (small_files.size() < 2) return;

    LOG_INFO("Compacting {} SSTables for tag {}", small_files.size(), tag_name);

    // 读取所有小文件的数据
    std::vector<DataPoint> all_points;
    for (const auto& f : small_files) {
        SSTableReader reader(f.string());
        if (!reader.Open()) continue;

        TimeRange full_range;
        full_range.start = 0;
        full_range.end = std::numeric_limits<Timestamp>::max();
        auto points = reader.ReadRange(full_range);
        all_points.insert(all_points.end(), points.begin(), points.end());
        reader.Close();
    }

    if (all_points.empty()) return;

    // 按时间戳排序并去重
    std::sort(all_points.begin(), all_points.end(),
              [](const DataPoint& a, const DataPoint& b) {
                  return a.ts < b.ts;
              });

    auto last = std::unique(all_points.begin(), all_points.end(),
                            [](const DataPoint& a, const DataPoint& b) {
                                return a.ts == b.ts;
                            });
    all_points.erase(last, all_points.end());

    // 压缩为新的 SSTable
    BlockCompressor compressor;
    auto block = compressor.Compress(all_points);

    std::string merged_file = tag_dir + "/merged_" +
        std::to_string(all_points.front().ts) + "_" +
        std::to_string(all_points.back().ts) + ".sst";

    SSTableWriter writer(merged_file);
    if (!writer.Open()) return;
    writer.AddBlock(block);
    writer.Close();

    // 删除旧的小文件
    for (const auto& f : small_files) {
        fs::remove(f);
    }

    LOG_INFO("Compacted {} files into {} ({} points)",
             small_files.size(), merged_file, all_points.size());
}

} // namespace minitsdb
