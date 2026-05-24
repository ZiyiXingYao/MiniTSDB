#include "storage/tier_manager.h"
#include "common/logger.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

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
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - std::chrono::hours(24 * hot_retention_days_);

        std::string tags_path = hot_path_ + "/tags";
        if (!fs::exists(tags_path)) return;

        for (const auto& tag_dir : fs::directory_iterator(tags_path)) {
            if (!tag_dir.is_directory()) continue;
            std::string tag_name = tag_dir.path().filename().string();

            for (const auto& sst_file : fs::directory_iterator(tag_dir.path())) {
                if (sst_file.path().extension() != ".sst") continue;

                auto last_write = sst_file.last_write_time();
                // 比较文件时间
                auto file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    last_write - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (file_time < cutoff) {
                    // 移到冷存
                    std::string cold_tag_dir = cold_path_ + "/" + tag_name;
                    fs::create_directories(cold_tag_dir);

                    std::string dest = cold_tag_dir + "/" + sst_file.path().filename().string();
                    fs::rename(sst_file.path(), dest);

                    if (on_move_cold_) {
                        on_move_cold_(tag_name, dest);
                    }

                    LOG_INFO("Moved {} to cold storage", sst_file.path().string());
                }
            }

            // 如果 hot 中的 tag 目录为空，删除
            if (fs::is_empty(tag_dir.path())) {
                fs::remove(tag_dir.path());
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("TierManager::MoveExpiredHotToCold error: {}", e.what());
    }
}

void TierManager::PruneExpiredCold() {
    try {
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - std::chrono::hours(24 * cold_retention_days_);

        if (!fs::exists(cold_path_)) return;

        for (const auto& tag_dir : fs::directory_iterator(cold_path_)) {
            if (!tag_dir.is_directory()) continue;

            for (const auto& sst_file : fs::directory_iterator(tag_dir.path())) {
                if (sst_file.path().extension() != ".sst") continue;

                auto last_write = sst_file.last_write_time();
                auto file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    last_write - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (file_time < cutoff) {
                    if (!archive_path_.empty()) {
                        ArchiveToExternal(sst_file.path().string());
                    }
                    fs::remove(sst_file.path());
                    LOG_INFO("Removed expired cold data: {}", sst_file.path().string());
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
        fs::create_directories(fs::path(archive_file).parent_path());
        fs::rename(file_path, archive_file);
        LOG_INFO("Archived to: {}", archive_file);
    } catch (const std::exception& e) {
        LOG_WARN("ArchiveToExternal error: {}", e.what());
    }
}

} // namespace minitsdb
