#ifndef MINI_SQL_STORAGE_H
#define MINI_SQL_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "parser.h"

typedef uint64_t RowRef;

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

typedef int (*RowCallback)(RowRef ref, const StringList *values, void *ctx);

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

/* 바이너리 저장 + id 인덱스 인터페이스 */
uint64_t next_id(void);
int index_init(void);
int index_insert(uint64_t id, RowRef ref);
int index_find(uint64_t id, RowRef *out_ref);
int binary_writer_append_row(const StringList *values, RowRef *out_ref);
int binary_reader_read_row_at(RowRef ref, StringList *out_values);
int binary_reader_scan_all(RowCallback cb, void *ctx);
int migrate_text_data_to_binary(const char *text_path, const char *bin_path);
bool is_id_equality_predicate(const SelectStatement *statement, uint64_t *out_id);
int run_select_by_id(uint64_t id, QueryResult *out);
int run_select_linear(const SelectStatement *statement, QueryResult *out);

/* 결과 메모리 해제 */
void free_table_definition(TableDefinition *table);
void free_query_result(QueryResult *result);

#endif
