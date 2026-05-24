#include "storage/compressor.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace minitsdb {

// ============================================================
//  Bit-level I/O helpers (shared between compressors)
// ============================================================
namespace {

class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>* buffer) : buf_(buffer) {}

  void WriteBit(uint8_t bit) {
    if (byte_pos_ >= buf_->size()) {
      buf_->push_back(0);
    }
    if (bit) {
      (*buf_)[byte_pos_] |= (1 << bit_pos_);
    }
    if (++bit_pos_ == 8) {
      bit_pos_ = 0;
      byte_pos_++;
    }
  }

  void WriteBits(uint64_t value, int bits) {
    for (int i = 0; i < bits; i++) {
      WriteBit(static_cast<uint8_t>((value >> i) & 1));
    }
  }

  void Flush() {
    if (bit_pos_ > 0) {
      byte_pos_++;
      bit_pos_ = 0;
    }
  }

  size_t ByteSize() const { return byte_pos_ + (bit_pos_ > 0 ? 1 : 0); }

 private:
  std::vector<uint8_t>* buf_;
  size_t byte_pos_ = 0;
  int bit_pos_ = 0;
};

class BitReader {
 public:
  explicit BitReader(const uint8_t* data, size_t len)
      : data_(data), len_(len) {}

  uint8_t ReadBit() {
    if (byte_pos_ >= len_) return 0;
    uint8_t bit = (data_[byte_pos_] >> bit_pos_) & 1;
    if (++bit_pos_ == 8) {
      bit_pos_ = 0;
      byte_pos_++;
    }
    return bit;
  }

  uint64_t ReadBits(int bits) {
    uint64_t result = 0;
    for (int i = 0; i < bits; i++) {
      result |= (static_cast<uint64_t>(ReadBit()) << i);
    }
    return result;
  }

  bool HasMore() const {
    return byte_pos_ < len_ || (byte_pos_ == len_ - 1 && bit_pos_ < 8);
  }

 private:
  const uint8_t* data_;
  size_t len_;
  size_t byte_pos_ = 0;
  int bit_pos_ = 0;
};

// 写入 bit 到 buffer_ 的当前位置（不创建新 BitWriter）
// 写入前 buffer_ 必须已有足够容量
void AppendBitToBuffer(std::vector<uint8_t>* buf, uint8_t bit,
                       size_t& byte_pos, int& bit_pos) {
  if (byte_pos >= buf->size()) {
    buf->push_back(0);
  }
  if (bit) {
    (*buf)[byte_pos] |= (1 << bit_pos);
  }
  if (++bit_pos == 8) {
    bit_pos = 0;
    byte_pos++;
  }
}

void AppendBitsToBuffer(std::vector<uint8_t>* buf, uint64_t value, int bits,
                        size_t& byte_pos, int& bit_pos) {
  for (int i = 0; i < bits; i++) {
    AppendBitToBuffer(buf, static_cast<uint8_t>((value >> i) & 1),
                      byte_pos, bit_pos);
  }
}

}  // namespace

// ============================================================
//  TimestampCompressor implementation
// ============================================================
TimestampCompressor::TimestampCompressor() = default;

void TimestampCompressor::Encode(Timestamp ts) {
  if (first_) {
    // 第一个时间戳：用 BitWriter 写入 buffer_ 开头
    // 缓存写入后的位置
    BitWriter writer(&buffer_);
    writer.WriteBits(static_cast<uint64_t>(ts), 64);
    write_byte_pos_ = writer.ByteSize() - (8 - 0) / 8;  // 不精确
    
    // 改用直接写入，缓存位置
    Reset();
    buffer_.clear();
    size_t bp = 0;
    int bitp = 0;
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(ts), 64, bp, bitp);
    write_byte_pos_ = bp;
    write_bit_pos_ = bitp;
    
    prev_ts_ = ts;
    prev_delta_ = 0;
    first_ = false;
    return;
  }

  // 后续从缓存的位置继续写入
  size_t bp = write_byte_pos_;
  int bitp = write_bit_pos_;
  
  int64_t delta = ts - prev_ts_;
  int64_t delta_delta = delta - prev_delta_;

  if (delta_delta == 0) {
    AppendBitToBuffer(&buffer_, 0, bp, bitp);
  } else if (delta_delta >= -63 && delta_delta <= 64) {
    // "10" + 7 bits => bits on disk: 1, 0
    AppendBitsToBuffer(&buffer_, 0b01, 2, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 63), 7, bp, bitp);
  } else if (delta_delta >= -255 && delta_delta <= 256) {
    AppendBitsToBuffer(&buffer_, 0b011, 3, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 255), 9, bp, bitp);
  } else if (delta_delta >= -2047 && delta_delta <= 2048) {
    AppendBitsToBuffer(&buffer_, 0b0111, 4, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 2047), 12, bp, bitp);
  } else {
    AppendBitsToBuffer(&buffer_, 0b1111, 4, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta), 32, bp, bitp);
  }

  write_byte_pos_ = bp;
  write_bit_pos_ = bitp;
  prev_ts_ = ts;
  prev_delta_ = delta;
}

const std::vector<uint8_t>& TimestampCompressor::GetBytes() const {
  return buffer_;
}

void TimestampCompressor::Reset() {
  buffer_.clear();
  prev_ts_ = 0;
  prev_delta_ = 0;
  first_ = true;
  write_byte_pos_ = 0;
  write_bit_pos_ = 0;
}

std::vector<Timestamp> TimestampCompressor::Decode(const uint8_t* data,
                                                    size_t len,
                                                    size_t count) {
  std::vector<Timestamp> result;
  if (data == nullptr || len == 0) return result;

  BitReader reader(data, len);

  if (!reader.HasMore()) return result;
  uint64_t first_ts = reader.ReadBits(64);
  result.push_back(static_cast<Timestamp>(first_ts));

  size_t remaining = (count > 0) ? count - 1 : SIZE_MAX;

  int64_t prev_ts = static_cast<int64_t>(first_ts);
  int64_t prev_delta = 0;

  while (remaining > 0 && reader.HasMore()) {
    uint8_t bit0 = reader.ReadBit();
    int64_t delta = 0;

    if (bit0 == 0) {
      delta = prev_delta;
    } else {
      uint8_t bit1 = reader.ReadBit();
      if (bit1 == 0) {
        uint64_t val = reader.ReadBits(7);
        delta = prev_delta + static_cast<int64_t>(val) - 63;
      } else {
        uint8_t bit2 = reader.ReadBit();
        if (bit2 == 0) {
          uint64_t val = reader.ReadBits(9);
          delta = prev_delta + static_cast<int64_t>(val) - 255;
        } else {
          uint8_t bit3 = reader.ReadBit();
          if (bit3 == 0) {
            uint64_t val = reader.ReadBits(12);
            delta = prev_delta + static_cast<int64_t>(val) - 2047;
          } else {
            uint64_t val = reader.ReadBits(32);
            // 符号扩展：将 32-bit 有符号值转为 int64_t
            int32_t sval = static_cast<int32_t>(val & 0xFFFFFFFF);
            delta = prev_delta + static_cast<int64_t>(sval);
          }
        }
      }
    }

    Timestamp ts = prev_ts + delta;
    result.push_back(ts);
    prev_ts = ts;
    prev_delta = delta;
    if (remaining != SIZE_MAX) remaining--;
  }

  return result;
}

// ============================================================
//  ValueCompressor implementation
// ============================================================
ValueCompressor::ValueCompressor() = default;

uint64_t ValueCompressor::DoubleToBits(double d) {
  uint64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  return bits;
}

double ValueCompressor::BitsToDouble(uint64_t bits) {
  double d;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

int ValueCompressor::LeadingZeros(uint64_t x) {
  if (x == 0) return 64;
  return __builtin_clzll(x);
}

int ValueCompressor::TrailingZeros(uint64_t x) {
  if (x == 0) return 64;
  return __builtin_ctzll(x);
}

void ValueCompressor::Encode(double value) {
  uint64_t bits = DoubleToBits(value);

  if (first_) {
    size_t bp = 0;
    int bitp = 0;
    AppendBitsToBuffer(&buffer_, bits, 64, bp, bitp);
    write_byte_pos_ = bp;
    write_bit_pos_ = bitp;
    prev_bits_ = bits;
    first_ = false;
    return;
  }

  size_t bp = write_byte_pos_;
  int bitp = write_bit_pos_;
  uint64_t xor_result = bits ^ prev_bits_;

  if (xor_result == 0) {
    AppendBitToBuffer(&buffer_, 0, bp, bitp);
  } else {
    int leading = LeadingZeros(xor_result);
    int trailing = TrailingZeros(xor_result);
    int meaningful_bits = 64 - leading - trailing;

    AppendBitToBuffer(&buffer_, 1, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(leading), 6, bp, bitp);
    // meaningful_bits 范围 [1, 64]，6-bit 存 0-63
    // 用 0 表示 64（因为 meaningful=0 不可能出现）
    uint64_t m = (meaningful_bits == 64) ? 0 : meaningful_bits;
    AppendBitsToBuffer(&buffer_, m, 6, bp, bitp);
    AppendBitsToBuffer(&buffer_, xor_result >> trailing, meaningful_bits, bp, bitp);
  }

  write_byte_pos_ = bp;
  write_bit_pos_ = bitp;
  prev_bits_ = bits;
}

const std::vector<uint8_t>& ValueCompressor::GetBytes() const {
  return buffer_;
}

void ValueCompressor::Reset() {
  buffer_.clear();
  prev_bits_ = 0;
  first_ = true;
  write_byte_pos_ = 0;
  write_bit_pos_ = 0;
}

std::vector<double> ValueCompressor::Decode(const uint8_t* data, size_t len,
                                             size_t count) {
  std::vector<double> result;
  if (data == nullptr || len == 0) return result;

  BitReader reader(data, len);

  if (!reader.HasMore()) return result;
  uint64_t prev_bits = reader.ReadBits(64);
  result.push_back(BitsToDouble(prev_bits));

  size_t remaining = (count > 0) ? count - 1 : SIZE_MAX;

  while (remaining > 0 && reader.HasMore()) {
    uint8_t bit = reader.ReadBit();

    if (bit == 0) {
      result.push_back(BitsToDouble(prev_bits));
    } else {
      int leading = static_cast<int>(reader.ReadBits(6));
      int meaningful = static_cast<int>(reader.ReadBits(6));
      // 0 → 64（超出 6-bit 范围的编码）
      int meaningful_val = (meaningful == 0) ? 64 : meaningful;
      uint64_t xor_value = reader.ReadBits(meaningful_val);

      uint64_t value_bits;
      if (meaningful_val == 64) {
        // 全部 64 bits 都不同，直接 XOR
        value_bits = prev_bits ^ xor_value;
      } else {
        // 标准 XOR 恢复：左移回到原始位置
        value_bits = prev_bits ^ (xor_value << (64 - leading - meaningful_val));
      }
      result.push_back(BitsToDouble(value_bits));
      prev_bits = value_bits;
    }
    if (remaining != SIZE_MAX) remaining--;
  }

  return result;
}

// ============================================================
//  BlockCompressor implementation
// ============================================================
CompressedBlock BlockCompressor::Compress(const std::vector<DataPoint>& points) {
  CompressedBlock block;
  if (points.empty()) return block;

  block.point_count = points.size();
  block.range.start = points.front().ts;
  block.range.end = points.back().ts;

  ts_comp_.Reset();
  val_comp_.Reset();

  for (const auto& dp : points) {
    ts_comp_.Encode(dp.ts);
    val_comp_.Encode(std::get<double>(dp.value));
  }

  block.timestamps = ts_comp_.GetBytes();
  block.values = val_comp_.GetBytes();

  return block;
}

std::vector<DataPoint> BlockCompressor::Decompress(const CompressedBlock& block) {
  std::vector<DataPoint> result;

  auto timestamps = TimestampCompressor::Decode(
      block.timestamps.data(), block.timestamps.size(), block.point_count);
  auto values = ValueCompressor::Decode(
      block.values.data(), block.values.size(), block.point_count);

  size_t count = std::min(timestamps.size(), values.size());
  result.reserve(count);

  for (size_t i = 0; i < count; i++) {
    DataPoint dp;
    dp.ts = timestamps[i];
    dp.value = values[i];
    result.push_back(dp);
  }

  return result;
}

}  // namespace minitsdb
