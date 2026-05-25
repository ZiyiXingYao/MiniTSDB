#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <iomanip>
#include <grpcpp/grpcpp.h>
#include "common/os/file.h"
#include "proto_gen/minitsdb.grpc.pb.h"
#include "proto_gen/snapshot.pb.h"

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

    bool SnapshotQuery(const SnapshotRequest& req, SnapshotResponse& res) {
        grpc::ClientContext ctx;
        auto status = stub_->Snapshot(&ctx, req, &res);
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

    // 计算每列最大宽度
    std::vector<size_t> widths(res.columns_size());
    for (int i = 0; i < res.columns_size(); i++) {
        widths[i] = res.columns(i).size();
    }
    for (int r = 0; r < res.rows_size(); r++) {
        for (int c = 0; c < res.rows(r).values_size() && c < res.columns_size(); c++) {
            widths[c] = std::max(widths[c], res.rows(r).values(c).size());
        }
    }

    // 打印表头
    auto PrintRow = [&](const std::vector<std::string>& cells) {
        std::cout << "| ";
        for (size_t i = 0; i < cells.size() && i < widths.size(); i++) {
            if (i > 0) std::cout << " | ";
            std::cout << std::left << std::setw(static_cast<int>(widths[i])) << cells[i];
        }
        std::cout << " |\n";
    };

    // 分隔线
    auto PrintSeparator = [&]() {
        std::cout << "+";
        for (size_t w : widths) {
            std::cout << std::string(w + 2, '-') << "+";
        }
        std::cout << "\n";
    };

    PrintSeparator();
    std::vector<std::string> header;
    for (int i = 0; i < res.columns_size(); i++) {
        header.push_back(res.columns(i));
    }
    PrintRow(header);
    PrintSeparator();

    // 数据行
    for (int r = 0; r < res.rows_size(); r++) {
        std::vector<std::string> row;
        for (int c = 0; c < res.rows(r).values_size(); c++) {
            row.push_back(res.rows(r).values(c));
        }
        PrintRow(row);
    }
    PrintSeparator();

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

    // 检查是否 LATEST 子命令
    std::string first_arg = (argc > 1) ? argv[1] : "";
    for (auto& c : first_arg) c = static_cast<char>(std::toupper(c));

    if (first_arg == "LATEST" || first_arg == "SNAPSHOT") {
        GrpcClient client(opts.host, opts.port);
        if (!client.Login(opts.user, opts.password)) {
            std::cerr << "ERROR: Login failed for user '" << opts.user
                      << "'@" << opts.host << ":" << opts.port << "\n";
            return 1;
        }

        std::string tag, pattern;
        bool query_all = false;
        std::string out_format = "table";

        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--all") query_all = true;
            else if (arg == "--pattern" && i + 1 < argc) pattern = argv[++i];
            else if (arg == "--format" && i + 1 < argc) out_format = argv[++i];
            else if (!arg.empty() && arg[0] != '-') tag = arg;
        }

        if (tag.empty() && pattern.empty() && !query_all) {
            std::cerr << "Usage: minitsdb_cli LATEST <tag> | --pattern <pattern> | --all [--format table|json]\n";
            return 1;
        }

        SnapshotRequest req;
        req.set_token(client.Token());
        if (!tag.empty()) {
            req.set_type(SnapshotQueryType::GET);
            req.set_tag(tag);
        } else if (!pattern.empty()) {
            req.set_type(SnapshotQueryType::GET_MANY);
            req.set_pattern(pattern);
        } else {
            req.set_type(SnapshotQueryType::GET_ALL);
        }

        SnapshotResponse res;
        if (!client.SnapshotQuery(req, res)) {
            std::cerr << "RPC failed\n";
            return 1;
        }
        if (!res.ok()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        // Output
        if (out_format == "json") {
            std::cout << "[\n";
            for (int i = 0; i < res.entries_size(); i++) {
                const auto& e = res.entries(i);
                std::cout << "  {\"tag\":\"" << EscapeJSON(e.tag()) << "\","
                          << "\"value\":" << e.value() << ","
                          << "\"ts\":" << e.timestamp() << "}";
                if (i < res.entries_size() - 1) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            printf("%-20s %-15s %s\n", "TAG", "VALUE", "TIMESTAMP");
            for (int i = 0; i < res.entries_size(); i++) {
                const auto& e = res.entries(i);
                printf("%-20s %-15.3f %ld\n", e.tag().c_str(), e.value(), static_cast<long>(e.timestamp()));
            }
            std::cout << "(" << res.entries_size() << " rows)\n";
        }
        return 0;
    }

    // 普通模式：登录
    GrpcClient client(opts.host, opts.port);
    if (!client.Login(opts.user, opts.password)) {
        std::cerr << "ERROR: Login failed for user '" << opts.user
                  << "'@" << opts.host << ":" << opts.port << "\n";
        return 1;
    }

    // 执行 -e 或 -f 模式
    if (!opts.exec_sql.empty()) {
        return ExecuteSQL(client, opts.exec_sql, opts.format) ? 0 : 1;
    }
    if (!opts.script_file.empty()) {
        os::File file;
        if (!file.Open(opts.script_file, os::FileMode::READ)) {
            std::cerr << "ERROR: Cannot open " << opts.script_file << "\n";
            return 1;
        }
        // 读取整个文件到字符串
        int64_t fsize = file.Size();
        std::string file_content;
        if (fsize > 0) {
            file_content.resize(static_cast<size_t>(fsize));
            file.Read(&file_content[0], static_cast<size_t>(fsize));
        }
        std::istringstream stream(file_content);
        std::string content = file_content;
        bool all_ok = true;
        // 按分号分割多条 SQL 语句
        size_t pos = 0;
        while (pos < content.size()) {
            auto semi = content.find(';', pos);
            if (semi == std::string::npos) semi = content.size();
            std::string stmt = content.substr(pos, semi - pos);
            pos = semi + 1;
            // 去除首尾空白
            auto start = stmt.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || stmt[start] == '#') continue;
            auto end = stmt.find_last_not_of(" \t\r\n");
            stmt = stmt.substr(start, end - start + 1);
            std::cout << "> " << stmt << "\n";
            if (!ExecuteSQL(client, stmt, opts.format)) all_ok = false;
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
