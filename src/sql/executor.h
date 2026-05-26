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

    QueryResult Execute(const SQLStmt& stmt);
    QueryResult ExecuteSQL(const std::string& sql);

    // 设置当前数据库（USE 语句）
    void SetCurrentDB(const std::string& db) { current_db_ = db; }
    const std::string& GetCurrentDB() const { return current_db_; }

    // 设置当前 token（用于权限检查）
    void SetToken(const std::string& token) { token_ = token; }

private:
    QueryResult ExecuteInsert(const InsertStmt& stmt);
    QueryResult ExecuteSelect(const SelectStmt& stmt);
    QueryResult ExecuteSelectLatest(const SelectStmt& stmt);
    QueryResult ExecuteSelectRaw(const SelectStmt& stmt);
    QueryResult ExecuteSelectAggregate(const SelectStmt& stmt);
    QueryResult ExecuteSnapshot(const SelectStmt& stmt);

    // 已有
    QueryResult ExecuteCreateTag(const CreateTagStmt& stmt);
    QueryResult ExecuteCreateAlarm(const CreateAlarmStmt& stmt);
    QueryResult ExecuteCreateUser(const CreateUserStmt& stmt);
    QueryResult ExecuteAlterSystem(const AlterSystemStmt& stmt);

    // 新增 DDL
    QueryResult ExecuteDropTag(const DropTagStmt& stmt);
    QueryResult ExecuteDropAlarm(const DropAlarmStmt& stmt);
    QueryResult ExecuteDropUser(const DropUserStmt& stmt);
    QueryResult ExecuteDropTable(const DropTableStmt& stmt);
    QueryResult ExecuteDropDatabase(const DropDatabaseStmt& stmt);
    QueryResult ExecuteCreateDatabase(const CreateDatabaseStmt& stmt);
    QueryResult ExecuteUse(const UseStmt& stmt);
    QueryResult ExecuteCreateTable(const CreateTableStmt& stmt);
    QueryResult ExecuteCreateTags(const CreateTagsStmt& stmt);
    QueryResult ExecuteAlterTag(const AlterTagStmt& stmt);
    QueryResult ExecuteAlterAlarm(const AlterAlarmStmt& stmt);
    QueryResult ExecuteAlterUser(const AlterUserStmt& stmt);

    // 新增 DML
    QueryResult ExecuteDelete(const DeleteStmt& stmt);
    QueryResult ExecuteUpdate(const UpdateStmt& stmt);

    // 新增 SHOW
    QueryResult ExecuteShow(const ShowStmt& stmt);

    // 权限检查
    bool CheckPermission(const std::string& operation);
    std::string GetOperationName(const SQLStmt& stmt);

    std::string FormatValue(const DataPoint& point);
    std::string FormatTimestamp(Timestamp ts);

    std::shared_ptr<StorageEngine> engine_;
    std::shared_ptr<LatestCache> cache_;
    std::shared_ptr<AuthManager> auth_;
    SQLParser parser_;
    std::string current_db_;
    std::string token_;
};

} // namespace minitsdb
