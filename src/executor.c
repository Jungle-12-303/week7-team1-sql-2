#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool execute_statement(
    const Statement *statement,
    const char *db_root,
    ExecutionResult *result,
    char *error,
    size_t error_size
) {
    memset(result, 0, sizeof(*result));

    /* 파서가 정한 문장 타입에 맞춰 스토리지 동작으로 연결한다. */
    if (statement->type == STATEMENT_INSERT) {
        result->kind = EXECUTION_INSERT;
        return append_insert_row(
            db_root,
            &statement->as.insert,
            &result->affected_rows,
            &result->insert_columns,
            &result->insert_values,
            error,
            error_size
        );
    }

    if (statement->type == STATEMENT_SELECT) {
        result->kind = EXECUTION_SELECT;
        return run_select_query(db_root, &statement->as.select, &result->query_result, error, error_size);
    }

    snprintf(error, error_size, "unsupported statement type");
    return false;
}

void free_execution_result(ExecutionResult *result) {
    /* SELECT 결과만 동적 결과 테이블을 들고 있으므로 별도 해제가 필요하다. */
    if (result->kind == EXECUTION_SELECT) {
        free_query_result(&result->query_result);
    }
    if (result->kind == EXECUTION_INSERT) {
        string_list_free(&result->insert_columns);
        string_list_free(&result->insert_values);
    }

    memset(result, 0, sizeof(*result));
}

static void print_separator(const size_t *widths, size_t count, FILE *stream) {
    size_t index;
    size_t inner;

    fputc('+', stream);
    for (index = 0; index < count; ++index) {
        for (inner = 0; inner < widths[index] + 2; ++inner) {
            fputc('-', stream);
        }
        fputc('+', stream);
    }
    fputc('\n', stream);
}

void print_execution_result(const ExecutionResult *result, FILE *stream) {
    if (result->kind == EXECUTION_INSERT) {
        fprintf(stream, "INSERT %zu\n", result->affected_rows);
        if (result->insert_columns.count == result->insert_values.count && result->insert_columns.count > 0) {
            size_t i;
            fprintf(stream, "입력된 row:\n");
            for (i = 0; i < result->insert_columns.count; ++i) {
                fprintf(stream, "  - %s: %s\n", result->insert_columns.items[i], result->insert_values.items[i]);
            }
        }
        return;
    }

    if (result->kind == EXECUTION_SELECT) {
        const QueryResult *query = &result->query_result;
        size_t *widths;
        size_t column_index;
        size_t row_index;

        widths = (size_t *) calloc(query->columns.count, sizeof(size_t));
        if (widths == NULL) {
            fprintf(stream, "out of memory while printing result\n");
            return;
        }

        /* 먼저 헤더 길이로 열 너비를 잡고, 이후 데이터 길이를 반영해 확장한다. */
        for (column_index = 0; column_index < query->columns.count; ++column_index) {
            widths[column_index] = strlen(query->columns.items[column_index]);
        }

        for (row_index = 0; row_index < query->row_count; ++row_index) {
            for (column_index = 0; column_index < query->columns.count; ++column_index) {
                size_t cell_length = strlen(query->rows[row_index].values.items[column_index]);
                if (cell_length > widths[column_index]) {
                    widths[column_index] = cell_length;
                }
            }
        }

        /* 계산된 열 너비를 기준으로 헤더와 본문을 같은 폭으로 맞춘다. */
        print_separator(widths, query->columns.count, stream);
        fputc('|', stream);
        for (column_index = 0; column_index < query->columns.count; ++column_index) {
            fprintf(stream, " %-*s |", (int) widths[column_index], query->columns.items[column_index]);
        }
        fputc('\n', stream);
        print_separator(widths, query->columns.count, stream);

        for (row_index = 0; row_index < query->row_count; ++row_index) {
            fputc('|', stream);
            for (column_index = 0; column_index < query->columns.count; ++column_index) {
                fprintf(
                    stream,
                    " %-*s |",
                    (int) widths[column_index],
                    query->rows[row_index].values.items[column_index]
                );
            }
            fputc('\n', stream);
        }

        print_separator(widths, query->columns.count, stream);
        fprintf(stream, "(%zu rows)\n", query->row_count);

        free(widths);
    }
}
