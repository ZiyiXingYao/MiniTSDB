#include "storage/compaction.h"
#include "storage/sstable.h"
#include "common/logger.h"
#include "common/os/fs.h"
#include <algorithm>
#include <set>

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
        if (!os::fs::Exists(tags_path)) return;

        std::vector<os::fs::DirEntry> entries;
        if (os::fs::ListDirectory(tags_path, entries)) {
            for (const auto& tag_dir : entries) {
                if (!tag_dir.is_directory) continue;
                CompactTag(tag_dir.name, tag_dir.path, threshold_bytes);
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Compaction::RunOnce error: {}", e.what());
    }
}

void Compaction::CompactTag(const std::string& tag_name,
                             const std::string& tag_dir,
                             size_t threshold_bytes) {
    // 收集需要合并的小文件
    std::vector<std::string> small_files;
    size_t total_size = 0;

    std::vector<os::fs::DirEntry> sst_entries;
    if (os::fs::ListDirectory(tag_dir, sst_entries)) {
        for (const auto& entry : sst_entries) {
            if (entry.name.size() < 4 ||
                entry.name.substr(entry.name.size() - 4) != ".sst") continue;
            size_t file_size = static_cast<size_t>(entry.file_size);
            if (file_size < threshold_bytes) {
                small_files.push_back(entry.path);
                total_size += file_size;
            }
        }
    }

    // 如果小文件太少，跳过
    if (small_files.size() < 2) return;

    LOG_INFO("Compacting {} SSTables for tag {}", small_files.size(), tag_name);

    // 读取所有小文件的数据（只保留可读取的文件）
    std::vector<std::string> read_files;
    std::vector<DataPoint> all_points;
    for (const auto& f : small_files) {
        SSTableReader reader(f);
        if (!reader.Open()) {
            LOG_WARN("Compaction: skipping corrupt SSTable: {}", f);
            continue;
        }

        TimeRange full_range;
        full_range.start = 0;
        full_range.end = std::numeric_limits<Timestamp>::max();
        auto points = reader.ReadRange(full_range);
        all_points.insert(all_points.end(), points.begin(), points.end());
        reader.Close();
        read_files.push_back(f);
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

    // 压缩为新 SSTable
    BlockCompressor compressor;
    auto block = compressor.Compress(all_points);

    // 先写入临时文件，再原子重命名，防止崩溃产生不完整文件
    std::string tmp_file = tag_dir + "/.tmp_merge_" +
        std::to_string(all_points.front().ts) + "_" +
        std::to_string(all_points.back().ts) + ".sst";
    std::string merged_file = tag_dir + "/merged_" +
        std::to_string(all_points.front().ts) + "_" +
        std::to_string(all_points.back().ts) + ".sst";

    SSTableWriter writer(tmp_file);
    if (!writer.Open()) return;
    writer.AddBlock(block);
    writer.Close();

    // 原子重命名
    if (!os::fs::Rename(tmp_file, merged_file)) {
        os::fs::Remove(tmp_file);
        LOG_WARN("Compaction rename failed");
        return;
    }

    // 删除旧的小文件（仅删除已成功读取的）
    for (const auto& f : read_files) {
        os::fs::Remove(f);
    }

    LOG_INFO("Compacted {} files into {} ({} points)",
             small_files.size(), merged_file, all_points.size());
}

} // namespace minitsdb
