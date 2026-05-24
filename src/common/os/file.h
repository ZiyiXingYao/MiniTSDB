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

// ============================================================
//  inline 实现
// ============================================================

inline File::File(File&& other) noexcept {
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
}

inline File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        Close();
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    return *this;
}

inline bool File::Open(const std::string& path, FileMode mode) {
    if (IsOpen()) Close();

#ifdef _WIN32
    DWORD access = 0;
    DWORD disposition = 0;
    DWORD share_mode = FILE_SHARE_READ;

    switch (mode) {
        case FileMode::READ:
            access = GENERIC_READ;
            disposition = OPEN_EXISTING;
            break;
        case FileMode::WRITE:
            access = GENERIC_WRITE;
            disposition = CREATE_ALWAYS;
            share_mode = 0;
            break;
        case FileMode::APPEND:
            access = GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
            share_mode = 0;
            break;
        case FileMode::READ_WRITE:
            access = GENERIC_READ | GENERIC_WRITE;
            disposition = CREATE_ALWAYS;
            share_mode = 0;
            break;
    }

    handle_ = CreateFileA(
        path.c_str(),
        access,
        share_mode,
        nullptr,
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle_ == INVALID_HANDLE_VALUE) return false;

    // APPEND 模式需要定位到文件末尾
    if (mode == FileMode::APPEND) {
        SetFilePointer(handle_, 0, nullptr, FILE_END);
    }

    return true;
#else
    int flags = 0;
    mode_t file_perm = 0644;

    switch (mode) {
        case FileMode::READ:
            flags = O_RDONLY;
            break;
        case FileMode::WRITE:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case FileMode::APPEND:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
        case FileMode::READ_WRITE:
            flags = O_RDWR | O_CREAT | O_TRUNC;
            break;
    }

    fd_ = ::open(path.c_str(), flags, file_perm);
    return fd_ >= 0;
#endif
}

inline void File::Close() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

inline bool File::Write(const void* data, size_t len) {
#ifdef _WIN32
    DWORD written = 0;
    return WriteFile(handle_, data, static_cast<DWORD>(len), &written, nullptr) &&
           static_cast<size_t>(written) == len;
#else
    if (fd_ < 0) return false;
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n <= 0) return false;
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
#endif
}

inline bool File::Read(void* buf, size_t len, size_t* bytes_read) {
#ifdef _WIN32
    DWORD read = 0;
    BOOL ok = ReadFile(handle_, buf, static_cast<DWORD>(len), &read, nullptr);
    if (bytes_read) *bytes_read = static_cast<size_t>(read);
    return ok == TRUE;
#else
    if (fd_ < 0) return false;
    ssize_t n = ::read(fd_, buf, len);
    if (n < 0) return false;
    if (bytes_read) *bytes_read = static_cast<size_t>(n);
    return true;
#endif
}

inline bool File::Seek(int64_t offset, int origin) {
#ifdef _WIN32
    DWORD move_method = FILE_BEGIN;
    if (origin == SEEK_CUR) move_method = FILE_CURRENT;
    else if (origin == SEEK_END) move_method = FILE_END;

    LARGE_INTEGER li;
    li.QuadPart = offset;
    return SetFilePointerEx(handle_, li, nullptr, move_method) != 0;
#else
    if (fd_ < 0) return false;
    return ::lseek(fd_, offset, origin) >= 0;
#endif
}

inline int64_t File::Tell() {
#ifdef _WIN32
    LARGE_INTEGER li = {};
    LARGE_INTEGER result = {};
    if (!SetFilePointerEx(handle_, li, &result, FILE_CURRENT))
        return kInvalidOffset;
    return static_cast<int64_t>(result.QuadPart);
#else
    if (fd_ < 0) return kInvalidOffset;
    off_t pos = ::lseek(fd_, 0, SEEK_CUR);
    return pos >= 0 ? static_cast<int64_t>(pos) : kInvalidOffset;
#endif
}

inline int64_t File::Size() {
#ifdef _WIN32
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(handle_, &size))
        return kInvalidOffset;
    return static_cast<int64_t>(size.QuadPart);
#else
    if (fd_ < 0) return kInvalidOffset;
    struct stat st;
    if (::fstat(fd_, &st) != 0)
        return kInvalidOffset;
    return static_cast<int64_t>(st.st_size);
#endif
}

inline bool File::Flush() {
#ifdef _WIN32
    return FlushFileBuffers(handle_) != 0;
#else
    if (fd_ < 0) return false;
    return ::fsync(fd_) == 0;
#endif
}

inline bool File::IsOpen() const {
#ifdef _WIN32
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

inline void File::Detach() {
#ifdef _WIN32
    handle_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}

} // namespace os
} // namespace minitsdb
