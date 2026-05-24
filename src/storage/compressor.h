#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>

namespace minitsdb {

class TimestampCompressor {
public:
    TimestampCompressor();
    ~TimestampCompressor() = default;
    void Encode(Timestamp ts);
    const std::vector<uint8_t>& GetBytes() const;
    void Reset();
    static std::vector<Timestamp> Decode(const uint8_t* data, size_t len,
                                          size_t count = 0);
private:
    std::vector<uint8_t> buffer_;
    int64_t prev_ts_ = 0;
    int64_t prev_delta_ = 0;
    bool first_ = true;
    size_t write_byte_pos_ = 0;
    int write_bit_pos_ = 0;
    void WriteBit(uint8_t bit);
    void WriteBits(uint64_t value, int bits);
    uint8_t ReadBit(const uint8_t*& data);
    uint64_t ReadBits(const uint8_t*& data, int bits);
};

// Multi-type value encoder (supports double, int64, string)
class ValueEncoder {
public:
    ValueEncoder();
    void Encode(const Value& value);
    const std::vector<uint8_t>& GetBytes() const;
    const std::vector<uint8_t>& GetStringData() const;
    void Reset();

    struct DecodedValue { Value value; };
    static std::vector<DecodedValue> Decode(const uint8_t* data, size_t len,
                                             const uint8_t* str_data, size_t str_len,
                                             size_t count = 0);
private:
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> string_data_;
    uint64_t prev_bits_ = 0;
    int64_t prev_int_ = 0;
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

struct CompressedBlock {
    TimeRange range;
    std::vector<uint8_t> timestamps;
    std::vector<uint8_t> values;
    std::vector<uint8_t> string_data;
    size_t point_count = 0;

    double CompressionRatio() const {
        size_t raw = point_count * sizeof(Timestamp);
        size_t compressed = timestamps.size() + values.size() + string_data.size();
        if (compressed == 0) return 0;
        return (double)raw / compressed;
    }
};

class BlockCompressor {
public:
    CompressedBlock Compress(const std::vector<DataPoint>& points);
    std::vector<DataPoint> Decompress(const CompressedBlock& block);
private:
    TimestampCompressor ts_comp_;
};

} // namespace minitsdb
