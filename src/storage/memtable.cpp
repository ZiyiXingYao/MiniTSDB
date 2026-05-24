#include "storage/memtable.h"

namespace minitsdb {

MemTable::MemTable(size_t flush_threshold_bytes)
    : flush_threshold_(flush_threshold_bytes) {}

void MemTable::Add(const std::string& tag, const DataPoint& point) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = buffers_.find(tag);
    if (it == buffers_.end()) {
        it = buffers_.emplace(tag, TagBuffer{}).first;
    }

    auto& buf = it->second;
    buf.points.push_back(point);
    buf.estimated_bytes += sizeof(DataPoint) + tag.size();

    CheckFlush(tag, buf);
}

void MemTable::AddBatch(const std::vector<DataBatch>& batches) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (const auto& batch : batches) {
        for (const auto& point : batch.points) {
            auto it = buffers_.find(batch.tag_name);
            if (it == buffers_.end()) {
                it = buffers_.emplace(batch.tag_name, TagBuffer{}).first;
            }
            auto& buf = it->second;
            buf.points.push_back(point);
            buf.estimated_bytes += sizeof(DataPoint) + batch.tag_name.size();
            CheckFlush(batch.tag_name, buf);
        }
    }
}

void MemTable::FlushAll() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [tag, buf] : buffers_) {
        FlushBuffer(tag, buf);
    }
}

void MemTable::FlushTag(const std::string& tag) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = buffers_.find(tag);
    if (it != buffers_.end()) {
        FlushBuffer(it->first, it->second);
    }
}

void MemTable::GetAllData(
    std::vector<std::pair<std::string, std::vector<DataPoint>>>& out) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    out.clear();
    for (auto& [tag, buf] : buffers_) {
        out.emplace_back(tag, buf.points);
    }
}

size_t MemTable::Size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& [tag, buf] : buffers_) {
        total += buf.estimated_bytes;
    }
    return total;
}

size_t MemTable::TagCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return buffers_.size();
}

void MemTable::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
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
