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
#include <memory>
#include <string>
#include <mutex>

namespace minitsdb {

// 自定义滚动文件 sink：容量到达上限后，按截止时间重命名 + gzip 压缩
class TimestampRotatingFileSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    TimestampRotatingFileSink(const std::string& base_filename,
                              size_t max_size,
                              size_t max_files,
                              bool compress = true);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override;

private:
    std::string base_filename_;
    size_t max_size_;
    size_t max_files_;
    bool compress_;
    os::File file_;
    size_t current_size_ = 0;
    std::mutex mutex_;

    static std::string GetTimestampStr();
    void Rotate();
    void CleanupOldFiles();
};

// 日志初始化
void LogInit();

} // namespace minitsdb

#define LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
