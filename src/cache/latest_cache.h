#pragma once

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <vector>

namespace minitsdb {

// 最新值缓存
// 每次写入时自动更新，用于大屏实时刷新和 LATEST 查询
class LatestCache {
public:
    LatestCache() = default;

    // ── 旧 API（兼容过渡期，内部调用新 API） ──
    void Update(const std::string& tag, const DataPoint& point);
    bool Get(const std::string& tag, DataPoint& out);
    std::vector<std::pair<std::string, DataPoint>> GetByPattern(const std::string& pattern);
    std::vector<std::pair<std::string, DataPoint>> GetAll();
    void Remove(const std::string& tag);

    // ── 新 API（三级命名） ──
    void Update(const std::string& db, const std::string& table,
                const std::string& tag, const DataPoint& point);
    bool Get(const std::string& db, const std::string& table,
             const std::string& tag, DataPoint& out);
    std::vector<std::pair<std::string, DataPoint>> GetByPattern(
        const std::string& db, const std::string& table, const std::string& pattern);
    std::vector<std::pair<std::string, DataPoint>> GetAll(
        const std::string& db, const std::string& table);
    void Remove(const std::string& db, const std::string& table,
                const std::string& tag);
    void RemoveByPrefix(const std::string& prefix);

    void Clear();
    size_t Size() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, DataPoint> cache_;

    bool MatchPattern(const std::string& tag, const std::string& pattern);
    std::string MakeKey(const std::string& db, const std::string& table,
                        const std::string& tag) const;
};

} // namespace minitsdb
