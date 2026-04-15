#include "executor.h"
#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#define INTERACTIVE_LINE_MAX 2048
#define HISTORY_MAX_ENTRIES 200

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

typedef struct InputHistory {
    char *items[HISTORY_MAX_ENTRIES];
    size_t count;
} InputHistory;

typedef enum ReadLineStatus {
    READ_LINE_OK = 0,
    READ_LINE_EOF,
    READ_LINE_ERROR
} ReadLineStatus;

typedef enum KeyType {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_UP,
    KEY_DOWN,
    KEY_EOF
} KeyType;

typedef struct KeyEvent {
    KeyType type;
    char ch;
} KeyEvent;

#if !defined(_WIN32)
typedef struct TerminalGuard {
    struct termios original;
    bool enabled;
} TerminalGuard;
#endif

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
    if (contains_text_ci(error, "duplicate key")
        || contains_text_ci(error, "duplicate id")
        || contains_text_ci(error, "duplicate student_no")) {
        return "이미 입력된 키 값입니다. 중복되지 않는 값으로 다시 시도해 주세요.";
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

static char *sql_strndup_local(const char *src, size_t n) {
    char *copy = (char *) malloc(n + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, n);
    copy[n] = '\0';
    return copy;
}

static void history_init(InputHistory *history) {
    memset(history, 0, sizeof(*history));
}

static void history_free(InputHistory *history) {
    size_t i;
    for (i = 0; i < history->count; ++i) {
        free(history->items[i]);
        history->items[i] = NULL;
    }
    history->count = 0;
}

static bool is_blank_line_no_newline(const char *text) {
    const char *p = text;
    while (*p != '\0') {
        if (!isspace((unsigned char) *p)) {
            return false;
        }
        p++;
    }
    return true;
}

static bool history_add(InputHistory *history, const char *line_with_newline, char *error, size_t error_size) {
    char normalized[INTERACTIVE_LINE_MAX];
    size_t len = 0;
    char *copy;

    while (line_with_newline[len] != '\0' && line_with_newline[len] != '\n' && line_with_newline[len] != '\r') {
        if (len + 1 >= sizeof(normalized)) {
            break;
        }
        normalized[len] = line_with_newline[len];
        len++;
    }
    normalized[len] = '\0';

    if (is_blank_line_no_newline(normalized)) {
        return true;
    }

    if (history->count > 0 && strcmp(history->items[history->count - 1], normalized) == 0) {
        return true;
    }

    copy = sql_strndup_local(normalized, len);
    if (copy == NULL) {
        snprintf(error, error_size, "out of memory while storing command history");
        return false;
    }

    if (history->count == HISTORY_MAX_ENTRIES) {
        free(history->items[0]);
        memmove(&history->items[0], &history->items[1], sizeof(history->items[0]) * (HISTORY_MAX_ENTRIES - 1));
        history->items[HISTORY_MAX_ENTRIES - 1] = copy;
        return true;
    }

    history->items[history->count++] = copy;
    return true;
}

static void render_line_prompt(const char *prompt, const char *line, size_t *previous_length) {
    size_t line_len = strlen(line);
    size_t i;

    printf("\r%s%s", prompt, line);
    if (*previous_length > line_len) {
        for (i = 0; i < *previous_length - line_len; ++i) {
            putchar(' ');
        }
        printf("\r%s%s", prompt, line);
    }
    fflush(stdout);
    *previous_length = line_len;
}

#if !defined(_WIN32)
static bool terminal_raw_enable(TerminalGuard *guard, char *error, size_t error_size) {
    struct termios raw;

    memset(guard, 0, sizeof(*guard));

    if (tcgetattr(STDIN_FILENO, &guard->original) != 0) {
        snprintf(error, error_size, "failed to read terminal state");
        return false;
    }

    raw = guard->original;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        snprintf(error, error_size, "failed to set terminal raw mode");
        return false;
    }

    guard->enabled = true;
    return true;
}

static void terminal_raw_disable(TerminalGuard *guard) {
    if (guard->enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &guard->original);
        guard->enabled = false;
    }
}
#endif

static KeyEvent read_key_event(void) {
    KeyEvent ev;
    ev.type = KEY_NONE;
    ev.ch = '\0';

#if defined(_WIN32)
    {
        int ch = _getch();
        if (ch == 13) {
            ev.type = KEY_ENTER;
            return ev;
        }
        if (ch == 8) {
            ev.type = KEY_BACKSPACE;
            return ev;
        }
        if (ch == 0 || ch == 224) {
            int ext = _getch();
            if (ext == 72) {
                ev.type = KEY_UP;
            } else if (ext == 80) {
                ev.type = KEY_DOWN;
            }
            return ev;
        }
        if (ch == 26) {
            ev.type = KEY_EOF;
            return ev;
        }
        if (isprint((unsigned char) ch) != 0 || ch == '\t') {
            ev.type = KEY_CHAR;
            ev.ch = (char) ch;
            return ev;
        }
    }
#else
    {
        unsigned char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) {
            ev.type = KEY_EOF;
            return ev;
        }
        if (ch == '\r' || ch == '\n') {
            ev.type = KEY_ENTER;
            return ev;
        }
        if (ch == 127 || ch == '\b') {
            ev.type = KEY_BACKSPACE;
            return ev;
        }
        if (ch == 4) {
            ev.type = KEY_EOF;
            return ev;
        }
        if (ch == 27) {
            unsigned char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[0] == '[' && seq[1] == 'A') {
                    ev.type = KEY_UP;
                    return ev;
                }
                if (seq[0] == '[' && seq[1] == 'B') {
                    ev.type = KEY_DOWN;
                    return ev;
                }
            }
            return ev;
        }
        if (isprint(ch) != 0 || ch == '\t') {
            ev.type = KEY_CHAR;
            ev.ch = (char) ch;
            return ev;
        }
    }
#endif

    return ev;
}

static ReadLineStatus read_line_with_history(
    const char *prompt,
    char *line,
    size_t line_size,
    InputHistory *history,
    char *error,
    size_t error_size
) {
    size_t len = 0;
    size_t previous_length = 0;
    size_t history_cursor = history->count;

#if !defined(_WIN32)
    TerminalGuard guard;
    if (!terminal_raw_enable(&guard, error, error_size)) {
        return READ_LINE_ERROR;
    }
#endif

    line[0] = '\0';
    render_line_prompt(prompt, line, &previous_length);

    while (true) {
        KeyEvent ev = read_key_event();

        if (ev.type == KEY_EOF) {
            if (len == 0) {
                printf("\n");
#if !defined(_WIN32)
                terminal_raw_disable(&guard);
#endif
                return READ_LINE_EOF;
            }
            ev.type = KEY_ENTER;
        }

        if (ev.type == KEY_ENTER) {
            putchar('\n');
            if (len + 1 >= line_size) {
#if !defined(_WIN32)
                terminal_raw_disable(&guard);
#endif
                snprintf(error, error_size, "input line too long");
                return READ_LINE_ERROR;
            }
            line[len++] = '\n';
            line[len] = '\0';
#if !defined(_WIN32)
            terminal_raw_disable(&guard);
#endif
            return READ_LINE_OK;
        }

        if (ev.type == KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                line[len] = '\0';
                render_line_prompt(prompt, line, &previous_length);
            }
            continue;
        }

        if (ev.type == KEY_UP) {
            if (history_cursor > 0) {
                history_cursor--;
                strncpy(line, history->items[history_cursor], line_size - 1);
                line[line_size - 1] = '\0';
                len = strlen(line);
                render_line_prompt(prompt, line, &previous_length);
            }
            continue;
        }

        if (ev.type == KEY_DOWN) {
            if (history_cursor < history->count) {
                history_cursor++;
                if (history_cursor == history->count) {
                    line[0] = '\0';
                    len = 0;
                } else {
                    strncpy(line, history->items[history_cursor], line_size - 1);
                    line[line_size - 1] = '\0';
                    len = strlen(line);
                }
                render_line_prompt(prompt, line, &previous_length);
            }
            continue;
        }

        if (ev.type == KEY_CHAR) {
            if (len + 2 < line_size) {
                line[len++] = ev.ch;
                line[len] = '\0';
                render_line_prompt(prompt, line, &previous_length);
            }
            continue;
        }
    }
}

static bool run_interactive(const char *db_root) {
    char line[INTERACTIVE_LINE_MAX];
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char error[SQL_ERROR_SIZE];
    InputHistory history;
    bool use_line_editor;

    history_init(&history);
    use_line_editor = is_stdin_tty();

    puts("Mini SQL interactive mode");
    puts("Type SQL and end each statement with ';'");
    puts("Type 'exit' or 'quit' to leave.");

    while (true) {
        const char *prompt = length == 0 ? "mini_sql> " : "...> ";
        ReadLineStatus status;

        if (use_line_editor) {
            memset(error, 0, sizeof(error));
            status = read_line_with_history(prompt, line, sizeof(line), &history, error, sizeof(error));
            if (status == READ_LINE_EOF) {
                break;
            }
            if (status == READ_LINE_ERROR) {
                if (contains_text_ci(error, "terminal")) {
                    use_line_editor = false;
                    continue;
                }
                print_error_ko("오류", error, false);
                history_free(&history);
                free(buffer);
                return false;
            }
        } else {
            if (fgets(line, sizeof(line), stdin) == NULL) {
                if (ferror(stdin)) {
                    perror("input error");
                    history_free(&history);
                    free(buffer);
                    return false;
                }
                break;
            }
        }

        memset(error, 0, sizeof(error));
        if (!history_add(&history, line, error, sizeof(error))) {
            print_error_ko("오류", error, false);
            history_free(&history);
            free(buffer);
            return false;
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
            history_free(&history);
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

    history_free(&history);
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
