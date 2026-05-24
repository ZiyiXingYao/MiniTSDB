#include "minitsdb.h"
#include "test_server.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    // 解析命令行端口参数，或使用默认
    int port = 9180;
    if (argc > 1) port = atoi(argv[1]);

    // 如果指定端口为 0，让 TestServer 自动选择
    printf("=== MiniTSDB C SDK Test ===\n\n");

    // 启动服务端
    printf("Starting server...\n");
    int actual_port = test_server_start(port, 10000);
    if (actual_port <= 0) {
        printf("FAIL: Could not start server\n");
        return 1;
    }
    printf("OK: Server started on port %d\n\n", actual_port);

    // 连接
    printf("Connecting to port %d...\n", actual_port);
    minitsdb_conn* conn = minitsdb_connect("127.0.0.1", actual_port, "admin", "admin123");
    if (!conn) {
        printf("FAIL: Connection failed\n");
        test_server_stop();
        return 1;
    }
    printf("OK: Connected\n\n");

    // 查询测试
    printf("Querying...\n");
    minitsdb_result* res = minitsdb_query(conn, "SELECT 1");

    if (!res) {
        printf("FAIL: Query returned NULL\n");
        minitsdb_disconnect(conn);
        test_server_stop();
        return 1;
    }

    const char* err = minitsdb_result_error(res);
    if (err && err[0] != '\0') {
        printf("Query error: %s\n", err);
    }

    printf("Columns: %d\n", minitsdb_result_cols(res));
    printf("Rows: %d\n", minitsdb_result_rows(res));

    for (int r = 0; r < minitsdb_result_rows(res); r++) {
        printf("Row %d:\n", r);
        for (int c = 0; c < minitsdb_result_cols(res); c++) {
            printf("  %s = %s\n",
                   minitsdb_result_col_name(res, c),
                   minitsdb_result_value(res, r, c));
        }
    }

    minitsdb_result_free(res);
    minitsdb_disconnect(conn);

    // 停止服务端
    test_server_stop();

    printf("\n=== SDK Test Complete ===\n");
    return 0;
}
