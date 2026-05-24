#pragma once

#include "storage/engine.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace minitsdb {

// gRPC 服务端配置
struct GrpcServerConfig {
    std::string listen_addr = "0.0.0.0";
    int port = 8086;
};

// gRPC 服务端
// 基于 MiniTSDB.proto 定义的 RPC 服务
class GrpcServer {
public:
    GrpcServer(const GrpcServerConfig& config,
               std::shared_ptr<StorageEngine> engine,
               std::shared_ptr<LatestCache> cache,
               std::shared_ptr<AuthManager> auth = nullptr);
    ~GrpcServer();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    GrpcServerConfig config_;
    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    void* server_ = nullptr;  // grpc::Server*
    void* service_ = nullptr; // MiniTSDB::Service*
};

} // namespace minitsdb
