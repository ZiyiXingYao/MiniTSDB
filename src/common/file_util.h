#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace minitsdb {
namespace file {

// 打开文件写入
inline FILE* OpenWrite(const std::string& path, bool append = false) {
#ifdef _WIN32
    return fopen(path.c_str(), append ? "ab" : "wb");
#else
    return fopen(path.c_str(), append ? "ab" : "wb");
#endif
}

// 打开文件读取
inline FILE* OpenRead(const std::string& path) {
    return fopen(path.c_str(), "rb");
}

// 写入数据
inline bool Write(FILE* f, const void* data, size_t len) {
    return fwrite(data, 1, len, f) == len;
}

// 读取数据
inline bool Read(FILE* f, void* data, size_t len) {
    return fread(data, 1, len, f) == len;
}

// 获取文件大小
inline size_t Size(FILE* f) {
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
    fseek(f, pos, SEEK_SET);
    return size;
}

// 跳过字节
inline bool Skip(FILE* f, size_t len) {
    return fseek(f, static_cast<long>(len), SEEK_CUR) == 0;
}

// 定位
inline bool Seek(FILE* f, size_t pos) {
    return fseek(f, static_cast<long>(pos), SEEK_SET) == 0;
}

// 获取当前位置
inline size_t Tell(FILE* f) {
    return static_cast<size_t>(ftell(f));
}

// 关闭文件
inline void Close(FILE* f) {
    if (f) fclose(f);
}

// 创建目录
inline bool CreateDirs(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// 删除文件
inline bool Remove(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

// 重命名
inline bool Rename(const std::string& from, const std::string& to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}

// 检查文件存在
inline bool Exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

} // namespace file
} // namespace minitsdb
