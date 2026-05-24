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

    // 更新某个 Tag 的最新值
    void Update(const std::string& tag, const DataPoint& point);

    // 获取某个 Tag 的最新值
    // 如果不存在返回 false
    bool Get(const std::string& tag, DataPoint& out);

    // 批量获取多个 Tag 的最新值
    // LIKE 模式匹配，如 "BOILER-%"
    std::vector<std::pair<std::string, DataPoint>> GetByPattern(
        const std::string& pattern);

    // 获取所有 Tag 的最新值
    std::vector<std::pair<std::string, DataPoint>> GetAll();

    // 删除某个 Tag
    void Remove(const std::string& tag);

    // 清空
    void Clear();

    // 当前缓存的 Tag 数量
    size_t Size() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, DataPoint> cache_;

    bool MatchPattern(const std::string& tag, const std::string& pattern);
};

} // namespace minitsdb
