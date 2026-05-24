#include "common/os/file.h"

namespace minitsdb {
namespace os {

File::File(File&& other) noexcept {
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
}

File& File::operator=(File&& other) noexcept {
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

bool File::Open(const std::string& path, FileMode mode) {
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

void File::Close() {
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

bool File::Write(const void* data, size_t len) {
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

bool File::Read(void* buf, size_t len, size_t* bytes_read) {
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

bool File::Seek(int64_t offset, int origin) {
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

int64_t File::Tell() {
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

int64_t File::Size() {
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

bool File::Flush() {
#ifdef _WIN32
    return FlushFileBuffers(handle_) != 0;
#else
    if (fd_ < 0) return false;
    return ::fsync(fd_) == 0;
#endif
}

bool File::IsOpen() const {
#ifdef _WIN32
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

void File::Detach() {
#ifdef _WIN32
    handle_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}

} // namespace os
} // namespace minitsdb
