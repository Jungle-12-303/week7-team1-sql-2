#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPTREE_MAX_KEYS 31
#define MAX_BINARY_FIELDS 4096
#define MAX_BINARY_FIELD_BYTES (16U * 1024U * 1024U)

typedef struct BptNode BptNode;

struct BptNode {
    bool is_leaf;
    size_t size;
    uint64_t keys[BPTREE_MAX_KEYS];
    union {
        struct {
            RowRef refs[BPTREE_MAX_KEYS];
            BptNode *next;
        } leaf;
        struct {
            BptNode *children[BPTREE_MAX_KEYS + 1];
        } internal;
    } as;
};

typedef struct {
    bool split;
    uint64_t promoted_key;
    BptNode *right;
} BptSplitResult;

static BptNode *g_index_root = NULL;
static bool g_index_ready = false;
static uint64_t g_next_id_counter = 0;
static char *g_active_data_path = NULL;
static const TableDefinition *g_active_table = NULL;
static int g_active_student_no_column = -1;

typedef enum {
    STUDENT_NO_SET_OK = 0,
    STUDENT_NO_SET_DUPLICATE = 1,
    STUDENT_NO_SET_ERROR = -1
} StudentNoSetResult;

typedef struct {
    char **slots;
    size_t capacity;
    size_t count;
} StudentNoSet;

static StudentNoSet g_student_no_set = {0};

static uint64_t hash_student_no(const char *text) {
    /* student_no 캐시 해시 함수(FNV-1a) */
    uint64_t hash = 1469598103934665603ULL;
    size_t i = 0;
    while (text[i] != '\0') {
        hash ^= (unsigned char) text[i];
        hash *= 1099511628211ULL;
        i++;
    }
    return hash;
}

static void student_no_set_free(void) {
    size_t i;
    if (g_student_no_set.slots != NULL) {
        for (i = 0; i < g_student_no_set.capacity; ++i) {
            free(g_student_no_set.slots[i]);
        }
    }
    free(g_student_no_set.slots);
    g_student_no_set.slots = NULL;
    g_student_no_set.capacity = 0;
    g_student_no_set.count = 0;
}

static bool student_no_set_rehash(size_t new_capacity) {
    char **new_slots;
    size_t i;

    new_slots = (char **) calloc(new_capacity, sizeof(char *));
    if (new_slots == NULL) {
        return false;
    }

    for (i = 0; i < g_student_no_set.capacity; ++i) {
        char *entry = g_student_no_set.slots[i];
        if (entry != NULL) {
            size_t mask = new_capacity - 1;
            size_t probe = (size_t) (hash_student_no(entry) & (uint64_t) mask);
            while (new_slots[probe] != NULL) {
                probe = (probe + 1) & mask;
            }
            new_slots[probe] = entry;
        }
    }

    free(g_student_no_set.slots);
    g_student_no_set.slots = new_slots;
    g_student_no_set.capacity = new_capacity;
    return true;
}

static bool student_no_set_init(void) {
    student_no_set_free();
    g_student_no_set.slots = (char **) calloc(1024, sizeof(char *));
    if (g_student_no_set.slots == NULL) {
        return false;
    }
    g_student_no_set.capacity = 1024;
    g_student_no_set.count = 0;
    return true;
}

static bool student_no_set_contains(const char *student_no) {
    size_t mask;
    size_t probe;

    if (g_student_no_set.capacity == 0 || g_student_no_set.slots == NULL) {
        return false;
    }

    mask = g_student_no_set.capacity - 1;
    probe = (size_t) (hash_student_no(student_no) & (uint64_t) mask);
    while (g_student_no_set.slots[probe] != NULL) {
        if (strcmp(g_student_no_set.slots[probe], student_no) == 0) {
            return true;
        }
        probe = (probe + 1) & mask;
    }

    return false;
}

static StudentNoSetResult student_no_set_insert(const char *student_no) {
    size_t mask;
    size_t probe;
    char *copy;

    if (g_student_no_set.capacity == 0 || g_student_no_set.slots == NULL) {
        return STUDENT_NO_SET_ERROR;
    }

    /* 로드팩터가 0.7 이상이면 확장한다. */
    if ((g_student_no_set.count + 1) * 10 >= g_student_no_set.capacity * 7) {
        if (!student_no_set_rehash(g_student_no_set.capacity * 2)) {
            return STUDENT_NO_SET_ERROR;
        }
    }

    mask = g_student_no_set.capacity - 1;
    probe = (size_t) (hash_student_no(student_no) & (uint64_t) mask);
    while (g_student_no_set.slots[probe] != NULL) {
        if (strcmp(g_student_no_set.slots[probe], student_no) == 0) {
            return STUDENT_NO_SET_DUPLICATE;
        }
        probe = (probe + 1) & mask;
    }

    copy = sql_strdup(student_no);
    if (copy == NULL) {
        return STUDENT_NO_SET_ERROR;
    }
    g_student_no_set.slots[probe] = copy;
    g_student_no_set.count++;
    return STUDENT_NO_SET_OK;
}

static BptNode *bpt_create_node(bool is_leaf) {
    BptNode *node = (BptNode *) calloc(1, sizeof(BptNode));
    if (node == NULL) {
        return NULL;
    }
    node->is_leaf = is_leaf;
    node->size = 0;
    node->as.leaf.next = NULL;
    return node;
}

static void bpt_free_node(BptNode *node) {
    size_t i;

    if (node == NULL) {
        return;
    }

    if (!node->is_leaf) {
        for (i = 0; i <= node->size; ++i) {
            bpt_free_node(node->as.internal.children[i]);
        }
    }

    free(node);
}

static int bpt_find(BptNode *root, uint64_t id, RowRef *out_ref) {
    BptNode *node = root;
    size_t i;

    while (!node->is_leaf) {
        i = 0;
        while (i < node->size && id >= node->keys[i]) {
            i++;
        }
        node = node->as.internal.children[i];
        if (node == NULL) {
            return -1;
        }
    }

    for (i = 0; i < node->size; ++i) {
        if (node->keys[i] == id) {
            *out_ref = node->as.leaf.refs[i];
            return 0;
        }
    }

    return 1;
}

static BptNode *bpt_leftmost_leaf(BptNode *root) {
    BptNode *node = root;
    if (node == NULL) {
        return NULL;
    }

    while (!node->is_leaf) {
        node = node->as.internal.children[0];
        if (node == NULL) {
            return NULL;
        }
    }
    return node;
}

static bool bpt_lower_bound(BptNode *root, uint64_t key, BptNode **out_leaf, size_t *out_index) {
    BptNode *node = root;
    size_t i;

    if (node == NULL) {
        return false;
    }

    while (!node->is_leaf) {
        i = 0;
        while (i < node->size && key >= node->keys[i]) {
            i++;
        }
        node = node->as.internal.children[i];
        if (node == NULL) {
            return false;
        }
    }

    i = 0;
    while (i < node->size && node->keys[i] < key) {
        i++;
    }

    if (i >= node->size) {
        node = node->as.leaf.next;
        i = 0;
    }

    if (node == NULL) {
        return false;
    }

    *out_leaf = node;
    *out_index = i;
    return true;
}

static int bpt_insert_recursive(BptNode *node, uint64_t id, RowRef ref, BptSplitResult *out_split) {
    size_t i;

    memset(out_split, 0, sizeof(*out_split));

    if (node->is_leaf) {
        uint64_t merged_keys[BPTREE_MAX_KEYS + 1];
        RowRef merged_refs[BPTREE_MAX_KEYS + 1];
        size_t insert_at = 0;
        BptNode *right;
        size_t total;
        size_t left_count;
        size_t right_count;

        while (insert_at < node->size && node->keys[insert_at] < id) {
            insert_at++;
        }

        if (insert_at < node->size && node->keys[insert_at] == id) {
            return 1;
        }

        if (node->size < BPTREE_MAX_KEYS) {
            for (i = node->size; i > insert_at; --i) {
                node->keys[i] = node->keys[i - 1];
                node->as.leaf.refs[i] = node->as.leaf.refs[i - 1];
            }
            node->keys[insert_at] = id;
            node->as.leaf.refs[insert_at] = ref;
            node->size++;
            return 0;
        }

        total = node->size + 1;
        for (i = 0; i < insert_at; ++i) {
            merged_keys[i] = node->keys[i];
            merged_refs[i] = node->as.leaf.refs[i];
        }
        merged_keys[insert_at] = id;
        merged_refs[insert_at] = ref;
        for (i = insert_at; i < node->size; ++i) {
            merged_keys[i + 1] = node->keys[i];
            merged_refs[i + 1] = node->as.leaf.refs[i];
        }

        left_count = total / 2;
        right_count = total - left_count;
        right = bpt_create_node(true);
        if (right == NULL) {
            return -1;
        }

        for (i = 0; i < left_count; ++i) {
            node->keys[i] = merged_keys[i];
            node->as.leaf.refs[i] = merged_refs[i];
        }
        node->size = left_count;

        for (i = 0; i < right_count; ++i) {
            right->keys[i] = merged_keys[left_count + i];
            right->as.leaf.refs[i] = merged_refs[left_count + i];
        }
        right->size = right_count;

        right->as.leaf.next = node->as.leaf.next;
        node->as.leaf.next = right;

        out_split->split = true;
        out_split->promoted_key = right->keys[0];
        out_split->right = right;
        return 0;
    }

    i = 0;
    while (i < node->size && id >= node->keys[i]) {
        i++;
    }

    {
        BptSplitResult child_split;
        int rc = bpt_insert_recursive(node->as.internal.children[i], id, ref, &child_split);
        if (rc != 0) {
            return rc;
        }

        if (!child_split.split) {
            return 0;
        }

        if (node->size < BPTREE_MAX_KEYS) {
            size_t j;

            for (j = node->size; j > i; --j) {
                node->keys[j] = node->keys[j - 1];
            }
            for (j = node->size + 1; j > i + 1; --j) {
                node->as.internal.children[j] = node->as.internal.children[j - 1];
            }

            node->keys[i] = child_split.promoted_key;
            node->as.internal.children[i + 1] = child_split.right;
            node->size++;
            return 0;
        }

        {
            uint64_t merged_keys[BPTREE_MAX_KEYS + 1];
            BptNode *merged_children[BPTREE_MAX_KEYS + 2];
            BptNode *right;
            size_t total_keys;
            size_t mid;
            size_t left_keys;
            size_t right_keys;
            size_t j;

            for (j = 0; j < i; ++j) {
                merged_keys[j] = node->keys[j];
            }
            merged_keys[i] = child_split.promoted_key;
            for (j = i; j < node->size; ++j) {
                merged_keys[j + 1] = node->keys[j];
            }

            for (j = 0; j <= i; ++j) {
                merged_children[j] = node->as.internal.children[j];
            }
            merged_children[i + 1] = child_split.right;
            for (j = i + 1; j <= node->size; ++j) {
                merged_children[j + 1] = node->as.internal.children[j];
            }

            total_keys = node->size + 1;
            mid = total_keys / 2;
            left_keys = mid;
            right_keys = total_keys - mid - 1;

            right = bpt_create_node(false);
            if (right == NULL) {
                return -1;
            }

            for (j = 0; j < left_keys; ++j) {
                node->keys[j] = merged_keys[j];
                node->as.internal.children[j] = merged_children[j];
            }
            node->as.internal.children[left_keys] = merged_children[left_keys];
            node->size = left_keys;

            for (j = 0; j < right_keys; ++j) {
                right->keys[j] = merged_keys[mid + 1 + j];
                right->as.internal.children[j] = merged_children[mid + 1 + j];
            }
            right->as.internal.children[right_keys] = merged_children[total_keys];
            right->size = right_keys;

            out_split->split = true;
            out_split->promoted_key = merged_keys[mid];
            out_split->right = right;
            return 0;
        }
    }
}

int index_init(void) {
    bpt_free_node(g_index_root);
    g_index_root = bpt_create_node(true);
    if (g_index_root == NULL) {
        g_index_ready = false;
        return -1;
    }
    g_index_ready = true;
    return 0;
}

int index_insert(uint64_t id, RowRef ref) {
    BptSplitResult split;
    int rc;

    if (!g_index_ready || g_index_root == NULL) {
        return -1;
    }

    rc = bpt_insert_recursive(g_index_root, id, ref, &split);
    if (rc != 0) {
        return rc;
    }

    if (split.split) {
        BptNode *new_root = bpt_create_node(false);
        if (new_root == NULL) {
            return -1;
        }
        new_root->keys[0] = split.promoted_key;
        new_root->size = 1;
        new_root->as.internal.children[0] = g_index_root;
        new_root->as.internal.children[1] = split.right;
        g_index_root = new_root;
    }

    return 0;
}

int index_find(uint64_t id, RowRef *out_ref) {
    if (!g_index_ready || g_index_root == NULL) {
        return -1;
    }

    return bpt_find(g_index_root, id, out_ref);
}

bool index_is_ready(void) {
    return g_index_ready && g_index_root != NULL;
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

static bool compare_with_operator_int(int cmp, WhereOperator op) {
    if (op == WHERE_OP_EQUAL) {
        return cmp == 0;
    }
    if (op == WHERE_OP_GREATER) {
        return cmp > 0;
    }
    if (op == WHERE_OP_GREATER_EQUAL) {
        return cmp >= 0;
    }
    if (op == WHERE_OP_LESS) {
        return cmp < 0;
    }
    if (op == WHERE_OP_LESS_EQUAL) {
        return cmp <= 0;
    }
    return false;
}

static bool where_value_matches(const char *lhs, const char *rhs, WhereOperator op) {
    uint64_t left_u64 = 0;
    uint64_t right_u64 = 0;

    if (parse_u64_strict(lhs, &left_u64) && parse_u64_strict(rhs, &right_u64)) {
        if (left_u64 < right_u64) {
            return compare_with_operator_int(-1, op);
        }
        if (left_u64 > right_u64) {
            return compare_with_operator_int(1, op);
        }
        return compare_with_operator_int(0, op);
    }

    return compare_with_operator_int(strcmp(lhs, rhs), op);
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

        {
            int cb_result = cb(ref, &values, ctx);
            if (cb_result < 0) {
                string_list_free(&values);
                fclose(file);
                return -1;
            }
            if (cb_result > 0) {
                string_list_free(&values);
                break;
            }
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
    int id_column_index;
    int student_no_column_index;
    char *error;
    size_t error_size;
} IndexBuildContext;

static int build_index_callback(RowRef ref, const StringList *values, void *ctx) {
    IndexBuildContext *context = (IndexBuildContext *) ctx;
    uint64_t id;
    int rc;
    StudentNoSetResult set_rc;
    const char *student_no_value;

    if (context->id_column_index < 0 || (size_t) context->id_column_index >= values->count) {
        snprintf(context->error, context->error_size, "binary row has invalid id column index");
        return -1;
    }

    if (!parse_u64_strict(values->items[context->id_column_index], &id)) {
        snprintf(context->error, context->error_size, "binary row has invalid id column");
        return -1;
    }

    if (context->student_no_column_index >= 0) {
        if ((size_t) context->student_no_column_index >= values->count) {
            snprintf(context->error, context->error_size, "binary row has invalid student_no column index");
            return -1;
        }

        student_no_value = values->items[context->student_no_column_index];
        if (student_no_value == NULL || student_no_value[0] == '\0') {
            snprintf(context->error, context->error_size, "student_no must not be empty");
            return -1;
        }

        set_rc = student_no_set_insert(student_no_value);
        if (set_rc == STUDENT_NO_SET_DUPLICATE) {
            snprintf(context->error, context->error_size, "duplicate student_no in data");
            return -1;
        }
        if (set_rc != STUDENT_NO_SET_OK) {
            snprintf(context->error, context->error_size, "failed to build student_no cache");
            return -1;
        }
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

    g_active_student_no_column = -1;
    student_no_set_free();

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
        context.id_column_index = find_column_index(&table->columns, "id");
        context.student_no_column_index = find_column_index(&table->columns, "student_no");
        context.error = error;
        context.error_size = error_size;

        if (context.id_column_index < 0) {
            snprintf(error, error_size, "table must include id column");
            return false;
        }

        if (context.student_no_column_index >= 0) {
            if (!student_no_set_init()) {
                snprintf(error, error_size, "failed to initialize student_no cache");
                return false;
            }
            g_active_student_no_column = context.student_no_column_index;
        }

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

    if (statement->where.op != WHERE_OP_EQUAL) {
        return false;
    }

    return parse_u64_strict(statement->where.value, out_id);
}

bool is_id_range_predicate(const SelectStatement *statement, WhereOperator *out_op, uint64_t *out_id) {
    if (!statement->where.enabled) {
        return false;
    }
    if (statement->where.column == NULL || sql_stricmp(statement->where.column, "id") != 0) {
        return false;
    }
    if (statement->where.op == WHERE_OP_EQUAL) {
        return false;
    }
    if (!parse_u64_strict(statement->where.value, out_id)) {
        return false;
    }
    *out_op = statement->where.op;
    return true;
}

static int append_row_by_ref(RowRef ref, QueryResult *out) {
    StringList row_values;
    char error[SQL_ERROR_SIZE];

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

int run_select_by_id(uint64_t id, QueryResult *out) {
    RowRef ref;

    if (g_active_table == NULL) {
        return -1;
    }

    if (index_find(id, &ref) != 0) {
        return 0;
    }

    return append_row_by_ref(ref, out);
}

int run_select_by_id_range(WhereOperator op, uint64_t id, QueryResult *out) {
    BptNode *leaf = NULL;
    size_t index = 0;

    if (g_active_table == NULL || !g_index_ready || g_index_root == NULL) {
        return -1;
    }

    if (op == WHERE_OP_GREATER || op == WHERE_OP_GREATER_EQUAL) {
        if (!bpt_lower_bound(g_index_root, id, &leaf, &index)) {
            return 0;
        }

        while (leaf != NULL) {
            while (index < leaf->size) {
                uint64_t key = leaf->keys[index];
                if (op == WHERE_OP_GREATER && key <= id) {
                    index++;
                    continue;
                }
                if (append_row_by_ref(leaf->as.leaf.refs[index], out) != 0) {
                    return -1;
                }
                index++;
            }
            leaf = leaf->as.leaf.next;
            index = 0;
        }
        return 0;
    }

    leaf = bpt_leftmost_leaf(g_index_root);
    while (leaf != NULL) {
        for (index = 0; index < leaf->size; ++index) {
            uint64_t key = leaf->keys[index];

            if (op == WHERE_OP_LESS && key >= id) {
                return 0;
            }
            if (op == WHERE_OP_LESS_EQUAL && key > id) {
                return 0;
            }

            if (append_row_by_ref(leaf->as.leaf.refs[index], out) != 0) {
                return -1;
            }
        }
        leaf = leaf->as.leaf.next;
    }

    return 0;
}

typedef struct {
    const SelectStatement *statement;
    const TableDefinition *table;
    QueryResult *result;
    char *error;
    size_t error_size;
    int where_index;
    bool stop_after_first_match;
} LinearScanContext;

static int linear_scan_callback(RowRef ref, const StringList *values, void *ctx) {
    LinearScanContext *context = (LinearScanContext *) ctx;

    (void) ref;

    if (context->statement->where.enabled) {
        if (!where_value_matches(values->items[context->where_index], context->statement->where.value, context->statement->where.op)) {
            return 0;
        }
    }

    if (!query_result_append_row(context->result, values, context->error, context->error_size)) {
        return -1;
    }

    if (context->stop_after_first_match) {
        return 1;
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
    context.where_index = -1;
    context.stop_after_first_match = false;

    if (statement->where.enabled) {
        context.where_index = find_column_index(&context.table->columns, statement->where.column);
        if (context.where_index < 0) {
            snprintf(error, sizeof(error), "unknown column in WHERE clause: %s", statement->where.column);
            return -1;
        }
        context.stop_after_first_match = (statement->where.op == WHERE_OP_EQUAL && sql_stricmp(statement->where.column, "student_no") == 0);
    }

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
    StringList *out_insert_columns,
    StringList *out_insert_values,
    char *error,
    size_t error_size
) {
    TableDefinition table;
    StringList ordered_values;
    bool *assigned_columns = NULL;
    size_t i;
    int id_column_index;
    int student_no_column_index;
    uint64_t generated_id;
    char id_buffer[32];
    RowRef ref;
    int idx_rc;
    StudentNoSetResult student_no_rc;
    const char *student_no_value = NULL;

    if (out_insert_columns != NULL) {
        memset(out_insert_columns, 0, sizeof(*out_insert_columns));
    }
    if (out_insert_values != NULL) {
        memset(out_insert_values, 0, sizeof(*out_insert_values));
    }

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

    student_no_column_index = find_column_index(&table.columns, "student_no");
    if (student_no_column_index >= 0) {
        /* student_no는 업무키이므로 필수 입력 + 유일성 보장을 강제한다. */
        if (!assigned_columns[student_no_column_index]) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "student_no is required in INSERT");
            return false;
        }

        student_no_value = ordered_values.items[student_no_column_index];
        if (student_no_value == NULL || student_no_value[0] == '\0') {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "student_no must not be empty");
            return false;
        }

        if (g_active_student_no_column == student_no_column_index && student_no_set_contains(student_no_value)) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "duplicate student_no: already inserted value");
            return false;
        }
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
        if (idx_rc == 1) {
            snprintf(error, error_size, "duplicate key: already inserted value");
        } else {
            snprintf(error, error_size, "failed to update id index");
        }
        return false;
    }

    if (student_no_column_index >= 0 && student_no_value != NULL) {
        student_no_rc = student_no_set_insert(student_no_value);
        if (student_no_rc == STUDENT_NO_SET_DUPLICATE) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "duplicate student_no: already inserted value");
            return false;
        }
        if (student_no_rc != STUDENT_NO_SET_OK) {
            string_list_free(&ordered_values);
            free(assigned_columns);
            free_table_definition(&table);
            snprintf(error, error_size, "failed to update student_no cache after insert");
            return false;
        }
    }

    if (out_insert_columns != NULL) {
        for (i = 0; i < table.columns.count; ++i) {
            if (!string_list_append(out_insert_columns, table.columns.items[i])) {
                string_list_free(&ordered_values);
                string_list_free(out_insert_columns);
                if (out_insert_values != NULL) {
                    string_list_free(out_insert_values);
                }
                free(assigned_columns);
                free_table_definition(&table);
                snprintf(error, error_size, "out of memory while preparing INSERT result");
                return false;
            }
        }
    }

    if (out_insert_values != NULL) {
        for (i = 0; i < ordered_values.count; ++i) {
            if (!string_list_append(out_insert_values, ordered_values.items[i])) {
                string_list_free(&ordered_values);
                if (out_insert_columns != NULL) {
                    string_list_free(out_insert_columns);
                }
                string_list_free(out_insert_values);
                free(assigned_columns);
                free_table_definition(&table);
                snprintf(error, error_size, "out of memory while preparing INSERT result");
                return false;
            }
        }
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
        WhereOperator id_op = WHERE_OP_EQUAL;
        if (is_id_equality_predicate(statement, &id_value)) {
            if (run_select_by_id(id_value, &full_result) != 0) {
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "failed to run id index path");
                return false;
            }
        } else if (is_id_range_predicate(statement, &id_op, &id_value)) {
            if (run_select_by_id_range(id_op, id_value, &full_result) != 0) {
                free_query_result(&full_result);
                free_table_definition(&table);
                snprintf(error, error_size, "failed to run id range index path");
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


