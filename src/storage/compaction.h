#pragma once

#include "common/types.h"
#include "storage/sstable.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace minitsdb {

// SSTable 压缩器
// 合并小 SSTable，减少文件数量
class Compaction {
public:
    Compaction(const std::string& hot_path);

    // 执行一次压缩
    // 合并小于 threshold_bytes 的 SSTable
    void RunOnce(size_t threshold_bytes = 1024 * 1024);  // 默认 1MB

    // 启动后台压缩线程
    void Start(int32_t interval_sec = 300);  // 默认 5 分钟

    // 停止后台线程
    void Stop();

private:
    std::string hot_path_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    void WorkerLoop(int32_t interval_sec);

    // 对单个 Tag 目录执行压缩
    void CompactTag(const std::string& tag_name,
                    const std::string& tag_dir,
                    size_t threshold_bytes);
};

} // namespace minitsdb
