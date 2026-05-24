#include <gtest/gtest.h>
#include "common/os/file.h"
#include "common/os/fs.h"
#include <string>
#include <cstring>

using namespace minitsdb::os;
using namespace minitsdb::os::fs;

class OsFileTest : public ::testing::Test {
protected:
    std::string test_dir_ = "test_os";
    std::string test_file_ = "test_os/test_file.bin";

    void SetUp() override {
        // 递归清理并重建
        RemoveAll(test_dir_);
        CreateDirectories(test_dir_);
    }

    void TearDown() override {
        RemoveAll(test_dir_);
    }
};

// ============================================================
//  os::File 测试
// ============================================================

TEST_F(OsFileTest, WriteAndRead) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
    const char* data = "Hello OS File!";
    ASSERT_TRUE(f.Write(data, 14));
    EXPECT_EQ(f.Tell(), 14);
    EXPECT_EQ(f.Size(), 14);
    f.Close();
    EXPECT_FALSE(f.IsOpen());

    File rf;
    ASSERT_TRUE(rf.Open(test_file_, FileMode::READ));
    char buf[32] = {0};
    ASSERT_TRUE(rf.Read(buf, 14));
    EXPECT_STREQ(buf, "Hello OS File!");
}

TEST_F(OsFileTest, AppendMode) {
    // 先写初始数据
    {
        File f;
        ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
        f.Write("Hello", 5);
    }

    // 追加
    {
        File f;
        ASSERT_TRUE(f.Open(test_file_, FileMode::APPEND));
        EXPECT_EQ(f.Size(), 5);
        f.Write(" World", 6);
        EXPECT_EQ(f.Size(), 11);
    }

    // 验证
    {
        File f;
        ASSERT_TRUE(f.Open(test_file_, FileMode::READ));
        char buf[16] = {0};
        ASSERT_TRUE(f.Read(buf, 11));
        EXPECT_STREQ(buf, "Hello World");
    }
}

TEST_F(OsFileTest, SeekAndTell) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
    const char* data = "0123456789";
    f.Write(data, 10);

    // Seek from beginning
    f.Seek(5, SEEK_SET);
    EXPECT_EQ(f.Tell(), 5);

    // Seek from current
    f.Seek(2, SEEK_CUR);
    EXPECT_EQ(f.Tell(), 7);

    // Seek from end
    f.Seek(-3, SEEK_END);
    EXPECT_EQ(f.Tell(), 7);
}

TEST_F(OsFileTest, FileSize) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
    EXPECT_EQ(f.Size(), 0);
    f.Write("12345", 5);
    EXPECT_EQ(f.Size(), 5);
}

TEST_F(OsFileTest, MoveSemantics) {
    File f1;
    ASSERT_TRUE(f1.Open(test_file_, FileMode::WRITE));
    f1.Write("test", 4);

    File f2(std::move(f1));
    EXPECT_FALSE(f1.IsOpen());
    EXPECT_TRUE(f2.IsOpen());
    EXPECT_EQ(f2.Tell(), 4);
    f2.Close();

    File f3;
    f3 = std::move(f2);
    EXPECT_FALSE(f2.IsOpen());
    EXPECT_FALSE(f3.IsOpen());  // f2 was already closed
}

TEST_F(OsFileTest, ReadWriteMode) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::READ_WRITE));
    f.Write("ABCD", 4);
    f.Seek(0, SEEK_SET);
    char buf[5] = {0};
    ASSERT_TRUE(f.Read(buf, 4));
    EXPECT_STREQ(buf, "ABCD");
}

TEST_F(OsFileTest, PartialRead) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
    f.Write("Hello World", 11);
    f.Close();

    ASSERT_TRUE(f.Open(test_file_, FileMode::READ));
    char buf[32] = {0};
    size_t bytes_read = 0;
    ASSERT_TRUE(f.Read(buf, 5, &bytes_read));
    EXPECT_EQ(bytes_read, 5);
    EXPECT_EQ(std::memcmp(buf, "Hello", 5), 0);
}

TEST_F(OsFileTest, FlushSuccess) {
    File f;
    ASSERT_TRUE(f.Open(test_file_, FileMode::WRITE));
    f.Write("data", 4);
    EXPECT_TRUE(f.Flush());
}

TEST_F(OsFileTest, NonExistentFile) {
    File f;
    EXPECT_FALSE(f.Open("nonexistent_dir/nonexistent_file.bin", FileMode::READ));
    EXPECT_FALSE(f.IsOpen());
}

// ============================================================
//  os::fs 测试
// ============================================================

TEST_F(OsFileTest, CreateDirectories) {
    std::string nested = test_dir_ + "/a/b/c";
    EXPECT_TRUE(CreateDirectories(nested));
    EXPECT_TRUE(Exists(nested));
}

TEST_F(OsFileTest, RemoveFile) {
    {
        File f;
        f.Open(test_dir_ + "/to_delete.txt", FileMode::WRITE);
        f.Write("delete me", 9);
    }
    EXPECT_TRUE(Remove(test_dir_ + "/to_delete.txt"));
    EXPECT_FALSE(Exists(test_dir_ + "/to_delete.txt"));
}

TEST_F(OsFileTest, RemoveDirectory) {
    std::string subdir = test_dir_ + "/subdir";
    CreateDirectories(subdir);
    EXPECT_TRUE(Exists(subdir));
    EXPECT_TRUE(Remove(subdir));
    EXPECT_FALSE(Exists(subdir));
}

TEST_F(OsFileTest, RenameFile) {
    std::string src = test_dir_ + "/src.txt";
    std::string dst = test_dir_ + "/dst.txt";
    {
        File f;
        f.Open(src, FileMode::WRITE);
        f.Write("rename test", 11);
    }
    EXPECT_TRUE(Rename(src, dst));
    EXPECT_FALSE(Exists(src));
    EXPECT_TRUE(Exists(dst));
}

TEST_F(OsFileTest, FileExists) {
    EXPECT_FALSE(Exists(test_dir_ + "/nonexistent"));
    ASSERT_TRUE(CreateDirectories(test_dir_ + "/exist_dir"));
    EXPECT_TRUE(Exists(test_dir_ + "/exist_dir"));
}

TEST_F(OsFileTest, CopyFile) {
    std::string src = test_dir_ + "/src.bin";
    std::string dst = test_dir_ + "/dst.bin";
    {
        File f;
        f.Open(src, FileMode::WRITE);
        f.Write("copy content", 12);
    }
    EXPECT_TRUE(Copy(src, dst));
    EXPECT_TRUE(Exists(dst));

    // 验证内容一致
    File rf;
    rf.Open(dst, FileMode::READ);
    char buf[32] = {0};
    rf.Read(buf, 12);
    EXPECT_STREQ(buf, "copy content");
}

TEST_F(OsFileTest, IsEmpty) {
    // 空目录
    EXPECT_TRUE(IsEmpty(test_dir_ + "/empty_dir"));
    CreateDirectories(test_dir_ + "/empty_dir");
    EXPECT_TRUE(IsEmpty(test_dir_ + "/empty_dir"));

    // 非空目录
    {
        File f;
        f.Open(test_dir_ + "/empty_dir/file.txt", FileMode::WRITE);
        f.Write("x", 1);
    }
    EXPECT_FALSE(IsEmpty(test_dir_ + "/empty_dir"));
}

TEST_F(OsFileTest, ListDirectory) {
    // 创建几个文件
    File f1, f2;
    f1.Open(test_dir_ + "/a.txt", FileMode::WRITE);
    f1.Write("aaa", 3);
    f1.Close();
    f2.Open(test_dir_ + "/b.txt", FileMode::WRITE);
    f2.Write("bbb", 3);
    f2.Close();

    std::vector<DirEntry> entries;
    EXPECT_TRUE(ListDirectory(test_dir_, entries));
    EXPECT_GE(entries.size(), 2);

    bool found_a = false, found_b = false;
    for (const auto& e : entries) {
        if (e.name == "a.txt") found_a = true;
        if (e.name == "b.txt") found_b = true;
        EXPECT_FALSE(e.name.empty());
        EXPECT_NE(e.path, "");
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(OsFileTest, LastWriteTime) {
    std::string path = test_dir_ + "/timestamp_test.txt";
    {
        File f;
        f.Open(path, FileMode::WRITE);
        f.Write("test", 4);
    }
    int64_t mtime = LastWriteTimeMs(path);
    EXPECT_GT(mtime, 0);
    EXPECT_LT(mtime, 2000000000000LL);  // 合理的时间范围
}

TEST_F(OsFileTest, FileSizeFs) {
    {
        File f;
        f.Open(test_dir_ + "/size_test.txt", FileMode::WRITE);
        f.Write("1234567890", 10);
    }
    EXPECT_EQ(FileSize(test_dir_ + "/size_test.txt"), 10);
    EXPECT_EQ(FileSize(test_dir_ + "/nonexistent"), -1);
}
