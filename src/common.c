#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

char *sql_strdup(const char *source) {
    size_t length;
    char *copy;

    if (source == NULL) {
        return NULL;
    }

    length = strlen(source);
    copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, source, length + 1);
    return copy;
}

int sql_stricmp(const char *left, const char *right) {
    unsigned char a;
    unsigned char b;

    while (*left != '\0' && *right != '\0') {
        a = (unsigned char) tolower((unsigned char) *left);
        b = (unsigned char) tolower((unsigned char) *right);

        if (a != b) {
            return (int) a - (int) b;
        }

        left++;
        right++;
    }

    return (int) tolower((unsigned char) *left) - (int) tolower((unsigned char) *right);
}

bool string_list_append(StringList *list, const char *value) {
    char **new_items;
    char *copy;

    /* 리스트 공간이 부족하면 먼저 버퍼를 늘린다. */
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        new_items = (char **) realloc(list->items, sizeof(char *) * new_capacity);
        if (new_items == NULL) {
            return false;
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    /* 외부 버퍼 수명과 무관하게 쓰도록 문자열을 복사해 저장한다. */
    copy = sql_strdup(value == NULL ? "" : value);
    if (copy == NULL) {
        return false;
    }

    list->items[list->count++] = copy;
    return true;
}

void string_list_free(StringList *list) {
    size_t index;

    /* 각 문자열이 개별 할당되어 있으므로 하나씩 먼저 해제한다. */
    for (index = 0; index < list->count; ++index) {
        free(list->items[index]);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

char *read_text_file(const char *path, char *error, size_t error_size) {
    FILE *file;
    long length;
    size_t read_size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "failed to open file: %s", path);
        return NULL;
    }

    /* 전체 크기를 먼저 알아야 한 번에 읽을 버퍼를 정확히 잡을 수 있다. */
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        snprintf(error, error_size, "failed to seek file: %s", path);
        return NULL;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        snprintf(error, error_size, "failed to determine file size: %s", path);
        return NULL;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        snprintf(error, error_size, "failed to rewind file: %s", path);
        return NULL;
    }

    buffer = (char *) malloc((size_t) length + 1);
    if (buffer == NULL) {
        fclose(file);
        snprintf(error, error_size, "out of memory while reading: %s", path);
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t) length, file);
    fclose(file);

    if (read_size != (size_t) length) {
        free(buffer);
        snprintf(error, error_size, "failed to read complete file: %s", path);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static bool write_mode_file(
    const char *path,
    const char *content,
    const char *mode,
    char *error,
    size_t error_size
) {
    FILE *file;

    /* 상위 디렉터리가 없으면 파일 생성 자체가 실패하므로 먼저 보장한다. */
    if (!ensure_parent_directory(path, error, error_size)) {
        return false;
    }

    file = fopen(path, mode);
    if (file == NULL) {
        snprintf(error, error_size, "failed to open file for writing: %s", path);
        return false;
    }

    /* 내용이 있을 때만 쓰고, 빈 파일 생성도 허용한다. */
    if (content != NULL && fputs(content, file) == EOF) {
        fclose(file);
        snprintf(error, error_size, "failed to write file: %s", path);
        return false;
    }

    fclose(file);
    return true;
}

bool write_text_file(const char *path, const char *content, char *error, size_t error_size) {
    return write_mode_file(path, content, "wb", error, error_size);
}

bool append_text_file(const char *path, const char *content, char *error, size_t error_size) {
    return write_mode_file(path, content, "ab", error, error_size);
}

bool ensure_directory_recursive(const char *path, char *error, size_t error_size) {
    size_t length;
    size_t index;
    char *buffer;
    int rc;

    if (path == NULL || path[0] == '\0') {
        return true;
    }

    length = strlen(path);
    buffer = (char *) malloc(length + 1);
    if (buffer == NULL) {
        snprintf(error, error_size, "out of memory while creating directory");
        return false;
    }

    /* 운영체제 차이를 줄이기 위해 경로 구분자를 슬래시로 통일한다. */
    for (index = 0; index < length; ++index) {
        buffer[index] = path[index];
        if (buffer[index] == '\\') {
            buffer[index] = '/';
        }
    }
    buffer[length] = '\0';

    /* 구분자를 만날 때마다 그 시점까지의 경로를 실제 디렉터리로 만든다. */
    for (index = 1; index <= length; ++index) {
        bool is_separator = buffer[index] == '/' || buffer[index] == '\0';
        if (!is_separator) {
            continue;
        }

        if (index == 2 && buffer[1] == ':') {
            continue;
        }

        {
            char saved = buffer[index];
            buffer[index] = '\0';

            if (strlen(buffer) > 0) {
                rc = MKDIR(buffer);
                if (rc != 0 && errno != EEXIST) {
                    snprintf(error, error_size, "failed to create directory: %s", buffer);
                    free(buffer);
                    return false;
                }
            }

            buffer[index] = saved;
        }
    }

    free(buffer);
    return true;
}

bool ensure_parent_directory(const char *path, char *error, size_t error_size) {
    const char *last_slash = strrchr(path, '/');
    const char *last_backslash = strrchr(path, '\\');
    const char *separator = last_slash;
    char *parent;
    size_t length;
    bool ok;

    /* 슬래시와 백슬래시 중 더 뒤에 있는 것을 실제 부모 경계로 본다. */
    if (last_backslash != NULL && (separator == NULL || last_backslash > separator)) {
        separator = last_backslash;
    }

    if (separator == NULL) {
        return true;
    }

    length = (size_t) (separator - path);
    parent = (char *) malloc(length + 1);
    if (parent == NULL) {
        snprintf(error, error_size, "out of memory while preparing parent directory");
        return false;
    }

    memcpy(parent, path, length);
    parent[length] = '\0';

    ok = ensure_directory_recursive(parent, error, error_size);
    free(parent);
    return ok;
}
