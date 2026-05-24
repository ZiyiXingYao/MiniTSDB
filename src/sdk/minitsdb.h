#ifndef MINITSDB_C_SDK_H
#define MINITSDB_C_SDK_H

#ifdef _WIN32
    #ifdef MINITSDB_SDK_BUILD
        #define MINITSDB_API __declspec(dllexport)
    #else
        #define MINITSDB_API __declspec(dllimport)
    #endif
#else
    #define MINITSDB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 不透明句柄类型
typedef struct minitsdb_conn minitsdb_conn;
typedef struct minitsdb_result minitsdb_result;

// ---------- 连接管理 ----------

// 连接到 MiniTSDB 服务器
// host: 服务器地址, port: 端口, user/password: 认证
// 返回连接句柄，失败返回 NULL
MINITSDB_API minitsdb_conn* minitsdb_connect(const char* host, int port,
                                              const char* user, const char* password);

// 断开连接
MINITSDB_API void minitsdb_disconnect(minitsdb_conn* conn);

// ---------- 查询 ----------

// 执行 SQL 查询
// 返回结果集，失败返回 NULL
MINITSDB_API minitsdb_result* minitsdb_query(minitsdb_conn* conn,
                                              const char* sql);

// ---------- 结果集访问 ----------

// 获取行数
MINITSDB_API int minitsdb_result_rows(const minitsdb_result* res);

// 获取列数
MINITSDB_API int minitsdb_result_cols(const minitsdb_result* res);

// 获取列名
MINITSDB_API const char* minitsdb_result_col_name(const minitsdb_result* res,
                                                    int col);

// 获取值（指定行、列）
// 返回字符串，使用后不需要释放
MINITSDB_API const char* minitsdb_result_value(const minitsdb_result* res,
                                                int row, int col);

// 获取错误信息
MINITSDB_API const char* minitsdb_result_error(const minitsdb_result* res);

// 获取影响行数（INSERT 等）
MINITSDB_API int minitsdb_result_affected(const minitsdb_result* res);

// 释放结果集
MINITSDB_API void minitsdb_result_free(minitsdb_result* res);

#ifdef __cplusplus
}
#endif

#endif /* MINITSDB_C_SDK_H */
