#include "executor.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] %s\n", message); \
            failures++; \
            return; \
        } \
    } while (0)

#define ASSERT_STRING(expected, actual, message) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "[FAIL] %s\n  expected: %s\n  actual:   %s\n", message, expected, actual); \
            failures++; \
            return; \
        } \
    } while (0)

static void make_test_db_root(const char *root) {
    char error[SQL_ERROR_SIZE];
    if (!ensure_directory_recursive(root, error, sizeof(error))) {
        fprintf(stderr, "failed to prepare test directory: %s\n", error);
        exit(1);
    }
}

static void test_parse_insert(void) {
    SQLScript script;
    char error[SQL_ERROR_SIZE];
    const char *sql = "INSERT INTO demo.students (id, name, major) VALUES (1, 'Alice', 'DB');";

    memset(&script, 0, sizeof(script));

    /* INSERT 핵심 구조가 AST에 정확히 들어가는지 확인한다. */
    ASSERT_TRUE(parse_sql_script(sql, &script, error, sizeof(error)), error);
    ASSERT_TRUE(script.count == 1, "expected one parsed statement");
    ASSERT_TRUE(script.items[0].type == STATEMENT_INSERT, "expected INSERT statement");
    ASSERT_STRING("demo", script.items[0].as.insert.target.schema, "schema mismatch");
    ASSERT_STRING("students", script.items[0].as.insert.target.table, "table mismatch");
    ASSERT_TRUE(script.items[0].as.insert.columns.count == 3, "expected three insert columns");
    ASSERT_STRING("Alice", script.items[0].as.insert.values.items[1], "string literal parse mismatch");
    free_script(&script);
}

static void test_parse_select_where(void) {
    SQLScript script;
    char error[SQL_ERROR_SIZE];
    const char *sql = "SELECT id, name FROM demo.students WHERE id = 1;";

    memset(&script, 0, sizeof(script));

    /* WHERE 절이 있더라도 컬럼, 값, 활성화 상태가 정확히 잡히는지 본다. */
    ASSERT_TRUE(parse_sql_script(sql, &script, error, sizeof(error)), error);
    ASSERT_TRUE(script.count == 1, "expected one parsed statement");
    ASSERT_TRUE(script.items[0].type == STATEMENT_SELECT, "expected SELECT statement");
    ASSERT_TRUE(script.items[0].as.select.where.enabled, "WHERE clause should be enabled");
    ASSERT_STRING("id", script.items[0].as.select.where.column, "WHERE column mismatch");
    ASSERT_STRING("1", script.items[0].as.select.where.value, "WHERE value mismatch");
    free_script(&script);
}

static void test_insert_and_select_roundtrip(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    InsertStatement insert_statement;
    SelectStatement select_statement;
    QueryResult result;
    size_t affected_rows = 0;

    make_test_db_root(root);

    /* 테스트용 스키마와 빈 데이터 파일을 먼저 만든다. */
    ASSERT_TRUE(
        write_text_file("tests/tmp/unit_db/demo/students.schema", "id|name|major", error, sizeof(error)),
        error
    );
    ASSERT_TRUE(
        write_text_file("tests/tmp/unit_db/demo/students.data", "", error, sizeof(error)),
        error
    );

    /* INSERT 입력을 코드로 직접 구성해 스토리지 계층까지 바로 검증한다. */
    memset(&insert_statement, 0, sizeof(insert_statement));
    insert_statement.target.schema = sql_strdup("demo");
    insert_statement.target.table = sql_strdup("students");
    ASSERT_TRUE(string_list_append(&insert_statement.columns, "id"), "append id column");
    ASSERT_TRUE(string_list_append(&insert_statement.columns, "name"), "append name column");
    ASSERT_TRUE(string_list_append(&insert_statement.columns, "major"), "append major column");
    ASSERT_TRUE(string_list_append(&insert_statement.values, "1"), "append id value");
    ASSERT_TRUE(string_list_append(&insert_statement.values, "Alice|Kim"), "append name value");
    ASSERT_TRUE(string_list_append(&insert_statement.values, "DB"), "append major value");

    ASSERT_TRUE(append_insert_row(root, &insert_statement, &affected_rows, error, sizeof(error)), error);
    ASSERT_TRUE(affected_rows == 1, "expected one affected row");

    /* 같은 데이터를 SELECT로 다시 읽어 escape/restore 흐름까지 한 번에 본다. */
    memset(&select_statement, 0, sizeof(select_statement));
    select_statement.source.schema = sql_strdup("demo");
    select_statement.source.table = sql_strdup("students");
    select_statement.select_all = true;
    select_statement.where.enabled = true;
    select_statement.where.column = sql_strdup("id");
    select_statement.where.value = sql_strdup("1");

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_statement, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "expected one selected row");
    ASSERT_STRING("Alice|Kim", result.rows[0].values.items[1], "escaped field should roundtrip");

    free_qualified_name(&insert_statement.target);
    string_list_free(&insert_statement.columns);
    string_list_free(&insert_statement.values);
    free_qualified_name(&select_statement.source);
    free(select_statement.where.column);
    free(select_statement.where.value);
    free_query_result(&result);
}

int main(void) {
    test_parse_insert();
    test_parse_select_where();
    test_insert_and_select_roundtrip();

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed.\n", failures);
        return 1;
    }

    puts("All unit tests passed.");
    return 0;
}
