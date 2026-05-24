# MiniTSDB OS 文件 IO 抽象层实现计划

> 创建时间：2026-05-24
> 
> 目标：创建 `src/common/os/` 目录，提供基于 OS 原生 API 的 `os::File` 和 `os::fs` 两套抽象，替换项目中所有 `std::fstream`/`std::filesystem` 的直接使用。

---

## 执行摘要

本计划分 8 个阶段共 14 个任务，按依赖关系排列。核心策略是：**先创建抽象层，再从最底层模块（WAL、SSTable）向上替换**，最后替换文件系统操作和工具函数。每个模块替换时保持接口兼容，确保编译通过。

**基准信息：**
- 编译器：Clang 22 (x86_64-pc-windows-msvc)
- 构建：CMake + Ninja，仅 Release
- MSVC STL 静态链接，需 `_ALLOW_COMPILER_IN_STL_FUNCTIONS`
- 项目命名空间：`minitsdb`，新抽象层位于 `minitsdb::os` 子命名空间

---

## 阶段 1：创建核心抽象层

### 任务 1.1：创建 `src/common/os/` 目录结构和 `file.h`

**文件：** `src/common/os/file.h`（新建）

**设计说明：**
- 使用 C++ 头文件，`#ifdef _WIN32` / `#else` 内部平台分支
- 关键操作为 inline 函数（与项目 `file_util.h` 风格一致），避免额外 .cpp 编译单元
- 使用 `#pragma once` 保护

**核心接口：**

```cpp
#pragma once

#include <string>
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace minitsdb::os {

enum class FileMode { READ, WRITE, APPEND, READ_WRITE };

class File {
public:
    File() = default;
    ~File();

    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool Open(const std::string& path, FileMode mode);
    void Close();
    bool Write(const void* data, size_t len);
    bool Read(void* buf, size_t len, size_t* bytes_read = nullptr);
    bool Seek(int64_t offset, int origin);
    int64_t Tell();
    int64_t Size();
    bool Flush();  // OS-level: FlushFileBuffers / fsync
    bool IsOpen() const;
    const std::string& Path() const { return path_; }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    std::string path_;
    bool is_open_ = false;
};

} // namespace minitsdb::os
```

**关键实现要点（Win32 部分）：**
- `Open`: 根据 FileMode 映射 GENERIC_READ/GENERIC_WRITE 和 CREATE_ALWAYS/OPEN_EXISTING/OPEN_ALWAYS
- `Flush`: `FlushFileBuffers(handle_)` -- 这是解决 WAL 不落盘问题的关键
- `Write`: `WriteFile(handle_, data, len, &written, nullptr)` + `written == len`
- `Read`: `ReadFile(handle_, buf, len, &read, nullptr)`，bytes_read 非空时允许多次读取
- `Size`: `GetFileSizeEx(handle_, &size)`
- `Seek/Tell`: `SetFilePointerEx`

**Linux 部分：**
- `Open`: `::open()` + `O_CREAT|O_WRONLY|O_RDONLY|O_RDWR` + `S_IRUSR|S_IWUSR`
- `Flush`: `::fsync(fd_)`
- `Read/Write`: `::read()` / `::write()`
- `Size`: `::lseek(fd_, 0, SEEK_END)` + restore position

**移动语义：** 转移 handle/fd，源对象置为 INVALID/关闭状态

---

### 任务 1.2：创建 `src/common/os/fs.h`

**文件：** `src/common/os/fs.h`（新建）

**接口：**

```cpp
namespace minitsdb::os::fs {

struct DirEntry {
    std::string path;
    std::string name;
    bool is_directory;
    int64_t file_size;           // -1 for directories
    int64_t last_write_time_ms;  // epoch milliseconds
};

bool CreateDirectories(const std::string& path);
bool Remove(const std::string& path);
bool Rename(const std::string& from, const std::string& to);
bool Exists(const std::string& path);
bool CopyFile(const std::string& from, const std::string& to);
bool IsEmpty(const std::string& path);
int64_t FileSize(const std::string& path);
bool ListDirectory(const std::string& path, std::vector<DirEntry>& entries);
int64_t LastWriteTimeMs(const std::string& path);

} // namespace minitsdb::os::fs
```

**关键 Win32 实现：**
- `CreateDirectories`: 逐级 CreateDirectoryA
- `Remove`: 先 DeleteFileA，失败则 RemoveDirectoryA
- `Rename`: MoveFileExA with MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED
- `ListDirectory`: FindFirstFileA/FindNextFileA，转换 FILETIME 到 epoch ms
- `LastWriteTimeMs`: GetFileAttributesExA + 转换 FILETIME

---

## 阶段 2：替换 WAL 模块

### 任务 2.1：修改 `wal.h`

- 删除 `#include <fstream>`
- 添加 `#include "common/os/file.h"`
- `WalWriter`: `std::ofstream file_` -> `os::File file_`
- `WalReader::ReadEntry` 参数: `std::ifstream&` -> `os::File&`

### 任务 2.2：修改 `wal.cpp`

- `WalWriter::Open`: `file_.open(...)` -> `file_.Open(path, FileMode::APPEND)`
- `WalWriter::WriteRaw`: `file_.write(...)` -> `file_.Write(data, len)`
- `WalWriter::Flush`: `file_.flush()` -> `file_.Flush()` (关键：现在刷到物理磁盘)
- `WalWriter::Size`: `tellp()` -> `file_.Size()`
- `WalWriter::Close`: `file_.close()` -> `file_.Close()`
- `WalReader::Open`: 局部 `std::ifstream` -> `os::File`; `peek()` 改为 `Tell() < Size()` 循环
- `WalReader::ReadEntry`: `file.read()` -> `file.Read()` + bytes_read 检查
- `WalReader::Truncate`: `std::remove()` -> `os::fs::Remove()`

---

## 阶段 3：替换 SSTable 模块

### 任务 3.1：修改 `sstable.h`

- 删除 `#include <fstream>`
- 添加 `#include "common/os/file.h"`
- `SSTableWriter`: `std::fstream file_` -> `os::File file_` (READ_WRITE 模式)
- `SSTableReader`: `std::ifstream file_` -> `os::File file_` (READ 模式)

### 任务 3.2：修改 `sstable.cpp`

- `SSTableWriter::Open`: `file_.open(... trunc | in | out)` -> `file_.Open(path, FileMode::READ_WRITE)`
- `SSTableWriter::AddBlock`: `tellp()` -> `Tell()`, `write()` -> `Write()`
- `SSTableWriter::Close`:
  - `tellp()` -> `Tell()`
  - `seekp(16, beg)` -> `Seek(16, SEEK_SET)`
  - **CRC 计算优化**：原实现读整个文件到 `vector<uint8_t>`，改为分块 64KB 增量更新 CRC
- `SSTableReader::Open`: `file_.open(...)` -> `file_.Open(path, FileMode::READ)`
- `SSTableReader`: 所有 `read()`/`seekg()`/`tellg()`/`close()` -> 对应的 `Read()`/`Seek()`/`Tell()`/`Close()`

---

## 阶段 4：替换 Logger 模块

### 任务 4.1：修改 `logger.h`

- 删除 `#include <fstream>` 和 `#include <filesystem>`
- 添加 `#include "common/os/file.h"` 和 `#include "common/os/fs.h"`
- 删除 `namespace fs = std::filesystem;`
- `std::ofstream file_` -> `os::File file_`
- 构造函数: 文件操作替换
- `sink_it_`: `Write()` + `Flush()` (OS 级刷盘)
- `Rotate`: `Close()` + `os::fs::Rename()` + 读归档用 `os::File(READ)` + `os::fs::Remove()`
- `CleanupOldFiles`: `os::fs::ListDirectory()` + `os::fs::LastWriteTimeMs()` + `os::fs::Remove()`

---

## 阶段 5：替换文件系统操作模块

### 任务 5.1：`compaction.cpp`
- 删除 `#include <filesystem>`
- 添加 `#include "common/os/fs.h"`
- `fs::exists` -> `os::fs::Exists`
- `fs::directory_iterator` -> `os::fs::ListDirectory` + 遍历 DirEntry 数组
- `entry.file_size()` -> `entry.file_size`
- `entry.is_directory()` -> `entry.is_directory`
- `entry.path().filename().string()` -> `entry.name`
- `entry.path().extension() == ".sst"` -> 手动检查文件名后缀
- `fs::rename` -> `os::fs::Rename`
- `fs::remove` -> `os::fs::Remove`

### 任务 5.2：`tier_manager.cpp`
- 同样的替换模式
- 时间比较：`fs::file_time_type::clock::now()` -> `system_clock::now()` 毫秒
- `last_write_time()` -> `os::fs::LastWriteTimeMs()`
- `create_directories` -> `os::fs::CreateDirectories`
- `is_empty` -> `os::fs::IsEmpty`
- `copy_file` -> `os::fs::CopyFile`

### 任务 5.3：`engine.cpp`
- 同样的 filesystem 替换模式

---

## 阶段 6：替换小型工具模块

### 任务 6.1：`auth_manager.cpp`
- ofstream -> `os::File(WRITE)`
- ifstream -> `os::File(READ)`
- `std::filesystem::create_directories` -> `os::fs::CreateDirectories`

### 任务 6.2：`config.h`
- ifstream -> `os::File(READ)` + 读全文到 string + `std::istringstream` 逐行解析

### 任务 6.3：`cli/main.cpp`
- ifstream -> `os::File(READ)` + 读全文到 string + `std::istringstream` 逐行解析

---

## 阶段 7：处理旧封装层

- `file_util.h`: 添加 `[[deprecated]]` 标记和新文件替代说明，保留现有函数
- CMakeLists.txt: 当前设计为纯 header-only，无需修改

---

## 阶段 8：单元测试

### 任务 8.1：`tests/test_os_file.cpp`
- os::File 测试：WriteAndRead, AppendMode, SeekAndTell, FileSize, FlushSuccess, MoveSemantics, ReadWriteMode
- os::fs 测试：CreateDirectories, RemoveFile, RemoveDirectory, RenameFile, FileExists, CopyFile, IsEmpty, ListDirectory, LastWriteTime, FileSizeFs

### 任务 8.2：回归测试
- `ctest --output-on-failure` 验证所有现有测试通过

---

## 执行顺序

```
阶段 1 (file.h + fs.h) ──→ 阶段 2 (WAL)
                                   │
                                   └──→ 阶段 3 (SSTable) ──→ 阶段 4 (Logger)
                                                                     │
                                                                     ├──→ 阶段 5.1 (compaction)
                                                                     ├──→ 阶段 5.2 (tier_manager)
                                                                     ├──→ 阶段 5.3 (engine)
                                                                     ├──→ 阶段 6.1 (auth_manager)
                                                                     ├──→ 阶段 6.2 (config.h)
                                                                     └──→ 阶段 6.3 (cli/main.cpp)
```

## 风险

1. **CRC 分块计算** - SSTableWriter::Close 原读全部文件到内存，需改为分块 CRC 增量更新防 OOM
2. **逐行读取** - Config/CLI 用 `std::getline` 不可用于 OS 句柄，需读全文到 `istringstream`
3. **时间比较** - tier_manager 用 `fs::file_time_type::clock`，替换后统一毫秒 epoch
4. **跨平台** - Linux 分支用 `#ifdef` 保护，当前仅验证 Windows
