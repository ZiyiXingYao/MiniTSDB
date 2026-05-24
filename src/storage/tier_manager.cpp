#include "storage/tier_manager.h"
#include "common/logger.h"
#include "common/os/fs.h"
#include <chrono>
#include <ctime>
#include <algorithm>

namespace minitsdb {

TierManager::TierManager(const std::string& hot_path,
                         const std::string& cold_path,
                         const std::string& archive_path,
                         int32_t hot_retention_days,
                         int32_t cold_retention_days)
    : hot_path_(hot_path), cold_path_(cold_path), archive_path_(archive_path),
      hot_retention_days_(hot_retention_days),
      cold_retention_days_(cold_retention_days) {}

TierManager::~TierManager() {
    Stop();
}

void TierManager::Start() {
    if (running_.exchange(true)) return;
    worker_thread_ = std::thread(&TierManager::WorkerLoop, this);
}

void TierManager::Stop() {
    if (running_.exchange(false)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

void TierManager::SetInterval(int32_t seconds) {
    check_interval_sec_ = std::max(seconds, 60);
}

void TierManager::RunOnce() {
    MoveExpiredHotToCold();
    PruneExpiredCold();
}

void TierManager::WorkerLoop() {
    while (running_) {
        RunOnce();
        // 间隔检查，支持被 Stop 中断
        for (int i = 0; i < check_interval_sec_ && running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void TierManager::MoveExpiredHotToCold() {
    try {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto cutoff_ms = now_ms - static_cast<int64_t>(24) * 3600 * 1000 * hot_retention_days_;

        std::string tags_path = hot_path_ + "/tags";
        if (!os::fs::Exists(tags_path)) return;

        std::vector<os::fs::DirEntry> tag_entries;
        if (os::fs::ListDirectory(tags_path, tag_entries)) {
            for (const auto& tag_dir : tag_entries) {
                if (!tag_dir.is_directory) continue;
                std::string tag_name = tag_dir.name;

                std::vector<os::fs::DirEntry> sst_entries;
                if (os::fs::ListDirectory(tag_dir.path, sst_entries)) {
                    for (const auto& sst_file : sst_entries) {
                        if (sst_file.name.size() < 4 ||
                            sst_file.name.substr(sst_file.name.size() - 4) != ".sst") continue;

                        auto last_write_ms = os::fs::LastWriteTimeMs(sst_file.path);
                        if (last_write_ms >= 0 && last_write_ms < cutoff_ms) {
                            // 移到冷存
                            std::string cold_tag_dir = cold_path_ + "/" + tag_name;
                            os::fs::CreateDirectories(cold_tag_dir);

                            std::string dest = cold_tag_dir + "/" + sst_file.name;
                            if (!os::fs::Rename(sst_file.path, dest)) {
                                // 跨卷时 copy + delete
                                if (os::fs::Copy(sst_file.path, dest)) {
                                    os::fs::Remove(sst_file.path);
                                }
                            }

                            if (on_move_cold_) {
                                on_move_cold_(tag_name, dest);
                            }

                            LOG_INFO("Moved {} to cold storage", sst_file.path);
                        }
                    }
                }

                // 如果 hot 中的 tag 目录为空，删除
                if (os::fs::IsEmpty(tag_dir.path)) {
                    os::fs::Remove(tag_dir.path);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("TierManager::MoveExpiredHotToCold error: {}", e.what());
    }
}

void TierManager::PruneExpiredCold() {
    try {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto cutoff_ms = now_ms - static_cast<int64_t>(24) * 3600 * 1000 * cold_retention_days_;

        if (!os::fs::Exists(cold_path_)) return;

        std::vector<os::fs::DirEntry> tag_entries;
        if (os::fs::ListDirectory(cold_path_, tag_entries)) {
            for (const auto& tag_dir : tag_entries) {
                if (!tag_dir.is_directory) continue;

                std::vector<os::fs::DirEntry> sst_entries;
                if (os::fs::ListDirectory(tag_dir.path, sst_entries)) {
                    for (const auto& sst_file : sst_entries) {
                        if (sst_file.name.size() < 4 ||
                            sst_file.name.substr(sst_file.name.size() - 4) != ".sst") continue;

                        auto last_write_ms = os::fs::LastWriteTimeMs(sst_file.path);
                        if (last_write_ms >= 0 && last_write_ms < cutoff_ms) {
                            if (!archive_path_.empty()) {
                                ArchiveToExternal(sst_file.path);
                            }
                            if (os::fs::Remove(sst_file.path)) {
                                LOG_INFO("Removed expired cold data: {}", sst_file.path);
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("TierManager::PruneExpiredCold error: {}", e.what());
    }
}

void TierManager::ArchiveToExternal(const std::string& file_path) {
    try {
        std::string rel_path = file_path.substr(cold_path_.size() + 1);
        std::string archive_file = archive_path_ + "/" + rel_path;
        auto pos = archive_file.find_last_of("/\\");
        if (pos != std::string::npos) {
            os::fs::CreateDirectories(archive_file.substr(0, pos));
        }
        os::fs::Rename(file_path, archive_file);
        LOG_INFO("Archived to: {}", archive_file);
    } catch (const std::exception& e) {
        LOG_WARN("ArchiveToExternal error: {}", e.what());
    }
}

} // namespace minitsdb
