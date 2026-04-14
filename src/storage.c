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

    /* ?¤í‚¤ë§ˆê? ?ˆìœ¼ë©?db_root/schema/table.*, ?†ìœ¼ë©?db_root/table.* ?•íƒœë¡?ë§Œë“ ?? */
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

        /* ì¤??ì— ?„ë‹¬?˜ë©´ ë§ˆì?ë§??„ë“œë¥?ê²°ê³¼ ë¦¬ìŠ¤?¸ì— ?•ì •?œë‹¤. */
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

        /* ?Œì´?„ë? ë§Œë‚˜ë©?ì§€ê¸ˆê¹Œì§€ ?½ì? ë²„í¼ë¥??˜ë‚˜???€ ê°’ìœ¼ë¡??€?¥í•œ?? */
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

        /* ì§ë ¬?????´ìŠ¤ì¼€?´í”„??ê°œí–‰/?Œì´??ë°±ìŠ¬?˜ì‹œë¥??¤ì‹œ ?ë¬¸?¼ë¡œ ë³µì›?œë‹¤. */
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

    /* ?€???•ì‹?ì„œ ?˜ë?ê°€ ê²¹ì¹˜??ë¬¸ìë§?ê³¨ë¼ ??ê¸€???œí˜„?¼ë¡œ ë°”ê¾¼?? */
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

    /* ëª¨ë“  ?€??ë¨¼ì? escape????ìµœì¢… ??ì¤??¬ê¸°ë¥?ê³„ì‚°?œë‹¤. */
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

    /* escape???€???Œì´?„ë¡œ ?´ì–´ ë¶™ì—¬ ?°ì´???Œì¼????ì¤„ì„ ë§Œë“ ?? */
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

    /* ê²°ê³¼ ??ë°°ì—´??ê½?ì°¨ë©´ ?¤ì— ?´ì–´ ë¶™ì¼ ???ˆê²Œ ?•ì¥?œë‹¤. */
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

    /* ?ë³¸ ?„ì‹œ ë²„í¼?€ ë¶„ë¦¬?˜ê¸° ?„í•´ ?€ ê°’ì„ ?˜ë‚˜??ë³µì‚¬??ê²°ê³¼ ?‰ì— ?´ëŠ”?? */
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

    /* ê²½ë¡œë¿??„ë‹ˆ???Œì´ë¸??´ë¦„ ?ì²´??ë³µì‚¬???ì–´ ?´í›„ ?´ì œ?€ ?¤ë¥˜ ë©”ì‹œì§€???´ë‹¤. */
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

    /* ?¤í‚¤ë§??Œì¼ ??ì¤„ì„ ì»¬ëŸ¼ ?´ë¦„ ëª©ë¡?¼ë¡œ ë³µì›?œë‹¤. */
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

    /* ?Œì´ë¸??„ì²´ ì»¬ëŸ¼ ?˜ì— ë§ì¶° ê¸°ë³¸ ?¬ë¡¯??ë§Œë“¤ê³? ì§€?•ë˜ì§€ ?Šì? ì»¬ëŸ¼?€ ë¹?ë¬¸ì?´ë¡œ ?”ë‹¤. */
    for (column_index = 0; column_index < table.columns.count; ++column_index) {
        if (!string_list_append(&ordered_values, "")) {
            free_table_definition(&table);
            string_list_free(&ordered_values);
            free(assigned_columns);
            snprintf(error, error_size, "out of memory while building row");
            return false;
        }
    }

    /* INSERT???¤ì–´??ì»¬ëŸ¼ëª…ì„ ?¤ì œ ?¤í‚¤ë§??œì„œ??ë§ëŠ” ?„ì¹˜ë¡??¤ì‹œ ë°°ì¹˜?œë‹¤. */
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

    /* SELECT * ?´ë©´ ëª¨ë“  ì»¬ëŸ¼??ê·¸ë?ë¡?ë³´ì—¬ ì£¼ê³ , ?„ë‹ˆë©??”ì²­ ì»¬ëŸ¼ë§??¸ë±?¤ë¡œ ë§¤í•‘?œë‹¤. */
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

    /* WHEREê°€ ?ˆìœ¼ë©?ë¹„êµ?????€??ì»¬ëŸ¼ ?„ì¹˜ë¥?ë¨¼ì? ê³ ì •???”ë‹¤. */
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
        /* ?°ì´???Œì¼???„ì§ ?†ìœ¼ë©?ë¹??Œì´ë¸”ë¡œ ê°„ì£¼?˜ê³  ë¹??Œì¼??ë§Œë“¤???°ë‹¤. */
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

    /* ?Œì¼????ì¤„ì”© ?½ìœ¼ë©´ì„œ WHERE ?„í„°ë¥??ìš©?˜ê³ , ?µê³¼???‰ë§Œ ?¬ì˜?´ì„œ ê²°ê³¼???£ëŠ”?? */
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

    /* ê²°ê³¼ ì»¬ëŸ¼ê³?ê°??‰ì˜ ë¬¸ì??ë¦¬ìŠ¤?¸ë? ?œì„œ?€ë¡?ëª¨ë‘ ?•ë¦¬?œë‹¤. */
    string_list_free(&result->columns);
    for (index = 0; index < result->row_count; ++index) {
        string_list_free(&result->rows[index].values);
    }

    free(result->rows);
    result->rows = NULL;
    result->row_count = 0;
    result->row_capacity = 0;
}
