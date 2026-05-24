#include <gtest/gtest.h>
#include "storage/compressor.h"
#include "common/types.h"

using namespace minitsdb;

TEST(TimestampCompressorTest, StableInterval) {
    TimestampCompressor comp;
    std::vector<Timestamp> original;
    for (int i = 0; i < 100; i++) original.push_back(1000 + i * 1000);
    for (auto ts : original) comp.Encode(ts);
    auto bytes = comp.GetBytes();
    auto decoded = TimestampCompressor::Decode(bytes.data(), bytes.size(), original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) ASSERT_EQ(decoded[i], original[i]);
    EXPECT_LT(bytes.size(), 200);
}

TEST(TimestampCompressorTest, JitterTimestamps) {
    TimestampCompressor comp;
    std::vector<Timestamp> original;
    for (int i = 0; i < 50; i++) {
        original.push_back(1000 + i * 1000 + (i % 5 == 0 ? 5 : 0));
    }
    for (auto ts : original) comp.Encode(ts);
    auto decoded = TimestampCompressor::Decode(comp.GetBytes().data(), comp.GetBytes().size(), original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) ASSERT_EQ(decoded[i], original[i]);
}

TEST(TimestampCompressorTest, LargeGap) {
    TimestampCompressor comp;
    std::vector<Timestamp> original = {1000, 2000, 3000, 100000, 101000, 102000};
    for (auto ts : original) comp.Encode(ts);
    auto decoded = TimestampCompressor::Decode(comp.GetBytes().data(), comp.GetBytes().size(), original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) ASSERT_EQ(decoded[i], original[i]);
}

TEST(TimestampCompressorTest, EmptyDecode) {
    EXPECT_TRUE(TimestampCompressor::Decode(nullptr, 0).empty());
}

// ============================================================
//  ValueEncoder Tests (double, int64, string)
// ============================================================

TEST(ValueEncoderTest, IdenticalDoubles) {
    ValueEncoder enc;
    std::vector<Value> vals;
    for (int i = 0; i < 100; i++) vals.push_back(523.7);
    for (auto& v : vals) enc.Encode(v);
    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), 100);
    ASSERT_EQ(decoded.size(), vals.size());
    for (size_t i = 0; i < vals.size(); i++)
        EXPECT_DOUBLE_EQ(std::get<double>(decoded[i].value), std::get<double>(vals[i]));
}

TEST(ValueEncoderTest, SlowChangingDoubles) {
    ValueEncoder enc;
    std::vector<Value> vals;
    double t = 500.0;
    for (int i = 0; i < 50; i++) { t += 0.1; vals.push_back(t); }
    for (auto& v : vals) enc.Encode(v);
    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), 50);
    ASSERT_EQ(decoded.size(), vals.size());
    for (size_t i = 0; i < vals.size(); i++)
        EXPECT_DOUBLE_EQ(std::get<double>(decoded[i].value), std::get<double>(vals[i]));
}

TEST(ValueEncoderTest, VolatileDoubles) {
    std::vector<Value> vals = {0.0, 1000.0, -500.0, 3.14159265358979, 1e-10, 1e10, 0.0001, 999999.999};
    ValueEncoder enc;
    for (auto& v : vals) enc.Encode(v);
    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), vals.size());
    ASSERT_EQ(decoded.size(), vals.size());
    for (size_t i = 0; i < vals.size(); i++)
        EXPECT_DOUBLE_EQ(std::get<double>(decoded[i].value), std::get<double>(vals[i]));
}

TEST(ValueEncoderTest, Int64Values) {
    ValueEncoder enc;
    std::vector<Value> vals = {int64_t(0), int64_t(1), int64_t(-1), int64_t(1000000), int64_t(-999999)};
    for (auto& v : vals) enc.Encode(v);
    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), vals.size());
    ASSERT_EQ(decoded.size(), vals.size());
    for (size_t i = 0; i < vals.size(); i++)
        EXPECT_EQ(std::get<int64_t>(decoded[i].value), std::get<int64_t>(vals[i]));
}

TEST(ValueEncoderTest, StringValues) {
    ValueEncoder enc;
    std::vector<Value> vals = {std::string("hello"), std::string("world"), std::string(""), std::string("a")};
    for (auto& v : vals) enc.Encode(v);
    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), vals.size());
    ASSERT_EQ(decoded.size(), vals.size());
    for (size_t i = 0; i < vals.size(); i++)
        EXPECT_EQ(std::get<std::string>(decoded[i].value), std::get<std::string>(vals[i]));
}

TEST(ValueEncoderTest, MixedTypes) {
    ValueEncoder enc;
    std::vector<Value> vals = {42.5, int64_t(123), std::string("test"), -1.0, int64_t(0)};
    for (auto& v : vals) enc.Encode(v);

    auto decoded = ValueEncoder::Decode(enc.GetBytes().data(), enc.GetBytes().size(),
                                         enc.GetStringData().data(), enc.GetStringData().size(), vals.size());
    ASSERT_EQ(decoded.size(), vals.size());
    EXPECT_DOUBLE_EQ(std::get<double>(decoded[0].value), 42.5);
    EXPECT_EQ(std::get<int64_t>(decoded[1].value), 123);
    EXPECT_EQ(std::get<std::string>(decoded[2].value), "test");
    EXPECT_DOUBLE_EQ(std::get<double>(decoded[3].value), -1.0);
    EXPECT_EQ(std::get<int64_t>(decoded[4].value), 0);
}

TEST(ValueEncoderTest, EmptyDecode) {
    auto r = ValueEncoder::Decode(nullptr, 0, nullptr, 0);
    EXPECT_TRUE(r.empty());
}

// ============================================================
//  BlockCompressor Tests
// ============================================================

TEST(BlockCompressorTest, CompressDecompress) {
    std::vector<DataPoint> original;
    for (int i = 0; i < 50; i++) {
        DataPoint dp;
        dp.ts = 1000 + i * 1000;
        dp.value = 500.0 + i * 0.5;
        original.push_back(dp);
    }
    BlockCompressor bc;
    auto block = bc.Compress(original);
    auto decompressed = bc.Decompress(block);
    ASSERT_EQ(decompressed.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        ASSERT_EQ(decompressed[i].ts, original[i].ts);
        EXPECT_DOUBLE_EQ(std::get<double>(decompressed[i].value), std::get<double>(original[i].value));
    }
}

TEST(BlockCompressorTest, MixedTypes) {
    std::vector<DataPoint> points;
    DataPoint p1; p1.ts = 1; p1.value = 42.5; points.push_back(p1);
    DataPoint p2; p2.ts = 2; p2.value = int64_t(999); points.push_back(p2);
    DataPoint p3; p3.ts = 3; p3.value = std::string("hello"); points.push_back(p3);
    DataPoint p4; p4.ts = 4; p4.value = -1.0; points.push_back(p4);

    BlockCompressor bc;
    auto block = bc.Compress(points);
    auto d = bc.Decompress(block);
    ASSERT_EQ(d.size(), points.size());
    EXPECT_DOUBLE_EQ(std::get<double>(d[0].value), 42.5);
    EXPECT_EQ(std::get<int64_t>(d[1].value), 999);
    EXPECT_EQ(std::get<std::string>(d[2].value), "hello");
    EXPECT_DOUBLE_EQ(std::get<double>(d[3].value), -1.0);
}

TEST(BlockCompressorTest, EmptyBlock) {
    BlockCompressor bc;
    auto block = bc.Compress({});
    EXPECT_EQ(block.point_count, 0);
    EXPECT_TRUE(bc.Decompress(block).empty());
}

TEST(BlockCompressorTest, SinglePoint) {
    std::vector<DataPoint> points;
    DataPoint dp; dp.ts = 1234567890; dp.value = 42.0; points.push_back(dp);
    BlockCompressor bc;
    auto block = bc.Compress(points);
    auto d = bc.Decompress(block);
    ASSERT_EQ(d.size(), 1);
    EXPECT_EQ(d[0].ts, 1234567890);
    EXPECT_DOUBLE_EQ(std::get<double>(d[0].value), 42.0);
}
