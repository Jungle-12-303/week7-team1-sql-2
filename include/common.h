#ifndef MINI_SQL_COMMON_H
#define MINI_SQL_COMMON_H

#include <stdbool.h>
#include <stddef.h>

#define SQL_ERROR_SIZE 512

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringList;

/* 문자열 유틸리티 */
char *sql_strdup(const char *source);
int sql_stricmp(const char *left, const char *right);
bool string_list_append(StringList *list, const char *value);
void string_list_free(StringList *list);

/* 파일/디렉터리 유틸리티 */
char *read_text_file(const char *path, char *error, size_t error_size);
bool write_text_file(const char *path, const char *content, char *error, size_t error_size);
bool append_text_file(const char *path, const char *content, char *error, size_t error_size);
bool ensure_directory_recursive(const char *path, char *error, size_t error_size);
bool ensure_parent_directory(const char *path, char *error, size_t error_size);

#endif
