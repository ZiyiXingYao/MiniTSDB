#include "storage/compressor.h"
#include <cstring>
#include <cstdint>

namespace minitsdb {

namespace {
void AppendBitToBuffer(std::vector<uint8_t>* buf, uint8_t bit,
                       size_t& byte_pos, int& bit_pos) {
  if (byte_pos >= buf->size()) buf->push_back(0);
  if (bit) (*buf)[byte_pos] |= (1 << bit_pos);
  if (++bit_pos == 8) { bit_pos = 0; byte_pos++; }
}

void AppendBitsToBuffer(std::vector<uint8_t>* buf, uint64_t value, int bits,
                        size_t& byte_pos, int& bit_pos) {
  for (int i = 0; i < bits; i++)
    AppendBitToBuffer(buf, static_cast<uint8_t>((value >> i) & 1), byte_pos, bit_pos);
}
}  // namespace

// ========== TimestampCompressor ==========
TimestampCompressor::TimestampCompressor() = default;

void TimestampCompressor::Encode(Timestamp ts) {
  if (first_) {
    size_t bp = 0; int bitp = 0;
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(ts), 64, bp, bitp);
    write_byte_pos_ = bp; write_bit_pos_ = bitp;
    prev_ts_ = ts; prev_delta_ = 0; first_ = false;
    return;
  }
  size_t bp = write_byte_pos_; int bitp = write_bit_pos_;
  int64_t delta = ts - prev_ts_;
  int64_t delta_delta = delta - prev_delta_;
  if (delta_delta == 0) {
    AppendBitToBuffer(&buffer_, 0, bp, bitp);
  } else if (delta_delta >= -63 && delta_delta <= 64) {
    AppendBitsToBuffer(&buffer_, 0b01, 2, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 63), 7, bp, bitp);
  } else if (delta_delta >= -255 && delta_delta <= 256) {
    AppendBitsToBuffer(&buffer_, 0b011, 3, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 255), 9, bp, bitp);
  } else if (delta_delta >= -2047 && delta_delta <= 2048) {
    AppendBitsToBuffer(&buffer_, 0b0111, 4, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta + 2047), 12, bp, bitp);
  } else {
    // Note: overflow uses 32-bit encoding (not standard Gorilla 64-bit).
    // This limits the max delta-delta to ±INT32_MAX (~24 days).
    // Acceptable for typical time-series blocks (< 1 hour range).
    AppendBitsToBuffer(&buffer_, 0b1111, 4, bp, bitp);
    AppendBitsToBuffer(&buffer_, static_cast<uint64_t>(delta_delta), 32, bp, bitp);
  }
  write_byte_pos_ = bp; write_bit_pos_ = bitp;
  prev_ts_ = ts; prev_delta_ = delta;
}

const std::vector<uint8_t>& TimestampCompressor::GetBytes() const { return buffer_; }
void TimestampCompressor::Reset() {
  buffer_.clear(); prev_ts_ = 0; prev_delta_ = 0; first_ = true;
  write_byte_pos_ = 0; write_bit_pos_ = 0;
}

std::vector<Timestamp> TimestampCompressor::Decode(const uint8_t* data, size_t len, size_t count) {
  std::vector<Timestamp> result;
  if (!data || !len) return result;
  size_t bp = 0; int bitp = 0;
  // first value: 64 bits
  auto rb = [&]() -> uint8_t {
    if (bp >= len) return 0;
    uint8_t b = (data[bp] >> bitp) & 1;
    if (++bitp == 8) { bitp = 0; bp++; }
    return b;
  };
  auto rbits = [&](int n) -> uint64_t {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (static_cast<uint64_t>(rb()) << i);
    return v;
  };
  result.push_back(static_cast<Timestamp>(rbits(64)));
  size_t remaining = (count > 0) ? count - 1 : SIZE_MAX;
  int64_t prev_ts = result[0], prev_delta = 0;

  while (remaining > 0 && bp < len) {
    uint8_t b0 = rb();
    int64_t delta = 0;
    if (b0 == 0) { delta = prev_delta; }
    else {
      uint8_t b1 = rb();
      if (b1 == 0) {
        uint64_t v = rbits(7);
        delta = prev_delta + static_cast<int64_t>(v) - 63;
      } else {
        uint8_t b2 = rb();
        if (b2 == 0) {
          uint64_t v = rbits(9);
          delta = prev_delta + static_cast<int64_t>(v) - 255;
        } else {
          uint8_t b3 = rb();
          if (b3 == 0) {
            uint64_t v = rbits(12);
            delta = prev_delta + static_cast<int64_t>(v) - 2047;
          } else {
            uint64_t v = rbits(32);
            int32_t s = static_cast<int32_t>(v & 0xFFFFFFFF);
            delta = prev_delta + static_cast<int64_t>(s);
          }
        }
      }
    }
    result.push_back(prev_ts + delta);
    prev_ts += delta; prev_delta = delta;
    if (remaining != SIZE_MAX) remaining--;
  }
  return result;
}

// ========== ValueEncoder ==========
ValueEncoder::ValueEncoder() = default;

uint64_t ValueEncoder::DoubleToBits(double d) { uint64_t b; std::memcpy(&b, &d, sizeof(b)); return b; }
double ValueEncoder::BitsToDouble(uint64_t b) { double d; std::memcpy(&d, &b, sizeof(d)); return d; }
int ValueEncoder::LeadingZeros(uint64_t x) { return (x == 0) ? 64 : __builtin_clzll(x); }
int ValueEncoder::TrailingZeros(uint64_t x) { return (x == 0) ? 64 : __builtin_ctzll(x); }

void ValueEncoder::WriteBit(uint8_t bit) {
  AppendBitToBuffer(&buffer_, bit, write_byte_pos_, write_bit_pos_);
}
void ValueEncoder::WriteBits(uint64_t value, int bits) {
  AppendBitsToBuffer(&buffer_, value, bits, write_byte_pos_, write_bit_pos_);
}

void ValueEncoder::Encode(const Value& value) {
  // 2-bit type tag: 00=double, 01=int64, 10=string
  if (std::holds_alternative<double>(value)) {
    WriteBit(0); WriteBit(0);
    double dval = std::get<double>(value);
    uint64_t bits = DoubleToBits(dval);
    if (first_) {
      WriteBits(bits, 64); prev_bits_ = bits; prev_int_ = 0; first_ = false; return;
    }
    uint64_t xor_r = bits ^ prev_bits_;
    if (xor_r == 0) { WriteBit(0); }
    else {
      WriteBit(1);
      int lead = LeadingZeros(xor_r), trail = TrailingZeros(xor_r), m = 64 - lead - trail;
      uint64_t mv = (m == 64) ? 0 : m;
      WriteBits(static_cast<uint64_t>(lead), 6); WriteBits(mv, 6);
      WriteBits(xor_r >> trail, m);
    }
    prev_bits_ = bits;
  } else if (std::holds_alternative<int64_t>(value)) {
    WriteBit(0); WriteBit(1);
    int64_t ival = std::get<int64_t>(value);
    if (first_) {
      WriteBits(static_cast<uint64_t>(ival), 64); prev_int_ = ival; prev_bits_ = 0; first_ = false; return;
    }
    uint64_t xor_r = static_cast<uint64_t>(ival) ^ static_cast<uint64_t>(prev_int_);
    if (xor_r == 0) { WriteBit(0); }
    else {
      WriteBit(1);
      int lead = LeadingZeros(xor_r), trail = TrailingZeros(xor_r), m = 64 - lead - trail;
      uint64_t mv = (m == 64) ? 0 : m;
      WriteBits(static_cast<uint64_t>(lead), 6); WriteBits(mv, 6);
      WriteBits(xor_r >> trail, m);
    }
    prev_int_ = ival;
  } else {
    WriteBit(1); WriteBit(0);
    const std::string& s = std::get<std::string>(value);
    uint32_t len = static_cast<uint32_t>(s.size());
    WriteBits(len, 32);
    string_data_.insert(string_data_.end(), s.begin(), s.end());
    if (first_) { prev_bits_ = 0; prev_int_ = 0; first_ = false; }
  }
}

const std::vector<uint8_t>& ValueEncoder::GetBytes() const { return buffer_; }
const std::vector<uint8_t>& ValueEncoder::GetStringData() const { return string_data_; }

void ValueEncoder::Reset() {
  buffer_.clear(); string_data_.clear();
  prev_bits_ = 0; prev_int_ = 0; first_ = true;
  write_byte_pos_ = 0; write_bit_pos_ = 0;
}

std::vector<ValueEncoder::DecodedValue> ValueEncoder::Decode(
    const uint8_t* data, size_t len, const uint8_t* str_data, size_t str_len, size_t count) {
  std::vector<DecodedValue> result;
  if (!data || !len) return result;
  size_t bp = 0; int bitp = 0; size_t str_off = 0;
  auto rb = [&]() -> uint8_t {
    if (bp >= len) return 0;
    uint8_t b = (data[bp] >> bitp) & 1;
    if (++bitp == 8) { bitp = 0; bp++; }
    return b;
  };
  auto rbits = [&](int n) -> uint64_t {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (static_cast<uint64_t>(rb()) << i);
    return v;
  };
  size_t remaining = (count > 0) ? count : SIZE_MAX;
  bool first_val = true;
  uint64_t prev_double_bits = 0;
  int64_t prev_int = 0;

  while (remaining > 0 && bp < len) {
    uint8_t tag = (rb() << 1) | rb();
    if (tag == 2) { // string
      uint32_t slen = static_cast<uint32_t>(rbits(32));
      std::string s;
      if (str_data && str_off + slen <= str_len)
        s.assign(reinterpret_cast<const char*>(str_data + str_off), slen);
      str_off += slen;
      result.push_back({s});
      first_val = false;
      if (remaining != SIZE_MAX) remaining--;
      continue;
    }
    if (first_val) {
      uint64_t raw = rbits(64);
      if (tag == 0) { result.push_back({BitsToDouble(raw)}); prev_double_bits = raw; }
      else { result.push_back({static_cast<int64_t>(raw)}); prev_int = static_cast<int64_t>(raw); }
      first_val = false;
      if (remaining != SIZE_MAX) remaining--;
      continue;
    }
    // subsequent values: XOR encoding
    uint8_t bit = rb();
    if (bit == 0) {
      // same as previous of same type
      if (tag == 0) result.push_back({BitsToDouble(prev_double_bits)});
      else result.push_back({prev_int});
    } else {
      int lead = static_cast<int>(rbits(6));
      int meaningful = static_cast<int>(rbits(6));
      int mv = (meaningful == 0) ? 64 : meaningful;
      uint64_t xv = rbits(mv);
      uint64_t pp = (tag == 0) ? prev_double_bits : static_cast<uint64_t>(prev_int);
      uint64_t vb = (mv == 64) ? pp ^ xv : pp ^ (xv << (64 - lead - mv));
      if (tag == 0) { result.push_back({BitsToDouble(vb)}); prev_double_bits = vb; }
      else { result.push_back({static_cast<int64_t>(vb)}); prev_int = static_cast<int64_t>(vb); }
    }
    if (remaining != SIZE_MAX) remaining--;
  }
  return result;
}

// ========== BlockCompressor ==========
CompressedBlock BlockCompressor::Compress(const std::vector<DataPoint>& points) {
  CompressedBlock block;
  if (points.empty()) return block;
  block.point_count = points.size();
  block.range.start = points.front().ts;
  block.range.end = points.back().ts;
  ts_comp_.Reset();
  ValueEncoder enc;
  for (const auto& p : points) { ts_comp_.Encode(p.ts); enc.Encode(p.value); }
  block.timestamps = ts_comp_.GetBytes();
  block.values = enc.GetBytes();
  block.string_data = enc.GetStringData();
  return block;
}

std::vector<DataPoint> BlockCompressor::Decompress(const CompressedBlock& block) {
  std::vector<DataPoint> result;
  auto ts = TimestampCompressor::Decode(block.timestamps.data(), block.timestamps.size(), block.point_count);
  auto vs = ValueEncoder::Decode(block.values.data(), block.values.size(),
                                  block.string_data.data(), block.string_data.size(), block.point_count);
  size_t n = ts.size() < vs.size() ? ts.size() : vs.size();
  result.reserve(n);
  for (size_t i = 0; i < n; i++) {
    DataPoint dp; dp.ts = ts[i]; dp.value = vs[i].value; result.push_back(dp);
  }
  return result;
}

} // namespace minitsdb
