#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>

namespace minitsdb {

// ============================================================
// Gorilla 时间戳压缩器
// 参考: Facebook Gorilla TSDB 论文
// ============================================================
class TimestampCompressor {
public:
    TimestampCompressor();
    ~TimestampCompressor() = default;

    // 编码一个时间戳，写入内部缓冲区
    void Encode(Timestamp ts);

    // 获取编码后的字节
    const std::vector<uint8_t>& GetBytes() const;

    // 重置状态（开始新 block）
    void Reset();

    // 解码：从字节流中恢复时间戳序列
    // count: 预期的时间戳数量（用于控制解码终止）
    static std::vector<Timestamp> Decode(const uint8_t* data, size_t len,
                                          size_t count = 0);

private:
    std::vector<uint8_t> buffer_;
    int64_t prev_ts_ = 0;
    int64_t prev_delta_ = 0;
    bool first_ = true;
    size_t write_byte_pos_ = 0;
    int write_bit_pos_ = 0;

    // 写入 bit 的工具方法
    void WriteBit(uint8_t bit);
    void WriteBits(uint64_t value, int bits);
    uint8_t ReadBit(const uint8_t*& data);
    uint64_t ReadBits(const uint8_t*& data, int bits);
};

// ============================================================
// Gorilla 浮点值压缩器（XOR 编码）
// ============================================================
class ValueCompressor {
public:
    ValueCompressor();
    ~ValueCompressor() = default;

    void Encode(double value);
    const std::vector<uint8_t>& GetBytes() const;
    void Reset();

    static std::vector<double> Decode(const uint8_t* data, size_t len,
                                       size_t count = 0);

private:
    std::vector<uint8_t> buffer_;
    uint64_t prev_bits_ = 0;
    bool first_ = true;
    size_t write_byte_pos_ = 0;
    int write_bit_pos_ = 0;

    void WriteBit(uint8_t bit);
    void WriteBits(uint64_t value, int bits);
    uint8_t ReadBit(const uint8_t*& data);
    uint64_t ReadBits(const uint8_t*& data, int bits);
    static uint64_t DoubleToBits(double d);
    static double BitsToDouble(uint64_t bits);

    static int LeadingZeros(uint64_t x);
    static int TrailingZeros(uint64_t x);
};

// ============================================================
// 数据块：一个 Tag 在一段时间内的压缩数据
// ============================================================
struct CompressedBlock {
    TimeRange range;                // 时间范围
    std::vector<uint8_t> timestamps;  // 压缩后的时间戳
    std::vector<uint8_t> values;      // 压缩后的值
    size_t point_count = 0;           // 原始数据点数量

    // 压缩率 = 原始大小 / 压缩大小
    double CompressionRatio() const {
        size_t raw = point_count * (sizeof(Timestamp) + sizeof(double));
        size_t compressed = timestamps.size() + values.size();
        if (compressed == 0) return 0;
        return (double)raw / compressed;
    }
};

// ============================================================
// 数据块压缩器：将 DataPoint 序列压缩为 CompressedBlock
// ============================================================
class BlockCompressor {
public:
    CompressedBlock Compress(const std::vector<DataPoint>& points);
    std::vector<DataPoint> Decompress(const CompressedBlock& block);

private:
    TimestampCompressor ts_comp_;
    ValueCompressor val_comp_;
};

} // namespace minitsdb
