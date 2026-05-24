#include "minitsdb.h"
#include <stdio.h>

int main() {
    printf("=== MiniTSDB C SDK Test ===\n\n");

    // 连接
    printf("Connecting...\n");
    minitsdb_conn* conn = minitsdb_connect("127.0.0.1", 8086, "admin", "admin123");
    if (!conn) {
        printf("FAIL: Connection failed (server not running?)\n");
        printf("Start server with: ./build/release/minitsdb.exe\n");
        return 1;
    }
    printf("OK: Connected\n\n");

    // 查询最新值
    printf("Querying...\n");
    minitsdb_result* res = minitsdb_query(conn, "SELECT 1");

    if (!res) {
        printf("FAIL: Query returned NULL\n");
        minitsdb_disconnect(conn);
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

    printf("\n=== SDK Test Complete ===\n");
    return 0;
}
