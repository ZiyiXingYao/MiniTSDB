#include "server/grpc_server.h"
#include "sql/executor.h"
#include "common/logger.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include "proto_gen/minitsdb.grpc.pb.h"
#include <sstream>

namespace minitsdb {

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

// ============================================================
// MiniTSDB gRPC Service 实现
// ============================================================
class MiniTSDBServiceImpl final : public MiniTSDB::Service {
public:
    MiniTSDBServiceImpl(std::shared_ptr<StorageEngine> engine,
                        std::shared_ptr<LatestCache> cache,
                        std::shared_ptr<AuthManager> auth)
        : engine_(std::move(engine)), cache_(std::move(cache)), auth_(std::move(auth)) {}

    Status Query(ServerContext* context, const QueryRequest* request,
                 QueryResponse* response) override {
        LOG_DEBUG("Query: {}", request->sql());

        // 权限检查
        if (auth_ && !auth_->CheckPermission(request->token(), request->sql())) {
            response->set_ok(false);
            response->set_error("Permission denied");
            LOG_WARN("Permission denied for query: {}", request->sql());
            return Status::OK;
        }

        Executor executor(engine_, cache_, auth_);
        auto result = executor.ExecuteSQL(request->sql());

        response->set_ok(result.ok);
        if (!result.ok) {
            response->set_error(result.error_msg);
            LOG_ERROR("Query failed: {}", result.error_msg);
            return Status::OK;
        }

        for (const auto& col : result.column_names) {
            response->add_columns(col);
        }
        for (const auto& row : result.rows) {
            auto* proto_row = response->add_rows();
            for (const auto& val : row) {
                proto_row->add_values(val);
            }
        }
        LOG_DEBUG("Query OK: {} rows", result.rows.size());
        return Status::OK;
    }

    Status Insert(ServerContext* context, const InsertRequest* request,
                  InsertResponse* response) override {
        // 权限检查
        if (auth_ && !auth_->CheckPermission(request->token(), "INSERT")) {
            response->set_ok(false);
            response->set_error("Permission denied");
            return Status::OK;
        }

        int64_t count = 0;
        for (const auto& p : request->points()) {
            DataPoint dp;
            dp.ts = p.timestamp() > 0 ? p.timestamp()
                : std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            dp.value = p.value();

            if (engine_) {
                engine_->Write(p.tag(), dp);
            }
            if (cache_) {
                cache_->Update(p.tag(), dp);
            }
            count++;
        }

        response->set_ok(true);
        response->set_count(count);
        LOG_DEBUG("Insert: {} points written", count);
        return Status::OK;
    }

    Status Auth(ServerContext* context, const AuthRequest* request,
                AuthResponse* response) override {
        if (!auth_) {
            // 无认证管理器，默认允许
            response->set_ok(true);
            response->set_token("no-auth-token");
            response->set_role("admin");
            return Status::OK;
        }

        std::string token = auth_->Login(request->username(), request->password());
        if (!token.empty()) {
            auto* user = auth_->ValidateToken(token);
            response->set_ok(true);
            response->set_token(token);
            if (user) {
                switch (user->role) {
                    case UserRole::ADMIN:    response->set_role("admin"); break;
                    case UserRole::OPERATOR: response->set_role("operator"); break;
                    case UserRole::VIEWER:   response->set_role("viewer"); break;
                }
            }
            LOG_INFO("User '{}' logged in successfully", request->username());
        } else {
            response->set_ok(false);
            response->set_error("Invalid credentials");
            LOG_WARN("Failed login attempt for user '{}'", request->username());
        }
        return Status::OK;
    }

    Status Admin(ServerContext* context, const AdminRequest* request,
                 AdminResponse* response) override {
        // 权限检查
        if (auth_ && !auth_->CheckPermission(request->token(), "CREATE TAG")) {
            response->set_ok(false);
            response->set_error("Permission denied");
            return Status::OK;
        }

        Executor executor(engine_, cache_, auth_);
        auto result = executor.ExecuteSQL(request->command());

        response->set_ok(result.ok);
        if (!result.ok) {
            response->set_error(result.error_msg);
        } else {
            std::ostringstream oss;
            oss << result.affected_rows << " rows affected";
            response->set_result(oss.str());
        }
        return Status::OK;
    }

private:
    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
};

// ============================================================
// GrpcServer implementation
// ============================================================
GrpcServer::GrpcServer(const GrpcServerConfig& config,
                       std::shared_ptr<StorageEngine> engine,
                       std::shared_ptr<LatestCache> cache,
                       std::shared_ptr<AuthManager> auth)
    : config_(config), engine_(std::move(engine)), cache_(std::move(cache)), auth_(std::move(auth)) {}

GrpcServer::~GrpcServer() {
    Stop();
}

bool GrpcServer::Start() {
    std::string server_addr = config_.listen_addr + ":" + std::to_string(config_.port);

    auto* service = new MiniTSDBServiceImpl(engine_, cache_, auth_);
    service_ = service;

    ServerBuilder builder;
    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(service);

    auto* server = builder.BuildAndStart().release();
    if (!server) {
        LOG_ERROR("Failed to build and start gRPC server on {}", server_addr);
        return false;
    }

    server_ = server;
    running_ = true;
    LOG_INFO("gRPC server listening on {}", server_addr);

    // gRPC Server 在单独的线程中运行
    server_thread_ = std::thread([this]() {
        static_cast<Server*>(server_)->Wait();
    });

    return true;
}

void GrpcServer::Stop() {
    if (running_.exchange(false)) {
        if (server_) {
            static_cast<Server*>(server_)->Shutdown();
            server_ = nullptr;
        }
        delete static_cast<MiniTSDB::Service*>(service_);
        service_ = nullptr;
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
}

bool GrpcServer::IsRunning() const {
    return running_.load();
}

} // namespace minitsdb
