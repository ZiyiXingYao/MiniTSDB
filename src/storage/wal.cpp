#include "storage/wal.h"
#include <cstring>
#include <sstream>
#include <mutex>

namespace minitsdb {

// 简易 CRC32 实现
namespace {

uint32_t crc32_table[256];
std::once_flag crc32_flag;

void InitCrc32() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

uint32_t CalculateCrc32(const void* data, size_t len) {
    std::call_once(crc32_flag, InitCrc32);
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

}  // namespace

// ============================================================
//  WalWriter
// ============================================================
WalWriter::WalWriter(const std::string& path) : path_(path) {}

WalWriter::~WalWriter() {
    Close();
}

bool WalWriter::Open() {
    if (!file_.Open(path_, os::FileMode::APPEND)) return false;

    // 获取当前文件大小
    size_ = static_cast<size_t>(file_.Size());
    opened_ = true;
    return true;
}

bool WalWriter::WriteRaw(const void* data, size_t len) {
    if (!opened_) return false;
    if (!file_.Write(data, len)) return false;
    size_ += len;
    return true;
}

bool WalWriter::AppendWrite(const std::string& tag, const DataPoint& point) {
    // 序列化 tag 名: uint16 length + data
    uint16_t tag_len = static_cast<uint16_t>(tag.size());

    // 构建条目数据
    std::vector<uint8_t> data;
    data.resize(sizeof(tag_len) + tag_len + sizeof(Timestamp) + sizeof(double));
    size_t offset = 0;

    std::memcpy(data.data() + offset, &tag_len, sizeof(tag_len));
    offset += sizeof(tag_len);
    std::memcpy(data.data() + offset, tag.data(), tag_len);
    offset += tag_len;
    std::memcpy(data.data() + offset, &point.ts, sizeof(Timestamp));
    offset += sizeof(Timestamp);

    double val = std::get<double>(point.value);
    std::memcpy(data.data() + offset, &val, sizeof(double));
    offset += sizeof(double);

    // 条目头
    WalEntryHeader header;
    header.type = WalEntryType::DATA_POINT;
    header.data_len = static_cast<uint32_t>(data.size());
    header.crc = CalculateCrc32(data.data(), data.size());

    // 写入头 + 数据
    if (!WriteRaw(&header, sizeof(header))) return false;
    if (!WriteRaw(data.data(), data.size())) return false;

    return true;
}

bool WalWriter::AppendBatch(const std::vector<DataBatch>& batches) {
    for (const auto& batch : batches) {
        for (const auto& point : batch.points) {
            if (!AppendWrite(batch.tag_name, point)) return false;
        }
    }
    return true;
}

bool WalWriter::WriteCheckpoint() {
    WalEntryHeader header;
    header.type = WalEntryType::CHECKPOINT;
    header.data_len = 0;
    header.crc = 0;

    if (!WriteRaw(&header, sizeof(header))) return false;
    if (!Flush()) return false;
    return true;
}

bool WalWriter::Flush() {
    if (!opened_) return false;
    return file_.Flush();  // OS-level flush: FlushFileBuffers / fsync
}

void WalWriter::Close() {
    if (opened_) {
        file_.Close();
        opened_ = false;
    }
}

size_t WalWriter::Size() const {
    return size_;
}

uint32_t WalWriter::CalculateCrc(const void* data, size_t len) {
    return CalculateCrc32(data, len);
}

// ============================================================
//  WalReader
// ============================================================
WalReader::WalReader(const std::string& path) : path_(path) {}

bool WalReader::Open() {
    os::File file;
    if (!file.Open(path_, os::FileMode::READ)) return false;

    entries_.clear();
    int64_t file_size = file.Size();
    while (file.Tell() < file_size) {
        if (!ReadEntry(file)) {
            // 遇到损坏条目，停止读取
            break;
        }
    }

    return true;
}

bool WalReader::ReadEntry(os::File& file) {
    WalEntryHeader header;
    size_t bytes_read = 0;
    if (!file.Read(&header, sizeof(header), &bytes_read) ||
        bytes_read != sizeof(header)) return false;

    if (header.type == WalEntryType::CHECKPOINT) {
        Entry entry;
        entry.type = WalEntryType::CHECKPOINT;
        entries_.push_back(entry);
        return true;
    }

    if (header.type != WalEntryType::DATA_POINT) return false;

    // 读取数据
    std::vector<uint8_t> data(header.data_len);
    size_t data_read = 0;
    if (!file.Read(data.data(), header.data_len, &data_read) ||
        data_read != header.data_len) return false;

    // 校验 CRC
    uint32_t expected_crc = CalculateCrc32(data.data(), data.size());
    if (expected_crc != header.crc) return false;

    // 解析数据
    Entry entry;
    entry.type = WalEntryType::DATA_POINT;

    size_t offset = 0;
    uint16_t tag_len;
    std::memcpy(&tag_len, data.data() + offset, sizeof(tag_len));
    offset += sizeof(tag_len);

    entry.tag_name.assign(reinterpret_cast<const char*>(data.data() + offset), tag_len);
    offset += tag_len;

    DataPoint point;
    std::memcpy(&point.ts, data.data() + offset, sizeof(Timestamp));
    offset += sizeof(Timestamp);

    double val;
    std::memcpy(&val, data.data() + offset, sizeof(double));
    point.value = val;

    entry.points.push_back(point);
    entries_.push_back(entry);

    return true;
}

void WalReader::Close() {
    entries_.clear();
}

bool WalReader::Truncate(const std::string& path) {
    os::fs::Remove(path);
    return true;  // 文件不存在也算成功
}

}  // namespace minitsdb
