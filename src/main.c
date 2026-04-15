#include "executor.h"
#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    puts("Usage:");
    puts("  mini_sql <db_root> <sql_file>");
    puts("  mini_sql --db <db_root> --file <sql_file>");
    puts("  mini_sql --db <db_root> --interactive");
    puts("  mini_sql <db_root>");
}

static bool execute_source_text(const char *db_root, const char *source, char *error, size_t error_size) {
    SQLScript script;
    size_t index;

    memset(&script, 0, sizeof(script));

    if (!parse_sql_script(source, &script, error, error_size)) {
        return false;
    }

    for (index = 0; index < script.count; ++index) {
        ExecutionResult result;
        memset(&result, 0, sizeof(result));

        if (!execute_statement(&script.items[index], db_root, &result, error, error_size)) {
            free_execution_result(&result);
            free_script(&script);
            return false;
        }

        print_execution_result(&result, stdout);
        free_execution_result(&result);
    }

    free_script(&script);
    return true;
}

static bool equals_ignore_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char) *left) != tolower((unsigned char) *right)) {
            return false;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static bool is_exit_command(const char *line) {
    const char *start = line;
    const char *end;
    size_t length;
    char token[16];

    while (*start != '\0' && isspace((unsigned char) *start)) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char) *(end - 1))) {
        end--;
    }

    length = (size_t) (end - start);
    if (length == 0 || length >= sizeof(token)) {
        return false;
    }

    memcpy(token, start, length);
    token[length] = '\0';
    return equals_ignore_case(token, "exit") || equals_ignore_case(token, "quit");
}

static bool buffer_append(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t text_length = strlen(text);
    size_t required = *length + text_length + 1;

    if (required > *capacity) {
        size_t new_capacity = *capacity == 0 ? 512 : *capacity;
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        {
            char *new_buffer = (char *) realloc(*buffer, new_capacity);
            if (new_buffer == NULL) {
                return false;
            }
            *buffer = new_buffer;
            *capacity = new_capacity;
        }
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return true;
}

static bool run_interactive(const char *db_root) {
    char line[2048];
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char error[SQL_ERROR_SIZE];

    puts("Mini SQL interactive mode");
    puts("Type SQL and end each statement with ';'");
    puts("Type 'exit' or 'quit' to leave.");

    while (true) {
        if (length == 0) {
            printf("mini_sql> ");
        } else {
            printf("...> ");
        }
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (ferror(stdin)) {
                perror("input error");
                free(buffer);
                return false;
            }
            break;
        }

        if (length == 0 && is_exit_command(line)) {
            break;
        }

        if (!buffer_append(&buffer, &length, &capacity, line)) {
            fprintf(stderr, "error: out of memory while reading input\n");
            free(buffer);
            return false;
        }

        if (strchr(line, ';') == NULL) {
            continue;
        }

        memset(error, 0, sizeof(error));
        if (!execute_source_text(db_root, buffer, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
        }

        length = 0;
        if (buffer != NULL) {
            buffer[0] = '\0';
        }
    }

    free(buffer);
    return true;
}

int main(int argc, char **argv) {
    const char *db_root = NULL;
    const char *sql_file = NULL;
    bool interactive = false;
    char error[SQL_ERROR_SIZE];
    char *source = NULL;

    memset(error, 0, sizeof(error));

    if (argc == 3) {
        db_root = argv[1];
        sql_file = argv[2];
    } else if (argc == 5 && strcmp(argv[1], "--db") == 0 && strcmp(argv[3], "--file") == 0) {
        db_root = argv[2];
        sql_file = argv[4];
    } else if (argc == 2) {
        db_root = argv[1];
        interactive = true;
    } else if (argc == 4 && strcmp(argv[1], "--db") == 0 && strcmp(argv[3], "--interactive") == 0) {
        db_root = argv[2];
        interactive = true;
    } else {
        print_usage();
        return 1;
    }

    if (interactive) {
        return run_interactive(db_root) ? 0 : 1;
    }

    source = read_text_file(sql_file, error, sizeof(error));
    if (source == NULL) {
        fprintf(stderr, "file error: %s\n", error);
        return 1;
    }

    memset(error, 0, sizeof(error));
    if (!execute_source_text(db_root, source, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        free(source);
        return 1;
    }

    free(source);
    return 0;
}
