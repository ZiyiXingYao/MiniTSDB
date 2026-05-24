#pragma once

#include <cstdint>
#include <string>
#include <cerrno>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#endif

namespace minitsdb {
namespace os {

enum class FileMode {
    READ,        // 只读，文件必须存在
    WRITE,       // 写入，创建/截断
    APPEND,      // 追加写入，创建/追加
    READ_WRITE   // 读写，创建（如不存在）
};

// RAII 风格的操作系统级文件句柄包装
// Windows: CreateFile/WriteFile/ReadFile/FlushFileBuffers
// Linux:   open/write/read/fsync
class File {
public:
    static constexpr int64_t kInvalidOffset = -1;

    File() = default;
    ~File() { Close(); }

    // 可移动，不可拷贝
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // 打开文件
    bool Open(const std::string& path, FileMode mode);

    // 关闭文件（析构时自动调用）
    void Close();

    // 写入 len 字节，返回 true 表示全部写入成功
    bool Write(const void* data, size_t len);

    // 读取最多 len 字节。bytes_read 可选，返回实际读取的字节数
    bool Read(void* buf, size_t len, size_t* bytes_read = nullptr);

    // 定位文件指针。origin: SEEK_SET=0, SEEK_CUR=1, SEEK_END=2
    bool Seek(int64_t offset, int origin);

    // 获取当前文件指针位置，失败返回 kInvalidOffset
    int64_t Tell();

    // 获取文件大小，失败返回 kInvalidOffset
    int64_t Size();

    // OS 级刷盘：FlushFileBuffers (Win) / fsync (Linux)
    bool Flush();

    // 是否已打开
    bool IsOpen() const;

    // 释放句柄所有权（不再负责关闭）
    void Detach();

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

} // namespace os
} // namespace minitsdb
