#include <gtest/gtest.h>
#include "storage/compressor.h"
#include "common/types.h"

using namespace minitsdb;

// ============================================================
//  TimestampCompressor Tests
// ============================================================

TEST(TimestampCompressorTest, StableInterval) {
    TimestampCompressor comp;
    std::vector<Timestamp> original;
    for (int i = 0; i < 100; i++) {
        original.push_back(1000 + i * 1000);
    }
    for (auto ts : original) comp.Encode(ts);
    auto bytes = comp.GetBytes();

    auto decoded = TimestampCompressor::Decode(bytes.data(), bytes.size(),
                                                original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        ASSERT_EQ(decoded[i], original[i]);
    }
    // 稳定间隔应高效压缩
    EXPECT_LT(bytes.size(), 200);
}

TEST(TimestampCompressorTest, JitterTimestamps) {
    TimestampCompressor comp;
    std::vector<Timestamp> original;
    for (int i = 0; i < 50; i++) {
        int64_t jitter = (i % 5 == 0) ? 5 : 0;
        original.push_back(1000 + i * 1000 + jitter);
    }
    for (auto ts : original) comp.Encode(ts);
    auto bytes = comp.GetBytes();
    auto decoded = TimestampCompressor::Decode(bytes.data(), bytes.size(),
                                                original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        ASSERT_EQ(decoded[i], original[i]);
    }
}

TEST(TimestampCompressorTest, LargeGap) {
    TimestampCompressor comp;
    std::vector<Timestamp> original = {
        1000, 2000, 3000, 100000, 101000, 102000
    };
    for (auto ts : original) comp.Encode(ts);
    auto bytes = comp.GetBytes();
    auto decoded = TimestampCompressor::Decode(bytes.data(), bytes.size(),
                                                original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        ASSERT_EQ(decoded[i], original[i]);
    }
}

TEST(TimestampCompressorTest, EmptyDecode) {
    auto result = TimestampCompressor::Decode(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

// ============================================================
//  ValueCompressor Tests
// ============================================================

TEST(ValueCompressorTest, IdenticalValues) {
    ValueCompressor comp;
    std::vector<double> original(100, 523.7);

    for (auto v : original) comp.Encode(v);
    auto bytes = comp.GetBytes();
    auto decoded = ValueCompressor::Decode(bytes.data(), bytes.size(),
                                            original.size());

    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        EXPECT_DOUBLE_EQ(decoded[i], original[i]);
    }
    // 相同值应极高压缩 (< 50 bytes for 100 doubles)
    EXPECT_LT(bytes.size(), 50);
}

TEST(ValueCompressorTest, SlowChangingValues) {
    ValueCompressor comp;
    std::vector<double> original;
    double temp = 500.0;
    for (int i = 0; i < 50; i++) {
        temp += 0.1;
        original.push_back(temp);
    }

    for (auto v : original) comp.Encode(v);
    auto bytes = comp.GetBytes();
    auto decoded = ValueCompressor::Decode(bytes.data(), bytes.size(),
                                            original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        EXPECT_DOUBLE_EQ(decoded[i], original[i]);
    }
}

TEST(ValueCompressorTest, VolatileValues) {
    std::vector<double> original = {0.0, 1000.0, -500.0, 3.14159265358979,
                                    1e-10, 1e10, 0.0001, 999999.999};
    ValueCompressor comp;
    for (auto v : original) comp.Encode(v);
    auto bytes = comp.GetBytes();
    auto decoded = ValueCompressor::Decode(bytes.data(), bytes.size(),
                                            original.size());
    ASSERT_EQ(decoded.size(), original.size());
    for (size_t i = 0; i < original.size(); i++) {
        EXPECT_DOUBLE_EQ(decoded[i], original[i]);
    }
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
        EXPECT_DOUBLE_EQ(std::get<double>(decompressed[i].value),
                         std::get<double>(original[i].value));
    }
    EXPECT_GT(block.CompressionRatio(), 3.0);
}

TEST(BlockCompressorTest, EmptyBlock) {
    std::vector<DataPoint> empty;
    BlockCompressor bc;
    auto block = bc.Compress(empty);
    EXPECT_EQ(block.point_count, 0);
    EXPECT_TRUE(block.timestamps.empty());
    EXPECT_TRUE(block.values.empty());
    auto decompressed = bc.Decompress(block);
    EXPECT_TRUE(decompressed.empty());
}

TEST(BlockCompressorTest, SinglePoint) {
    std::vector<DataPoint> points;
    DataPoint dp;
    dp.ts = 1234567890;
    dp.value = 42.0;
    points.push_back(dp);

    BlockCompressor bc;
    auto block = bc.Compress(points);
    auto decompressed = bc.Decompress(block);
    ASSERT_EQ(decompressed.size(), 1);
    ASSERT_EQ(decompressed[0].ts, 1234567890);
    EXPECT_DOUBLE_EQ(std::get<double>(decompressed[0].value), 42.0);
}
