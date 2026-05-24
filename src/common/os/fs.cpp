#include "common/os/fs.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fileapi.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#endif

namespace minitsdb {
namespace os {
namespace fs {

bool CreateDirectories(const std::string& path) {
    if (path.empty()) return false;

#ifdef _WIN32
    // 先检查是否已存在
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    // 逐级创建
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            if (!cur.empty()) {
                CreateDirectoryA(cur.c_str(), nullptr);
            }
        }
        cur += c;
    }
    if (!cur.empty()) {
        if (!CreateDirectoryA(cur.c_str(), nullptr)) {
            DWORD err = GetLastError();
            return err == ERROR_ALREADY_EXISTS;
        }
    }
    return true;
#else
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        char c = path[i];
        if (c == '/') {
            if (!cur.empty()) {
                ::mkdir(cur.c_str(), 0755);
            }
        }
        cur += c;
    }
    if (!cur.empty()) {
        if (::mkdir(cur.c_str(), 0755) != 0) {
            return errno == EEXIST;
        }
    }
    return true;
#endif
}

bool Remove(const std::string& path) {
#ifdef _WIN32
    // 先检查是文件还是目录
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;  // 不存在也算成功
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return RemoveDirectoryA(path.c_str()) != 0;
    else
        return DeleteFileA(path.c_str()) != 0;
#else
    if (::remove(path.c_str()) != 0 && errno != ENOENT)
        return false;
    return true;
#endif
}

bool Rename(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool Exists(const std::string& path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    return ::access(path.c_str(), F_OK) == 0;
#endif
}

bool Copy(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return ::CopyFileA(from.c_str(), to.c_str(), FALSE) != 0;
#else
    int src = ::open(from.c_str(), O_RDONLY);
    if (src < 0) return false;

    int dst = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        ::close(src);
        return false;
    }

    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t r = ::write(dst, buf + written, static_cast<size_t>(n - written));
            if (r <= 0) { ok = false; break; }
            written += r;
        }
        if (!ok) break;
    }
    if (n < 0) ok = false;

    // 保留文件权限
    struct stat st;
    if (::fstat(src, &st) == 0) {
        ::fchmod(dst, st.st_mode);
    }

    ::close(src);
    ::close(dst);
    return ok;
#endif
}

bool IsEmpty(const std::string& path) {
    if (!Exists(path)) return true;  // 不存在即为空

#ifdef _WIN32
    std::string pattern = path + "/*";
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        if (strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
    return empty;
#else
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return true;
    bool empty = true;
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    ::closedir(dir);
    return empty;
#endif
}

int64_t FileSize(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info))
        return -1;
    LARGE_INTEGER li;
    li.LowPart = info.nFileSizeLow;
    li.HighPart = static_cast<LONG>(info.nFileSizeHigh);
    return static_cast<int64_t>(li.QuadPart);
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<int64_t>(st.st_size);
#endif
}

namespace detail {

// FileTime 转 epoch 毫秒
int64_t FileTimeToMs(uint32_t low, uint32_t high) {
    // Windows FILETIME: 1601-01-01 以来的 100ns 间隔
    // epoch: 1970-01-01，差 11644473600 秒
    LARGE_INTEGER li;
    li.LowPart = low;
    li.HighPart = static_cast<LONG>(high);
    constexpr int64_t kUnixEpochDiff = 116444736000000000LL;
    return (li.QuadPart - kUnixEpochDiff) / 10000;
}

} // namespace detail

int64_t LastWriteTimeMs(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info))
        return -1;
    return detail::FileTimeToMs(info.ftLastWriteTime.dwLowDateTime,
                                info.ftLastWriteTime.dwHighDateTime);
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    // st_mtime 是秒，转为毫秒
    return static_cast<int64_t>(st.st_mtime) * 1000;
#endif
}

bool ListDirectory(const std::string& path, std::vector<DirEntry>& entries) {
    entries.clear();

#ifdef _WIN32
    std::string pattern = path + "\\*";
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;

        DirEntry entry;
        entry.name = ffd.cFileName;
        entry.path = path + "\\" + ffd.cFileName;
        entry.is_directory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!entry.is_directory) {
            LARGE_INTEGER li;
            li.LowPart = ffd.nFileSizeLow;
            li.HighPart = static_cast<LONG>(ffd.nFileSizeHigh);
            entry.file_size = static_cast<int64_t>(li.QuadPart);
        } else {
            entry.file_size = 0;
        }
        entry.last_write_time_ms = detail::FileTimeToMs(
            ffd.ftLastWriteTime.dwLowDateTime,
            ffd.ftLastWriteTime.dwHighDateTime);
        entries.push_back(std::move(entry));
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return true;
#else
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return false;

    struct dirent* dent;
    while ((dent = ::readdir(dir)) != nullptr) {
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
            continue;

        DirEntry entry;
        entry.name = dent->d_name;
        entry.path = path + "/" + dent->d_name;

        struct stat st;
        if (::stat(entry.path.c_str(), &st) == 0) {
            entry.is_directory = S_ISDIR(st.st_mode);
            entry.file_size = entry.is_directory ? 0 : static_cast<int64_t>(st.st_size);
            entry.last_write_time_ms = static_cast<int64_t>(st.st_mtime) * 1000;
        } else {
            entry.is_directory = (dent->d_type == DT_DIR);
            entry.file_size = 0;
            entry.last_write_time_ms = 0;
        }

        entries.push_back(std::move(entry));
    }

    ::closedir(dir);
    return true;
#endif
}

bool RemoveAll(const std::string& path) {
    if (!Exists(path)) return true;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        std::vector<DirEntry> entries;
        ListDirectory(path, entries);
        for (const auto& e : entries) {
            RemoveAll(e.path);
        }
        return RemoveDirectoryA(path.c_str()) != 0;
    } else {
        return DeleteFileA(path.c_str()) != 0;
    }
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return true;
    if (S_ISDIR(st.st_mode)) {
        std::vector<DirEntry> entries;
        ListDirectory(path, entries);
        for (const auto& e : entries) {
            RemoveAll(e.path);
        }
        return ::rmdir(path.c_str()) == 0;
    } else {
        return ::remove(path.c_str()) == 0;
    }
#endif
}

} // namespace fs
} // namespace os
} // namespace minitsdb
