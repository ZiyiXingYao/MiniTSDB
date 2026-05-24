#include <gtest/gtest.h>
#include "storage/sstable.h"
#include "storage/compaction.h"
#include "storage/compressor.h"
#include "storage/wal.h"
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace minitsdb;

class StorageTest : public ::testing::Test {
protected:
    std::string test_dir_ = "./test_storage_data";

    void SetUp() override {
        fs::remove_all(test_dir_);
        fs::create_directories(test_dir_ + "/tags/TESTTAG");
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }
};

TEST_F(StorageTest, SSTableWriteRead) {
    // 创建测试数据
    BlockCompressor bc;
    std::vector<DataPoint> points;
    for (int i = 0; i < 100; i++) {
        DataPoint dp;
        dp.ts = 1000 + i * 1000;
        dp.value = 500.0 + i * 0.5;
        points.push_back(dp);
    }
    auto block = bc.Compress(points);

    // 写入 SSTable
    std::string sst_path = test_dir_ + "/tags/TESTTAG/data.sst";
    SSTableWriter writer(sst_path);
    ASSERT_TRUE(writer.Open());
    writer.AddBlock(block);
    writer.Close();
    ASSERT_GT(writer.FileSize(), 0);

    // 读取 SSTable
    SSTableReader reader(sst_path);
    ASSERT_TRUE(reader.Open()) << "Failed to open SSTable";
    printf("SSTable: block_count=%zu, range=[%lld, %lld]\n",
           reader.BlockCount(),
           (long long)reader.GetTimeRange().start,
           (long long)reader.GetTimeRange().end);

    TimeRange range;
    range.start = 1000;
    range.end = 100000;
    auto read_points = reader.ReadRange(range);

    ASSERT_EQ(read_points.size(), points.size());
    for (size_t i = 0; i < points.size(); i++) {
        EXPECT_EQ(read_points[i].ts, points[i].ts);
        EXPECT_DOUBLE_EQ(std::get<double>(read_points[i].value),
                         std::get<double>(points[i].value));
    }
}

TEST_F(StorageTest, Compaction) {
    // 创建多个小 SSTable
    for (int f = 0; f < 5; f++) {
        BlockCompressor bc;
        std::vector<DataPoint> points;
        for (int i = 0; i < 10; i++) {
            DataPoint dp;
            dp.ts = (f * 10 + i) * 1000;
            dp.value = 100.0 + f * 10 + i;
            points.push_back(dp);
        }
        auto block = bc.Compress(points);
        std::string path = test_dir_ + "/tags/TESTTAG/file" + std::to_string(f) + ".sst";
        SSTableWriter writer(path);
        ASSERT_TRUE(writer.Open());
        writer.AddBlock(block);
        writer.Close();
    }

    // 执行压缩
    Compaction compaction(test_dir_);
    compaction.RunOnce(1024 * 1024);  // 1MB threshold

    // 验证小文件被合并（文件数应减少）
    int sst_count = 0;
    for (const auto& entry : fs::directory_iterator(test_dir_ + "/tags/TESTTAG")) {
        if (entry.path().extension() == ".sst") sst_count++;
    }
    EXPECT_LE(sst_count, 3);  // 最多剩 3 个文件（5个合并为1个merged + 可能的残留）
}

TEST_F(StorageTest, WALWriteRecover) {
    std::string wal_path = test_dir_ + "/wal.log";

    // 写入 WAL
    WalWriter writer(wal_path);
    ASSERT_TRUE(writer.Open());

    DataPoint dp;
    dp.ts = 12345;
    dp.value = 67.89;
    ASSERT_TRUE(writer.AppendWrite("TESTTAG", dp));
    writer.WriteCheckpoint();
    writer.Close();

    // 读取 WAL
    WalReader reader(wal_path);
    ASSERT_TRUE(reader.Open());
    ASSERT_GE(reader.Entries().size(), 1);

    auto& entry = reader.Entries()[0];
    EXPECT_EQ(entry.type, WalEntryType::DATA_POINT);
    EXPECT_EQ(entry.tag_name, "TESTTAG");
    EXPECT_EQ(entry.points[0].ts, 12345);
    EXPECT_DOUBLE_EQ(std::get<double>(entry.points[0].value), 67.89);
}

TEST_F(StorageTest, WALTruncate) {
    std::string wal_path = test_dir_ + "/wal_truncate.log";

    WalWriter writer(wal_path);
    ASSERT_TRUE(writer.Open());
    DataPoint p;
    p.ts = 1; p.value = 1.0;
    writer.AppendWrite("T", p);
    writer.Close();

    EXPECT_TRUE(WalReader::Truncate(wal_path));
    WalReader reader(wal_path);
    EXPECT_FALSE(reader.Open());
}
