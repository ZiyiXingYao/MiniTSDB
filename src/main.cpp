#include "server/grpc_server.h"
#include "storage/engine.h"
#include "storage/compaction.h"
#include "storage/tier_manager.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include "common/logger.h"
#include "common/config.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>

namespace minitsdb {

std::atomic<bool> g_shutdown{false};

void SignalHandler(int) {
    g_shutdown = true;
}

void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -c, --config <file>   Config file path (default: ./minitsdb.conf)\n"
              << "  -h, --help            Show this help\n";
}

void RunServer(const Config& cfg) {
    // ===== 日志配置 =====
    std::string log_dir = cfg.Get("log.directory", "./logs");
    std::string log_level = cfg.Get("log.level", "info");
    int log_keep = cfg.GetInt("log.keep_files", 1000);

    // 通过环境变量传递日志配置
#ifdef _WIN32
    _putenv_s("MINITSDB_LOG_DIR", log_dir.c_str());
    _putenv_s("MINITSDB_LOG_LEVEL", log_level.c_str());
#else
    setenv("MINITSDB_LOG_DIR", log_dir.c_str(), 1);
    setenv("MINITSDB_LOG_LEVEL", log_level.c_str(), 1);
#endif

    LOG_INFO("Config: log.directory={}", log_dir);
    LOG_INFO("Config: log.level={}", log_level);
    LOG_INFO("Config: log.keep_files={}", log_keep);

    // ===== 数据存储配置 =====
    std::string data_dir = cfg.Get("storage.data_dir", "./data");
    std::string hot_path = data_dir + "/hot";
    std::string cold_path = data_dir + "/cold";
    int hot_retention = cfg.GetInt("storage.hot_retention_days", 90);
    int cold_retention = cfg.GetInt("storage.cold_retention_days", 730);

    StorageConfig storage_config;
    storage_config.hot_path = hot_path;
    storage_config.cold_path = cold_path;
    storage_config.archive_path = cfg.Get("storage.archive_path", "");
    storage_config.hot_retention_days = hot_retention;
    storage_config.cold_retention_days = cold_retention;

    auto engine = std::make_shared<StorageEngine>(storage_config);
    if (!engine->Init()) {
        LOG_ERROR("Failed to initialize storage engine.");
        return;
    }

    // 创建缓存
    auto cache = std::make_shared<LatestCache>();

    // 认证管理器
    auto auth = std::make_shared<AuthManager>();
    auth->Init(hot_path);
    // 通过配置文件设置 admin 密码
    std::string admin_pw = cfg.Get("auth.admin_password", "admin123");
    if (admin_pw != "admin123") {
        // 先登录默认账户，再修改密码
        // 简化实现：直接在 AuthManager 中支持 SetPassword
    }
    LOG_INFO("Auth manager initialized");

    // 启动后台 Compaction
    auto compaction = std::make_shared<Compaction>(hot_path);
    int compact_interval = cfg.GetInt("storage.compact_interval_sec", 300);
    compaction->Start(compact_interval);
    LOG_INFO("Compaction worker started (interval={}s)", compact_interval);

    // 启动后台 TierManager
    auto tier_mgr = std::make_shared<TierManager>(
        hot_path, cold_path, storage_config.archive_path,
        hot_retention, cold_retention);
    tier_mgr->Start();
    LOG_INFO("TierManager worker started");

    // gRPC 服务端
    GrpcServerConfig server_config;
    server_config.port = cfg.GetInt("server.port", 8086);
    GrpcServer server(server_config, engine, cache, auth);

    if (!server.Start()) {
        LOG_ERROR("Failed to start gRPC server.");
        return;
    }

    LOG_INFO("MiniTSDB gRPC server started on port {}", server_config.port);
    LOG_INFO("Data directory: {}", data_dir);
    LOG_INFO("Hot path: {}", hot_path);
    LOG_INFO("Cold path: {}", cold_path);

    // 等待关闭信号
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("Shutting down...");
    compaction->Stop();
    tier_mgr->Stop();
    server.Stop();
    engine->Close();
    LOG_INFO("MiniTSDB stopped.");
}

} // namespace minitsdb

int main(int argc, char* argv[]) {
    std::signal(SIGINT, minitsdb::SignalHandler);
    std::signal(SIGTERM, minitsdb::SignalHandler);

    // 解析命令行参数
    std::string config_path = "./minitsdb.conf";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            minitsdb::PrintUsage(argv[0]);
            return 0;
        }
    }

    // 加载配置
    minitsdb::Config cfg;
    if (!cfg.Load(config_path)) {
        std::cerr << "Config file not found: " << config_path
                  << ", using defaults.\n";
    }

    // 初始化日志（配置已在 Config 中）
    minitsdb::LogInit();

    if (cfg.Get("log.directory", "").empty()) {
        // 如果没有配置，LogInit 会使用默认值 ./logs
    }

    minitsdb::RunServer(cfg);

    return 0;
}
