#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool build_table_paths(
    const char *db_root,
    const QualifiedName *name,
    char **schema_path,
    char **data_path,
    char *error,
    size_t error_size
) {
    const char *schema = name->schema;
    size_t schema_length;
    size_t data_length;

    if (name->table == NULL || name->table[0] == '\0') {
        snprintf(error, error_size, "table name is empty");
        return false;
    }

    /* 스키마가 있으면 db_root/schema/table.*, 없으면 db_root/table.* 형태로 만든다. */
    if (schema != NULL && schema[0] != '\0') {
        schema_length = strlen(db_root) + strlen(schema) + strlen(name->table) + strlen(".schema") + 3;
        data_length = strlen(db_root) + strlen(schema) + strlen(name->table) + strlen(".data") + 3;

        *schema_path = (char *) malloc(schema_length);
        *data_path = (char *) malloc(data_length);
        if (*schema_path == NULL || *data_path == NULL) {
            snprintf(error, error_size, "out of memory while building file paths");
            free(*schema_path);
            free(*data_path);
            *schema_path = NULL;
            *data_path = NULL;
            return false;
        }

        snprintf(*schema_path, schema_length, "%s/%s/%s.schema", db_root, schema, name->table);
        snprintf(*data_path, data_length, "%s/%s/%s.data", db_root, schema, name->table);
    } else {
        schema_length = strlen(db_root) + strlen(name->table) + strlen(".schema") + 2;
        data_length = strlen(db_root) + strlen(name->table) + strlen(".data") + 2;

        *schema_path = (char *) malloc(schema_length);
        *data_path = (char *) malloc(data_length);
        if (*schema_path == NULL || *data_path == NULL) {
            snprintf(error, error_size, "out of memory while building file paths");
            free(*schema_path);
            free(*data_path);
            *schema_path = NULL;
            *data_path = NULL;
            return false;
        }

        snprintf(*schema_path, schema_length, "%s/%s.schema", db_root, name->table);
        snprintf(*data_path, data_length, "%s/%s.data", db_root, name->table);
    }

    return true;
}

static int find_column_index(const StringList *columns, const char *name) {
    size_t index;

    for (index = 0; index < columns->count; ++index) {
        if (sql_stricmp(columns->items[index], name) == 0) {
            return (int) index;
        }
    }

    return -1;
}

static bool split_pipe_line(const char *line, StringList *values, char *error, size_t error_size) {
    size_t index = 0;
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    memset(values, 0, sizeof(*values));

    while (true) {
        char ch = line[index];

        /* 줄 끝에 도달하면 마지막 필드를 결과 리스트에 확정한다. */
        if (ch == '\0' || ch == '\n') {
            if (buffer == NULL) {
                buffer = (char *) malloc(1);
                if (buffer == NULL) {
                    snprintf(error, error_size, "out of memory while parsing row");
                    return false;
                }
            }
            buffer[length] = '\0';
            if (!string_list_append(values, buffer)) {
                free(buffer);
                string_list_free(values);
                snprintf(error, error_size, "out of memory while parsing row");
                return false;
            }
            free(buffer);
            return true;
        }

        if (ch == '\r') {
            index++;
            continue;
        }

        /* 파이프를 만나면 지금까지 읽은 버퍼를 하나의 셀 값으로 저장한다. */
        if (ch == '|') {
            if (buffer == NULL) {
                buffer = (char *) malloc(1);
                if (buffer == NULL) {
                    snprintf(error, error_size, "out of memory while parsing row");
                    return false;
                }
            }
            buffer[length] = '\0';
            if (!string_list_append(values, buffer)) {
                free(buffer);
                string_list_free(values);
                snprintf(error, error_size, "out of memory while parsing row");
                return false;
            }
            free(buffer);
            buffer = NULL;
            length = 0;
            capacity = 0;
            index++;
            continue;
        }

        if (length + 2 >= capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            char *new_buffer = (char *) realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                string_list_free(values);
                snprintf(error, error_size, "out of memory while parsing row");
                return false;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        /* 직렬화 때 이스케이프된 개행/파이프/백슬래시를 다시 원문으로 복원한다. */
        if (ch == '\\') {
            char next = line[index + 1];
            if (next == 'n') {
                buffer[length++] = '\n';
                index += 2;
                continue;
            }
            if (next == 'r') {
                buffer[length++] = '\r';
                index += 2;
                continue;
            }
            if (next == '|' || next == '\\') {
                buffer[length++] = next;
                index += 2;
                continue;
            }
        }

        buffer[length++] = ch;
        index++;
    }
}

static char *escape_field(const char *value) {
    size_t index;
    size_t capacity = 16;
    size_t length = 0;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        return NULL;
    }

    /* 저장 형식에서 의미가 겹치는 문자만 골라 두 글자 표현으로 바꾼다. */
    for (index = 0; value[index] != '\0'; ++index) {
        char ch = value[index];
        size_t extra = (ch == '\\' || ch == '|' || ch == '\n' || ch == '\r') ? 2 : 1;

        if (length + extra + 1 >= capacity) {
            size_t new_capacity = capacity * 2 + extra + 1;
            char *new_buffer = (char *) realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        if (ch == '\\') {
            buffer[length++] = '\\';
            buffer[length++] = '\\';
        } else if (ch == '|') {
            buffer[length++] = '\\';
            buffer[length++] = '|';
        } else if (ch == '\n') {
            buffer[length++] = '\\';
            buffer[length++] = 'n';
        } else if (ch == '\r') {
            buffer[length++] = '\\';
            buffer[length++] = 'r';
        } else {
            buffer[length++] = ch;
        }
    }

    buffer[length] = '\0';
    return buffer;
}

static char *serialize_row(const StringList *values) {
    size_t index;
    size_t total = 2;
    char **escaped = (char **) calloc(values->count, sizeof(char *));
    char *output;

    if (escaped == NULL) {
        return NULL;
    }

    /* 모든 셀을 먼저 escape한 뒤 최종 한 줄 크기를 계산한다. */
    for (index = 0; index < values->count; ++index) {
        escaped[index] = escape_field(values->items[index]);
        if (escaped[index] == NULL) {
            size_t free_index;
            for (free_index = 0; free_index < values->count; ++free_index) {
                free(escaped[free_index]);
            }
            free(escaped);
            return NULL;
        }
        total += strlen(escaped[index]) + 1;
    }

    output = (char *) malloc(total);
    if (output == NULL) {
        for (index = 0; index < values->count; ++index) {
            free(escaped[index]);
        }
        free(escaped);
        return NULL;
    }

    output[0] = '\0';

    /* escape된 셀을 파이프로 이어 붙여 데이터 파일의 한 줄을 만든다. */
    for (index = 0; index < values->count; ++index) {
        strcat(output, escaped[index]);
        if (index + 1 < values->count) {
            strcat(output, "|");
        }
        free(escaped[index]);
    }
    strcat(output, "\n");
    free(escaped);
    return output;
}

static bool query_result_append_row(QueryResult *result, const StringList *values, char *error, size_t error_size) {
    size_t index;
    ResultRow *new_rows;
    ResultRow *row;

    /* 결과 행 배열이 꽉 차면 뒤에 이어 붙일 수 있게 확장한다. */
    if (result->row_count == result->row_capacity) {
        size_t new_capacity = result->row_capacity == 0 ? 4 : result->row_capacity * 2;
        new_rows = (ResultRow *) realloc(result->rows, sizeof(ResultRow) * new_capacity);
        if (new_rows == NULL) {
            snprintf(error, error_size, "out of memory while building result");
            return false;
        }
        result->rows = new_rows;
        result->row_capacity = new_capacity;
    }

    row = &result->rows[result->row_count];
    memset(row, 0, sizeof(*row));

    /* 원본 임시 버퍼와 분리하기 위해 셀 값을 하나씩 복사해 결과 행에 담는다. */
    for (index = 0; index < values->count; ++index) {
        if (!string_list_append(&row->values, values->items[index])) {
            string_list_free(&row->values);
            snprintf(error, error_size, "out of memory while building result");
            return false;
        }
    }

    result->row_count++;
    return true;
}

bool load_table_definition(
    const char *db_root,
    const QualifiedName *name,
    TableDefinition *table,
    char *error,
    size_t error_size
) {
    char *schema_content;

    memset(table, 0, sizeof(*table));

    if (!build_table_paths(db_root, name, &table->schema_path, &table->data_path, error, error_size)) {
        return false;
    }

    /* 경로뿐 아니라 테이블 이름 자체도 복사해 두어 이후 해제와 오류 메시지에 쓴다. */
    table->name.schema = sql_strdup(name->schema);
    table->name.table = sql_strdup(name->table);
    if ((name->schema != NULL && table->name.schema == NULL) || (name->table != NULL && table->name.table == NULL)) {
        free_table_definition(table);
        snprintf(error, error_size, "out of memory while loading table");
        return false;
    }

    schema_content = read_text_file(table->schema_path, error, error_size);
    if (schema_content == NULL) {
        free_table_definition(table);
        return false;
    }

    /* 스키마 파일 한 줄을 컬럼 이름 목록으로 복원한다. */
    if (!split_pipe_line(schema_content, &table->columns, error, error_size)) {
        free(schema_content);
        free_table_definition(table);
        return false;
    }

    if (table->columns.count == 0 || (table->columns.count == 1 && table->columns.items[0][0] == '\0')) {
        free(schema_content);
        free_table_definition(table);
        snprintf(error, error_size, "schema file is empty: %s", table->schema_path);
        return false;
    }

    free(schema_content);
    return true;
}

bool append_insert_row(
    
    const char *db_root,
    
    //파싱 결과 구조체 포인터
    const InsertStatement *statement,


    size_t *affected_rows,
    char *error,
    size_t error_size
) {
    TableDefinition table;
    StringList ordered_values;
    char *serialized_row;
    bool *assigned_columns = NULL;
    size_t column_index;

    if (statement->columns.count != statement->values.count) {
        snprintf(error, error_size, "INSERT column count and value count do not match");
        return false;
    }

    if (!load_table_definition(db_root, &statement->target, &table, error, error_size)) {
        return false;
    }

    memset(&ordered_values, 0, sizeof(ordered_values));
    assigned_columns = (bool *) calloc(table.columns.count, sizeof(bool));
    if (assigned_columns == NULL) {
        free_table_definition(&table);
        snprintf(error, error_size, "out of memory while preparing INSERT");
        return false;
    }

    /* 테이블 전체 컬럼 수에 맞춰 기본 슬롯을 만들고, 지정되지 않은 컬럼은 빈 문자열로 둔다. */
    for (column_index = 0; column_index < table.columns.count; ++column_index) {
        if (!string_list_append(&ordered_values, "")) {
            free_table_definition(&table);
            string_list_free(&ordered_values);
            free(assigned_columns);
            snprintf(error, error_size, "out of memory while building row");
            return false;
        }
    }

    /* INSERT에 들어온 컬럼명을 실제 스키마 순서에 맞는 위치로 다시 배치한다. */
    for (column_index = 0; column_index < statement->columns.count; ++column_index) {
        int target_index = find_column_index(&table.columns, statement->columns.items[column_index]);
        if (target_index < 0) {
            free_table_definition(&table);
            string_list_free(&ordered_values);
            free(assigned_columns);
            snprintf(error, error_size, "unknown column in INSERT: %s", statement->columns.items[column_index]);
            return false;
        }

        if (assigned_columns[target_index]) {
            free_table_definition(&table);
            string_list_free(&ordered_values);
            free(assigned_columns);
            snprintf(error, error_size, "duplicate column in INSERT: %s", statement->columns.items[column_index]);
            return false;
        }
        assigned_columns[target_index] = true;

        free(ordered_values.items[target_index]);
        ordered_values.items[target_index] = sql_strdup(statement->values.items[column_index]);
        if (ordered_values.items[target_index] == NULL) {
            free_table_definition(&table);
            string_list_free(&ordered_values);
            free(assigned_columns);
            snprintf(error, error_size, "out of memory while building row");
            return false;
        }
    }

    serialized_row = serialize_row(&ordered_values);
    if (serialized_row == NULL) {
        free_table_definition(&table);
        string_list_free(&ordered_values);
        free(assigned_columns);
        snprintf(error, error_size, "out of memory while serializing row");
        return false;
    }

    if (!append_text_file(table.data_path, serialized_row, error, error_size)) {
        free(serialized_row);
        free_table_definition(&table);
        string_list_free(&ordered_values);
        free(assigned_columns);
        return false;
    }

    *affected_rows = 1;

    free(serialized_row);
    string_list_free(&ordered_values);
    free_table_definition(&table);
    free(assigned_columns);
    return true;
}

bool run_select_query(
    const char *db_root,
    const SelectStatement *statement,
    QueryResult *result,
    char *error,
    size_t error_size
) {
    TableDefinition table;
    FILE *file;
    char line[4096];
    int where_index = -1;
    StringList projected_columns;
    int *selected_indexes = NULL;
    size_t selected_count = 0;
    size_t selected_index = 0;

    memset(result, 0, sizeof(*result));
    memset(&projected_columns, 0, sizeof(projected_columns));

    if (!load_table_definition(db_root, &statement->source, &table, error, error_size)) {
        return false;
    }

    /* SELECT * 이면 모든 컬럼을 그대로 보여 주고, 아니면 요청 컬럼만 인덱스로 매핑한다. */
    if (statement->select_all) {
        for (selected_index = 0; selected_index < table.columns.count; ++selected_index) {
            if (!string_list_append(&projected_columns, table.columns.items[selected_index])) {
                snprintf(error, error_size, "out of memory while preparing result columns");
                free_table_definition(&table);
                string_list_free(&projected_columns);
                return false;
            }
        }

        selected_indexes = (int *) malloc(sizeof(int) * table.columns.count);
        if (selected_indexes == NULL) {
            snprintf(error, error_size, "out of memory while preparing result");
            free_table_definition(&table);
            string_list_free(&projected_columns);
            return false;
        }
        selected_count = table.columns.count;
        for (selected_index = 0; selected_index < selected_count; ++selected_index) {
            selected_indexes[selected_index] = (int) selected_index;
        }
    } else {
        selected_indexes = (int *) malloc(sizeof(int) * statement->columns.count);
        if (selected_indexes == NULL) {
            snprintf(error, error_size, "out of memory while preparing result");
            free_table_definition(&table);
            return false;
        }

        selected_count = statement->columns.count;
        for (selected_index = 0; selected_index < statement->columns.count; ++selected_index) {
            int index = find_column_index(&table.columns, statement->columns.items[selected_index]);
            if (index < 0) {
                snprintf(error, error_size, "unknown column in SELECT: %s", statement->columns.items[selected_index]);
                free(selected_indexes);
                free_table_definition(&table);
                return false;
            }
            selected_indexes[selected_index] = index;
            if (!string_list_append(&projected_columns, table.columns.items[index])) {
                snprintf(error, error_size, "out of memory while preparing result columns");
                free(selected_indexes);
                free_table_definition(&table);
                string_list_free(&projected_columns);
                return false;
            }
        }
    }

    /* WHERE가 있으면 비교에 쓸 대상 컬럼 위치를 먼저 고정해 둔다. */
    if (statement->where.enabled) {
        where_index = find_column_index(&table.columns, statement->where.column);
        if (where_index < 0) {
            snprintf(error, error_size, "unknown column in WHERE clause: %s", statement->where.column);
            free(selected_indexes);
            free_table_definition(&table);
            string_list_free(&projected_columns);
            return false;
        }
    }

    file = fopen(table.data_path, "rb");
    if (file == NULL) {
        /* 데이터 파일이 아직 없으면 빈 테이블로 간주하고 빈 파일을 만들어 연다. */
        if (!ensure_parent_directory(table.data_path, error, error_size) ||
            !write_text_file(table.data_path, "", error, error_size)) {
            free(selected_indexes);
            free_table_definition(&table);
            string_list_free(&projected_columns);
            return false;
        }

        file = fopen(table.data_path, "rb");
        if (file == NULL) {
            snprintf(error, error_size, "failed to open data file: %s", table.data_path);
            free(selected_indexes);
            free_table_definition(&table);
            string_list_free(&projected_columns);
            return false;
        }
    }

    result->columns = projected_columns;

    /* 파일을 한 줄씩 읽으면서 WHERE 필터를 적용하고, 통과한 행만 투영해서 결과에 넣는다. */
    while (fgets(line, sizeof(line), file) != NULL) {
        StringList row_values;
        StringList projected_row;

        if (!split_pipe_line(line, &row_values, error, error_size)) {
            fclose(file);
            free(selected_indexes);
            free_table_definition(&table);
            free_query_result(result);
            return false;
        }

        if (row_values.count != table.columns.count) {
            fclose(file);
            free(selected_indexes);
            free_table_definition(&table);
            string_list_free(&row_values);
            free_query_result(result);
            snprintf(error, error_size, "row column count mismatch in data file");
            return false;
        }

        if (where_index >= 0 && strcmp(row_values.items[where_index], statement->where.value) != 0) {
            string_list_free(&row_values);
            continue;
        }

        memset(&projected_row, 0, sizeof(projected_row));
        for (selected_index = 0; selected_index < selected_count; ++selected_index) {
            if (!string_list_append(&projected_row, row_values.items[selected_indexes[selected_index]])) {
                fclose(file);
                free(selected_indexes);
                free_table_definition(&table);
                string_list_free(&row_values);
                string_list_free(&projected_row);
                free_query_result(result);
                snprintf(error, error_size, "out of memory while building result");
                return false;
            }
        }

        if (!query_result_append_row(result, &projected_row, error, error_size)) {
            fclose(file);
            free(selected_indexes);
            free_table_definition(&table);
            string_list_free(&row_values);
            string_list_free(&projected_row);
            free_query_result(result);
            return false;
        }

        string_list_free(&row_values);
        string_list_free(&projected_row);
    }

    fclose(file);
    free(selected_indexes);
    free_table_definition(&table);
    return true;
}

void free_table_definition(TableDefinition *table) {
    free_qualified_name(&table->name);
    string_list_free(&table->columns);
    free(table->schema_path);
    free(table->data_path);
    table->schema_path = NULL;
    table->data_path = NULL;
}

void free_query_result(QueryResult *result) {
    size_t index;

    /* 결과 컬럼과 각 행의 문자열 리스트를 순서대로 모두 정리한다. */
    string_list_free(&result->columns);
    for (index = 0; index < result->row_count; ++index) {
        string_list_free(&result->rows[index].values);
    }

    free(result->rows);
    result->rows = NULL;
    result->row_count = 0;
    result->row_capacity = 0;
}
