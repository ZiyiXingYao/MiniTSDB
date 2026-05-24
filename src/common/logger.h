#pragma once

#ifndef SPDLOG_HEADER_ONLY
#define SPDLOG_HEADER_ONLY
#endif
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/base_sink.h>
#include "common/os/file.h"
#include "common/os/fs.h"
#include <memory>
#include <string>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <zlib.h>

namespace minitsdb {

// 自定义滚动文件 sink：容量到达上限后，按截止时间重命名 + gzip 压缩
class TimestampRotatingFileSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    TimestampRotatingFileSink(const std::string& base_filename,
                              size_t max_size,
                              size_t max_files,
                              bool compress = true)
        : base_filename_(base_filename)
        , max_size_(max_size)
        , max_files_(max_files)
        , compress_(compress) {
        // 创建日志目录
        auto pos = std::string(base_filename).find_last_of("/\\");
        if (pos != std::string::npos) {
            os::fs::CreateDirectories(std::string(base_filename).substr(0, pos));
        }

        // 打开当前日志文件（追加模式）
        if (!file_.Open(base_filename, os::FileMode::APPEND)) {
            throw spdlog::spdlog_ex("Failed to open log file: " + base_filename);
        }
        current_size_ = static_cast<size_t>(file_.Size());
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        base_sink::formatter_->format(msg, formatted);

        // 写入文件
        std::lock_guard<std::mutex> lock(mutex_);
        current_size_ += formatted.size();
        file_.Write(formatted.data(), formatted.size());
        file_.Flush();

        // 检查是否需要滚动
        if (current_size_ >= max_size_) {
            Rotate();
        }
    }

    void flush_() override {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.Flush();
    }

private:
    std::string base_filename_;
    size_t max_size_;
    size_t max_files_;
    bool compress_;
    os::File file_;
    size_t current_size_ = 0;
    std::mutex mutex_;

    // 获取当前时间戳字符串：20260524154908990
    static std::string GetTimestampStr() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return std::to_string(ms);
    }

    void Rotate() {
        file_.Close();

        // 生成时间戳文件名：minitsdb.log.20260524154908990
        std::string ts = GetTimestampStr();
        std::string archived_name = base_filename_ + "." + ts;

        // 重命名当前日志为带时间戳的文件
        os::fs::Rename(base_filename_, archived_name);
        SPDLOG_DEBUG("Log rotated: {} -> {}", base_filename_, archived_name);

        // 如果开启了压缩，使用嵌入式 zlib 进行 gzip 压缩
        if (compress_) {
            std::string gz_path = archived_name + ".gz";
            gzFile gz = gzopen(gz_path.c_str(), "wb9");  // 最高压缩比
            if (gz) {
                os::File in;
                if (in.Open(archived_name, os::FileMode::READ)) {
                    char buf[8192];
                    while (true) {
                        size_t bytes = 0;
                        if (!in.Read(buf, sizeof(buf), &bytes) || bytes == 0) break;
                        gzwrite(gz, buf, bytes);
                    }
                }
                gzclose(gz);
                // 删除未压缩的原文件
                os::fs::Remove(archived_name);
                SPDLOG_DEBUG("Log compressed: {}.gz", archived_name);
            } else {
                SPDLOG_WARN("gzip compression failed for {}, keeping uncompressed",
                            archived_name);
            }
        }

        // 清理超过 max_files_ 的旧归档文件（按修改时间排序）
        CleanupOldFiles();

        // 重新打开当前日志文件
        if (!file_.Open(base_filename_, os::FileMode::WRITE)) {
            throw spdlog::spdlog_ex("Failed to reopen log file: " + base_filename_);
        }
        current_size_ = 0;
    }

    void CleanupOldFiles() {
        std::string prefix = base_filename_ + ".";
        std::vector<std::string> archived_files;
        std::string dir = base_filename_;
        auto dpos = dir.find_last_of("/\\");
        if (dpos != std::string::npos) {
            dir = dir.substr(0, dpos);
        } else {
            dir = ".";
        }
        std::string base_name = base_filename_;
        auto bpos = base_name.find_last_of("/\\");
        if (bpos != std::string::npos) {
            base_name = base_name.substr(bpos + 1);
        }

        std::vector<os::fs::DirEntry> entries;
        if (os::fs::ListDirectory(dir, entries)) {
            for (const auto& entry : entries) {
                if (entry.name.find(prefix) == 0 && entry.name != base_name) {
                    archived_files.push_back(entry.path);
                }
            }
        }

        // 按修改时间排序（最新的在前面）
        std::sort(archived_files.begin(), archived_files.end(),
                  [](const std::string& a, const std::string& b) {
                      return os::fs::LastWriteTimeMs(a) > os::fs::LastWriteTimeMs(b);
                  });

        // 删除超出 max_files_ 的旧文件
        while (archived_files.size() > max_files_) {
            os::fs::Remove(archived_files.back());
            SPDLOG_DEBUG("Removed old log: {}", archived_files.back());
            archived_files.pop_back();
        }
    }
};

// 日志初始化
inline void LogInit() {
    // 控制台日志
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%L%$] [%s:%#] %v");
    console_sink->set_level(spdlog::level::trace);

    // 文件日志（自定义时间戳滚动 + 压缩）
    std::string log_dir = "./logs";
    if (auto env = std::getenv("MINITSDB_LOG_DIR")) {
        log_dir = env;
    }

    auto file_sink = std::make_shared<TimestampRotatingFileSink>(
        log_dir + "/minitsdb.log",  // 基础文件名
        25 * 1024 * 1024,          // 25MB 后滚动
        1000,                       // 保留 1000 个归档
        true                        // 启用 gzip 压缩
    );
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%L] [%s:%#] %v");
    file_sink->set_level(spdlog::level::debug);

    // 合并多 sink
    std::vector<spdlog::sink_ptr> sinks = {console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("minitsdb", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);

    spdlog::info("Log initialized: console + file (dir={}, max_size=10MB, keep=5, compress=true)",
                 log_dir);
}

} // namespace minitsdb

#define LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
