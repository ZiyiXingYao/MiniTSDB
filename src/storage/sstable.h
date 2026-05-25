#pragma once

#include "common/types.h"
#include "storage/compressor.h"
#include "common/os/file.h"
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace minitsdb {

// SSTable 文件格式 (v2, CRC enabled):
// [Magic: 8 bytes] "MINITSDB"
// [Version: 4 bytes] uint32 (2 = CRC enabled)
// [Tag name len: 2 bytes] uint16
// [Tag name: N bytes]
// [Block count: 4 bytes] uint32
// [Blocks...]
//   [Min ts: 8 bytes]
//   [Max ts: 8 bytes]
//   [Ts len: 4 bytes]
//   [Timestamp data: TsLen bytes]
//   [Val len: 4 bytes]
//   [Value data: ValLen bytes]
// [CRC32: 4 bytes] 文件尾校验

constexpr uint64_t SSTABLE_MAGIC = 0x4D494E4954534442;  // "MINITSDB"
constexpr uint32_t SSTABLE_VERSION_CRC = 2;               // 带 CRC 校验的版本

// 计算 CRC-32（标准多项式 0xEDB88320）
uint32_t Crc32(const uint8_t* data, size_t len);

// SSTable 单块的描述信息（内存索引）
struct BlockIndex {
    TimeRange range;
    size_t file_offset;  // 块在文件中的偏移
    size_t ts_comp_size;
    size_t val_comp_size;
};

// SSTable 写入器
class SSTableWriter {
public:
    explicit SSTableWriter(const std::string& filepath);
    ~SSTableWriter();

    bool Open();
    void AddBlock(const CompressedBlock& block);
    void Close();
    size_t FileSize() const { return file_size_; }

private:
    std::string filepath_;
    os::File file_;
    size_t file_size_ = 0;
    size_t data_end_ = 0;    // 实际数据尾部（不含 CRC）
    size_t block_count_ = 0;
    std::vector<size_t> block_offsets_;
    bool opened_ = false;
    std::string tag_name_;
    TimeRange range_{INT64_MAX, 0};
};

// SSTable 读取器
class SSTableReader {
public:
    explicit SSTableReader(const std::string& filepath);
    ~SSTableReader();

    bool Open();
    void Close();

    // 获取覆盖的时间范围
    TimeRange GetTimeRange() const { return range_; }
    const std::string& TagName() const { return tag_name_; }
    size_t BlockCount() const { return blocks_.size(); }

    // 读取与时间范围重叠的所有块
    std::vector<DataPoint> ReadRange(const TimeRange& range);

    // 获取所有块的索引
    const std::vector<BlockIndex>& GetBlockIndices() const { return blocks_; }

private:
    std::string filepath_;
    os::File file_;
    std::string tag_name_;
    TimeRange range_{INT64_MAX, 0};
    uint32_t block_count_ = 0;
    std::vector<BlockIndex> blocks_;
    bool opened_ = false;

    bool ReadHeader();
    bool ReadBlockIndex();
};

} // namespace minitsdb
