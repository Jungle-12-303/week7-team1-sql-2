#ifndef MINI_SQL_STORAGE_H
#define MINI_SQL_STORAGE_H

#include <stddef.h>

#include "parser.h"

typedef struct {
    QualifiedName name;
    StringList columns;
    char *schema_path;
    char *data_path;
} TableDefinition;

typedef struct {
    StringList values;
} ResultRow;

typedef struct {
    StringList columns;
    ResultRow *rows;
    size_t row_count;
    size_t row_capacity;
} QueryResult;

/* 테이블 로딩 및 질의 실행 */
bool load_table_definition(
    const char *db_root,
    const QualifiedName *name,
    TableDefinition *table,
    char *error,
    size_t error_size
);
bool append_insert_row(
    const char *db_root,
    const InsertStatement *statement,
    size_t *affected_rows,
    char *error,
    size_t error_size
);
bool run_select_query(
    const char *db_root,
    const SelectStatement *statement,
    QueryResult *result,
    char *error,
    size_t error_size
);

/* 결과 메모리 해제 */
void free_table_definition(TableDefinition *table);
void free_query_result(QueryResult *result);

#endif
