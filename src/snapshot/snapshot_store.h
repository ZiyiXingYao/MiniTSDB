#pragma once

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <ctime>

namespace minitsdb {

struct CachedSnapshot {
    int64_t timestamp = 0;
    double value = 0.0;
    std::string tag;
    bool valid = false;
};

class SnapshotStore {
public:
    SnapshotStore() = default;
    ~SnapshotStore();

    SnapshotStore(const SnapshotStore&) = delete;
    SnapshotStore& operator=(const SnapshotStore&) = delete;

    bool Init(const std::string& snapshot_dir);
    void Shutdown();
    void SetSaveInterval(int sec) { save_interval_sec_ = sec; }

    // ── 旧 API（兼容） ──
    void OnWrite(const std::string& tag, const DataPoint& point);
    bool Get(const std::string& tag, CachedSnapshot& out);
    std::vector<CachedSnapshot> GetByPattern(const std::string& pattern);
    std::vector<CachedSnapshot> GetAll();

    // ── 新 API（三级命名） ──
    void OnWrite(const std::string& db, const std::string& table,
                 const std::string& tag, const DataPoint& point);
    bool Get(const std::string& db, const std::string& table,
             const std::string& tag, CachedSnapshot& out);
    std::vector<CachedSnapshot> GetByPattern(const std::string& db,
        const std::string& table, const std::string& pattern);
    std::vector<CachedSnapshot> GetAll(const std::string& db,
        const std::string& table);

    size_t Count();

private:
    bool SaveToFile();
    bool LoadFromFile();
    void SaveLoop();

    std::unordered_map<std::string, CachedSnapshot> snapshot_;
    mutable std::shared_mutex mutex_;
    std::string snapshot_path_;
    std::unique_ptr<std::thread> save_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> dirty_{false};
    int save_interval_sec_ = 10;
    std::mutex save_mutex_;
    std::condition_variable save_cv_;

    bool MatchPattern(const std::string& tag, const std::string& pattern);
};

} // namespace minitsdb
