#include <gtest/gtest.h>
#include "snapshot/snapshot_store.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

static void RemoveDir(const std::string& path) {
#if defined(_WIN32)
    std::string cmd = "rmdir /s /q \"" + path + "\" 2>nul";
    system(cmd.c_str());
#else
    std::string cmd = "rm -rf \"" + path + "\" 2>/dev/null";
    system(cmd.c_str());
#endif
}

class SnapshotStoreTest : public ::testing::Test {
protected:
    std::string test_dir_ = "./test_snapshot_data";

    void SetUp() override {
        RemoveDir(test_dir_);
    }

    void TearDown() override {
        RemoveDir(test_dir_);
    }
};

TEST_F(SnapshotStoreTest, InitAndWrite) {
    minitsdb::SnapshotStore store;
    ASSERT_TRUE(store.Init(test_dir_));

    store.OnWrite("test-tag", minitsdb::DataPoint{1000, 42.5});

    minitsdb::CachedSnapshot entry;
    ASSERT_TRUE(store.Get("test-tag", entry));
    ASSERT_EQ(entry.timestamp, 1000);
    ASSERT_DOUBLE_EQ(entry.value, 42.5);
    ASSERT_TRUE(entry.valid);

    store.Shutdown();
}

TEST_F(SnapshotStoreTest, GetNonExistent) {
    minitsdb::SnapshotStore store;
    ASSERT_TRUE(store.Init(test_dir_));

    minitsdb::CachedSnapshot entry;
    ASSERT_FALSE(store.Get("nonexistent", entry));

    store.Shutdown();
}

TEST_F(SnapshotStoreTest, PersistAndLoad) {
    {
        minitsdb::SnapshotStore store;
        store.Init(test_dir_);
        store.OnWrite("tag-1", minitsdb::DataPoint{1000, 1.0});
        store.OnWrite("tag-2", minitsdb::DataPoint{2000, 2.0});
        store.Shutdown();
    }

    {
        minitsdb::SnapshotStore store;
        store.Init(test_dir_);
        ASSERT_EQ(store.Count(), size_t(2));

        minitsdb::CachedSnapshot e;
        ASSERT_TRUE(store.Get("tag-1", e));
        ASSERT_DOUBLE_EQ(e.value, 1.0);
        ASSERT_EQ(e.timestamp, 1000);

        ASSERT_TRUE(store.Get("tag-2", e));
        ASSERT_DOUBLE_EQ(e.value, 2.0);
        ASSERT_EQ(e.timestamp, 2000);

        store.Shutdown();
    }
}

TEST_F(SnapshotStoreTest, PatternMatchPercent) {
    minitsdb::SnapshotStore store;
    store.Init(test_dir_);

    store.OnWrite("BOILER-001", minitsdb::DataPoint{1000, 520.0});
    store.OnWrite("BOILER-002", minitsdb::DataPoint{2000, 530.0});
    store.OnWrite("TURBINE-001", minitsdb::DataPoint{3000, 3000.0});

    auto results = store.GetByPattern("BOILER-%");
    ASSERT_EQ(results.size(), size_t(2));

    std::vector<std::string> tags;
    for (const auto& e : results) tags.push_back(e.tag);
    std::sort(tags.begin(), tags.end());
    ASSERT_EQ(tags[0], "BOILER-001");
    ASSERT_EQ(tags[1], "BOILER-002");

    store.Shutdown();
}

TEST_F(SnapshotStoreTest, PatternMatchUnderscore) {
    minitsdb::SnapshotStore store;
    store.Init(test_dir_);

    store.OnWrite("TAG-001", minitsdb::DataPoint{1000, 1.0});
    store.OnWrite("TAG-002", minitsdb::DataPoint{2000, 2.0});
    store.OnWrite("TAG-X01", minitsdb::DataPoint{3000, 3.0});

    auto results = store.GetByPattern("TAG-00_");
    ASSERT_EQ(results.size(), size_t(2));

    store.Shutdown();
}

TEST_F(SnapshotStoreTest, Count) {
    minitsdb::SnapshotStore store;
    store.Init(test_dir_);
    ASSERT_EQ(store.Count(), size_t(0));

    store.OnWrite("tag-1", minitsdb::DataPoint{1000, 1.0});
    store.OnWrite("tag-2", minitsdb::DataPoint{2000, 2.0});
    ASSERT_EQ(store.Count(), size_t(2));

    store.OnWrite("tag-3", minitsdb::DataPoint{3000, 3.0});
    ASSERT_EQ(store.Count(), size_t(3));

    store.Shutdown();
}

TEST_F(SnapshotStoreTest, GetAll) {
    minitsdb::SnapshotStore store;
    store.Init(test_dir_);

    store.OnWrite("tag-a", minitsdb::DataPoint{1000, 10.0});
    store.OnWrite("tag-b", minitsdb::DataPoint{2000, 20.0});

    auto all = store.GetAll();
    ASSERT_EQ(all.size(), size_t(2));

    store.Shutdown();
}
