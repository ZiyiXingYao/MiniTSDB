#include "storage/memtable.h"

namespace minitsdb {

MemTable::MemTable(size_t flush_threshold_bytes)
    : flush_threshold_(flush_threshold_bytes) {}

void MemTable::Add(const std::string& tag, const DataPoint& point) {
    auto it = buffers_.find(tag);
    if (it == buffers_.end()) {
        it = buffers_.emplace(tag, TagBuffer{}).first;
    }

    auto& buf = it->second;
    buf.points.push_back(point);
    // 估算大小：时间戳(8) + 值(8) + tag引用 + 向量开销
    buf.estimated_bytes += sizeof(Timestamp) + 8;

    CheckFlush(tag, buf);
}

void MemTable::AddBatch(const std::vector<DataBatch>& batches) {
    for (const auto& batch : batches) {
        for (const auto& point : batch.points) {
            Add(batch.tag_name, point);
        }
    }
}

void MemTable::FlushAll() {
    for (auto& [tag, buf] : buffers_) {
        FlushBuffer(tag, buf);
    }
}

void MemTable::FlushTag(const std::string& tag) {
    auto it = buffers_.find(tag);
    if (it != buffers_.end()) {
        FlushBuffer(it->first, it->second);
    }
}

void MemTable::GetAllData(
    std::vector<std::pair<std::string, std::vector<DataPoint>>>& out) {
    out.clear();
    for (auto& [tag, buf] : buffers_) {
        out.emplace_back(tag, buf.points);
    }
}

size_t MemTable::Size() const {
    size_t total = 0;
    for (const auto& [tag, buf] : buffers_) {
        total += buf.estimated_bytes;
    }
    return total;
}

size_t MemTable::TagCount() const {
    return buffers_.size();
}

void MemTable::Clear() {
    buffers_.clear();
}

void MemTable::CheckFlush(const std::string& tag, TagBuffer& buf) {
    if (buf.estimated_bytes >= flush_threshold_ && flush_cb_) {
        FlushBuffer(tag, buf);
    }
}

void MemTable::FlushBuffer(const std::string& tag, TagBuffer& buf) {
    if (buf.points.empty()) return;

    if (flush_cb_) {
        flush_cb_(tag, std::move(buf.points));
    }

    buf.points.clear();
    buf.estimated_bytes = 0;
}

} // namespace minitsdb
