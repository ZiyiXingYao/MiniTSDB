#include <gtest/gtest.h>
#include "storage/sstable.h"
#include "storage/compaction.h"
#include "storage/compressor.h"
#include "storage/wal.h"
#include "common/os/file.h"
#include "common/os/fs.h"
#include <cstdio>

using namespace minitsdb;

class StorageTest : public ::testing::Test {
protected:
    std::string test_dir_ = "./test_storage_data";

    void SetUp() override {
        // 递归清理并重建
        os::fs::RemoveAll(test_dir_);
        os::fs::CreateDirectories(test_dir_ + "/tags/TESTTAG");
    }

    void TearDown() override {
        os::fs::RemoveAll(test_dir_);
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

TEST_F(StorageTest, SSTableCRCVerify) {
    // 创建测试数据
    BlockCompressor bc;
    std::vector<DataPoint> points;
    for (int i = 0; i < 10; i++) {
        DataPoint dp;
        dp.ts = 1000 + i * 1000;
        dp.value = static_cast<double>(i);
        points.push_back(dp);
    }
    auto block = bc.Compress(points);

    std::string sst_path = test_dir_ + "/tags/TESTTAG/data.sst";
    {
        SSTableWriter writer(sst_path);
        ASSERT_TRUE(writer.Open());
        writer.AddBlock(block);
        writer.Close();
    }

    // 正常读取应成功
    SSTableReader reader(sst_path);
    EXPECT_TRUE(reader.Open());
    reader.Close();

    // 损坏文件尾部 CRC，读取应失败
    {
        os::File f;
        ASSERT_TRUE(f.Open(sst_path, os::FileMode::READ_WRITE));
        f.Seek(0, SEEK_END);
        int64_t pos = f.Tell();
        // 覆盖最后 4 字节的 CRC（倒数第 4 字节）
        uint32_t bad_crc = 0xDEADBEEF;
        f.Seek(pos - 4, SEEK_SET);
        f.Write(&bad_crc, sizeof(bad_crc));
        f.Close();
    }

    // CRC 不匹配应导致 Open 失败
    SSTableReader bad_reader(sst_path);
    EXPECT_FALSE(bad_reader.Open());
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
    std::vector<os::fs::DirEntry> sst_entries;
    if (os::fs::ListDirectory(test_dir_ + "/tags/TESTTAG", sst_entries)) {
        for (const auto& entry : sst_entries) {
            if (entry.name.size() > 4 &&
                entry.name.substr(entry.name.size() - 4) == ".sst") {
                sst_count++;
            }
        }
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
