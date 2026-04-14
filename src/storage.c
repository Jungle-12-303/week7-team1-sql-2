#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_INITIAL_CAPACITY 1024
#define INDEX_LOAD_NUM 7
#define INDEX_LOAD_DEN 10
#define MAX_BINARY_FIELDS 4096
#define MAX_BINARY_FIELD_BYTES (16U * 1024U * 1024U)

typedef struct {
    uint64_t id;
    RowRef ref;
    bool used;
} IndexBucket;

typedef struct {
    IndexBucket *buckets;
    size_t bucket_count;
    size_t size;
} IdIndex;

static IdIndex g_index;
static bool g_index_ready = false;
static uint64_t g_next_id_counter = 0;
static char *g_active_data_path = NULL;
static const TableDefinition *g_active_table = NULL;

static uint64_t hash_u64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

static void index_free(IdIndex *index) {
    free(index->buckets);
    index->buckets = NULL;
    index->bucket_count = 0;
    index->size = 0;
}

static bool index_allocate(IdIndex *index, size_t bucket_count) {
    index->buckets = (IndexBucket *) calloc(bucket_count, sizeof(IndexBucket));
    if (index->buckets == NULL) {
        return false;
    }
    index->bucket_count = bucket_count;
    index->size = 0;
    return true;
}

static bool index_rehash(IdIndex *index, size_t new_bucket_count) {
    size_t i;
    IdIndex new_index;

    memset(&new_index, 0, sizeof(new_index));
    if (!index_allocate(&new_index, new_bucket_count)) {
        return false;
    }

    for (i = 0; i < index->bucket_count; ++i) {
        if (index->buckets[i].used) {
            size_t slot = (size_t) (hash_u64(index->buckets[i].id) % new_index.bucket_count);
            while (new_index.buckets[slot].used) {
                slot = (slot + 1) % new_index.bucket_count;
            }
            new_index.buckets[slot] = index->buckets[i];
            new_index.size++;
        }
    }

    index_free(index);
    *index = new_index;
    return true;
}

int index_init(void) {
    index_free(&g_index);
    if (!index_allocate(&g_index, INDEX_INITIAL_CAPACITY)) {
        return -1;
    }
    g_index_ready = true;
    return 0;
}

int index_insert(uint64_t id, RowRef ref) {
    size_t slot;

    if (!g_index_ready) {
        return -1;
    }

    if ((g_index.size + 1) * INDEX_LOAD_DEN > g_index.bucket_count * INDEX_LOAD_NUM) {
        if (!index_rehash(&g_index, g_index.bucket_count * 2)) {
            return -1;
        }
    }

    slot = (size_t) (hash_u64(id) % g_index.bucket_count);
    while (g_index.buckets[slot].used) {
        if (g_index.buckets[slot].id == id) {
            return 1;
        }
        slot = (slot + 1) % g_index.bucket_count;
    }

    g_index.buckets[slot].id = id;
    g_index.buckets[slot].ref = ref;
    g_index.buckets[slot].used = true;
    g_index.size++;
    return 0;
}

int index_find(uint64_t id, RowRef *out_ref) {
    size_t slot;
    size_t scanned = 0;

    if (!g_index_ready) {
        return -1;
    }

    slot = (size_t) (hash_u64(id) % g_index.bucket_count);
    while (scanned < g_index.bucket_count) {
        if (!g_index.buckets[slot].used) {
            return 1;
        }
        if (g_index.buckets[slot].id == id) {
            *out_ref = g_index.buckets[slot].ref;
            return 0;
        }
        slot = (slot + 1) % g_index.bucket_count;
        scanned++;
    }

    return 1;
}

uint64_t next_id(void) {
    g_next_id_counter++;
    return g_next_id_counter;
}

static bool parse_u64_strict(const char *text, uint64_t *out_value) {
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *out_value = (uint64_t) parsed;
    return true;
}

static int find_column_index(const StringList *columns, const char *name) {
    size_t i;
    for (i = 0; i < columns->count; ++i) {
        if (sql_stricmp(columns->items[i], name) == 0) {
            return (int) i;
        }
    }
    return -1;
}

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

    if (schema != NULL && schema[0] != '\0') {
        schema_length = strlen(db_root) + strlen(schema) + strlen(name->table) + strlen(".schema") + 3;
        data_length = strlen(db_root) + strlen(schema) + strlen(name->table) + strlen(".data") + 3;

        *schema_path = (char *) malloc(schema_length);
        *data_path = (char *) malloc(data_length);
        if (*schema_path == NULL || *data_path == NULL) {
            free(*schema_path);
            free(*data_path);
            *schema_path = NULL;
            *data_path = NULL;
            snprintf(error, error_size, "out of memory while building file paths");
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
            free(*schema_path);
            free(*data_path);
            *schema_path = NULL;
            *data_path = NULL;
            snprintf(error, error_size, "out of memory while building file paths");
            return false;
        }

        snprintf(*schema_path, schema_length, "%s/%s.schema", db_root, name->table);
        snprintf(*data_path, data_length, "%s/%s.data", db_root, name->table);
    }

    return true;
}

static bool split_pipe_line(const char *line, StringList *values, char *error, size_t error_size) {
    size_t index = 0;
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    memset(values, 0, sizeof(*values));

    while (true) {
        char ch = line[index];

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

static bool read_text_line(FILE *file, char **out_line, char *error, size_t error_size) {
    int ch;
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        snprintf(error, error_size, "out of memory while reading text line");
        return false;
    }

    while ((ch = fgetc(file)) != EOF) {
        if (length + 2 >= capacity) {
            size_t new_capacity = capacity * 2;
            char *new_buffer = (char *) realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                snprintf(error, error_size, "out of memory while reading text line");
                return false;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        buffer[length++] = (char) ch;
        if (ch == '\n') {
            break;
        }
    }

    if (length == 0 && ch == EOF) {
        free(buffer);
        *out_line = NULL;
        return true;
    }

    buffer[length] = '\0';
    *out_line = buffer;
    return true;
}

static bool read_u32(FILE *file, uint32_t *out, bool *out_eof, char *error, size_t error_size) {
    if (fread(out, sizeof(*out), 1, file) == 1) {
        if (out_eof != NULL) {
            *out_eof = false;
        }
        return true;
    }

    if (feof(file)) {
        if (out_eof != NULL) {
            *out_eof = true;
        }
        return true;
    }

    snprintf(error, error_size, "failed to read binary data");
    return false;
}

static bool binary_read_row_from_file(
    FILE *file,
    RowRef *out_ref,
    StringList *out_values,
    bool *out_eof,
    char *error,
    size_t error_size
) {
    uint32_t field_count;
    size_t i;
    bool eof = false;

    memset(out_values, 0, sizeof(*out_values));

    if (out_ref != NULL) {
        long offset = ftell(file);
        if (offset < 0) {
            snprintf(error, error_size, "failed to get file offset");
            return false;
        }
        *out_ref = (RowRef) offset;
    }

    if (!read_u32(file, &field_count, &eof, error, error_size)) {
        return false;
    }

    if (eof) {
        if (out_eof != NULL) {
            *out_eof = true;
        }
        return true;
    }

    if (field_count == 0 || field_count > MAX_BINARY_FIELDS) {
        snprintf(error, error_size, "invalid binary field_count");
        return false;
    }

    for (i = 0; i < field_count; ++i) {
        uint32_t length;
        char *buffer;

        if (!read_u32(file, &length, NULL, error, error_size)) {
            string_list_free(out_values);
            return false;
        }

        if (length > MAX_BINARY_FIELD_BYTES) {
            string_list_free(out_values);
            snprintf(error, error_size, "binary field too large");
            return false;
        }

        buffer = (char *) malloc((size_t) length + 1);
        if (buffer == NULL) {
            string_list_free(out_values);
            snprintf(error, error_size, "out of memory while reading binary row");
            return false;
        }

        if (length > 0 && fread(buffer, 1, (size_t) length, file) != (size_t) length) {
            free(buffer);
            string_list_free(out_values);
            snprintf(error, error_size, "failed to read binary payload");
            return false;
        }

        buffer[length] = '\0';
        if (!string_list_append(out_values, buffer)) {
            free(buffer);
            string_list_free(out_values);
            snprintf(error, error_size, "out of memory while materializing row");
            return false;
        }

        free(buffer);
    }

    if (out_eof != NULL) {
        *out_eof = false;
    }

    return true;
}

int binary_writer_append_row(const StringList *values, RowRef *out_ref) {
    FILE *file;
    size_t i;

    if (g_active_data_path == NULL) {
        return -1;
    }

    file = fopen(g_active_data_path, "ab");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    if (out_ref != NULL) {
        long offset = ftell(file);
        if (offset < 0) {
            fclose(file);
            return -1;
        }
        *out_ref = (RowRef) offset;
    }

    if (fwrite(&(uint32_t){(uint32_t) values->count}, sizeof(uint32_t), 1, file) != 1) {
        fclose(file);
        return -1;
    }

    for (i = 0; i < values->count; ++i) {
        const char *value = values->items[i] == NULL ? "" : values->items[i];
        size_t length = strlen(value);
        uint32_t len32;

        if (length > UINT32_MAX) {
            fclose(file);
            return -1;
        }

        len32 = (uint32_t) length;
        if (fwrite(&len32, sizeof(len32), 1, file) != 1) {
            fclose(file);
            return -1;
        }

        if (len32 > 0 && fwrite(value, 1, len32, file) != len32) {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}

int binary_reader_read_row_at(RowRef ref, StringList *out_values) {
    FILE *file;
    bool eof = false;
    char error[SQL_ERROR_SIZE];

    if (g_active_data_path == NULL) {
        return -1;
    }

    file = fopen(g_active_data_path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, (long) ref, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    memset(error, 0, sizeof(error));
    if (!binary_read_row_from_file(file, NULL, out_values, &eof, error, sizeof(error))) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return eof ? 1 : 0;
}

int binary_reader_scan_all(RowCallback cb, void *ctx) {
    FILE *file;

    if (g_active_data_path == NULL) {
        return -1;
    }

    file = fopen(g_active_data_path, "rb");
    if (file == NULL) {
        return -1;
    }

    while (true) {
        RowRef ref = 0;
        StringList values;
        bool eof = false;
        char error[SQL_ERROR_SIZE];

        memset(error, 0, sizeof(error));
        if (!binary_read_row_from_file(file, &ref, &values, &eof, error, sizeof(error))) {
            fclose(file);
            return -1;
        }

        if (eof) {
            break;
        }

        if (cb(ref, &values, ctx) != 0) {
            string_list_free(&values);
            fclose(file);
            return -1;
        }

        string_list_free(&values);
    }

    fclose(file);
    return 0;
}

int migrate_text_data_to_binary(const char *text_path, const char *bin_path) {
    FILE *in;
    FILE *out;
    char error[SQL_ERROR_SIZE];

    in = fopen(text_path, "rb");
    if (in == NULL) {
        return -1;
    }

    out = fopen(bin_path, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }

    while (true) {
        char *line = NULL;
        StringList values;
        size_t i;

        memset(&values, 0, sizeof(values));

        if (!read_text_line(in, &line, error, sizeof(error))) {
            fclose(in);
            fclose(out);
            return -1;
        }

        if (line == NULL) {
            break;
        }

        if (!split_pipe_line(line, &values, error, sizeof(error))) {
            free(line);
            fclose(in);
            fclose(out);
            return -1;
        }

        free(line);

        if (values.count == 1 && values.items[0][0] == '\0') {
            string_list_free(&values);
            continue;
        }

        if (fwrite(&(uint32_t){(uint32_t) values.count}, sizeof(uint32_t), 1, out) != 1) {
            string_list_free(&values);
            fclose(in);
            fclose(out);
            return -1;
        }

        for (i = 0; i < values.count; ++i) {
            size_t len = strlen(values.items[i]);
            uint32_t len32;

            if (len > UINT32_MAX) {
                string_list_free(&values);
                fclose(in);
                fclose(out);
                return -1;
            }

            len32 = (uint32_t) len;
            if (fwrite(&len32, sizeof(len32), 1, out) != 1) {
                string_list_free(&values);
                fclose(in);
                fclose(out);
                return -1;
            }
            if (len32 > 0 && fwrite(values.items[i], 1, len32, out) != len32) {
                string_list_free(&values);
                fclose(in);
                fclose(out);
                return -1;
            }
        }

        string_list_free(&values);
    }

    fclose(in);
    fclose(out);
    return 0;
}

static bool file_looks_binary(const char *path) {
    FILE *file;
    uint32_t field_count;
    size_t i;
    bool eof = false;
    char error[SQL_ERROR_SIZE];

    file = fopen(path, "rb");
    if (file == NULL) {
        return true;
    }

    if (!read_u32(file, &field_count, &eof, error, sizeof(error))) {
        fclose(file);
        return false;
    }

    if (eof) {
        fclose(file);
        return true;
    }

    if (field_count == 0 || field_count > MAX_BINARY_FIELDS) {
        fclose(file);
        return false;
    }

    for (i = 0; i < field_count; ++i) {
        uint32_t len;
        if (!read_u32(file, &len, NULL, error, sizeof(error))) {
            fclose(file);
            return false;
        }
        if (len > MAX_BINARY_FIELD_BYTES) {
            fclose(file);
            return false;
        }
        if (fseek(file, (long) len, SEEK_CUR) != 0) {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

typedef struct {
    uint64_t max_id;
    char *error;
    size_t error_size;
} IndexBuildContext;

static int build_index_callback(RowRef ref, const StringList *values, void *ctx) {
    IndexBuildContext *context = (IndexBuildContext *) ctx;
    uint64_t id;
    int rc;

    (void) ref;

    if (values->count == 0 || !parse_u64_strict(values->items[0], &id)) {
        snprintf(context->error, context->error_size, "binary row has invalid id column");
        return -1;
    }

    rc = index_insert(id, ref);
    if (rc == 1) {
        snprintf(context->error, context->error_size, "duplicate id in data");
        return -1;
    }
    if (rc != 0) {
        snprintf(context->error, context->error_size, "failed to build index");
        return -1;
    }

    if (id > context->max_id) {
        context->max_id = id;
    }

    return 0;
}

static bool activate_storage_for_table(const TableDefinition *table, char *error, size_t error_size) {
    if (g_active_data_path != NULL && strcmp(g_active_data_path, table->data_path) == 0 && g_index_ready) {
        g_active_table = table;
        return true;
    }

    free(g_active_data_path);
    g_active_data_path = sql_strdup(table->data_path);
    if (g_active_data_path == NULL) {
        snprintf(error, error_size, "out of memory while switching active table");
        return false;
    }

    if (!ensure_parent_directory(g_active_data_path, error, error_size)) {
        return false;
    }

    {
        FILE *probe = fopen(g_active_data_path, "rb");
        if (probe == NULL) {
            if (!write_text_file(g_active_data_path, "", error, error_size)) {
                return false;
            }
        } else {
            fclose(probe);
        }
    }

    if (!file_looks_binary(g_active_data_path)) {
        char *tmp_path;
        char *bak_path;
        size_t base_len = strlen(g_active_data_path);

        tmp_path = (char *) malloc(base_len + strlen(".bin.tmp") + 1);
        bak_path = (char *) malloc(base_len + strlen(".text.bak") + 1);
        if (tmp_path == NULL || bak_path == NULL) {
            free(tmp_path);
            free(bak_path);
            snprintf(error, error_size, "out of memory while preparing migration paths");
            return false;
        }

        sprintf(tmp_path, "%s.bin.tmp", g_active_data_path);
        sprintf(bak_path, "%s.text.bak", g_active_data_path);

        if (migrate_text_data_to_binary(g_active_data_path, tmp_path) != 0) {
            remove(tmp_path);
            free(tmp_path);
            free(bak_path);
            snprintf(error, error_size, "failed to migrate text data to binary");
            return false;
        }

        remove(bak_path);
        if (rename(g_active_data_path, bak_path) != 0) {
            remove(tmp_path);
            free(tmp_path);
            free(bak_path);
            snprintf(error, error_size, "failed to backup text data during migration");
            return false;
        }

        if (rename(tmp_path, g_active_data_path) != 0) {
            rename(bak_path, g_active_data_path);
            remove(tmp_path);
            free(tmp_path);
            free(bak_path);
            snprintf(error, error_size, "failed to finalize binary migration");
            return false;
        }

        free(tmp_path);
        free(bak_path);
    }

    if (index_init() != 0) {
        snprintf(error, error_size, "failed to initialize id index");
        return false;
    }

    {
        IndexBuildContext context;
        memset(&context, 0, sizeof(context));
        context.error = error;
        context.error_size = error_size;

        if (binary_reader_scan_all(build_index_callback, &context) != 0) {
            if (error[0] == '\0') {
                snprintf(error, error_size, "failed to scan binary data for index build");
            }
            return false;
        }

        g_next_id_counter = context.max_id;
    }

    g_active_table = table;
    return true;
}

static bool query_result_append_row(QueryResult *result, const StringList *values, char *error, size_t error_size) {
    size_t i;
    ResultRow *new_rows;
    ResultRow *row;

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

    for (i = 0; i < values->count; ++i) {
        if (!string_list_append(&row->values, values->items[i])) {
            string_list_free(&row->values);
            snprintf(error, error_size, "out of memory while building result");
            return false;
        }
    }

    result->row_count++;
    return true;
}

static bool project_row(
    const StringList *source_values,
    const int *selected_indexes,
    size_t selected_count,
    QueryResult *result,
    char *error,
    size_t error_size
) {
    StringList projected;
    size_t i;

    memset(&projected, 0, sizeof(projected));

    for (i = 0; i < selected_count; ++i) {
        int source_index = selected_indexes[i];
        if (source_index < 0 || (size_t) source_index >= source_values->count) {
            string_list_free(&projected);
            snprintf(error, error_size, "projection index out of range");
            return false;
        }

        if (!string_list_append(&projected, source_values->items[source_index])) {
            string_list_free(&projected);
            snprintf(error, error_size, "out of memory while projecting row");
            return false;
        }
    }

    if (!query_result_append_row(result, &projected, error, error_size)) {
        string_list_free(&projected);
        return false;
    }

    string_list_free(&projected);
    return true;
}

bool is_id_equality_predicate(const SelectStatement *statement, uint64_t *out_id) {
    if (!statement->where.enabled) {
        return false;
    }

    if (statement->where.column == NULL || sql_stricmp(statement->where.column, "id") != 0) {
        return false;
    }

    return parse_u64_strict(statement->where.value, out_id);
}

int run_select_by_id(uint64_t id, QueryResult *out) {
    RowRef ref;
    StringList row_values;
    char error[SQL_ERROR_SIZE];

    if (g_active_table == NULL) {
        return -1;
    }

    if (index_find(id, &ref) != 0) {
        return 0;
    }

    memset(&row_values, 0, sizeof(row_values));
    if (binary_reader_read_row_at(ref, &row_values) != 0) {
        return -1;
    }

    if (row_values.count != g_active_table->columns.count) {
        string_list_free(&row_values);
        return -1;
    }

    memset(error, 0, sizeof(error));
    if (!query_result_append_row(out, &row_values, error, sizeof(error))) {
        string_list_free(&row_values);
        return -1;
    }

    string_list_free(&row_values);
    return 0;
}

typedef struct {
    const SelectStatement *statement;
    const TableDefinition *table;
    QueryResult *result;
    char *error;
    size_t error_size;
} LinearScanContext;

static int linear_scan_callback(RowRef ref, const StringList *values, void *ctx) {
    LinearScanContext *context = (LinearScanContext *) ctx;
    int where_index = -1;

    (void) ref;

    if (context->statement->where.enabled) {
        where_index = find_column_index(&context->table->columns, context->statement->where.column);
        if (where_index < 0) {
            snprintf(context->error, context->error_size, "unknown column in WHERE clause: %s", context->statement->where.column);
            return -1;
        }

        if (strcmp(values->items[where_index], context->statement->where.value) != 0) {
            return 0;
        }
    }

    if (!query_result_append_row(context->result, values, context->error, context->error_size)) {
        return -1;
    }

    return 0;
}

int run_select_linear(const SelectStatement *statement, QueryResult *out) {
    LinearScanContext context;
    char error[SQL_ERROR_SIZE];

    if (g_active_table == NULL) {
        return -1;
    }

    memset(error, 0, sizeof(error));
    memset(&context, 0, sizeof(context));
    context.statement = statement;
    context.table = g_active_table;
    context.result = out;
    context.error = error;
    context.error_size = sizeof(error);

    if (binary_reader_scan_all(linear_scan_callback, &context) != 0) {
        return -1;
    }

    return 0;
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
    bool *assigned_columns = NULL;
    size_t i;
    int id_column_index;
    uint64_t generated_id;
    char id_buffer[32];
    RowRef ref;
    int idx_rc;

    if (statement->columns.count != statement->values.count) {
        snprintf(error, error_size, "INSERT column count and value count do not match");
        return false;
    }

    if (!load_table_definition(db_root, &statement->target, &table, error, error_size)) {
        return false;
    }

    if (!activate_storage_for_table(&table, error, error_size)) {
        free_table_definition(&table);
        return false;
    }

    memset(&ordered_values, 0, sizeof(ordered_values));
    assigned_columns = (bool *) calloc(table.columns.count, sizeof(bool));
    if (assigned_columns == NULL) {
        free_table_definition(&table);
        snprintf(error, error_size, "out of memory while preparing INSERT");
        return false;
    }

    for (i = 0; i < table.columns.count; ++i) {
        if (!string_list_append(&ordered_values, "")) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "out of memory while building row");
            return false;
        }
    }

    for (i = 0; i < statement->columns.count; ++i) {
        int target = find_column_index(&table.columns, statement->columns.items[i]);
        if (target < 0) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "unknown column in INSERT: %s", statement->columns.items[i]);
            return false;
        }

        if (assigned_columns[target]) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "duplicate column in INSERT: %s", statement->columns.items[i]);
            return false;
        }
        assigned_columns[target] = true;

        if (sql_stricmp(table.columns.items[target], "id") == 0) {
            continue;
        }

        free(ordered_values.items[target]);
        ordered_values.items[target] = sql_strdup(statement->values.items[i]);
        if (ordered_values.items[target] == NULL) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "out of memory while building row");
            return false;
        }
    }

    id_column_index = find_column_index(&table.columns, "id");
    if (id_column_index < 0) {
        string_list_free(&ordered_values);
        free(assigned_columns);
        free_table_definition(&table);
        snprintf(error, error_size, "table must include id column for auto-id INSERT");
        return false;
    }

    generated_id = next_id();
    snprintf(id_buffer, sizeof(id_buffer), "%llu", (unsigned long long) generated_id);

    free(ordered_values.items[id_column_index]);
    ordered_values.items[id_column_index] = sql_strdup(id_buffer);
    if (ordered_values.items[id_column_index] == NULL) {
        string_list_free(&ordered_values);
        free(assigned_columns);
        free_table_definition(&table);
        snprintf(error, error_size, "out of memory while assigning auto id");
        return false;
    }

    if (binary_writer_append_row(&ordered_values, &ref) != 0) {
        string_list_free(&ordered_values);
        free(assigned_columns);
        free_table_definition(&table);
        snprintf(error, error_size, "failed to write binary row");
        return false;
    }

    idx_rc = index_insert(generated_id, ref);
    if (idx_rc != 0) {
        string_list_free(&ordered_values);
        free(assigned_columns);
        free_table_definition(&table);
        snprintf(error, error_size, "failed to update id index");
        return false;
    }

    *affected_rows = 1;

    string_list_free(&ordered_values);
    free(assigned_columns);
    free_table_definition(&table);
    g_active_table = NULL;
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
    QueryResult full_result;
    int *selected_indexes = NULL;
    size_t selected_count = 0;
    size_t i;

    memset(result, 0, sizeof(*result));
    memset(&full_result, 0, sizeof(full_result));

    if (!load_table_definition(db_root, &statement->source, &table, error, error_size)) {
        return false;
    }

    if (!activate_storage_for_table(&table, error, error_size)) {
        free_table_definition(&table);
        return false;
    }

    for (i = 0; i < table.columns.count; ++i) {
        if (!string_list_append(&full_result.columns, table.columns.items[i])) {
            free_query_result(&full_result);
            free_table_definition(&table);
            snprintf(error, error_size, "out of memory while preparing full result columns");
            return false;
        }
    }

    {
        uint64_t id_value = 0;
        if (is_id_equality_predicate(statement, &id_value)) {
            if (run_select_by_id(id_value, &full_result) != 0) {
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "failed to run id index path");
                return false;
            }
        } else {
            if (run_select_linear(statement, &full_result) != 0) {
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "failed to run linear scan path");
                return false;
            }
        }
    }

    if (statement->select_all) {
        selected_count = table.columns.count;
        selected_indexes = (int *) malloc(sizeof(int) * selected_count);
        if (selected_indexes == NULL) {
            free_query_result(&full_result);
            free_table_definition(&table);
            snprintf(error, error_size, "out of memory while preparing projection");
            return false;
        }

        for (i = 0; i < selected_count; ++i) {
            selected_indexes[i] = (int) i;
            if (!string_list_append(&result->columns, table.columns.items[i])) {
                free(selected_indexes);
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "out of memory while preparing result columns");
                return false;
            }
        }
    } else {
        selected_count = statement->columns.count;
        selected_indexes = (int *) malloc(sizeof(int) * selected_count);
        if (selected_indexes == NULL) {
            free_query_result(&full_result);
            free_table_definition(&table);
            snprintf(error, error_size, "out of memory while preparing projection");
            return false;
        }

        for (i = 0; i < selected_count; ++i) {
            int index = find_column_index(&table.columns, statement->columns.items[i]);
            if (index < 0) {
                free(selected_indexes);
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "unknown column in SELECT: %s", statement->columns.items[i]);
                return false;
            }
            selected_indexes[i] = index;
            if (!string_list_append(&result->columns, table.columns.items[index])) {
                free(selected_indexes);
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "out of memory while preparing result columns");
                return false;
            }
        }
    }

    for (i = 0; i < full_result.row_count; ++i) {
        if (!project_row(&full_result.rows[i].values, selected_indexes, selected_count, result, error, error_size)) {
            free(selected_indexes);
            free_query_result(&full_result);
            free_query_result(result);
            free_table_definition(&table);
            return false;
        }
    }

    free(selected_indexes);
    free_query_result(&full_result);
    free_table_definition(&table);
    g_active_table = NULL;
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
    size_t i;

    string_list_free(&result->columns);
    for (i = 0; i < result->row_count; ++i) {
        string_list_free(&result->rows[i].values);
    }

    free(result->rows);
    result->rows = NULL;
    result->row_count = 0;
    result->row_capacity = 0;
}


