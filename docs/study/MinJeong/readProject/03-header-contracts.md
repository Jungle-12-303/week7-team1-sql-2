# 헤더 파일의 자료구조와 함수가 필요한 이유

이 문서는 `include/*.h` 파일을 공부할 때 "이 구조체와 함수가 왜 필요한가"를 이해하기 위한 문서입니다.

01 문서는 파일을 읽는 순서, 02 문서는 함수 호출 흐름을 다뤘습니다.
03 문서는 그 흐름에서 여러 `.c` 파일이 서로 데이터를 주고받기 위해 어떤 공통 약속을 만들었는지 설명합니다.

## 1. 헤더 파일은 왜 필요한가

C 프로젝트에서 `.h` 파일은 모듈 사이의 약속입니다.

예를 들어 `src/main.c`는 SQL을 직접 파싱하지 않고 `parse_sql_script`를 호출합니다.
그런데 `main.c`가 `parser.c` 내부 구현을 전부 알 필요는 없습니다.
대신 `include/parser.h`에 공개된 함수와 구조체만 알면 됩니다.

```text
main.c
-> parser.h에 적힌 parse_sql_script 계약을 보고 호출
-> 실제 구현은 parser.c에 있음
```

이 프로젝트의 헤더는 크게 네 가지 책임으로 나뉩니다.

| 헤더 | 책임 |
| --- | --- |
| `common.h` | 여러 모듈에서 같이 쓰는 문자열 리스트, 파일 유틸리티 |
| `parser.h` | SQL 문자열을 구조화한 AST 자료구조와 파싱 함수 |
| `storage.h` | 테이블, row, QueryResult, 바이너리 저장소, B+ Tree 인덱스 함수 |
| `executor.h` | 파싱된 Statement를 실행하고 출력하는 실행 결과 자료구조 |

## 2. 왜 SQL 문장을 구조체로 표현해야 하는가

SQL은 문자열입니다.

```sql
SELECT name FROM demo.students WHERE id >= 10;
```

하지만 실행 단계에서 문자열 그대로 다루면 매번 이런 일을 반복해야 합니다.

```text
이 문장이 SELECT인가?
테이블 이름은 어디인가?
선택 컬럼은 무엇인가?
WHERE가 있는가?
비교 연산자는 무엇인가?
비교 값은 무엇인가?
```

그래서 파서는 SQL 문자열을 한 번 분석해서 실행하기 좋은 구조체로 바꿉니다.

```text
SQL 문자열
-> parse_sql_script
-> Statement 구조체
```

이 구조체 표현을 AST라고 볼 수 있습니다.
AST는 Abstract Syntax Tree의 줄임말인데, 이 프로젝트에서는 복잡한 트리라기보다는 "SQL 문장의 의미를 담은 구조체 묶음"에 가깝습니다.

구조체로 바꾸면 좋은 점은 다음과 같습니다.

1. 실행기가 문자열 파싱을 다시 하지 않아도 된다.
2. `INSERT`와 `SELECT`를 명확히 구분할 수 있다.
3. 테이블명, 컬럼명, WHERE 조건을 안정적으로 꺼낼 수 있다.
4. 저장소 계층은 SQL 문법을 몰라도 필요한 값만 받을 수 있다.

## 3. `common.h`: 공통 도구가 필요한 이유

관련 파일:

- [`include/common.h`](../../../../include/common.h)
- [`src/common.c`](../../../../src/common.c)

`common.h`는 파서, 저장소, 실행기가 공통으로 쓰는 작은 도구들을 정의합니다.

### `SQL_ERROR_SIZE`

```c
#define SQL_ERROR_SIZE 512
```

에러 메시지 버퍼 크기를 통일하기 위한 상수입니다.

여러 함수가 아래처럼 에러 메시지 버퍼를 받습니다.

```c
char *error, size_t error_size
```

이 방식을 쓰면 함수가 실패했을 때 단순히 `false`만 반환하는 것이 아니라, 왜 실패했는지도 호출자에게 알려줄 수 있습니다.

### `StringList`

```c
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringList;
```

이 프로젝트에는 문자열 목록이 자주 필요합니다.

예를 들어:

```text
INSERT 컬럼 목록: name, major, grade
INSERT 값 목록: Alice, DB, A
SELECT 컬럼 목록: id, name
테이블 스키마 컬럼 목록: id, name, major, grade
row 값 목록: 1, Alice, DB, A
```

C에는 동적 문자열 배열이 기본으로 없기 때문에, `StringList`를 직접 만들었습니다.

각 필드의 의미는 다음과 같습니다.

- `items`: 문자열 포인터 배열
- `count`: 현재 들어 있는 문자열 개수
- `capacity`: 현재 확보한 배열 크기

`count`와 `capacity`를 분리한 이유는 문자열을 추가할 때마다 매번 새 배열을 만들지 않기 위해서입니다.
공간이 부족할 때만 크게 늘리고, 평소에는 기존 배열을 재사용합니다.

### 문자열 함수들

```c
char *sql_strdup(const char *source);
int sql_stricmp(const char *left, const char *right);
bool string_list_append(StringList *list, const char *value);
void string_list_free(StringList *list);
```

이 함수들이 필요한 이유는 다음과 같습니다.

- `sql_strdup`: 외부 문자열의 수명에 의존하지 않도록 복사본을 만든다.
- `sql_stricmp`: SQL 키워드와 컬럼명을 대소문자 구분 없이 비교한다.
- `string_list_append`: 동적 문자열 목록에 안전하게 값을 추가한다.
- `string_list_free`: 목록 안의 문자열과 배열을 함께 해제한다.

특히 `string_list_free`가 중요한 이유는 `StringList` 안의 문자열들이 각각 동적 할당되기 때문입니다.
배열만 `free`하면 문자열들이 메모리에 남아버립니다.

### 파일/디렉터리 함수들

```c
char *read_text_file(...);
bool write_text_file(...);
bool append_text_file(...);
bool ensure_directory_recursive(...);
bool ensure_parent_directory(...);
```

이 프로젝트는 `.schema`, `.data`, SQL 파일을 직접 읽고 씁니다.
그래서 파일 입출력과 디렉터리 생성 로직을 여러 곳에 흩뿌리지 않고 공통 함수로 모았습니다.

## 4. `parser.h`: SQL 문장을 표현하는 자료구조

관련 파일:

- [`include/parser.h`](../../../../include/parser.h)
- [`src/parser.c`](../../../../src/parser.c)

`parser.h`는 SQL 문자열을 실행 가능한 구조로 바꾼 결과를 정의합니다.

### `QualifiedName`

```c
typedef struct {
    char *schema;
    char *table;
} QualifiedName;
```

테이블 이름은 두 형태로 올 수 있습니다.

```sql
students
demo.students
```

그래서 테이블 이름을 단순 문자열 하나로 두지 않고 `schema`와 `table`로 나눴습니다.

```text
demo.students
-> schema = "demo"
-> table = "students"
```

이렇게 나눠두면 저장소에서 실제 파일 경로를 만들기 쉽습니다.

```text
db_root/demo/students.schema
db_root/demo/students.data
```

### `WhereOperator`

```c
typedef enum {
    WHERE_OP_EQUAL = 0,
    WHERE_OP_GREATER,
    WHERE_OP_GREATER_EQUAL,
    WHERE_OP_LESS,
    WHERE_OP_LESS_EQUAL
} WhereOperator;
```

`WHERE` 조건의 비교 연산자를 표현합니다.

문자열로 `">="`를 계속 들고 다니지 않고 enum으로 바꾼 이유는 실행 단계에서 분기하기 쉽기 때문입니다.

```text
WHERE_OP_EQUAL         -> =
WHERE_OP_GREATER       -> >
WHERE_OP_GREATER_EQUAL -> >=
WHERE_OP_LESS          -> <
WHERE_OP_LESS_EQUAL    -> <=
```

예를 들어 저장소 계층에서는 이 값으로 id 단건 조회인지 범위 조회인지 판단합니다.

```text
WHERE_OP_EQUAL이면 run_select_by_id
그 외 id 비교 연산자면 run_select_by_id_range
```

### `WhereClause`

```c
typedef struct {
    bool enabled;
    char *column;
    char *value;
    WhereOperator op;
} WhereClause;
```

`WHERE` 절 전체를 표현합니다.

예를 들어:

```sql
WHERE id >= 10
```

은 아래처럼 저장됩니다.

```text
enabled = true
column = "id"
op = WHERE_OP_GREATER_EQUAL
value = "10"
```

`enabled`가 필요한 이유는 `WHERE`가 없는 SELECT도 있기 때문입니다.

```sql
SELECT * FROM demo.students;
```

이 경우에는 `where.enabled = false`로 두고, 실행 단계에서는 조건 없이 전체 row를 조회합니다.

### `InsertStatement`

```c
typedef struct {
    QualifiedName target;
    StringList columns;
    StringList values;
} InsertStatement;
```

`INSERT` 문장의 의미를 담습니다.

예를 들어:

```sql
INSERT INTO demo.students (name, major) VALUES ('Alice', 'DB');
```

은 아래처럼 표현됩니다.

```text
target = demo.students
columns = ["name", "major"]
values = ["Alice", "DB"]
```

컬럼과 값을 각각 `StringList`로 둔 이유는 컬럼 개수가 고정되어 있지 않기 때문입니다.
또한 실행 단계에서는 컬럼 목록과 값 목록의 개수가 같은지 검사하고, 스키마 순서에 맞춰 값을 재배치합니다.

### `SelectStatement`

```c
typedef struct {
    QualifiedName source;
    bool select_all;
    StringList columns;
    WhereClause where;
} SelectStatement;
```

`SELECT` 문장의 의미를 담습니다.

예를 들어:

```sql
SELECT id, name FROM demo.students WHERE id <= 50;
```

은 아래처럼 표현됩니다.

```text
source = demo.students
select_all = false
columns = ["id", "name"]
where.enabled = true
where.column = "id"
where.op = WHERE_OP_LESS_EQUAL
where.value = "50"
```

`select_all`이 따로 있는 이유는 `SELECT *`와 `SELECT id, name`의 의미가 다르기 때문입니다.

- `select_all = true`: 테이블의 모든 컬럼을 출력한다.
- `select_all = false`: `columns`에 들어 있는 컬럼만 출력한다.

### `StatementType`과 `Statement`

```c
typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;
```

```c
typedef struct {
    StatementType type;
    union {
        InsertStatement insert;
        SelectStatement select;
    } as;
} Statement;
```

하나의 SQL 문장은 현재 `INSERT` 또는 `SELECT` 중 하나입니다.
그래서 `type`으로 문장 종류를 표시하고, `union`으로 실제 내용을 담습니다.

`union`을 사용한 이유는 한 문장이 동시에 INSERT이면서 SELECT일 수 없기 때문입니다.

```text
type = STATEMENT_INSERT이면 as.insert만 의미 있음
type = STATEMENT_SELECT이면 as.select만 의미 있음
```

이 구조 덕분에 실행기는 아래처럼 단순하게 분기할 수 있습니다.

```text
if INSERT -> append_insert_row
if SELECT -> run_select_query
```

### `SQLScript`

```c
typedef struct {
    Statement *items;
    size_t count;
    size_t capacity;
} SQLScript;
```

SQL 파일 하나에는 문장이 여러 개 들어갈 수 있습니다.

```sql
INSERT ...;
INSERT ...;
SELECT ...;
```

그래서 파싱 결과도 `Statement` 하나가 아니라 `Statement` 배열이어야 합니다.
`SQLScript`는 그 배열을 관리합니다.

`StringList`와 비슷하게 `count`와 `capacity`를 둔 이유는 SQL 문장 개수가 고정되어 있지 않기 때문입니다.

### 파서 함수와 해제 함수

```c
bool parse_sql_script(...);
void free_qualified_name(...);
void free_statement(...);
void free_script(...);
```

`parse_sql_script`는 SQL 문자열을 `SQLScript`로 바꿉니다.

해제 함수들이 따로 있는 이유는 파싱 결과 안에 동적 할당된 문자열과 배열이 여러 단계로 들어 있기 때문입니다.

```text
SQLScript
-> Statement 배열
-> InsertStatement 또는 SelectStatement
-> QualifiedName, StringList, WhereClause
-> char* 문자열들
```

이 구조를 호출자가 일일이 알면 실수하기 쉽습니다.
그래서 `free_script` 하나를 호출하면 내부 구조까지 정리되도록 해제 함수를 제공합니다.

## 5. `storage.h`: 저장소와 인덱스의 계약

관련 파일:

- [`include/storage.h`](../../../../include/storage.h)
- [`src/storage.c`](../../../../src/storage.c)

`storage.h`는 파싱된 SQL 문장을 실제 데이터 파일과 B+ Tree 인덱스에 연결하는 계층입니다.

### `RowRef`

```c
typedef uint64_t RowRef;
```

`RowRef`는 바이너리 `.data` 파일 안에서 row가 시작되는 byte offset입니다.

예를 들어:

```text
id=2 row starts at byte 37
```

이면:

```text
RowRef = 37
```

별도 타입 이름을 붙인 이유는 단순 숫자 `uint64_t`보다 의미가 분명하기 때문입니다.

```text
uint64_t id
RowRef row_ref
```

둘 다 숫자지만 의미가 다릅니다.
타입 이름을 분리하면 코드 읽을 때 "이 값은 id가 아니라 파일 위치구나"라고 바로 알 수 있습니다.

### `TableDefinition`

```c
typedef struct {
    QualifiedName name;
    StringList columns;
    char *schema_path;
    char *data_path;
} TableDefinition;
```

테이블을 실행 단계에서 다루기 위해 필요한 정보를 모아둔 구조체입니다.

테이블 실행에는 단순히 이름만 필요한 것이 아닙니다.

```text
테이블 이름
스키마 컬럼 목록
.schema 파일 경로
.data 파일 경로
```

이 네 가지가 함께 필요합니다.
그래서 `TableDefinition`으로 묶었습니다.

### `ResultRow`와 `QueryResult`

```c
typedef struct {
    StringList values;
} ResultRow;
```

```c
typedef struct {
    StringList columns;
    ResultRow *rows;
    size_t row_count;
    size_t row_capacity;
} QueryResult;
```

`SELECT` 결과는 표 형태입니다.

```text
columns: id, name
rows:
  1, Alice
  2, Bob
```

그래서 결과는 컬럼 이름 목록과 row 목록을 모두 가져야 합니다.

`ResultRow`는 row 하나의 값 목록이고, `QueryResult`는 전체 결과 테이블입니다.

`row_count`와 `row_capacity`를 나눠 둔 이유는 결과 row 개수가 실행 전에는 정해져 있지 않기 때문입니다.
조건에 맞는 row가 나올 때마다 동적으로 늘립니다.

### `RowCallback`

```c
typedef int (*RowCallback)(RowRef ref, const StringList *values, void *ctx);
```

`RowCallback`은 바이너리 파일을 순회할 때 row 하나마다 호출할 함수의 형태입니다.

이런 callback을 쓰는 이유는 "파일을 읽는 방식"과 "row를 보고 무엇을 할지"를 분리하기 위해서입니다.

예를 들어 같은 `binary_reader_scan_all`이라도 목적이 다를 수 있습니다.

```text
인덱스 재구성: row의 id를 읽어 index_insert
선형 조회: WHERE 조건에 맞으면 QueryResult에 추가
```

파일을 처음부터 끝까지 읽는 코드는 하나로 두고, row마다 할 일은 callback으로 바꿔 끼우는 구조입니다.

### 테이블 로딩/질의 실행 함수

```c
bool load_table_definition(...);
bool append_insert_row(...);
bool run_select_query(...);
```

이 세 함수는 저장소 계층의 핵심 공개 함수입니다.

- `load_table_definition`: `.schema`를 읽고 테이블 정보를 만든다.
- `append_insert_row`: `INSERT` 문장을 실제 바이너리 row로 저장한다.
- `run_select_query`: `SELECT` 문장을 실행해 `QueryResult`를 만든다.

실행기인 `executor.c`는 저장소 내부의 바이너리 포맷이나 B+ Tree 구현을 몰라도 됩니다.
그냥 `append_insert_row` 또는 `run_select_query`만 호출하면 됩니다.

### B+ Tree 인덱스 함수

```c
uint64_t next_id(void);
int index_init(void);
int index_insert(uint64_t id, RowRef ref);
int index_find(uint64_t id, RowRef *out_ref);
bool index_is_ready(void);
```

이 함수들은 id 인덱스를 다룹니다.

- `next_id`: 새 자동 id를 만든다.
- `index_init`: 빈 B+ Tree를 만든다.
- `index_insert`: `id -> row_ref`를 B+ Tree에 등록한다.
- `index_find`: id로 row_ref를 찾는다.
- `index_is_ready`: 인덱스가 준비되었는지 확인한다.

`index_find`가 `RowRef *out_ref`를 받는 이유는 반환값을 두 종류로 나누기 위해서입니다.

```text
함수 반환값 int:
  0  = 찾음
  1  = 없음
  -1 = 오류

out_ref:
  찾았을 때 실제 row 위치 저장
```

### 바이너리 reader/writer 함수

```c
int binary_writer_append_row(const StringList *values, RowRef *out_ref);
int binary_reader_read_row_at(RowRef ref, StringList *out_values);
int binary_reader_scan_all(RowCallback cb, void *ctx);
int migrate_text_data_to_binary(const char *text_path, const char *bin_path);
```

이 함수들은 `.data` 파일의 바이너리 포맷을 다룹니다.

- `binary_writer_append_row`: row를 파일 끝에 쓰고 시작 offset을 반환한다.
- `binary_reader_read_row_at`: 특정 offset의 row 하나를 읽는다.
- `binary_reader_scan_all`: 모든 row를 순회하며 callback을 호출한다.
- `migrate_text_data_to_binary`: 기존 텍스트 데이터를 바이너리로 변환한다.

이렇게 나눠둔 이유는 조회 경로가 두 가지이기 때문입니다.

```text
id 인덱스 조회:
  row_ref를 알고 있으므로 binary_reader_read_row_at 사용

일반 조건 조회:
  모든 row를 봐야 하므로 binary_reader_scan_all 사용
```

### SELECT 경로 판별 함수

```c
bool is_id_equality_predicate(...);
bool is_id_range_predicate(...);
int run_select_by_id(...);
int run_select_by_id_range(...);
int run_select_linear(...);
```

`SELECT`는 조건에 따라 실행 경로가 갈라집니다.

```text
WHERE id = 2
-> run_select_by_id

WHERE id >= 10
-> run_select_by_id_range

WHERE major = 'AI'
-> run_select_linear
```

이 함수들을 분리한 이유는 각 경로가 완전히 다른 성능 특성을 가지기 때문입니다.

- 단건 id 조회: B+ Tree에서 row_ref 하나를 찾는다.
- id 범위 조회: B+ Tree leaf 연결을 따라 여러 row_ref를 찾는다.
- 일반 조회: 바이너리 파일 전체를 scan한다.

## 6. `executor.h`: 파서와 저장소 사이의 실행 결과 계약

관련 파일:

- [`include/executor.h`](../../../../include/executor.h)
- [`src/executor.c`](../../../../src/executor.c)

`executor.h`는 파싱된 `Statement`를 실행한 결과를 어떻게 표현할지 정의합니다.

### `ExecutionKind`

```c
typedef enum {
    EXECUTION_INSERT,
    EXECUTION_SELECT
} ExecutionKind;
```

실행 결과도 문장 종류에 따라 다릅니다.

```text
INSERT 결과: 몇 row가 삽입되었는가
SELECT 결과: 어떤 컬럼과 row들이 조회되었는가
```

그래서 결과 종류를 `ExecutionKind`로 구분합니다.

### `ExecutionResult`

```c
typedef struct {
    ExecutionKind kind;
    size_t affected_rows;
    QueryResult query_result;
} ExecutionResult;
```

`INSERT`와 `SELECT`의 결과를 하나의 타입으로 담기 위한 구조체입니다.

- `kind = EXECUTION_INSERT`: `affected_rows`를 사용한다.
- `kind = EXECUTION_SELECT`: `query_result`를 사용한다.

이 구조가 필요한 이유는 `execute_source_text`가 여러 Statement를 같은 방식으로 실행하고 출력하기 때문입니다.

```text
Statement 하나 실행
-> ExecutionResult 받기
-> print_execution_result로 출력
```

### 실행/출력 함수

```c
bool execute_statement(...);
void free_execution_result(...);
void print_execution_result(...);
```

- `execute_statement`: `Statement` 타입을 보고 저장소 함수로 연결한다.
- `free_execution_result`: SELECT 결과가 들고 있는 동적 메모리를 해제한다.
- `print_execution_result`: INSERT/SELECT 결과를 사용자에게 출력한다.

`print_execution_result`가 따로 있는 이유는 실행과 출력 책임을 분리하기 위해서입니다.
실행 함수는 데이터 처리에 집중하고, 출력 함수는 화면에 어떻게 보여줄지에 집중합니다.

## 7. 헤더끼리 연결되는 흐름

헤더 간 의존 관계를 간단히 보면 다음과 같습니다.

```text
common.h
  -> parser.h
      -> storage.h
          -> executor.h
```

실제 의미는 이렇습니다.

```text
common.h:
  문자열 목록과 파일 유틸리티 제공

parser.h:
  common.h의 StringList를 사용해 SQL 문장 구조체 정의

storage.h:
  parser.h의 InsertStatement, SelectStatement를 받아 실제 저장소 작업 수행

executor.h:
  storage.h의 QueryResult를 포함해 실행 결과를 정의
```

이 구조 덕분에 프로젝트는 아래 흐름을 유지할 수 있습니다.

```text
SQL 문자열
-> parser가 Statement로 변환
-> executor가 Statement 종류 판단
-> storage가 실제 데이터 저장/조회
-> executor가 ExecutionResult 출력
```

## 8. 이 문서에서 기억할 것

헤더 파일은 단순히 함수 선언을 모아둔 파일이 아닙니다.
이 프로젝트에서는 각 계층이 서로 어떤 데이터를 주고받을지 정하는 계약서입니다.

핵심은 다음과 같습니다.

1. SQL 문자열은 실행하기 어렵기 때문에 `Statement` 구조체로 바꾼다.
2. `INSERT`와 `SELECT`는 서로 필요한 정보가 달라서 별도 구조체로 둔다.
3. `WHERE`는 컬럼, 연산자, 값을 나눠 저장해야 실행 경로를 고를 수 있다.
4. `RowRef`는 바이너리 파일에서 row 위치를 뜻하는 별도 의미의 숫자다.
5. `QueryResult`는 SELECT 결과를 표 형태로 표현하기 위한 구조체다.
6. B+ Tree 함수들은 `id -> row_ref` 인덱스를 만들고 사용하는 계약이다.
7. 실행 결과는 `ExecutionResult`로 통일해 `INSERT`와 `SELECT`를 같은 흐름에서 처리한다.

한 문장으로 요약하면:

```text
include/*.h는 SQL 문자열, 실행 계획, 저장소 row, 인덱스 결과를 C 코드가 안전하게 주고받기 위한 공통 언어이다.
```
