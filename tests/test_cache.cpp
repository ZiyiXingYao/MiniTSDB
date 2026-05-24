#include <gtest/gtest.h>
#include "cache/latest_cache.h"

using namespace minitsdb;

TEST(LatestCacheTest, UpdateAndGet) {
    LatestCache cache;

    DataPoint dp;
    dp.ts = 1000;
    dp.value = 42.0;
    cache.Update("TAG-1", dp);

    DataPoint out;
    EXPECT_TRUE(cache.Get("TAG-1", out));
    EXPECT_EQ(out.ts, 1000);
    EXPECT_DOUBLE_EQ(std::get<double>(out.value), 42.0);

    EXPECT_FALSE(cache.Get("NONEXIST", out));
}

TEST(LatestCacheTest, NewerOverwrites) {
    LatestCache cache;

    DataPoint old_dp;
    old_dp.ts = 100;
    old_dp.value = 10.0;
    cache.Update("T", old_dp);

    DataPoint new_dp;
    new_dp.ts = 200;
    new_dp.value = 20.0;
    cache.Update("T", new_dp);

    DataPoint out;
    cache.Get("T", out);
    EXPECT_EQ(out.ts, 200);
    EXPECT_DOUBLE_EQ(std::get<double>(out.value), 20.0);
}

TEST(LatestCacheTest, OlderDoesNotOverwrite) {
    LatestCache cache;

    DataPoint new_dp;
    new_dp.ts = 200;
    new_dp.value = 20.0;
    cache.Update("T", new_dp);

    DataPoint old_dp;
    old_dp.ts = 100;
    old_dp.value = 10.0;
    cache.Update("T", old_dp);

    DataPoint out;
    cache.Get("T", out);
    EXPECT_EQ(out.ts, 200);
    EXPECT_DOUBLE_EQ(std::get<double>(out.value), 20.0);
}

TEST(LatestCacheTest, LikePatternMatching) {
    LatestCache cache;

    for (int i = 1; i <= 5; i++) {
        DataPoint dp;
        dp.ts = i * 1000;
        dp.value = i * 1.0;
        cache.Update("BOILER-00" + std::to_string(i), dp);
    }
    cache.Update("PUMP-001", {6000, 6.0});

    auto results = cache.GetByPattern("BOILER-%");
    EXPECT_EQ(results.size(), 5);

    results = cache.GetByPattern("%-001");
    EXPECT_EQ(results.size(), 2);

    results = cache.GetByPattern("BOILER-___");  // _ wildcard (3 chars)
    EXPECT_EQ(results.size(), 5);
}

TEST(LatestCacheTest, GetAll) {
    LatestCache cache;
    cache.Update("A", {1, 1.0});
    cache.Update("B", {2, 2.0});

    auto all = cache.GetAll();
    EXPECT_EQ(all.size(), 2);
}

TEST(LatestCacheTest, Remove) {
    LatestCache cache;
    cache.Update("X", {1, 1.0});
    cache.Remove("X");
    DataPoint out;
    EXPECT_FALSE(cache.Get("X", out));
}
