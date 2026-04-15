# 실행 흐름 기준 파일 읽기 순서

이 문서는 프로젝트를 처음 공부할 때 어떤 파일을 어떤 순서로 읽으면 좋은지 정리한 가이드입니다.
파일 구조를 알파벳순으로 보는 대신, 실제 프로그램이 실행될 때의 호출 흐름을 따라 읽는 것을 목표로 합니다.

## 0. 먼저 전체 목표 잡기

### 0-1. `README.md`

가장 먼저 읽을 파일입니다.

이 프로젝트가 무엇을 하려는지 큰 그림을 잡습니다.

- 텍스트 기반 `.data` 저장소를 바이너리 저장소로 바꾼다.
- `INSERT` 시 `id`를 자동으로 부여한다.
- 자동 부여된 `id`와 레코드 위치인 `row_ref`를 B+ 트리에 저장한다.
- `SELECT ... WHERE id = ?`는 B+ 트리에서 단건 row 위치를 찾는다.
- `SELECT ... WHERE id >, >=, <, <= ?`는 B+ 트리 leaf 순회로 범위 조회한다.
- `id`가 아닌 조건은 바이너리 파일을 선형 스캔한다.

여기서 이해해야 할 핵심은 이 프로젝트가 단순 SQL 파서가 아니라, 저장 방식과 조회 경로를 바꾸는 과제라는 점입니다.

### 0-2. `include/*.h`

소스 파일을 읽기 전에 헤더를 가볍게 훑습니다.

- `include/parser.h`: SQL 문장이 어떤 구조체로 표현되는지 확인합니다.
- `include/storage.h`: 테이블, 결과 row, B+ 트리 인덱스, 바이너리 reader/writer 함수 목록을 확인합니다.
- `include/executor.h`: 파싱된 문장이 실행 결과로 어떻게 이어지는지 확인합니다.
- `include/common.h`: 문자열 리스트와 파일 유틸리티를 확인합니다.

이 단계에서는 모든 구현을 이해하려고 하지 말고, 함수 이름과 자료구조 이름만 익숙하게 만드는 것이 좋습니다.

## 1. 프로그램 시작점부터 읽기

### 1-1. `src/main.c`

실제 실행은 `main` 함수에서 시작합니다.

여기서 확인할 것은 다음입니다.

- 명령행 인자를 어떻게 해석하는가
- SQL 파일 실행, 표준입력 실행, 인터랙티브 모드를 어떻게 나누는가
- SQL 파일 내용을 어떻게 읽어오는가
- 읽은 SQL 문자열을 어떤 함수로 넘기는가

중요한 호출 흐름은 아래와 같습니다.

```text
main
-> parse_cli_options
-> read_text_file
   또는 read_stream_text
   또는 run_interactive
-> execute_source_text
-> parse_sql_script
-> execute_statement
-> print_execution_result
```

`main.c`에서 가장 중요한 함수는 `execute_source_text`입니다.
이 함수는 SQL 문자열 전체를 받아서 파싱하고, 파싱된 문장을 하나씩 실행합니다.

따라서 다음으로는 `parse_sql_script`가 구현된 `src/parser.c`를 읽어야 합니다.

## 2. SQL 문자열이 AST로 바뀌는 과정 읽기

### 2-1. `src/parser.c`

`main.c`에서 SQL 문자열은 `parse_sql_script`로 넘어갑니다.
그래서 두 번째로 읽을 핵심 파일은 `src/parser.c`입니다.

이 파일에서 이해해야 할 일은 크게 두 가지입니다.

1. SQL 문자열을 토큰으로 나눈다.
2. 토큰을 `Statement` 구조체로 바꾼다.

중요한 흐름은 아래와 같습니다.

```text
parse_sql_script
-> tokenize_sql
-> parse_insert 또는 parse_select
-> script_append
```

`tokenize_sql`에서는 `INSERT`, `SELECT`, `FROM`, `WHERE`, 문자열, 숫자, 쉼표, 괄호, 비교 연산자(`=`, `>`, `>=`, `<`, `<=`) 같은 조각을 토큰으로 만듭니다.

그 다음 `parse_sql_script`는 첫 토큰을 보고 문장 종류를 고릅니다.

- `INSERT`로 시작하면 `parse_insert`
- `SELECT`로 시작하면 `parse_select`

`parse_insert`를 읽을 때는 아래 구조체가 어떻게 채워지는지 보면 됩니다.

```c
InsertStatement
```

여기에는 삽입 대상 테이블, 컬럼 목록, 값 목록이 들어갑니다.

`parse_select`를 읽을 때는 아래 구조체가 어떻게 채워지는지 보면 됩니다.

```c
SelectStatement
```

여기에는 조회 대상 테이블, 선택 컬럼, `WHERE` 조건과 비교 연산자가 들어갑니다.

파서는 SQL을 실제로 실행하지 않습니다.
파서는 문자열을 실행하기 쉬운 구조체로 바꿔주는 역할만 합니다.

파싱된 `Statement`는 다시 `main.c`의 `execute_source_text`로 돌아가고, 거기서 `execute_statement`를 호출합니다.
따라서 다음 순서는 `src/executor.c`입니다.

## 3. 파싱 결과가 실제 실행으로 연결되는 과정 읽기

### 3-1. `src/executor.c`

`src/executor.c`는 파서와 저장소 사이를 연결하는 파일입니다.

가장 중요한 함수는 `execute_statement`입니다.

```text
execute_statement
-> INSERT면 append_insert_row
-> SELECT면 run_select_query
```

이 파일은 직접 많은 일을 하지 않습니다.
대신 문장 타입을 보고 저장소 계층 함수로 넘깁니다.

여기서 이해해야 할 핵심은 다음입니다.

- `INSERT` 실행의 실제 구현은 `storage.c`의 `append_insert_row`에 있다.
- `SELECT` 실행의 실제 구현은 `storage.c`의 `run_select_query`에 있다.
- 실행 결과 출력은 `print_execution_result`가 담당한다.

즉, 이 프로젝트의 진짜 핵심 로직은 저장 계층인 `src/storage.c`에 있습니다.
따라서 다음 순서는 `src/storage.c`입니다.

## 4. INSERT 저장 흐름 읽기

### 4-1. `src/storage.c`의 `append_insert_row`

`INSERT`를 이해하려면 `append_insert_row`부터 읽습니다.

호출 흐름은 아래와 같습니다.

```text
append_insert_row
-> load_table_definition
-> activate_storage_for_table
-> next_id
-> binary_writer_append_row
-> index_insert
```

각 단계에서 하는 일은 다음과 같습니다.

### `load_table_definition`

`.schema` 파일을 읽어서 테이블 컬럼 목록을 가져옵니다.

예를 들어 아래 스키마가 있으면:

```text
id|name|major|grade
```

테이블 컬럼은 `id`, `name`, `major`, `grade` 순서로 저장됩니다.

### `activate_storage_for_table`

현재 사용할 `.data` 파일을 활성화합니다.

이 함수에서 중요한 일이 많이 일어납니다.

- 데이터 파일이 없으면 빈 파일을 만든다.
- 기존 `.data`가 텍스트처럼 보이면 바이너리로 마이그레이션한다.
- B+ 트리 인덱스를 초기화한다.
- 바이너리 파일 전체를 스캔해서 기존 row의 `id -> row_ref` 인덱스를 다시 만든다.
- 가장 큰 id를 찾아 다음 자동 id 기준으로 삼는다.

### `next_id`

자동 증가 id를 하나 생성합니다.

이 프로젝트에서는 사용자가 `INSERT`에 `id` 값을 직접 넣는 것이 아니라, 저장소가 자동으로 id 값을 채웁니다.

### `binary_writer_append_row`

완성된 row를 바이너리 포맷으로 `.data` 파일 끝에 추가합니다.

바이너리 레코드 포맷은 다음과 같습니다.

```text
uint32 field_count
uint32 field_length + field_bytes
uint32 field_length + field_bytes
...
```

이때 파일에 쓰기 직전의 offset이 `row_ref`가 됩니다.

### `index_insert`

방금 생성한 `id`와 `row_ref`를 B+ 트리에 저장합니다.

```text
id -> row_ref
```

이 매핑이 있어야 나중에 `WHERE id = ?` 조회에서 파일 전체를 읽지 않고 해당 위치로 바로 이동할 수 있습니다.

`append_insert_row`를 이해했다면, 다음은 `SELECT` 경로를 읽어야 합니다.

## 5. SELECT 조회 흐름 읽기

### 5-1. `src/storage.c`의 `run_select_query`

`SELECT`를 이해하려면 `run_select_query`를 읽습니다.

호출 흐름은 아래와 같습니다.

```text
run_select_query
-> load_table_definition
-> activate_storage_for_table
-> is_id_equality_predicate
-> is_id_range_predicate
-> run_select_by_id 또는 run_select_by_id_range 또는 run_select_linear
-> project_row
```

여기서 가장 중요한 분기는 `is_id_equality_predicate`와 `is_id_range_predicate`입니다.

### `is_id_equality_predicate`

`WHERE` 조건이 정확히 `id = 숫자` 형태인지 검사합니다.

예를 들어 아래 SQL은 인덱스 경로로 갑니다.

```sql
SELECT name FROM demo.students WHERE id = 2;
```

### `is_id_range_predicate`

`WHERE` 조건이 `id > 숫자`, `id >= 숫자`, `id < 숫자`, `id <= 숫자` 형태인지 검사합니다.

예를 들어 아래 SQL은 B+ 트리 범위 조회 경로로 갑니다.

```sql
SELECT id, name FROM demo.students WHERE id >= 4;
```

반면 아래 SQL은 `id`가 아닌 컬럼 조건이므로 선형 스캔 경로로 갑니다.

```sql
SELECT name FROM demo.students WHERE major = 'AI';
```

### `run_select_by_id`

`WHERE id = ?` 전용 빠른 경로입니다.

호출 흐름은 아래와 같습니다.

```text
run_select_by_id
-> index_find
-> append_row_by_ref
-> binary_reader_read_row_at
-> query_result_append_row
```

`index_find`로 id에 해당하는 `row_ref`를 찾고, `append_row_by_ref`를 거쳐 `binary_reader_read_row_at`으로 해당 offset의 row만 읽습니다.

### `run_select_by_id_range`

`WHERE id >, >=, <, <= ?` 전용 범위 조회 경로입니다.

호출 흐름은 아래와 같습니다.

```text
run_select_by_id_range
-> bpt_lower_bound 또는 bpt_leftmost_leaf
-> leaf next 포인터를 따라 순회
-> append_row_by_ref
-> binary_reader_read_row_at
```

이 경로는 B+ 트리 leaf 노드가 `next` 포인터로 연결되어 있다는 점을 활용합니다.
`id >= ?`, `id > ?`는 lower bound 위치부터 오른쪽 leaf들을 순회하고, `id <= ?`, `id < ?`는 가장 왼쪽 leaf부터 조건이 깨질 때까지 순회합니다.

### `run_select_linear`

id 인덱스를 쓸 수 없는 일반 조건은 이 경로를 사용합니다.

호출 흐름은 아래와 같습니다.

```text
run_select_linear
-> binary_reader_scan_all
-> linear_scan_callback
-> query_result_append_row
```

이 경로는 바이너리 파일 전체를 처음부터 끝까지 읽으면서 조건에 맞는 row를 찾습니다.
현재 일반 조건도 `=`, `>`, `>=`, `<`, `<=` 비교 연산자를 사용할 수 있지만, B+ 트리 인덱스는 `id` 조건에만 적용됩니다.

### `project_row`

조회 결과에서 사용자가 요청한 컬럼만 골라냅니다.

예를 들어 아래 SQL은 전체 row를 찾은 뒤 `name`, `grade` 컬럼만 결과에 남깁니다.

```sql
SELECT name, grade FROM demo.students WHERE id = 2;
```

여기까지 읽으면 `INSERT`와 `SELECT`가 저장소에서 어떻게 처리되는지 큰 흐름이 잡힙니다.
다음은 인덱스 자체인 B+ 트리를 읽습니다.

## 6. B+ 트리 인덱스 읽기

### 6-1. `src/bptree.c`

B+ 트리만 따로 이해하고 싶다면 `src/bptree.c`를 읽습니다.

중요한 공개 함수는 세 개입니다.

```text
index_init
index_insert
index_find
```

각 함수의 역할은 다음과 같습니다.

- `index_init`: 빈 B+ 트리를 만든다.
- `index_insert`: `id -> row_ref`를 삽입한다.
- `index_find`: id로 row_ref를 찾는다.

내부적으로는 leaf node와 internal node를 나누어 관리합니다.

읽는 순서는 아래를 추천합니다.

1. `BptNode` 구조체
2. `index_init`
3. `index_insert`
4. `bpt_insert_recursive`
5. `index_find`
6. `bpt_find`

특히 `bpt_insert_recursive`는 가장 어렵지만 가장 중요한 함수입니다.
여기서 leaf node가 가득 찼을 때 split하고, split 결과를 부모 node로 올립니다.

주의할 점이 있습니다.
현재 실제 빌드는 `Makefile` 기준으로 `src/storage.c` 안의 B+ 트리 구현을 사용합니다.
`src/bptree.c`에도 알고리즘을 따로 볼 수 있는 구현이 남아 있지만, `Makefile`의 `COMMON_SOURCES`에는 포함되지 않습니다.
공부할 때는 B+ 트리 알고리즘 자체는 `src/bptree.c`에서 먼저 읽고, 실제 저장소와 연결되는 흐름은 `src/storage.c`에서 확인하면 됩니다.

## 7. 공통 유틸리티 읽기

### 7-1. `src/common.c`

핵심 실행 흐름을 먼저 읽은 뒤, 마지막에 공통 유틸리티를 읽습니다.

이 파일은 여러 곳에서 반복해서 필요한 작은 기능을 제공합니다.

- `sql_strdup`: 문자열 복사
- `sql_stricmp`: 대소문자 무시 문자열 비교
- `string_list_append`: 동적 문자열 리스트에 값 추가
- `string_list_free`: 문자열 리스트 메모리 해제
- `read_text_file`: 파일 전체 읽기
- `write_text_file`: 파일 쓰기
- `ensure_directory_recursive`: 디렉터리 생성

이 파일을 먼저 읽지 않는 이유는, 여기 있는 함수들은 프로젝트의 핵심 정책보다는 보조 도구에 가깝기 때문입니다.
`main.c`, `parser.c`, `storage.c`를 읽다가 자주 보이는 함수가 궁금할 때 돌아와서 확인하면 됩니다.

## 8. 테스트로 이해 확인하기

### 8-1. `tests/test_runner.c`

구현 흐름을 읽은 뒤에는 테스트를 읽으면 이해가 훨씬 단단해집니다.

추천 읽기 순서는 아래와 같습니다.

1. `test_parse_insert`
2. `test_parse_select_where`
3. `test_index_insert_find`
4. `test_auto_id_and_where_id`
5. `test_where_name_linear`
6. `test_where_id_not_found`
7. `test_text_to_binary_migration`

테스트는 이 프로젝트가 어떤 동작을 보장하려는지 가장 직접적으로 보여줍니다.

특히 아래 세 테스트가 중요합니다.

- `test_auto_id_and_where_id`: 자동 id와 단건 인덱스 조회가 연결되는지 확인합니다.
- id 범위 조회 테스트: `WHERE id >= ?`, `WHERE id <= ?`가 B+ 트리 범위 경로로 동작하는지 확인합니다.
- `test_where_name_linear`: id가 아닌 조건은 선형 스캔으로 조회되는지 확인합니다.
- `test_text_to_binary_migration`: 텍스트 데이터가 바이너리로 바뀐 뒤에도 조회 가능한지 확인합니다.

## 9. 데모와 벤치마크 문서 읽기

### 9-1. `examples/sql/demo_workflow.sql`

실제 데모 SQL입니다.

```sql
INSERT ...
INSERT ...
SELECT * ...
SELECT ... WHERE id = 2;
```

이 파일을 읽으면 발표에서 어떤 기능을 보여주려는지 바로 알 수 있습니다.

### 9-2. `docs/demo/demo_scenario.md`

발표용 시나리오입니다.

이 문서는 코드 이해보다는 설명 순서를 잡는 데 도움이 됩니다.

### 9-3. `scripts/*.ps1`

PowerShell 환경에서 빌드, 테스트, 데모, 벤치마크를 실행하는 스크립트입니다.

추천 순서는 아래와 같습니다.

1. `scripts/build.ps1`
2. `scripts/test.ps1`
3. `scripts/demo.ps1`
4. `scripts/verify_migration.ps1`
5. `scripts/bench_docker.ps1`

현재 `scripts/build.ps1`과 `Makefile` 모두 실제 실행 파일 빌드에는 `src/storage.c` 안의 B+ 트리 구현을 사용합니다.
`src/bptree.c`는 알고리즘을 따로 공부하기 좋은 파일이지만, 현재 기본 빌드 경로에는 포함되지 않습니다.

## 최종 추천 읽기 순서

아래 순서대로 읽으면 실행 흐름을 가장 자연스럽게 따라갈 수 있습니다.

1. `README.md`
2. `include/parser.h`
3. `include/storage.h`
4. `include/executor.h`
5. `include/common.h`
6. `src/main.c`
7. `src/parser.c`
8. `src/executor.c`
9. `src/storage.c`의 `append_insert_row`
10. `src/storage.c`의 `run_select_query`
11. `src/storage.c`의 바이너리 reader/writer 함수들
12. `src/storage.c`의 마이그레이션 함수들
13. `src/bptree.c`
14. `src/common.c`
15. `tests/test_runner.c`
16. `examples/sql/demo_workflow.sql`
17. `examples/sql/demo_range_workflow.sql`
18. `docs/demo/demo_scenario.md`
19. `scripts/build.ps1`, `scripts/test.ps1`, `scripts/bench_docker.ps1`

## 한 번에 보는 실행 흐름

### 파일 실행 모드

```text
src/main.c
main
-> read_text_file
-> execute_source_text
-> parse_sql_script
-> execute_statement
```

### INSERT 실행 흐름

```text
execute_statement
-> append_insert_row
-> load_table_definition
-> activate_storage_for_table
-> next_id
-> binary_writer_append_row
-> index_insert
```

### SELECT WHERE id 실행 흐름

```text
execute_statement
-> run_select_query
-> load_table_definition
-> activate_storage_for_table
-> is_id_equality_predicate
-> run_select_by_id
-> index_find
-> append_row_by_ref
-> binary_reader_read_row_at
-> project_row
```

### SELECT WHERE id 범위 실행 흐름

```text
execute_statement
-> run_select_query
-> load_table_definition
-> activate_storage_for_table
-> is_id_equality_predicate
-> is_id_range_predicate
-> run_select_by_id_range
-> bpt_lower_bound 또는 bpt_leftmost_leaf
-> append_row_by_ref
-> binary_reader_read_row_at
-> project_row
```

### SELECT 일반 조건 실행 흐름

```text
execute_statement
-> run_select_query
-> load_table_definition
-> activate_storage_for_table
-> is_id_equality_predicate
-> is_id_range_predicate
-> run_select_linear
-> binary_reader_scan_all
-> linear_scan_callback
-> project_row
```

## 공부할 때의 기준 질문

각 파일을 읽을 때 아래 질문에 답할 수 있으면 충분히 이해한 것입니다.

1. 이 파일은 어떤 책임을 갖는가?
2. 이 파일은 이전 단계에서 어떤 데이터를 넘겨받는가?
3. 이 파일은 다음 단계의 어떤 함수를 호출하는가?
4. `INSERT`와 `SELECT` 중 어느 흐름에 더 중요한가?
5. 이 파일이 실패하면 사용자에게 어떤 오류로 보이는가?

이 질문을 기준으로 읽으면, 단순히 코드를 눈으로 훑는 것이 아니라 프로그램의 실행 흐름을 따라가며 이해할 수 있습니다.
