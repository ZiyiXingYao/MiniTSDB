#include "snapshot/snapshot_store.h"
#include "common/os/fs.h"
#include "common/logger.h"
#include "proto_gen/snapshot.pb.h"
#include <fstream>
#include <algorithm>
#include <chrono>

namespace minitsdb {

SnapshotStore::~SnapshotStore() { Shutdown(); }

bool SnapshotStore::Init(const std::string& snapshot_dir) {
    snapshot_path_ = snapshot_dir + "/snapshot.pb";
    os::fs::CreateDirectories(snapshot_dir);

    LoadFromFile();

    running_ = true;
    save_thread_ = std::make_unique<std::thread>(&SnapshotStore::SaveLoop, this);
    LOG_INFO("SnapshotStore initialized at {}", snapshot_path_);
    return true;
}

void SnapshotStore::Shutdown() {
    {
        std::lock_guard lock(save_mutex_);
        running_ = false;
    }
    save_cv_.notify_one();  // 唤醒 SaveLoop 退出
    if (save_thread_ && save_thread_->joinable()) {
        save_thread_->join();
    }
    if (dirty_.exchange(false)) {
        SaveToFile();
        LOG_DEBUG("Snapshot saved on shutdown");
    }
}

void SnapshotStore::SaveLoop() {
    std::unique_lock lock(save_mutex_);
    while (running_) {
        // 等待固定间隔或立即唤醒（OnWrite 通知）
        save_cv_.wait_for(lock, std::chrono::seconds(save_interval_sec_),
                          [this] { return !running_; });
        if (dirty_.exchange(false)) {
            SaveToFile();
        }
    }
}

void SnapshotStore::OnWrite(const std::string& tag, const DataPoint& point) {
    CachedSnapshot entry;
    entry.tag = tag;
    entry.timestamp = point.ts;
    entry.valid = true;

    // DataPoint::value 是 variant<double, int64_t, string>
    if (std::holds_alternative<double>(point.value)) {
        entry.value = std::get<double>(point.value);
    } else if (std::holds_alternative<int64_t>(point.value)) {
        entry.value = static_cast<double>(std::get<int64_t>(point.value));
    }
    // string 类型不设 value（保持 0.0），已标记 valid = true

    {
        std::unique_lock lock(mutex_);
        snapshot_[tag] = entry;
    }
    dirty_.store(true, std::memory_order_relaxed);
    save_cv_.notify_one();  // 通知 SaveLoop 尽快保存
}

bool SnapshotStore::Get(const std::string& tag, CachedSnapshot& out) {
    std::shared_lock lock(mutex_);
    auto it = snapshot_.find(tag);
    if (it == snapshot_.end()) return false;
    out = it->second;
    return true;
}

std::vector<CachedSnapshot> SnapshotStore::GetByPattern(const std::string& pattern) {
    std::vector<CachedSnapshot> result;
    std::shared_lock lock(mutex_);
    for (const auto& [tag, entry] : snapshot_) {
        if (MatchPattern(tag, pattern)) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<CachedSnapshot> SnapshotStore::GetAll() {
    std::vector<CachedSnapshot> result;
    std::shared_lock lock(mutex_);
    result.reserve(snapshot_.size());
    for (const auto& [tag, entry] : snapshot_) {
        result.push_back(entry);
    }
    return result;
}

size_t SnapshotStore::Count() {
    std::shared_lock lock(mutex_);
    return snapshot_.size();
}

bool SnapshotStore::SaveToFile() {
    minitsdb::SnapshotFile pb_file;
    pb_file.set_save_time(static_cast<int64_t>(std::time(nullptr)));
    pb_file.set_version(1);

    {
        std::shared_lock lock(mutex_);
        for (const auto& [tag, entry] : snapshot_) {
            auto* pb_entry = pb_file.add_entries();
            pb_entry->set_tag(tag);
            pb_entry->set_timestamp(entry.timestamp);
            pb_entry->set_value(entry.value);
            pb_entry->set_valid(entry.valid);
        }
    }

    std::ofstream ofs(snapshot_path_, std::ios::binary);
    if (!ofs) {
        LOG_WARN("Failed to open snapshot file for writing: {}", snapshot_path_);
        return false;
    }
    if (!pb_file.SerializeToOstream(&ofs)) {
        LOG_WARN("Failed to serialize snapshot to {}", snapshot_path_);
        return false;
    }
    LOG_DEBUG("Snapshot saved: {} entries", pb_file.entries_size());
    return true;
}

bool SnapshotStore::LoadFromFile() {
    if (!os::fs::Exists(snapshot_path_)) {
        LOG_INFO("No snapshot file found at {}, starting fresh", snapshot_path_);
        return true;
    }

    minitsdb::SnapshotFile pb_file;
    std::ifstream ifs(snapshot_path_, std::ios::binary);
    if (!ifs || !pb_file.ParseFromIstream(&ifs)) {
        LOG_WARN("Failed to parse snapshot file: {}", snapshot_path_);
        return false;
    }

    {
        std::unique_lock lock(mutex_);
        snapshot_.clear();
        for (int i = 0; i < pb_file.entries_size(); i++) {
            const auto& pb_entry = pb_file.entries(i);
            CachedSnapshot entry;
            entry.tag = pb_entry.tag();
            entry.timestamp = pb_entry.timestamp();
            entry.value = pb_entry.value();
            entry.valid = pb_entry.valid();
            snapshot_[entry.tag] = entry;
        }
    }
    LOG_INFO("Snapshot loaded: {} entries from {}", snapshot_.size(), snapshot_path_);
    return true;
}

bool SnapshotStore::MatchPattern(const std::string& tag, const std::string& pattern) {
    size_t pi = 0, ti = 0;
    while (pi < pattern.size() && ti < tag.size()) {
        if (pattern[pi] == '%') {
            pi++;
            if (pi == pattern.size()) return true;
            while (ti < tag.size() && tag[ti] != pattern[pi]) ti++;
            if (ti >= tag.size()) return false;
        } else if (pattern[pi] == '_') {
            pi++;
            ti++;
        } else if (pattern[pi] == tag[ti]) {
            pi++;
            ti++;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '%') pi++;
    return pi == pattern.size() && ti == tag.size();
}

} // namespace minitsdb
