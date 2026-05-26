#include "cache/latest_cache.h"

namespace minitsdb {

void LatestCache::Update(const std::string& tag, const DataPoint& point) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(tag);
    if (it != cache_.end()) {
        if (point.ts >= it->second.ts) {
            it->second = point;
        }
    } else {
        cache_[tag] = point;
    }
}

bool LatestCache::Get(const std::string& tag, DataPoint& out) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(tag);
    if (it != cache_.end()) {
        out = it->second;
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, DataPoint>> LatestCache::GetByPattern(
    const std::string& pattern) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<std::string, DataPoint>> result;
    for (const auto& [tag, point] : cache_) {
        if (MatchPattern(tag, pattern)) {
            result.emplace_back(tag, point);
        }
    }
    return result;
}

std::vector<std::pair<std::string, DataPoint>> LatestCache::GetAll() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<std::string, DataPoint>> result;
    result.reserve(cache_.size());
    for (const auto& pair : cache_) {
        result.push_back(pair);
    }
    return result;
}

void LatestCache::Remove(const std::string& tag) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.erase(tag);
}

void LatestCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
}

size_t LatestCache::Size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.size();
}

bool LatestCache::MatchPattern(const std::string& tag,
                                const std::string& pattern) {
    size_t ti = 0, pi = 0;
    while (ti < tag.size() && pi < pattern.size()) {
        if (pattern[pi] == '%') {
            // % 匹配任意字符序列
            pi++;
            if (pi >= pattern.size()) return true;
            while (ti < tag.size()) {
                if (MatchPattern(tag.substr(ti), pattern.substr(pi)))
                    return true;
                ti++;
            }
            return false;
        } else if (pattern[pi] == '_') {
            // _ 匹配单个任意字符
            pi++;
            ti++;
        } else if (pattern[pi] == tag[ti]) {
            pi++;
            ti++;
        } else {
            return false;
        }
    }
    return ti >= tag.size() && pi >= pattern.size();
}

// ── 新 API（三级命名） ──

std::string LatestCache::MakeKey(const std::string& db,
                                  const std::string& table,
                                  const std::string& tag) const {
    return db + ":" + table + ":" + tag;
}

void LatestCache::Update(const std::string& db, const std::string& table,
                          const std::string& tag, const DataPoint& point) {
    auto key = MakeKey(db, table, tag);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        if (point.ts >= it->second.ts) it->second = point;
    } else {
        cache_[key] = point;
    }
}

bool LatestCache::Get(const std::string& db, const std::string& table,
                       const std::string& tag, DataPoint& out) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = cache_.find(MakeKey(db, table, tag));
    if (it != cache_.end()) { out = it->second; return true; }
    return false;
}

std::vector<std::pair<std::string, DataPoint>> LatestCache::GetByPattern(
    const std::string& db, const std::string& table, const std::string& pattern) {
    std::string prefix = db + ":" + table + ":";
    std::string full_pattern = prefix + pattern;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<std::string, DataPoint>> result;
    for (const auto& [key, point] : cache_) {
        if (key.compare(0, prefix.size(), prefix) == 0 &&
            MatchPattern(key, full_pattern)) {
            result.emplace_back(key, point);
        }
    }
    return result;
}

std::vector<std::pair<std::string, DataPoint>> LatestCache::GetAll(
    const std::string& db, const std::string& table) {
    std::string prefix = db + ":" + table + ":";
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<std::string, DataPoint>> result;
    for (const auto& [key, point] : cache_) {
        if (key.compare(0, prefix.size(), prefix) == 0) {
            result.emplace_back(key, point);
        }
    }
    return result;
}

void LatestCache::Remove(const std::string& db, const std::string& table,
                          const std::string& tag) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.erase(MakeKey(db, table, tag));
}

void LatestCache::RemoveByPrefix(const std::string& prefix) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            it = cache_.erase(it);
        } else { ++it; }
    }
}

} // namespace minitsdb
