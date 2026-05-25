#include "sdk/minitsdb.h"
#include <grpcpp/grpcpp.h>
#include "proto_gen/minitsdb.grpc.pb.h"
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

using namespace minitsdb;

// 内部结构
struct minitsdb_conn {
    std::unique_ptr<MiniTSDB::Stub> stub;
    std::string token;
};

struct minitsdb_result {
    bool ok = false;
    std::string error;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    int affected = 0;
    // 用于返回字符串的生命周期管理
    std::string last_str;
};

// ============================================================
//  连接管理 (extern "C" for C SDK consumers)
// ============================================================
extern "C" {
minitsdb_conn* minitsdb_connect(const char* host, int port,
                                 const char* user, const char* password) {
    auto conn = new minitsdb_conn();
    std::string addr = std::string(host) + ":" + std::to_string(port);

    conn->stub = MiniTSDB::NewStub(
        grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

    // 登录认证
    AuthRequest req;
    req.set_username(user);
    req.set_password(password);
    AuthResponse res;
    grpc::ClientContext ctx;
    auto status = conn->stub->Auth(&ctx, req, &res);

    if (!status.ok() || !res.ok()) {
        delete conn;
        return nullptr;
    }

    conn->token = res.token();
    return conn;
}

void minitsdb_disconnect(minitsdb_conn* conn) {
    delete conn;
}

// ============================================================
//  查询
// ============================================================
minitsdb_result* minitsdb_query(minitsdb_conn* conn, const char* sql) {
    auto res = new minitsdb_result();

    QueryRequest req;
    req.set_sql(sql);
    req.set_token(conn->token);

    QueryResponse pb_res;
    grpc::ClientContext ctx;
    auto status = conn->stub->Query(&ctx, req, &pb_res);

    if (!status.ok()) {
        res->ok = false;
        res->error = "gRPC error: " + std::to_string(status.error_code());
        return res;
    }

    res->ok = pb_res.ok();
    if (!pb_res.ok()) {
        res->error = pb_res.error();
        return res;
    }

    for (int i = 0; i < pb_res.columns_size(); i++) {
        res->columns.push_back(pb_res.columns(i));
    }
    for (int r = 0; r < pb_res.rows_size(); r++) {
        std::vector<std::string> row;
        for (int c = 0; c < pb_res.rows(r).values_size(); c++) {
            row.push_back(pb_res.rows(r).values(c));
        }
        res->rows.push_back(row);
    }

    return res;
}

// ============================================================
//  结果集访问
// ============================================================
int minitsdb_result_rows(const minitsdb_result* res) {
    return static_cast<int>(res->rows.size());
}

int minitsdb_result_cols(const minitsdb_result* res) {
    return static_cast<int>(res->columns.size());
}

const char* minitsdb_result_col_name(const minitsdb_result* res, int col) {
    if (col < 0 || col >= static_cast<int>(res->columns.size())) return "";
    return res->columns[col].c_str();
}

const char* minitsdb_result_value(const minitsdb_result* res, int row, int col) {
    if (row < 0 || row >= static_cast<int>(res->rows.size())) return "";
    if (col < 0 || col >= static_cast<int>(res->rows[row].size())) return "";
    return res->rows[row][col].c_str();
}

const char* minitsdb_result_error(const minitsdb_result* res) {
    return res->error.c_str();
}

int minitsdb_result_affected(const minitsdb_result* res) {
    return res->affected;
}

void minitsdb_result_free(minitsdb_result* res) {
    delete res;
}

// ============================================================
//  写入
// ============================================================
int minitsdb_insert(minitsdb_conn* conn, const char* tag,
                    int64_t timestamp, double value) {
    if (!conn || !conn->stub || !tag) return -1;

    InsertRequest req;
    req.set_token(conn->token);
    auto* pv = req.add_points();
    pv->set_tag(tag);
    pv->set_timestamp(timestamp);
    pv->set_value(value);

    InsertResponse pb_res;
    grpc::ClientContext ctx;
    auto status = conn->stub->Insert(&ctx, req, &pb_res);

    if (!status.ok() || !pb_res.ok()) {
        return -1;
    }
    return static_cast<int>(pb_res.count());
}
} // extern "C"
