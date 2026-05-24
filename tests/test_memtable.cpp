#include <gtest/gtest.h>
#include "storage/memtable.h"

using namespace minitsdb;

TEST(MemTableTest, AddAndSize) {
    MemTable mt(1024);
    EXPECT_EQ(mt.TagCount(), 0);
    EXPECT_EQ(mt.Size(), 0);

    DataPoint dp;
    dp.ts = 1000;
    dp.value = 42.0;
    mt.Add("TAG-1", dp);

    EXPECT_EQ(mt.TagCount(), 1);
    EXPECT_GT(mt.Size(), 0);
}

TEST(MemTableTest, FlushCallbackTriggered) {
    MemTable mt(16);  // small threshold to trigger flush
    bool flushed = false;
    mt.SetFlushCallback([&](const std::string& tag, std::vector<DataPoint>&& points) {
        flushed = true;
        EXPECT_EQ(tag, "FLUSH-TAG");
        EXPECT_EQ(points.size(), 1);
    });

    DataPoint dp;
    dp.ts = 2000;
    dp.value = 99.9;
    mt.Add("FLUSH-TAG", dp);

    EXPECT_TRUE(flushed);
}

TEST(MemTableTest, MultipleTags) {
    MemTable mt(65536);
    DataPoint dp;
    dp.ts = 1;
    dp.value = 1.0;

    for (int i = 0; i < 10; i++) {
        dp.value = i;
        mt.Add("TAG-A", dp);
        mt.Add("TAG-B", dp);
    }

    EXPECT_EQ(mt.TagCount(), 2);
}

TEST(MemTableTest, FlushAllAndClear) {
    MemTable mt(65536);
    DataPoint dp;
    dp.ts = 1;
    dp.value = 1.0;

    int flush_count = 0;
    mt.SetFlushCallback([&](const std::string&, std::vector<DataPoint>&&) {
        flush_count++;
    });

    for (int i = 0; i < 5; i++) {
        dp.value = i;
        mt.Add("T", dp);
    }

    mt.FlushAll();
    EXPECT_EQ(flush_count, 1);

    mt.Clear();
    EXPECT_EQ(mt.TagCount(), 0);
}

TEST(MemTableTest, BatchWrite) {
    MemTable mt(65536);
    DataBatch batch;
    batch.tag_name = "BATCH";
    for (int i = 0; i < 10; i++) {
        DataPoint dp;
        dp.ts = i * 1000;
        dp.value = i * 1.5;
        batch.points.push_back(dp);
    }
    mt.AddBatch({batch});

    EXPECT_EQ(mt.TagCount(), 1);
}
