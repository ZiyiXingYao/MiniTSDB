#pragma once

#include "ast.h"
#include "sql/parser.h"
#include "storage/engine.h"
#include "cache/latest_cache.h"
#include "auth/auth_manager.h"
#include <memory>
#include <vector>
#include <string>

namespace minitsdb {

// 查询执行结果
struct QueryResult {
    bool ok = true;
    std::string error_msg;
    std::vector<std::string> column_names;  // 列名
    std::vector<std::vector<std::string>> rows;  // 行数据
    int64_t affected_rows = 0;  // INSERT 影响的记录数
};

// 查询执行器
// 接收解析后的 Statement，驱动存储引擎执行
class Executor {
public:
    Executor(std::shared_ptr<StorageEngine> engine,
             std::shared_ptr<LatestCache> cache,
             std::shared_ptr<AuthManager> auth = nullptr);

    // 执行一条 SQL 语句
    QueryResult Execute(const SQLStmt& stmt);

    // 通过 SQL 字符串直接执行（Parse + Execute）
    QueryResult ExecuteSQL(const std::string& sql);

private:
    QueryResult ExecuteInsert(const InsertStmt& stmt);
    QueryResult ExecuteSelect(const SelectStmt& stmt);
    QueryResult ExecuteSelectLatest(const SelectStmt& stmt);
    QueryResult ExecuteSelectRaw(const SelectStmt& stmt);
    QueryResult ExecuteSelectAggregate(const SelectStmt& stmt);
    QueryResult ExecuteSnapshot(const SelectStmt& stmt);
    QueryResult ExecuteCreateTag(const CreateTagStmt& stmt);
    QueryResult ExecuteCreateAlarm(const CreateAlarmStmt& stmt);
    QueryResult ExecuteCreateUser(const CreateUserStmt& stmt);
    QueryResult ExecuteAlterSystem(const AlterSystemStmt& stmt);

    // 格式化输出
    std::string FormatValue(const DataPoint& point);
    std::string FormatTimestamp(Timestamp ts);

    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    SQLParser parser_;
};

} // namespace minitsdb
