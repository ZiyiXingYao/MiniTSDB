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

} // namespace minitsdb
