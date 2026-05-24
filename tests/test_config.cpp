#include <gtest/gtest.h>
#include "common/config.h"

using namespace minitsdb;

TEST(ConfigTest, SimpleKeyValue) {
    Config cfg;
    ASSERT_TRUE(cfg.Load("minitsdb.conf"));
    EXPECT_EQ(cfg.Get("log.directory", ""), "./logs");
    EXPECT_EQ(cfg.Get("log.level", ""), "info");
    EXPECT_EQ(cfg.GetInt("log.keep_files", 0), 1000);
}

TEST(ConfigTest, DefaultValues) {
    Config cfg;
    EXPECT_EQ(cfg.Get("nonexistent", "default"), "default");
    EXPECT_EQ(cfg.GetInt("nonexistent", 42), 42);
}

TEST(ConfigTest, Sections) {
    Config cfg;
    ASSERT_TRUE(cfg.Load("minitsdb.conf"));
    EXPECT_EQ(cfg.Get("storage.data_dir", ""), "./data");
    EXPECT_EQ(cfg.GetInt("storage.hot_retention_days", 0), 90);
    EXPECT_EQ(cfg.GetInt("server.port", 0), 8086);
}

TEST(ConfigTest, LoadNonExistent) {
    Config cfg;
    EXPECT_FALSE(cfg.Load("nonexistent_file.conf"));
}
