#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace minitsdb {

// MemTable 刷新回调
// 参数: tag_name, 待刷新的数据点列表
using FlushCallback = std::function<void(const std::string&,
                                          std::vector<DataPoint>&&)>;

// 内存写入缓冲
// 按 Tag 分组存储近期写入的数据点，达到阈值自动触发刷新
class MemTable {
public:
    explicit MemTable(size_t flush_threshold_bytes = 64 * 1024);

    // 写入一个数据点
    void Add(const std::string& tag, const DataPoint& point);

    // 批量写入
    void AddBatch(const std::vector<DataBatch>& batches);

    // 设置刷新回调
    void SetFlushCallback(FlushCallback cb) { flush_cb_ = std::move(cb); }

    // 强制刷新所有 Tag 的数据
    void FlushAll();

    // 强制刷新指定 Tag
    void FlushTag(const std::string& tag);

    // 获取所有 Tag 的最新数据（用于热点查询）
    void GetAllData(std::vector<std::pair<std::string, std::vector<DataPoint>>>& out);

    // 获取当前大小（字节）
    size_t Size() const;

    // 获取 Tag 数量
    size_t TagCount() const;

    // 清空
    void Clear();

private:
    struct TagBuffer {
        std::vector<DataPoint> points;
        size_t estimated_bytes = 0;
    };

    size_t flush_threshold_;
    FlushCallback flush_cb_;
    std::unordered_map<std::string, TagBuffer> buffers_;

    void CheckFlush(const std::string& tag, TagBuffer& buf);
    void FlushBuffer(const std::string& tag, TagBuffer& buf);
};

} // namespace minitsdb
