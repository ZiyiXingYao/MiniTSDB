#pragma once

#include "common/types.h"
#include "common/os/file.h"
#include "common/os/fs.h"
#include <string>
#include <vector>

namespace minitsdb {

// WAL 条目类型
enum class WalEntryType : uint8_t {
    DATA_POINT = 0,      // 数据点写入
    TAG_META = 1,        // 注册测点
    CHECKPOINT = 2       // 检查点标记
};

// WAL 条目头（固定 9 字节）
struct WalEntryHeader {
    uint32_t crc;            // CRC32 校验
    WalEntryType type;       // 条目类型
    uint32_t data_len;       // 数据长度（不含头）
};

// WAL 写入器
class WalWriter {
public:
    explicit WalWriter(const std::string& path);
    ~WalWriter();

    // 初始化（创建/打开 WAL 文件）
    bool Open();

    // 追加写入一个数据点
    bool AppendWrite(const std::string& tag, const DataPoint& point);

    // 追加写入批量数据点
    bool AppendBatch(const std::vector<DataBatch>& batches);

    // 写入检查点标记
    bool WriteCheckpoint();

    // 刷盘
    bool Flush();

    // 关闭
    void Close();

    // 获取当前 WAL 大小
    size_t Size() const;

    // 当前文件路径
    const std::string& Path() const { return path_; }

private:
    std::string path_;
    os::File file_;
    size_t size_ = 0;
    bool opened_ = false;

    bool WriteRaw(const void* data, size_t len);
    uint32_t CalculateCrc(const void* data, size_t len);
};

// WAL 读取器（用于恢复）
class WalReader {
public:
    explicit WalReader(const std::string& path);

    // 打开并读取所有条目
    bool Open();

    // 读取结果
    struct Entry {
        WalEntryType type;
        std::string tag_name;
        std::vector<DataPoint> points;
    };

    // 获取所有条目
    const std::vector<Entry>& Entries() const { return entries_; }

    // 关闭
    void Close();

    // 清空并删除 WAL 文件
    static bool Truncate(const std::string& path);

private:
    std::string path_;
    std::vector<Entry> entries_;

    bool ReadEntry(os::File& file);
};

} // namespace minitsdb
