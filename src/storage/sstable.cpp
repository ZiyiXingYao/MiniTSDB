#include "storage/sstable.h"
#include "storage/compressor.h"
#include <cstring>
#include <algorithm>

namespace minitsdb {

// SSTable 文件格式（固定 Header 大小 32 字节）:
// [Magic: 8 bytes] "MINITSDB"
// [Version: 4 bytes]
// [Tag name: 18 bytes] 固定宽度
// [Block count: 4 bytes]  --> 第 30 字节
// [Reserved: 2 bytes]
// --- 之后是 Block 数据 ---

// ============================================================
//  SSTableWriter
// ============================================================
SSTableWriter::SSTableWriter(const std::string& filepath)
    : filepath_(filepath) {}

SSTableWriter::~SSTableWriter() {
    if (opened_) Close();
}

bool SSTableWriter::Open() {
    file_.open(filepath_, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) return false;

    // 写入完整的 32 字节 Header 占位
    uint8_t header[32] = {0};
    // Magic
    std::memcpy(header, "MINITSDB", 8);
    // Version = 1
    uint32_t ver = 1;
    std::memcpy(header + 8, &ver, 4);
    // Tag name 空
    // Block count 占位（后续更新）
    // 写入
    file_.write(reinterpret_cast<const char*>(header), 32);
    file_size_ = 32;
    opened_ = true;
    return true;
}

void SSTableWriter::AddBlock(const CompressedBlock& block) {
    if (!opened_) return;

    block_offsets_.push_back(static_cast<size_t>(file_.tellp()));

    // 写入块数据
    file_.write(reinterpret_cast<const char*>(&block.range.start), sizeof(Timestamp));
    file_.write(reinterpret_cast<const char*>(&block.range.end), sizeof(Timestamp));

    uint32_t ts_len = static_cast<uint32_t>(block.timestamps.size());
    uint32_t val_len = static_cast<uint32_t>(block.values.size());
    file_.write(reinterpret_cast<const char*>(&ts_len), sizeof(ts_len));
    file_.write(reinterpret_cast<const char*>(block.timestamps.data()), ts_len);
    file_.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
    file_.write(reinterpret_cast<const char*>(block.values.data()), val_len);

    if (range_.start == 0 || block.range.start < range_.start)
        range_.start = block.range.start;
    if (block.range.end > range_.end)
        range_.end = block.range.end;

    block_count_++;
}

void SSTableWriter::Close() {
    if (!opened_) return;

    // 更新 Header 中的 Block count
    file_.seekp(16, std::ios::beg);  // Magic(8) + Version(4) + Tag(4 padding)
    // Tag name 在偏移 12，用 4 字节 tag_len + 0 字节数据
    uint32_t tag_len = 0;
    file_.write(reinterpret_cast<const char*>(&tag_len), sizeof(tag_len));
    // Block count
    file_.write(reinterpret_cast<const char*>(&block_count_), sizeof(block_count_));

    file_size_ = static_cast<size_t>(file_.tellp());
    file_.close();
    opened_ = false;
}

uint32_t SSTableWriter::CalculateFileCrc() {
    return 0;
}

// ============================================================
//  SSTableReader
// ============================================================
SSTableReader::SSTableReader(const std::string& filepath)
    : filepath_(filepath) {}

SSTableReader::~SSTableReader() {
    Close();
}

bool SSTableReader::Open() {
    file_.open(filepath_, std::ios::binary);
    if (!file_.is_open()) return false;

    if (!ReadHeader()) return false;
    if (!ReadBlockIndex()) return false;

    opened_ = true;
    return true;
}

void SSTableReader::Close() {
    if (opened_) {
        file_.close();
        opened_ = false;
    }
}

bool SSTableReader::ReadHeader() {
    char magic[9] = {0};
    file_.read(magic, 8);
    if (std::strncmp(magic, "MINITSDB", 8) != 0) return false;

    uint32_t version;
    file_.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) return false;

    // 跳到偏移 16 读取 tag_len 和 block_count
    file_.seekg(16);
    uint32_t tag_len;
    file_.read(reinterpret_cast<char*>(&tag_len), sizeof(tag_len));
    if (tag_len > 0) {
        std::vector<char> buf(tag_len);
        file_.read(buf.data(), tag_len);
        tag_name_.assign(buf.data(), tag_len);
    }

    uint32_t block_count;
    file_.read(reinterpret_cast<char*>(&block_count), sizeof(block_count));
    block_count_ = block_count;

    // Block 数据从偏移 32 开始（固定 Header 大小）
    file_.seekg(32);
    return true;  // allow empty SSTable
}

bool SSTableReader::ReadBlockIndex() {
    // Block 数据从 header_size 开始
    size_t header_size = static_cast<size_t>(file_.tellg());

    for (uint32_t i = 0; i < block_count_; i++) {
        BlockIndex idx;
        idx.file_offset = header_size;

        file_.read(reinterpret_cast<char*>(&idx.range.start), sizeof(Timestamp));
        file_.read(reinterpret_cast<char*>(&idx.range.end), sizeof(Timestamp));

        uint32_t ts_len, val_len;
        file_.read(reinterpret_cast<char*>(&ts_len), sizeof(ts_len));
        idx.ts_comp_size = ts_len;
        file_.seekg(ts_len, std::ios::cur);
        file_.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        idx.val_comp_size = val_len;
        file_.seekg(val_len, std::ios::cur);

        blocks_.push_back(idx);
        header_size = static_cast<size_t>(file_.tellg());

        // 更新时间范围
        if (range_.start == 0 || idx.range.start < range_.start)
            range_.start = idx.range.start;
        if (idx.range.end > range_.end)
            range_.end = idx.range.end;
    }

    return true;
}

std::vector<DataPoint> SSTableReader::ReadRange(const TimeRange& range) {
    if (!opened_) return {};

    BlockCompressor decompressor;
    std::vector<DataPoint> result;

    for (const auto& idx : blocks_) {
        if (!idx.range.Overlaps(range)) continue;

        file_.seekg(idx.file_offset);

        // 跳过 block header 到压缩数据
        Timestamp tmp;
        file_.read(reinterpret_cast<char*>(&tmp), sizeof(Timestamp));
        file_.read(reinterpret_cast<char*>(&tmp), sizeof(Timestamp));

        uint32_t ts_len, val_len;
        file_.read(reinterpret_cast<char*>(&ts_len), sizeof(ts_len));

        CompressedBlock block;
        block.timestamps.resize(ts_len);
        file_.read(reinterpret_cast<char*>(block.timestamps.data()), ts_len);

        file_.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        block.values.resize(val_len);
        file_.read(reinterpret_cast<char*>(block.values.data()), val_len);

        auto points = decompressor.Decompress(block);
        for (auto& p : points) {
            if (p.ts >= range.start && p.ts <= range.end) {
                result.push_back(std::move(p));
            }
        }
    }

    return result;
}

} // namespace minitsdb
