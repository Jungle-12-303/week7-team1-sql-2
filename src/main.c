#include "executor.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    puts("Usage:");
    puts("  mini_sql <db_root> <sql_file>");
    puts("  mini_sql --db <db_root> --file <sql_file>");
}

int main(int argc, char **argv) {
    const char *db_root = NULL;
    const char *sql_file = NULL;
    char error[SQL_ERROR_SIZE];
    char *source = NULL;
    SQLScript script;
    size_t index;

    memset(&script, 0, sizeof(script));
    memset(error, 0, sizeof(error));

    /* 짧은 형식과 긴 형식 둘 다 받아서 CLI 사용성을 유지한다. */
    if (argc == 3) {
        db_root = argv[1];
        sql_file = argv[2];
    } else if (argc == 5 && strcmp(argv[1], "--db") == 0 && strcmp(argv[3], "--file") == 0) {
        db_root = argv[2];
        sql_file = argv[4];
    } else {
        print_usage();
        return 1;
    }

    /* 파일 읽기 실패와 파싱 실패를 분리해서 보여 주면 원인 파악이 쉽다. */
    source = read_text_file(sql_file, error, sizeof(error));
    if (source == NULL) {
        fprintf(stderr, "file error: %s\n", error);
        return 1;
    }

    if (!parse_sql_script(source, &script, error, sizeof(error))) {
        fprintf(stderr, "parse error: %s\n", error);
        free(source);
        return 1;
    }

    /* 하나의 SQL 파일 안에 여러 문장이 들어올 수 있으므로 순서대로 실행한다. */
    for (index = 0; index < script.count; ++index) {
        ExecutionResult result;
        memset(&result, 0, sizeof(result));

        if (!execute_statement(&script.items[index], db_root, &result, error, sizeof(error))) {
            fprintf(stderr, "execution error: %s\n", error);
            free_execution_result(&result);
            free_script(&script);
            free(source);
            return 1;
        }

        print_execution_result(&result, stdout);
        free_execution_result(&result);
    }

    free_script(&script);
    free(source);
    return 0;
}
