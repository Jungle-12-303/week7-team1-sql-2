#include "executor.h"
#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

typedef enum CliParseResult {
    CLI_PARSE_OK = 0,
    CLI_PARSE_HELP = 1,
    CLI_PARSE_ERROR = 2
} CliParseResult;

typedef struct CliOptions {
    const char *db_root;
    const char *sql_file;
    bool interactive_requested;
} CliOptions;

static bool contains_text_ci(const char *text, const char *pattern) {
    size_t i;
    size_t j;

    if (text == NULL || pattern == NULL || pattern[0] == '\0') {
        return false;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        for (j = 0; pattern[j] != '\0'; ++j) {
            char a = (char) tolower((unsigned char) text[i + j]);
            char b = (char) tolower((unsigned char) pattern[j]);
            if (text[i + j] == '\0' || a != b) {
                break;
            }
        }
        if (pattern[j] == '\0') {
            return true;
        }
    }

    return false;
}

static const char *error_hint_ko(const char *error) {
    if (contains_text_ci(error, "unknown option")) {
        return "지원하지 않는 옵션입니다. -h/--help로 사용 가능한 옵션을 확인해 주세요.";
    }
    if (contains_text_ci(error, "missing value for")) {
        return "옵션 값이 누락되었습니다. 옵션 뒤에 경로/파일 값을 함께 입력해 주세요.";
    }
    if (contains_text_ci(error, "too many positional arguments")) {
        return "위치 인자가 너무 많습니다. <db_root> <sql_file> 형태만 허용됩니다.";
    }
    if (contains_text_ci(error, "cannot mix --db") || contains_text_ci(error, "cannot mix --file")) {
        return "옵션 인자와 위치 인자를 혼용했습니다. 한 가지 방식만 사용해 주세요.";
    }
    if (contains_text_ci(error, "unexpected character")
        || contains_text_ci(error, "expected ")
        || contains_text_ci(error, "unterminated string literal")
        || contains_text_ci(error, "unsupported statement")) {
        return "SQL 문법 또는 지원하지 않는 구문 문제입니다. 키워드/괄호/세미콜론(;)을 확인해 주세요.";
    }
    if (contains_text_ci(error, "unknown column")) {
        return "존재하지 않는 컬럼을 사용했습니다. 스키마 컬럼명 오타를 확인해 주세요.";
    }
    if (contains_text_ci(error, "schema file is empty")) {
        return "스키마 파일이 비어 있습니다. 테이블 스키마를 먼저 정의해 주세요.";
    }
    if (contains_text_ci(error, "failed to open file") || contains_text_ci(error, "failed to read file")) {
        return "파일 경로가 잘못되었거나 접근 권한이 없습니다. 경로를 다시 확인해 주세요.";
    }
    if (contains_text_ci(error, "no SQL input provided")) {
        return "표준입력으로 SQL이 전달되지 않았습니다. 파일 인자(-f) 또는 파이프 입력을 사용해 주세요.";
    }
    if (contains_text_ci(error, "failed to run id index path")
        || contains_text_ci(error, "failed to run id range index path")
        || contains_text_ci(error, "failed to run linear scan path")) {
        return "조회 실행 중 내부 오류가 발생했습니다. 조건값과 데이터 파일 상태를 확인해 주세요.";
    }
    if (contains_text_ci(error, "out of memory")) {
        return "메모리가 부족합니다. 입력 크기를 줄이거나 프로세스를 다시 실행해 주세요.";
    }
    return "입력값 또는 데이터 상태를 확인한 뒤 다시 시도해 주세요.";
}

static void print_error_ko(const char *label, const char *error, bool suggest_retry) {
    fprintf(stderr, "%s: %s\n", label, error);
    fprintf(stderr, "안내: %s\n", error_hint_ko(error));
    if (suggest_retry) {
        fprintf(stderr, "다시 입력해 주세요.\n");
    }
}

static void print_usage(void) {
    puts("Usage:");
    puts("  mini_sql <db_root> <sql_file>");
    puts("  mini_sql -d <db_root> -f <sql_file>");
    puts("  mini_sql --db <db_root> --file <sql_file>");
    puts("  mini_sql -d <db_root> -i");
    puts("  mini_sql --db <db_root> --interactive");
    puts("  mini_sql <db_root>");
    puts("  mini_sql <db_root> < input.sql");
    puts("");
    puts("Options:");
    puts("  -d, --db <path>         Database root directory");
    puts("  -f, --file <path>       SQL file to execute");
    puts("  -i, --interactive       Force interactive mode");
    puts("  -h, --help              Show this help message");
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
        if (result.kind == EXECUTION_SELECT && result.query_result.row_count == 0) {
            puts("안내: 조회 결과가 0건입니다. 조건값(특히 id 범위) 또는 데이터 존재 여부를 확인해 주세요.");
        }
        free_execution_result(&result);
    }

    free_script(&script);
    return true;
}

static bool is_stdin_tty(void) {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(0) != 0;
#endif
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

static bool read_single_token(const char *line, char *token, size_t token_size) {
    const char *start = line;
    const char *end;
    size_t length;

    while (*start != '\0' && isspace((unsigned char) *start)) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char) *(end - 1))) {
        end--;
    }

    length = (size_t) (end - start);
    if (length == 0 || length >= token_size) {
        return false;
    }

    memcpy(token, start, length);
    token[length] = '\0';
    return true;
}

static bool is_exit_command(const char *line) {
    char token[16];

    if (!read_single_token(line, token, sizeof(token))) {
        return false;
    }

    return equals_ignore_case(token, "exit") || equals_ignore_case(token, "quit");
}

static bool is_help_command(const char *line) {
    char token[16];

    if (!read_single_token(line, token, sizeof(token))) {
        return false;
    }

    return equals_ignore_case(token, "help");
}

static bool buffer_append_n(char **buffer, size_t *length, size_t *capacity, const char *data, size_t data_length) {
    size_t required = *length + data_length + 1;

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

    memcpy(*buffer + *length, data, data_length);
    *length += data_length;
    (*buffer)[*length] = '\0';
    return true;
}

static bool buffer_append(char **buffer, size_t *length, size_t *capacity, const char *text) {
    return buffer_append_n(buffer, length, capacity, text, strlen(text));
}

static char *read_stream_text(FILE *stream, char *error, size_t error_size) {
    char chunk[1024];
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    while (true) {
        size_t read_bytes = fread(chunk, 1, sizeof(chunk), stream);
        if (read_bytes > 0) {
            if (!buffer_append_n(&buffer, &length, &capacity, chunk, read_bytes)) {
                snprintf(error, error_size, "out of memory while reading stdin");
                free(buffer);
                return NULL;
            }
        }

        if (read_bytes < sizeof(chunk)) {
            if (feof(stream)) {
                break;
            }
            if (ferror(stream)) {
                snprintf(error, error_size, "failed to read from stdin");
                free(buffer);
                return NULL;
            }
        }
    }

    if (length == 0) {
        snprintf(error, error_size, "no SQL input provided");
        free(buffer);
        return NULL;
    }

    return buffer;
}

static CliParseResult parse_cli_options(int argc, char **argv, CliOptions *options, char *error, size_t error_size) {
    const char *db_option = NULL;
    const char *file_option = NULL;
    const char *positionals[2];
    size_t positional_count = 0;
    bool show_help = false;
    int i;

    memset(options, 0, sizeof(*options));
    memset(positionals, 0, sizeof(positionals));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            show_help = true;
            continue;
        }

        if (strcmp(arg, "--interactive") == 0 || strcmp(arg, "-i") == 0) {
            options->interactive_requested = true;
            continue;
        }

        if (strcmp(arg, "--db") == 0 || strcmp(arg, "-d") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing value for %s", arg);
                return CLI_PARSE_ERROR;
            }
            if (db_option != NULL) {
                snprintf(error, error_size, "database root specified more than once");
                return CLI_PARSE_ERROR;
            }
            db_option = argv[++i];
            continue;
        }

        if (strcmp(arg, "--file") == 0 || strcmp(arg, "-f") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing value for %s", arg);
                return CLI_PARSE_ERROR;
            }
            if (file_option != NULL) {
                snprintf(error, error_size, "SQL file specified more than once");
                return CLI_PARSE_ERROR;
            }
            file_option = argv[++i];
            continue;
        }

        if (arg[0] == '-') {
            snprintf(error, error_size, "unknown option: %s", arg);
            return CLI_PARSE_ERROR;
        }

        if (positional_count >= 2) {
            snprintf(error, error_size, "too many positional arguments");
            return CLI_PARSE_ERROR;
        }

        positionals[positional_count++] = arg;
    }

    if (show_help) {
        return CLI_PARSE_HELP;
    }

    if (db_option != NULL && positional_count >= 1) {
        snprintf(error, error_size, "cannot mix --db/-d with positional <db_root>");
        return CLI_PARSE_ERROR;
    }

    if (file_option != NULL && positional_count == 2) {
        snprintf(error, error_size, "cannot mix --file/-f with positional <sql_file>");
        return CLI_PARSE_ERROR;
    }

    options->db_root = db_option != NULL ? db_option : (positional_count >= 1 ? positionals[0] : NULL);
    options->sql_file = file_option != NULL ? file_option : (positional_count == 2 ? positionals[1] : NULL);

    if (options->db_root == NULL) {
        snprintf(error, error_size, "missing <db_root>");
        return CLI_PARSE_ERROR;
    }

    if (options->interactive_requested && options->sql_file != NULL) {
        snprintf(error, error_size, "cannot use --interactive/-i with --file/-f");
        return CLI_PARSE_ERROR;
    }

    return CLI_PARSE_OK;
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

        if (length == 0 && is_help_command(line)) {
            puts("Interactive commands:");
            puts("  help          Show this message");
            puts("  exit | quit   Leave interactive mode");
            puts("Tip: end SQL with ';' to execute.");
            continue;
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
            print_error_ko("오류", error, true);
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
    CliOptions options;
    CliParseResult parse_result;
    bool interactive_mode;
    char error[SQL_ERROR_SIZE];
    char *source = NULL;

    memset(error, 0, sizeof(error));

    parse_result = parse_cli_options(argc, argv, &options, error, sizeof(error));
    if (parse_result == CLI_PARSE_HELP) {
        print_usage();
        return 0;
    }
    if (parse_result == CLI_PARSE_ERROR) {
        print_error_ko("사용법 오류", error, false);
        fputc('\n', stderr);
        print_usage();
        return 2;
    }

    interactive_mode = options.interactive_requested || (options.sql_file == NULL && is_stdin_tty());

    if (interactive_mode) {
        return run_interactive(options.db_root) ? 0 : 1;
    }

    if (options.sql_file != NULL) {
        source = read_text_file(options.sql_file, error, sizeof(error));
        if (source == NULL) {
            print_error_ko("파일 오류", error, false);
            return 1;
        }
    } else {
        source = read_stream_text(stdin, error, sizeof(error));
        if (source == NULL) {
            print_error_ko("입력 오류", error, false);
            return 1;
        }
    }

    memset(error, 0, sizeof(error));
    if (!execute_source_text(options.db_root, source, error, sizeof(error))) {
        print_error_ko("실행 오류", error, false);
        free(source);
        return 1;
    }

    free(source);
    return 0;
}
