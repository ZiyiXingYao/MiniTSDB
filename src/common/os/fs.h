#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace minitsdb {
namespace os {
namespace fs {

// 目录条目信息
struct DirEntry {
    std::string path;       // 完整路径
    std::string name;       // 文件名（不含目录部分）
    bool is_directory;      // 是否是目录
    int64_t file_size;      // 文件大小（目录为 0）
    int64_t last_write_time_ms;  // 最后修改时间（epoch 毫秒）
};

// 逐级创建目录。path 可以是 "a/b/c" 格式
bool CreateDirectories(const std::string& path);

// 删除文件或空目录
bool Remove(const std::string& path);

// 重命名/移动文件。overwrite=true 时覆盖目标
bool Rename(const std::string& from, const std::string& to);

// 检查文件或目录是否存在
bool Exists(const std::string& path);

// 拷贝文件
bool Copy(const std::string& from, const std::string& to);

// 检查目录是否为空
bool IsEmpty(const std::string& path);

// 获取文件大小，失败返回 -1
int64_t FileSize(const std::string& path);

// 获取文件最后修改时间（epoch 毫秒），失败返回 -1
int64_t LastWriteTimeMs(const std::string& path);

// 列出目录内容，返回 DirEntry 列表
bool ListDirectory(const std::string& path, std::vector<DirEntry>& entries);

// 递归删除文件或目录（目录会先删除所有子内容）
bool RemoveAll(const std::string& path);

} // namespace fs
} // namespace os
} // namespace minitsdb
