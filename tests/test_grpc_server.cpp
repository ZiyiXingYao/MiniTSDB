#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "proto_gen/minitsdb.grpc.pb.h"
#include "storage/engine.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include "sql/executor.h"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace minitsdb;
using namespace grpc;

// gRPC 集成测试：在进程中启动服务端，通过 stub 连接测试
class GrpcIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    std::unique_ptr<grpc::Server> grpc_server_;
    std::unique_ptr<MiniTSDB::Stub> stub_;
    int port_;

    void SetUp() override {
        static int next_port = 9186;
        port_ = next_port++;
        // 初始化
        StorageConfig sc;
        sc.hot_path = "./test_grpc_data/hot";
        sc.cold_path = "./test_grpc_data/cold";
        engine_ = std::make_shared<StorageEngine>(sc);
        engine_->Init();
        cache_ = std::make_shared<LatestCache>();
        auth_ = std::make_shared<AuthManager>();
        auth_->Init("./test_grpc_data/hot");

        // 创建 service 实现
        class TestService : public MiniTSDB::Service {
        public:
            TestService(std::shared_ptr<StorageEngine> e,
                        std::shared_ptr<LatestCache> c,
                        std::shared_ptr<AuthManager> a)
                : engine_(e), cache_(c), auth_(a) {}

            Status Query(ServerContext*, const QueryRequest* req,
                         QueryResponse* res) override {
                Executor exec(engine_, cache_, auth_);
                auto r = exec.ExecuteSQL(req->sql());
                res->set_ok(r.ok);
                res->set_error(r.error_msg);
                for (auto& col : r.column_names) res->add_columns(col);
                for (auto& row : r.rows) {
                    auto* proto_row = res->add_rows();
                    for (auto& v : row) proto_row->add_values(v);
                }
                return Status::OK;
            }

            Status Auth(ServerContext*, const AuthRequest* req,
                        AuthResponse* res) override {
                std::string token = auth_->Login(req->username(), req->password());
                if (!token.empty()) {
                    res->set_ok(true);
                    res->set_token(token);
                    res->set_role("admin");
                } else {
                    res->set_ok(false);
                    res->set_error("Invalid credentials");
                }
                return Status::OK;
            }

            Status Insert(ServerContext*, const InsertRequest* req,
                          InsertResponse* res) override {
                for (const auto& p : req->points()) {
                    DataPoint dp;
                    dp.ts = p.timestamp() > 0 ? p.timestamp() : 0;
                    dp.value = p.value();
                    if (engine_) engine_->Write(p.tag(), dp);
                    if (cache_) cache_->Update(p.tag(), dp);
                }
                res->set_ok(true);
                res->set_count(req->points_size());
                return Status::OK;
            }
        private:
            std::shared_ptr<StorageEngine> engine_;
            std::shared_ptr<LatestCache> cache_;
            std::shared_ptr<AuthManager> auth_;
        };

        auto* service = new TestService(engine_, cache_, auth_);

        ServerBuilder builder;
        builder.AddListeningPort("0.0.0.0:" + std::to_string(port_),
                                 InsecureServerCredentials());
        builder.RegisterService(service);
        grpc_server_ = builder.BuildAndStart();
        ASSERT_NE(grpc_server_, nullptr);

        stub_ = MiniTSDB::NewStub(
            CreateChannel("localhost:" + std::to_string(port_),
                          InsecureChannelCredentials()));
    }

    void TearDown() override {
        if (grpc_server_) grpc_server_->Shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::error_code ec;
        std::filesystem::remove_all("./test_grpc_data", ec);
    }
};

TEST_F(GrpcIntegrationTest, AuthAndQuery) {
    AuthRequest auth_req;
    auth_req.set_username("admin");
    auth_req.set_password("admin123");
    AuthResponse auth_res;
    ClientContext auth_ctx;
    auto status = stub_->Auth(&auth_ctx, auth_req, &auth_res);
    ASSERT_TRUE(status.ok());
    ASSERT_TRUE(auth_res.ok());
    std::string token = auth_res.token();

    // Query - use a simple SELECT that the parser can handle
    QueryRequest q_req;
    q_req.set_sql("SELECT tag, value FROM test_grpc WHERE tag='test' LATEST");
    q_req.set_token(token);
    QueryResponse q_res;
    ClientContext q_ctx;
    status = stub_->Query(&q_ctx, q_req, &q_res);
    ASSERT_TRUE(status.ok());
    ASSERT_TRUE(q_res.ok());
}

TEST_F(GrpcIntegrationTest, InsertAndLatest) {
    // 登录
    ClientContext auth_ctx;
    AuthRequest auth_req;
    auth_req.set_username("admin");
    auth_req.set_password("admin123");
    AuthResponse auth_res;
    stub_->Auth(&auth_ctx, auth_req, &auth_res);
    std::string token = auth_res.token();

    // 写入数据
    InsertRequest ins_req;
    ins_req.set_token(token);
    auto* p = ins_req.add_points();
    p->set_tag("INTEG-TEST");
    p->set_timestamp(1000);
    p->set_value(42.5);

    InsertResponse ins_res;
    ClientContext ins_ctx;
    auto status = stub_->Insert(&ins_ctx, ins_req, &ins_res);
    ASSERT_TRUE(status.ok()) << "Insert failed: " << status.error_message() << " (code=" << status.error_code() << ")";
    ASSERT_TRUE(ins_res.ok());
    EXPECT_EQ(ins_res.count(), 1);

    // 查询最新值
    QueryRequest q_req;
    q_req.set_sql("SELECT tag, value, ts FROM INTEG-TEST WHERE tag='INTEG-TEST' LATEST");
    q_req.set_token(token);
    QueryResponse q_res;
    ClientContext q_ctx;
    status = stub_->Query(&q_ctx, q_req, &q_res);
    ASSERT_TRUE(status.ok());
    ASSERT_TRUE(q_res.ok());
    EXPECT_EQ(q_res.rows_size(), 1);
    if (q_res.rows_size() > 0) {
        EXPECT_EQ(q_res.rows(0).values(0), "INTEG-TEST");
    }
}

TEST_F(GrpcIntegrationTest, FailedLogin) {
    AuthRequest req;
    req.set_username("admin");
    req.set_password("wrong");
    AuthResponse res;
    ClientContext ctx;
    stub_->Auth(&ctx, req, &res);
    EXPECT_FALSE(res.ok());
}
