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

static void prepare_schema(const char *root, const char *schema_line) {
    char error[SQL_ERROR_SIZE];
    make_test_db_root(root);
    ASSERT_TRUE(write_text_file("tests/tmp/unit_db/demo/students.schema", schema_line, error, sizeof(error)), error);
    ASSERT_TRUE(write_text_file("tests/tmp/unit_db/demo/students.data", "", error, sizeof(error)), error);
}

static void fill_insert_statement(InsertStatement *insert, const char *student_no, const char *name, const char *major) {
    memset(insert, 0, sizeof(*insert));
    insert->target.schema = sql_strdup("demo");
    insert->target.table = sql_strdup("students");
    ASSERT_TRUE(string_list_append(&insert->columns, "student_no"), "append student_no column");
    ASSERT_TRUE(string_list_append(&insert->columns, "name"), "append name column");
    ASSERT_TRUE(string_list_append(&insert->columns, "major"), "append major column");
    ASSERT_TRUE(string_list_append(&insert->values, student_no), "append student_no value");
    ASSERT_TRUE(string_list_append(&insert->values, name), "append name value");
    ASSERT_TRUE(string_list_append(&insert->values, major), "append major value");
}

static void test_parse_insert(void) {
    SQLScript script;
    char error[SQL_ERROR_SIZE];
    const char *sql = "INSERT INTO demo.students (name, major) VALUES ('Alice', 'DB');";

    memset(&script, 0, sizeof(script));

    ASSERT_TRUE(parse_sql_script(sql, &script, error, sizeof(error)), error);
    ASSERT_TRUE(script.count == 1, "expected one parsed statement");
    ASSERT_TRUE(script.items[0].type == STATEMENT_INSERT, "expected INSERT statement");
    ASSERT_STRING("demo", script.items[0].as.insert.target.schema, "schema mismatch");
    ASSERT_STRING("students", script.items[0].as.insert.target.table, "table mismatch");
    ASSERT_TRUE(script.items[0].as.insert.columns.count == 2, "expected two insert columns");
    ASSERT_STRING("Alice", script.items[0].as.insert.values.items[0], "string literal parse mismatch");
    free_script(&script);
}

static void test_parse_select_where(void) {
    SQLScript script;
    char error[SQL_ERROR_SIZE];
    const char *sql = "SELECT id, name FROM demo.students WHERE id = 1;";

    memset(&script, 0, sizeof(script));

    ASSERT_TRUE(parse_sql_script(sql, &script, error, sizeof(error)), error);
    ASSERT_TRUE(script.count == 1, "expected one parsed statement");
    ASSERT_TRUE(script.items[0].type == STATEMENT_SELECT, "expected SELECT statement");
    ASSERT_TRUE(script.items[0].as.select.where.enabled, "WHERE clause should be enabled");
    ASSERT_STRING("id", script.items[0].as.select.where.column, "WHERE column mismatch");
    ASSERT_STRING("1", script.items[0].as.select.where.value, "WHERE value mismatch");
    ASSERT_TRUE(script.items[0].as.select.where.op == WHERE_OP_EQUAL, "WHERE op should be '='");
    free_script(&script);
}

static void test_parse_select_where_range(void) {
    SQLScript script;
    char error[SQL_ERROR_SIZE];
    const char *sql = "SELECT id FROM demo.students WHERE id >= 2;";

    memset(&script, 0, sizeof(script));

    ASSERT_TRUE(parse_sql_script(sql, &script, error, sizeof(error)), error);
    ASSERT_TRUE(script.count == 1, "expected one parsed statement");
    ASSERT_TRUE(script.items[0].type == STATEMENT_SELECT, "expected SELECT statement");
    ASSERT_TRUE(script.items[0].as.select.where.enabled, "WHERE clause should be enabled");
    ASSERT_TRUE(script.items[0].as.select.where.op == WHERE_OP_GREATER_EQUAL, "WHERE op should be '>='");
    free_script(&script);
}

static void test_index_insert_find(void) {
    RowRef ref = 0;

    ASSERT_TRUE(index_init() == 0, "index_init should succeed");
    ASSERT_TRUE(index_insert(10, 1234) == 0, "index_insert should insert first key");
    ASSERT_TRUE(index_insert(10, 9999) == 1, "duplicate id should be rejected");
    ASSERT_TRUE(index_find(10, &ref) == 0, "index_find should find inserted key");
    ASSERT_TRUE(ref == 1234, "index_find should return same row_ref");
    ASSERT_TRUE(index_find(9999, &ref) == 1, "unknown id should not be found");
}

static void test_auto_id_and_where_id(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    InsertStatement insert1;
    InsertStatement insert2;
    SelectStatement select_stmt;
    QueryResult result;
    size_t affected = 0;

    prepare_schema(root, "id|student_no|name|major");

    fill_insert_statement(&insert1, "2026000001", "Alice", "DB");
    fill_insert_statement(&insert2, "2026000002", "Bob", "AI");

    ASSERT_TRUE(append_insert_row(root, &insert1, &affected, NULL, NULL, error, sizeof(error)), error);
    ASSERT_TRUE(affected == 1, "first insert should affect one row");
    ASSERT_TRUE(append_insert_row(root, &insert2, &affected, NULL, NULL, error, sizeof(error)), error);
    ASSERT_TRUE(affected == 1, "second insert should affect one row");

    memset(&select_stmt, 0, sizeof(select_stmt));
    select_stmt.source.schema = sql_strdup("demo");
    select_stmt.source.table = sql_strdup("students");
    select_stmt.select_all = true;
    select_stmt.where.enabled = true;
    select_stmt.where.column = sql_strdup("id");
    select_stmt.where.value = sql_strdup("2");

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "WHERE id should return one row");
    ASSERT_STRING("2", result.rows[0].values.items[0], "auto id should be generated as 2");
    ASSERT_STRING("Bob", result.rows[0].values.items[2], "name should match id lookup");

    free_qualified_name(&insert1.target);
    string_list_free(&insert1.columns);
    string_list_free(&insert1.values);
    free_qualified_name(&insert2.target);
    string_list_free(&insert2.columns);
    string_list_free(&insert2.values);
    free_qualified_name(&select_stmt.source);
    free(select_stmt.where.column);
    free(select_stmt.where.value);
    free_query_result(&result);
}

static void test_where_name_linear(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    SelectStatement select_stmt;
    QueryResult result;

    memset(&select_stmt, 0, sizeof(select_stmt));
    select_stmt.source.schema = sql_strdup("demo");
    select_stmt.source.table = sql_strdup("students");
    select_stmt.select_all = false;
    ASSERT_TRUE(string_list_append(&select_stmt.columns, "name"), "append select column");
    select_stmt.where.enabled = true;
    select_stmt.where.column = sql_strdup("major");
    select_stmt.where.value = sql_strdup("AI");

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "WHERE major should return one row");
    ASSERT_STRING("Bob", result.rows[0].values.items[0], "linear WHERE should match Bob");

    free_qualified_name(&select_stmt.source);
    string_list_free(&select_stmt.columns);
    free(select_stmt.where.column);
    free(select_stmt.where.value);
    free_query_result(&result);
}

static void test_student_no_unique_and_linear_query(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    InsertStatement dup_insert;
    SelectStatement select_stmt;
    QueryResult result;
    size_t affected = 0;

    fill_insert_statement(&dup_insert, "2026000002", "Eve", "Math");
    ASSERT_TRUE(!append_insert_row(root, &dup_insert, &affected, NULL, NULL, error, sizeof(error)),
        "duplicate student_no insert should fail");
    ASSERT_TRUE(strstr(error, "duplicate student_no") != NULL, "duplicate student_no message should be returned");
    free_qualified_name(&dup_insert.target);
    string_list_free(&dup_insert.columns);
    string_list_free(&dup_insert.values);

    memset(&select_stmt, 0, sizeof(select_stmt));
    select_stmt.source.schema = sql_strdup("demo");
    select_stmt.source.table = sql_strdup("students");
    select_stmt.select_all = false;
    ASSERT_TRUE(string_list_append(&select_stmt.columns, "name"), "append select name");
    select_stmt.where.enabled = true;
    select_stmt.where.column = sql_strdup("student_no");
    select_stmt.where.value = sql_strdup("2026000002");
    select_stmt.where.op = WHERE_OP_EQUAL;

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "WHERE student_no should return exactly one row");
    ASSERT_STRING("Bob", result.rows[0].values.items[0], "student_no lookup should match Bob");

    free_qualified_name(&select_stmt.source);
    string_list_free(&select_stmt.columns);
    free(select_stmt.where.column);
    free(select_stmt.where.value);
    free_query_result(&result);
}

static void test_where_id_not_found(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    SelectStatement select_stmt;
    QueryResult result;

    memset(&select_stmt, 0, sizeof(select_stmt));
    select_stmt.source.schema = sql_strdup("demo");
    select_stmt.source.table = sql_strdup("students");
    select_stmt.select_all = true;
    select_stmt.where.enabled = true;
    select_stmt.where.column = sql_strdup("id");
    select_stmt.where.value = sql_strdup("999");

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 0, "unknown id should return empty result");

    free_qualified_name(&select_stmt.source);
    free(select_stmt.where.column);
    free(select_stmt.where.value);
    free_query_result(&result);
}

static void test_where_id_range(void) {
    const char *root = "tests/tmp/unit_db";
    char error[SQL_ERROR_SIZE];
    SelectStatement gt_stmt;
    SelectStatement le_stmt;
    QueryResult result;

    memset(&gt_stmt, 0, sizeof(gt_stmt));
    gt_stmt.source.schema = sql_strdup("demo");
    gt_stmt.source.table = sql_strdup("students");
    gt_stmt.select_all = true;
    gt_stmt.where.enabled = true;
    gt_stmt.where.column = sql_strdup("id");
    gt_stmt.where.value = sql_strdup("1");
    gt_stmt.where.op = WHERE_OP_GREATER;

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &gt_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "WHERE id > 1 should return one row");
    ASSERT_STRING("2", result.rows[0].values.items[0], "range query should return id=2");
    free_query_result(&result);

    memset(&le_stmt, 0, sizeof(le_stmt));
    le_stmt.source.schema = sql_strdup("demo");
    le_stmt.source.table = sql_strdup("students");
    le_stmt.select_all = true;
    le_stmt.where.enabled = true;
    le_stmt.where.column = sql_strdup("id");
    le_stmt.where.value = sql_strdup("1");
    le_stmt.where.op = WHERE_OP_LESS_EQUAL;

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &le_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "WHERE id <= 1 should return one row");
    ASSERT_STRING("1", result.rows[0].values.items[0], "range query should return id=1");

    free_qualified_name(&gt_stmt.source);
    free(gt_stmt.where.column);
    free(gt_stmt.where.value);
    free_qualified_name(&le_stmt.source);
    free(le_stmt.where.column);
    free(le_stmt.where.value);
    free_query_result(&result);
}

static void test_student_no_predicate_not_index_path(void) {
    SelectStatement stmt;
    uint64_t parsed_id = 0;

    memset(&stmt, 0, sizeof(stmt));
    stmt.where.enabled = true;
    stmt.where.column = sql_strdup("student_no");
    stmt.where.value = sql_strdup("2026000002");
    stmt.where.op = WHERE_OP_EQUAL;

    ASSERT_TRUE(!is_id_equality_predicate(&stmt, &parsed_id), "student_no predicate must not use id index path");

    free(stmt.where.column);
    free(stmt.where.value);
}

static void test_text_to_binary_migration(void) {
    const char *root = "tests/tmp/migration_db";
    char error[SQL_ERROR_SIZE];
    SelectStatement select_stmt;
    QueryResult result;
    FILE *file;
    unsigned char first;

    make_test_db_root(root);
    ASSERT_TRUE(write_text_file("tests/tmp/migration_db/demo/students.schema", "id|name|major", error, sizeof(error)), error);
    ASSERT_TRUE(write_text_file("tests/tmp/migration_db/demo/students.data", "1|Legacy|Text\n", error, sizeof(error)), error);

    memset(&select_stmt, 0, sizeof(select_stmt));
    select_stmt.source.schema = sql_strdup("demo");
    select_stmt.source.table = sql_strdup("students");
    select_stmt.select_all = true;

    memset(&result, 0, sizeof(result));
    ASSERT_TRUE(run_select_query(root, &select_stmt, &result, error, sizeof(error)), error);
    ASSERT_TRUE(result.row_count == 1, "migrated data should be queryable");
    ASSERT_STRING("Legacy", result.rows[0].values.items[1], "migrated name should match");

    file = fopen("tests/tmp/migration_db/demo/students.data", "rb");
    ASSERT_TRUE(file != NULL, "migrated data file should exist");
    ASSERT_TRUE(fread(&first, 1, 1, file) == 1, "migrated data should not be empty");
    fclose(file);
    ASSERT_TRUE(first != '1', "migrated data should be binary format, not text line");

    free_qualified_name(&select_stmt.source);
    free_query_result(&result);
}

int main(void) {
    test_parse_insert();
    test_parse_select_where();
    test_parse_select_where_range();
    test_index_insert_find();
    test_auto_id_and_where_id();
    test_where_name_linear();
    test_student_no_unique_and_linear_query();
    test_where_id_not_found();
    test_where_id_range();
    test_student_no_predicate_not_index_path();
    test_text_to_binary_migration();

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed.\n", failures);
        return 1;
    }

    puts("All unit tests passed.");
    return 0;
}
