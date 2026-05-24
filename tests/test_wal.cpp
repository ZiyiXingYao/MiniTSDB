#include <gtest/gtest.h>
#include "storage/wal.h"
#include "common/types.h"
#include <cstdio>
#include <string>

using namespace minitsdb;

class WALTest : public ::testing::Test {
protected:
    void SetUp() override {
        wal_path_ = "test_wal.bin";
        // 确保测试前干净
        WalReader::Truncate(wal_path_);
        std::remove(wal_path_.c_str());
    }

    void TearDown() override {
        WalReader::Truncate(wal_path_);
        std::remove(wal_path_.c_str());
    }

    std::string wal_path_;
};

TEST_F(WALTest, WriteAndReadSinglePoint) {
    WalWriter writer(wal_path_);
    ASSERT_TRUE(writer.Open());

    DataPoint dp;
    dp.ts = 1000;
    dp.value = 523.7;
    ASSERT_TRUE(writer.AppendWrite("BOILER-001", dp));
    writer.WriteCheckpoint();
    writer.Close();

    WalReader reader(wal_path_);
    ASSERT_TRUE(reader.Open());
    ASSERT_EQ(reader.Entries().size(), 2);

    auto& e = reader.Entries()[0];
    EXPECT_EQ(e.type, WalEntryType::DATA_POINT);
    EXPECT_EQ(e.tag_name, "BOILER-001");
    EXPECT_EQ(e.points[0].ts, 1000);
    EXPECT_DOUBLE_EQ(std::get<double>(e.points[0].value), 523.7);
}

TEST_F(WALTest, BatchWrite) {
    WalWriter writer(wal_path_);
    ASSERT_TRUE(writer.Open());

    for (int i = 0; i < 10; i++) {
        DataPoint p;
        p.ts = 2000 + i * 1000;
        p.value = 500.0 + i * 0.5;
        ASSERT_TRUE(writer.AppendWrite("BOILER-002", p));
    }
    writer.WriteCheckpoint();
    writer.Close();

    WalReader reader(wal_path_);
    ASSERT_TRUE(reader.Open());
    EXPECT_GE(reader.Entries().size(), 10);
}

TEST_F(WALTest, MultipleTags) {
    WalWriter writer(wal_path_);
    ASSERT_TRUE(writer.Open());

    DataPoint p1; p1.ts = 100; p1.value = 1.0;
    DataPoint p2; p2.ts = 200; p2.value = 2.0;
    ASSERT_TRUE(writer.AppendWrite("TAG-A", p1));
    ASSERT_TRUE(writer.AppendWrite("TAG-B", p2));
    writer.Close();

    WalReader reader(wal_path_);
    ASSERT_TRUE(reader.Open());
    EXPECT_EQ(reader.Entries().size(), 2);
    EXPECT_EQ(reader.Entries()[0].tag_name, "TAG-A");
    EXPECT_EQ(reader.Entries()[1].tag_name, "TAG-B");
}

TEST_F(WALTest, Truncate) {
    WalWriter writer(wal_path_);
    ASSERT_TRUE(writer.Open());
    DataPoint p; p.ts = 1; p.value = 1.0;
    writer.AppendWrite("T", p);
    writer.Close();

    EXPECT_TRUE(WalReader::Truncate(wal_path_));
    // 截断后读取应失败（文件不存在）
    WalReader reader(wal_path_);
    EXPECT_FALSE(reader.Open());
}
