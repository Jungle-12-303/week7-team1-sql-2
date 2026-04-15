#ifndef MINI_SQL_PARSER_H
#define MINI_SQL_PARSER_H

#include "common.h"

typedef struct {
    char *schema;
    char *table;
} QualifiedName;

typedef enum {
    WHERE_OP_EQUAL = 0,
    WHERE_OP_GREATER,
    WHERE_OP_GREATER_EQUAL,
    WHERE_OP_LESS,
    WHERE_OP_LESS_EQUAL
} WhereOperator;

typedef struct {
    bool enabled;
    char *column;
    char *value;
    WhereOperator op;
} WhereClause;

typedef struct {
    QualifiedName target;
    StringList columns;
    StringList values;
} InsertStatement;

typedef struct {
    QualifiedName source;
    bool select_all;
    StringList columns;
    WhereClause where;
} SelectStatement;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
    union {
        InsertStatement insert;
        SelectStatement select;
    } as;
} Statement;

typedef struct {
    Statement *items;
    size_t count;
    size_t capacity;
} SQLScript;

/* AST 수명 관리 */
void free_qualified_name(QualifiedName *name);
void free_statement(Statement *statement);
void free_script(SQLScript *script);

/* SQL 파싱 */
bool parse_sql_script(const char *source, SQLScript *script, char *error, size_t error_size);

#endif
