#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void free_qualified_name(QualifiedName *name);
void free_statement(Statement *statement);
void free_script(SQLScript *script);

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_STAR,
    TOKEN_EQUAL,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_END
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    int line;
    int column;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

static void free_tokens(TokenList *tokens);

static bool token_list_append(
    TokenList *tokens,
    TokenType type,
    const char *start,
    size_t length,
    int line,
    int column
) {
    Token *new_items;
    Token token;

    if (tokens->count == tokens->capacity) {
        size_t new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
        new_items = (Token *) realloc(tokens->items, sizeof(Token) * new_capacity);
        if (new_items == NULL) {
            return false;
        }

        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }

    token.type = type;
    token.lexeme = (char *) malloc(length + 1);
    token.line = line;
    token.column = column;
    if (token.lexeme == NULL) {
        return false;
    }

    memcpy(token.lexeme, start, length);
    token.lexeme[length] = '\0';
    tokens->items[tokens->count++] = token;
    return true;
}

static TokenType keyword_type(const char *lexeme) {
    if (sql_stricmp(lexeme, "INSERT") == 0) {
        return TOKEN_INSERT;
    }
    if (sql_stricmp(lexeme, "INTO") == 0) {
        return TOKEN_INTO;
    }
    if (sql_stricmp(lexeme, "VALUES") == 0) {
        return TOKEN_VALUES;
    }
    if (sql_stricmp(lexeme, "SELECT") == 0) {
        return TOKEN_SELECT;
    }
    if (sql_stricmp(lexeme, "FROM") == 0) {
        return TOKEN_FROM;
    }
    if (sql_stricmp(lexeme, "WHERE") == 0) {
        return TOKEN_WHERE;
    }
    return TOKEN_IDENTIFIER;
}

static bool append_simple_token(
    TokenList *tokens,
    TokenType type,
    char ch,
    int line,
    int column,
    char *error,
    size_t error_size
) {
    if (!token_list_append(tokens, type, &ch, 1, line, column)) {
        snprintf(error, error_size, "out of memory while tokenizing");
        return false;
    }
    return true;
}

static bool read_string_token(
    const char *source,
    size_t *index,
    int *line,
    int *column,
    TokenList *tokens,
    char *error,
    size_t error_size
) {
    size_t cursor = *index + 1;
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int start_column = *column;

    /* 문자열 내부에서는 ''를 하나의 따옴표 문자로 취급한다. */
    while (source[cursor] != '\0') {
        char ch = source[cursor];
        if (ch == '\'') {
            if (source[cursor + 1] == '\'') {
                if (length + 1 >= capacity) {
                    size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
                    char *new_buffer = (char *) realloc(buffer, new_capacity);
                    if (new_buffer == NULL) {
                        free(buffer);
                        snprintf(error, error_size, "out of memory while tokenizing string");
                        return false;
                    }
                    buffer = new_buffer;
                    capacity = new_capacity;
                }
                buffer[length++] = '\'';
                cursor += 2;
                *column += 2;
                continue;
            }
            break;
        }

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            char *new_buffer = (char *) realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                snprintf(error, error_size, "out of memory while tokenizing string");
                return false;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        buffer[length++] = ch;
        cursor++;
        (*column)++;
    }

    if (source[cursor] != '\'') {
        free(buffer);
        snprintf(error, error_size, "unterminated string literal at line %d, column %d", *line, start_column);
        return false;
    }

    if (buffer == NULL) {
        buffer = (char *) malloc(1);
        if (buffer == NULL) {
            snprintf(error, error_size, "out of memory while tokenizing string");
            return false;
        }
    }

    buffer[length] = '\0';
    if (tokens->count == tokens->capacity) {
        size_t new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
        Token *new_items = (Token *) realloc(tokens->items, sizeof(Token) * new_capacity);
        if (new_items == NULL) {
            free(buffer);
            snprintf(error, error_size, "out of memory while tokenizing");
            return false;
        }
        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }

    tokens->items[tokens->count].type = TOKEN_STRING;
    tokens->items[tokens->count].lexeme = buffer;
    tokens->items[tokens->count].line = *line;
    tokens->items[tokens->count].column = start_column;
    tokens->count++;

    *index = cursor + 1;
    (*column)++;
    return true;
}

static bool tokenize_sql(
    const char *source,
    TokenList *tokens,
    char *error,
    size_t error_size
) {
    size_t index = 0;
    int line = 1;
    int column = 1;

    memset(tokens, 0, sizeof(*tokens));

    /* UTF-8 BOM(EF BB BF)이 있으면 토큰화 전에 건너뛴다. */
    if ((unsigned char) source[0] == 0xEF &&
        (unsigned char) source[1] == 0xBB &&
        (unsigned char) source[2] == 0xBF) {
        index = 3;
        column = 4;
    }

    while (source[index] != '\0') {
        char ch = source[index];

        /* 공백은 건너뛰되 줄 번호와 열 번호는 계속 맞춰 둔다. */
        if (isspace((unsigned char) ch)) {
            if (ch == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            index++;
            continue;
        }

        /* 한 줄 주석은 줄 끝까지 통째로 무시한다. */
        if (ch == '-' && source[index + 1] == '-') {
            index += 2;
            column += 2;
            while (source[index] != '\0' && source[index] != '\n') {
                index++;
                column++;
            }
            continue;
        }

        /* 블록 주석은 닫는 지점까지 소비하면서 줄 위치도 함께 추적한다. */
        if (ch == '/' && source[index + 1] == '*') {
            index += 2;
            column += 2;
            while (source[index] != '\0' && !(source[index] == '*' && source[index + 1] == '/')) {
                if (source[index] == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
                index++;
            }

            if (source[index] == '\0') {
                snprintf(error, error_size, "unterminated block comment");
                free_tokens(tokens);
                return false;
            }

            index += 2;
            column += 2;
            continue;
        }

        /* 알파벳/언더스코어로 시작하면 식별자 또는 예약어로 읽는다. */
        if (isalpha((unsigned char) ch) || ch == '_') {
            size_t start = index;
            int start_column = column;

            while (isalnum((unsigned char) source[index]) || source[index] == '_') {
                index++;
                column++;
            }

            if (!token_list_append(tokens, TOKEN_IDENTIFIER, source + start, index - start, line, start_column)) {
                snprintf(error, error_size, "out of memory while tokenizing");
                free_tokens(tokens);
                return false;
            }

            tokens->items[tokens->count - 1].type = keyword_type(tokens->items[tokens->count - 1].lexeme);
            continue;
        }

        /* 숫자와 음수 리터럴은 한 덩어리로 읽어 값 토큰으로 만든다. */
        if (isdigit((unsigned char) ch) || (ch == '-' && isdigit((unsigned char) source[index + 1]))) {
            size_t start = index;
            int start_column = column;

            index++;
            column++;
            while (isdigit((unsigned char) source[index])) {
                index++;
                column++;
            }

            if (!token_list_append(tokens, TOKEN_NUMBER, source + start, index - start, line, start_column)) {
                snprintf(error, error_size, "out of memory while tokenizing");
                free_tokens(tokens);
                return false;
            }
            continue;
        }

        if (ch == '\'') {
            if (!read_string_token(source, &index, &line, &column, tokens, error, error_size)) {
                free_tokens(tokens);
                return false;
            }
            continue;
        }

        /* 남은 구분 기호는 한 글자 토큰으로 바로 추가한다. */
        switch (ch) {
            case ',':
                if (!append_simple_token(tokens, TOKEN_COMMA, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case '.':
                if (!append_simple_token(tokens, TOKEN_DOT, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case '(':
                if (!append_simple_token(tokens, TOKEN_LPAREN, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case ')':
                if (!append_simple_token(tokens, TOKEN_RPAREN, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case ';':
                if (!append_simple_token(tokens, TOKEN_SEMICOLON, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case '*':
                if (!append_simple_token(tokens, TOKEN_STAR, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            case '=':
                if (!append_simple_token(tokens, TOKEN_EQUAL, ch, line, column, error, error_size)) {
                    free_tokens(tokens);
                    return false;
                }
                break;
            default:
                snprintf(error, error_size, "unexpected character '%c' at line %d, column %d", ch, line, column);
                free_tokens(tokens);
                return false;
        }

        index++;
        column++;
    }

    if (!token_list_append(tokens, TOKEN_END, "", 0, line, column)) {
        snprintf(error, error_size, "out of memory while tokenizing");
        free_tokens(tokens);
        return false;
    }

    return true;
}

static void free_tokens(TokenList *tokens) {
    size_t index;

    for (index = 0; index < tokens->count; ++index) {
        free(tokens->items[index].lexeme);
    }

    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

void free_qualified_name(QualifiedName *name) {
    free(name->schema);
    free(name->table);
    name->schema = NULL;
    name->table = NULL;
}

void free_statement(Statement *statement) {
    if (statement->type == STATEMENT_INSERT) {
        free_qualified_name(&statement->as.insert.target);
        string_list_free(&statement->as.insert.columns);
        string_list_free(&statement->as.insert.values);
    } else if (statement->type == STATEMENT_SELECT) {
        free_qualified_name(&statement->as.select.source);
        string_list_free(&statement->as.select.columns);
        free(statement->as.select.where.column);
        free(statement->as.select.where.value);
        statement->as.select.where.column = NULL;
        statement->as.select.where.value = NULL;
    }

    statement->type = STATEMENT_SELECT;
}

void free_script(SQLScript *script) {
    size_t index;

    for (index = 0; index < script->count; ++index) {
        free_statement(&script->items[index]);
    }

    free(script->items);
    script->items = NULL;
    script->count = 0;
    script->capacity = 0;
}

typedef struct {
    TokenList tokens;
    size_t current;
} Parser;

static Token *peek(Parser *parser) {
    return &parser->tokens.items[parser->current];
}

static bool at_end(Parser *parser) {
    return peek(parser)->type == TOKEN_END;
}

static bool match(Parser *parser, TokenType type) {
    if (peek(parser)->type == type) {
        parser->current++;
        return true;
    }
    return false;
}

static bool consume(Parser *parser, TokenType type, const char *message, char *error, size_t error_size) {
    Token *token = peek(parser);
    if (token->type == type) {
        parser->current++;
        return true;
    }

    snprintf(error, error_size, "%s at line %d, column %d", message, token->line, token->column);
    return false;
}

static bool script_append(SQLScript *script, const Statement *statement) {
    Statement *new_items;

    if (script->count == script->capacity) {
        size_t new_capacity = script->capacity == 0 ? 4 : script->capacity * 2;
        new_items = (Statement *) realloc(script->items, sizeof(Statement) * new_capacity);
        if (new_items == NULL) {
            return false;
        }
        script->items = new_items;
        script->capacity = new_capacity;
    }

    script->items[script->count++] = *statement;
    return true;
}

static bool parse_identifier_list(Parser *parser, StringList *list, char *error, size_t error_size) {
    Token *token;

    memset(list, 0, sizeof(*list));

    /* 식별자 하나를 읽고, 뒤에 쉼표가 있으면 같은 규칙을 반복한다. */
    do {
        token = peek(parser);
        if (token->type != TOKEN_IDENTIFIER) {
            snprintf(error, error_size, "expected identifier at line %d, column %d", token->line, token->column);
            string_list_free(list);
            return false;
        }

        if (!string_list_append(list, token->lexeme)) {
            snprintf(error, error_size, "out of memory while parsing identifiers");
            string_list_free(list);
            return false;
        }
        parser->current++;
    } while (match(parser, TOKEN_COMMA));

    return true;
}

static bool parse_value_list(Parser *parser, StringList *list, char *error, size_t error_size) {
    Token *token;

    memset(list, 0, sizeof(*list));

    /* VALUES 절도 식별자 목록과 같은 방식으로 쉼표 단위 반복 파싱한다. */
    do {
        token = peek(parser);
        if (token->type != TOKEN_STRING && token->type != TOKEN_NUMBER && token->type != TOKEN_IDENTIFIER) {
            snprintf(error, error_size, "expected literal value at line %d, column %d", token->line, token->column);
            string_list_free(list);
            return false;
        }

        if (!string_list_append(list, token->lexeme)) {
            snprintf(error, error_size, "out of memory while parsing values");
            string_list_free(list);
            return false;
        }
        parser->current++;
    } while (match(parser, TOKEN_COMMA));

    return true;
}

static bool parse_qualified_name(Parser *parser, QualifiedName *name, char *error, size_t error_size) {
    Token *first;
    Token *second;
    bool has_schema = false;

    memset(name, 0, sizeof(*name));
    first = peek(parser);
    if (first->type != TOKEN_IDENTIFIER) {
        snprintf(error, error_size, "expected table name at line %d, column %d", first->line, first->column);
        return false;
    }

    parser->current++;

    /* 점이 나오면 schema.table, 아니면 table 하나만 있는 이름으로 해석한다. */
    if (match(parser, TOKEN_DOT)) {
        has_schema = true;
        second = peek(parser);
        if (second->type != TOKEN_IDENTIFIER) {
            snprintf(error, error_size, "expected identifier after '.' at line %d, column %d", second->line, second->column);
            return false;
        }

        name->schema = sql_strdup(first->lexeme);
        name->table = sql_strdup(second->lexeme);
        parser->current++;
    } else {
        name->schema = NULL;
        name->table = sql_strdup(first->lexeme);
    }

    if (name->table == NULL || (has_schema && name->schema == NULL)) {
        free_qualified_name(name);
        snprintf(error, error_size, "out of memory while parsing qualified name");
        return false;
    }

    return true;
}

static bool parse_insert(Parser *parser, Statement *statement, char *error, size_t error_size) {
    InsertStatement insert_statement;

    memset(&insert_statement, 0, sizeof(insert_statement));

    /* INSERT는 INTO 대상, 컬럼 목록, VALUES 목록 순서가 반드시 맞아야 한다. */
    if (!consume(parser, TOKEN_INTO, "expected INTO after INSERT", error, error_size)) {
        return false;
    }

    if (!parse_qualified_name(parser, &insert_statement.target, error, error_size)) {
        return false;
    }

    if (!consume(parser, TOKEN_LPAREN, "expected '(' after table name", error, error_size)) {
        free_qualified_name(&insert_statement.target);
        return false;
    }

    if (!parse_identifier_list(parser, &insert_statement.columns, error, error_size)) {
        free_qualified_name(&insert_statement.target);
        return false;
    }

    if (!consume(parser, TOKEN_RPAREN, "expected ')' after column list", error, error_size)) {
        free_qualified_name(&insert_statement.target);
        string_list_free(&insert_statement.columns);
        return false;
    }

    if (!consume(parser, TOKEN_VALUES, "expected VALUES keyword", error, error_size)) {
        free_qualified_name(&insert_statement.target);
        string_list_free(&insert_statement.columns);
        return false;
    }

    if (!consume(parser, TOKEN_LPAREN, "expected '(' before VALUES list", error, error_size)) {
        free_qualified_name(&insert_statement.target);
        string_list_free(&insert_statement.columns);
        return false;
    }

    if (!parse_value_list(parser, &insert_statement.values, error, error_size)) {
        free_qualified_name(&insert_statement.target);
        string_list_free(&insert_statement.columns);
        return false;
    }

    if (!consume(parser, TOKEN_RPAREN, "expected ')' after VALUES list", error, error_size)) {
        free_qualified_name(&insert_statement.target);
        string_list_free(&insert_statement.columns);
        string_list_free(&insert_statement.values);
        return false;
    }

    statement->type = STATEMENT_INSERT;
    statement->as.insert = insert_statement;
    return true;
}

static bool parse_select(Parser *parser, Statement *statement, char *error, size_t error_size) {
    SelectStatement select_statement;

    memset(&select_statement, 0, sizeof(select_statement));

    /* SELECT 대상은 * 이거나, 쉼표로 구분된 컬럼 목록 중 하나다. */
    if (match(parser, TOKEN_STAR)) {
        select_statement.select_all = true;
    } else if (!parse_identifier_list(parser, &select_statement.columns, error, error_size)) {
        return false;
    }

    if (!consume(parser, TOKEN_FROM, "expected FROM after select list", error, error_size)) {
        string_list_free(&select_statement.columns);
        return false;
    }

    if (!parse_qualified_name(parser, &select_statement.source, error, error_size)) {
        string_list_free(&select_statement.columns);
        return false;
    }

    /* WHERE가 있으면 컬럼 = 값 형태만 지원하므로 그 구조를 그대로 검증한다. */
    if (match(parser, TOKEN_WHERE)) {
        Token *column = peek(parser);
        Token *value;

        if (column->type != TOKEN_IDENTIFIER) {
            snprintf(error, error_size, "expected column name after WHERE at line %d, column %d", column->line, column->column);
            string_list_free(&select_statement.columns);
            free_qualified_name(&select_statement.source);
            return false;
        }

        select_statement.where.column = sql_strdup(column->lexeme);
        select_statement.where.enabled = true;
        parser->current++;

        if (!consume(parser, TOKEN_EQUAL, "expected '=' inside WHERE clause", error, error_size)) {
            string_list_free(&select_statement.columns);
            free_qualified_name(&select_statement.source);
            free(select_statement.where.column);
            return false;
        }

        value = peek(parser);
        if (value->type != TOKEN_STRING && value->type != TOKEN_NUMBER && value->type != TOKEN_IDENTIFIER) {
            snprintf(error, error_size, "expected literal after '=' at line %d, column %d", value->line, value->column);
            string_list_free(&select_statement.columns);
            free_qualified_name(&select_statement.source);
            free(select_statement.where.column);
            return false;
        }

        select_statement.where.value = sql_strdup(value->lexeme);
        if (select_statement.where.value == NULL) {
            string_list_free(&select_statement.columns);
            free_qualified_name(&select_statement.source);
            free(select_statement.where.column);
            snprintf(error, error_size, "out of memory while parsing WHERE clause");
            return false;
        }
        parser->current++;
    }

    statement->type = STATEMENT_SELECT;
    statement->as.select = select_statement;
    return true;
}

bool parse_sql_script(const char *source, SQLScript *script, char *error, size_t error_size) {
    Parser parser;
    Statement statement;

    memset(script, 0, sizeof(*script));
    memset(&parser, 0, sizeof(parser));

    if (!tokenize_sql(source, &parser.tokens, error, error_size)) {
        return false;
    }

    while (!at_end(&parser)) {
        memset(&statement, 0, sizeof(statement));

        /* 세미콜론만 연속으로 들어온 빈 문장은 그대로 건너뛴다. */
        if (match(&parser, TOKEN_SEMICOLON)) {
            continue;
        }

        /* 현재 문장의 첫 예약어를 기준으로 INSERT/SELECT 파서를 선택한다. */
        if (match(&parser, TOKEN_INSERT)) {
            if (!parse_insert(&parser, &statement, error, error_size)) {
                free_tokens(&parser.tokens);
                free_script(script);
                return false;
            }
        } else if (match(&parser, TOKEN_SELECT)) {
            if (!parse_select(&parser, &statement, error, error_size)) {
                free_tokens(&parser.tokens);
                free_script(script);
                return false;
            }
        } else {
            Token *token = peek(&parser);
            snprintf(error, error_size, "unsupported statement at line %d, column %d", token->line, token->column);
            free_tokens(&parser.tokens);
            free_script(script);
            return false;
        }

        /* AST 배열에 문장을 저장한 뒤, 있으면 문장 끝 세미콜론도 소비한다. */
        if (!script_append(script, &statement)) {
            free_statement(&statement);
            free_tokens(&parser.tokens);
            free_script(script);
            snprintf(error, error_size, "out of memory while building AST");
            return false;
        }

        match(&parser, TOKEN_SEMICOLON);
    }

    free_tokens(&parser.tokens);
    return true;
}
