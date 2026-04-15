#ifndef MINI_SQL_EXECUTOR_H
#define MINI_SQL_EXECUTOR_H

#include <stdio.h>

#include "storage.h"
#include "parser.h"

typedef enum {
    EXECUTION_INSERT,
    EXECUTION_SELECT
} ExecutionKind;

typedef struct {
    ExecutionKind kind;
    size_t affected_rows;
    StringList insert_columns;
    StringList insert_values;
    QueryResult query_result;
} ExecutionResult;

/* 실행 및 결과 처리 */
bool execute_statement(
    const Statement *statement,
    const char *db_root,
    ExecutionResult *result,
    char *error,
    size_t error_size
);
void free_execution_result(ExecutionResult *result);
void print_execution_result(const ExecutionResult *result, FILE *stream);

#endif
