#pragma once

#include "common/types.h"
#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace minitsdb {

// 冷热分层管理器
class TierManager {
public:
    TierManager(const std::string& hot_path,
                const std::string& cold_path,
                const std::string& archive_path,
                int32_t hot_retention_days,
                int32_t cold_retention_days);

    ~TierManager();

    // 启动后台分层管理线程
    void Start();

    // 停止后台线程
    void Stop();

    // 立即执行一次分层操作
    void RunOnce();

    // 设置检查间隔（秒）
    void SetInterval(int32_t seconds);

    // 回调：数据从 hot 移到 cold 时触发
    using TierCallback = std::function<void(const std::string& tag,
                                             const std::string& sst_file)>;
    void SetOnMoveToCold(TierCallback cb) { on_move_cold_ = std::move(cb); }

private:
    std::string hot_path_;
    std::string cold_path_;
    std::string archive_path_;
    int32_t hot_retention_days_;
    int32_t cold_retention_days_;
    int32_t check_interval_sec_ = 3600;  // 默认 1 小时

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    TierCallback on_move_cold_;

    void WorkerLoop();

    // 将过期热数据移到冷存
    void MoveExpiredHotToCold();

    // 删除过期冷数据
    void PruneExpiredCold();

    // 归档到外部磁盘
    void ArchiveToExternal(const std::string& file_path);
};

} // namespace minitsdb
