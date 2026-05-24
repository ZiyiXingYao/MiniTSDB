#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "proto_gen/minitsdb.grpc.pb.h"

using namespace minitsdb;

// 输出格式
enum class OutputFormat { TABLE, CSV, JSON };

struct CliOptions {
    std::string host = "127.0.0.1";
    int port = 8086;
    std::string user = "admin";
    std::string password = "admin123";
    OutputFormat format = OutputFormat::TABLE;
    std::string script_file;   // -f 参数
    std::string exec_sql;      // -e 参数
};

void PrintUsage(const char* prog) {
    std::cerr << "MiniTSDB CLI Client\n"
              << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --host <host>       Server host (default: 127.0.0.1)\n"
              << "  --port <port>       Server port (default: 8086)\n"
              << "  --user <user>       Username (default: admin)\n"
              << "  --password <pass>   Password (default: admin123)\n"
              << "  --format <fmt>      Output format: table|csv|json (default: table)\n"
              << "  -f <file>           Execute SQL from file\n"
              << "  -e <sql>            Execute SQL string\n"
              << "  -h, --help          Show this help\n";
}

CliOptions ParseArgs(int argc, char* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) opts.host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) opts.port = std::stoi(argv[++i]);
        else if (arg == "--user" && i + 1 < argc) opts.user = argv[++i];
        else if (arg == "--password" && i + 1 < argc) opts.password = argv[++i];
        else if (arg == "--format" && i + 1 < argc) {
            std::string fmt = argv[++i];
            if (fmt == "csv") opts.format = OutputFormat::CSV;
            else if (fmt == "json") opts.format = OutputFormat::JSON;
            else opts.format = OutputFormat::TABLE;
        }
        else if (arg == "-f" && i + 1 < argc) opts.script_file = argv[++i];
        else if (arg == "-e" && i + 1 < argc) opts.exec_sql = argv[++i];
        else if (arg == "-h" || arg == "--help") { PrintUsage(argv[0]); exit(0); }
    }
    return opts;
}

// gRPC 客户端
class GrpcClient {
public:
    GrpcClient(const std::string& host, int port)
        : stub_(MiniTSDB::NewStub(grpc::CreateChannel(
              host + ":" + std::to_string(port),
              grpc::InsecureChannelCredentials()))) {}

    bool Login(const std::string& user, const std::string& pass) {
        AuthRequest req;
        req.set_username(user);
        req.set_password(pass);
        AuthResponse res;
        grpc::ClientContext ctx;
        auto status = stub_->Auth(&ctx, req, &res);
        if (status.ok() && res.ok()) {
            token_ = res.token();
            role_ = res.role();
            return true;
        }
        return false;
    }

    bool Query(const std::string& sql, QueryResponse& res) {
        QueryRequest req;
        req.set_sql(sql);
        req.set_token(token_);
        grpc::ClientContext ctx;
        auto status = stub_->Query(&ctx, req, &res);
        return status.ok() && res.ok();
    }

    const std::string& Token() const { return token_; }
    const std::string& Role() const { return role_; }

private:
    std::unique_ptr<MiniTSDB::Stub> stub_;
    std::string token_;
    std::string role_;
};

// 格式化输出
void PrintTable(const QueryResponse& res) {
    if (res.columns_size() == 0) return;

    // 打印表头
    for (int i = 0; i < res.columns_size(); i++) {
        if (i > 0) std::cout << " | ";
        std::cout << res.columns(i);
    }
    std::cout << "\n";

    for (int i = 0; i < res.columns_size(); i++) {
        if (i > 0) std::cout << "-+-";
        else for (size_t j = 0; j < res.columns(i).size(); j++) std::cout << "-";
    }
    std::cout << "\n";

    // 打印数据行
    for (int r = 0; r < res.rows_size(); r++) {
        for (int c = 0; c < res.rows(r).values_size(); c++) {
            if (c > 0) std::cout << " | ";
            std::cout << res.rows(r).values(c);
        }
        std::cout << "\n";
    }
    std::cout << "(" << res.rows_size() << " rows)\n";
}

// 转义 CSV 字段中的双引号
std::string EscapeCSV(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '"') r += "\"\"";
        else r += c;
    }
    return r;
}

// 转义 JSON 字符串中的特殊字符
std::string EscapeJSON(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;       break;
        }
    }
    return r;
}

void PrintCSV(const QueryResponse& res) {
    for (int i = 0; i < res.columns_size(); i++) {
        if (i > 0) std::cout << ",";
        std::cout << "\"" << EscapeCSV(res.columns(i)) << "\"";
    }
    std::cout << "\n";

    for (int r = 0; r < res.rows_size(); r++) {
        for (int c = 0; c < res.rows(r).values_size(); c++) {
            if (c > 0) std::cout << ",";
            std::cout << "\"" << EscapeCSV(res.rows(r).values(c)) << "\"";
        }
        std::cout << "\n";
    }
}

void PrintJSON(const QueryResponse& res) {
    std::cout << "{\n  \"columns\": [";
    for (int i = 0; i < res.columns_size(); i++) {
        if (i > 0) std::cout << ", ";
        std::cout << "\"" << EscapeJSON(res.columns(i)) << "\"";
    }
    std::cout << "],\n  \"rows\": [\n";
    for (int r = 0; r < res.rows_size(); r++) {
        if (r > 0) std::cout << ",\n";
        std::cout << "    {";
        for (int c = 0; c < res.rows(r).values_size(); c++) {
            if (c > 0) std::cout << ", ";
            std::cout << "\"" << EscapeJSON(res.columns(c)) << "\": \""
                      << EscapeJSON(res.rows(r).values(c)) << "\"";
        }
        std::cout << "}";
    }
    std::cout << "\n  ]\n}\n";
}

void PrintResult(const QueryResponse& res, OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::TABLE: PrintTable(res); break;
        case OutputFormat::CSV:   PrintCSV(res); break;
        case OutputFormat::JSON:  PrintJSON(res); break;
    }
}

bool ExecuteSQL(GrpcClient& client, const std::string& sql,
                OutputFormat fmt) {
    QueryResponse res;
    if (!client.Query(sql, res)) {
        std::cerr << "ERROR: Query failed\n";
        return false;
    }
    if (!res.ok()) {
        std::cerr << "ERROR: " << res.error() << "\n";
        return false;
    }
    PrintResult(res, fmt);
    return true;
}

int main(int argc, char* argv[]) {
    auto opts = ParseArgs(argc, argv);

    GrpcClient client(opts.host, opts.port);
    if (!client.Login(opts.user, opts.password)) {
        std::cerr << "ERROR: Login failed\n";
        return 1;
    }

    // 执行 -e 或 -f 模式
    if (!opts.exec_sql.empty()) {
        return ExecuteSQL(client, opts.exec_sql, opts.format) ? 0 : 1;
    }
    if (!opts.script_file.empty()) {
        std::ifstream file(opts.script_file);
        if (!file.is_open()) {
            std::cerr << "ERROR: Cannot open " << opts.script_file << "\n";
            return 1;
        }
        std::string line;
        bool all_ok = true;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::cout << "> " << line << "\n";
            if (!ExecuteSQL(client, line, opts.format)) all_ok = false;
        }
        return all_ok ? 0 : 1;
    }

    // 交互模式
    std::cout << "MiniTSDB CLI (" << opts.user << "@" << opts.host
              << ":" << opts.port << ")\n";
    std::cout << "Type SQL or 'quit' to exit.\n\n";

    std::string line;
    while (true) {
        std::cout << "minitsdb> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;

        ExecuteSQL(client, line, opts.format);
    }

    return 0;
}
