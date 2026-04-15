# main insert select flow

## 한 줄 요약

우리 코드에서 `main`은 직접 `INSERT`와 `SELECT`를 처리하지 않고,
**SQL 읽기 -> 파싱 -> 실행 -> 결과 출력**만 담당합니다.

실제 동작은 아래 파일들이 나눠서 처리합니다.

- `main.c` : 입력 받기, 전체 실행 흐름 제어
- `parser.c` : SQL 문장을 구조체로 해석
- `executor.c` : 문장 종류에 따라 실행 함수 연결
- `storage.c` : 실제 저장, 조회, 인덱스 사용
- `bptree.c` : `id -> ref` 인덱스 처리

---

## 전체 흐름

```text
main
-> SQL 파일 읽기
-> parse_sql_script()
-> execute_statement()
-> INSERT 또는 SELECT 실행
-> 결과 출력
```

---

## 1. main에서 공통으로 하는 일

`main()`은 먼저 SQL 파일 내용을 문자열로 읽고 `execute_source_text()`를 호출합니다.

그 안에서:

1. `parse_sql_script()`로 SQL을 구조체로 바꾼다
2. 문장 하나씩 `execute_statement()`에 넘긴다
3. 실행 결과를 `print_execution_result()`로 출력한다

즉 `main`은 전체 흐름을 연결하는 입구 역할입니다.

---

## 2. INSERT 흐름

예시:

```sql
INSERT INTO demo.students (name, major, grade) VALUES ('Alice', 'DB', 'A');
```

### 흐름

```text
main
-> parse_insert()
-> execute_statement()
-> append_insert_row()
-> 테이블 스키마 읽기
-> storage 활성화
-> id 자동 생성
-> row를 바이너리 파일에 저장
-> B+트리에 (id -> ref) 추가
-> INSERT 1 출력
```

### 쉽게 설명하면

1. `parser.c`가 INSERT 문장을 읽어서 `InsertStatement`로 만든다
2. `executor.c`가 이 문장이 INSERT라고 보고 `append_insert_row()`를 호출한다
3. `storage.c`가 테이블 구조를 읽는다
4. `activate_storage_for_table()`가 현재 테이블을 활성화한다
5. 이 과정에서 기존 데이터 파일을 기준으로 인덱스를 다시 준비한다
6. `next_id()`로 새로운 id를 만든다
7. 새 row를 바이너리 파일 끝에 저장한다
8. 저장된 위치(ref)를 받아서 `index_insert(id, ref)`로 B+트리에 넣는다
9. 성공하면 `INSERT 1`을 출력한다

### 예시 결과

사용자가 아래처럼 입력했다고 가정합니다.

```sql
INSERT INTO demo.students (name, major, grade) VALUES ('Alice', 'DB', 'A');
```

실제로 내부에서는 대략 이렇게 저장됩니다.

```text
id = 1
name = Alice
major = DB
grade = A
ref = 120
```

그리고 인덱스에는:

```text
1 -> 120
```

이렇게 들어갑니다.

---

## 3. SELECT 흐름

예시:

```sql
SELECT * FROM demo.students WHERE id = 1;
```

### 흐름

```text
main
-> parse_select()
-> execute_statement()
-> run_select_query()
-> 테이블 스키마 읽기
-> storage 활성화
-> WHERE 조건 확인
-> id 조건이면 인덱스 사용
-> 아니면 선형 탐색
-> 결과 출력
```

### 쉽게 설명하면

1. `parser.c`가 SELECT 문장을 읽어서 `SelectStatement`로 만든다
2. `executor.c`가 이 문장이 SELECT라고 보고 `run_select_query()`를 호출한다
3. `storage.c`가 테이블 구조를 읽는다
4. `activate_storage_for_table()`가 현재 테이블과 인덱스를 준비한다
5. `WHERE` 조건을 보고 어떤 조회 경로를 쓸지 결정한다

---

## 4. SELECT가 갈라지는 3가지 경로

### 경우 1. `WHERE id = ?`

예:

```sql
SELECT * FROM demo.students WHERE id = 1;
```

이 경우는 B+트리 인덱스를 사용합니다.

```text
run_select_query()
-> run_select_by_id()
-> index_find(id, &ref)
-> ref 위치의 row만 읽기
```

쉽게 말하면:

- `id = 1`을 B+트리에서 찾고
- `1 -> 120` 같은 ref를 얻고
- 파일의 120 위치로 바로 이동해서 row를 읽습니다

즉, **빠른 인덱스 검색 경로**입니다.

---

### 경우 2. `WHERE id > ?`, `>=`, `<`, `<=`

예:

```sql
SELECT * FROM demo.students WHERE id >= 10;
```

이 경우도 B+트리를 사용하지만,
리프 노드의 `next`를 따라가며 여러 row를 읽습니다.

```text
run_select_query()
-> run_select_by_id_range()
-> 시작 리프 찾기
-> next를 따라가며 범위 조회
```

즉, **인덱스를 이용한 범위 검색 경로**입니다.

---

### 경우 3. `WHERE name = 'Alice'` 같은 id 외 조건

예:

```sql
SELECT * FROM demo.students WHERE name = 'Alice';
```

이 경우는 B+트리를 못 씁니다.

```text
run_select_query()
-> run_select_linear()
-> 파일 전체를 처음부터 끝까지 읽기
-> 조건이 맞는 row만 결과에 추가
```

즉, **선형 탐색 경로**입니다.

---

## 5. 아주 쉬운 예시

### INSERT 예시

```sql
INSERT INTO demo.students (name, major, grade) VALUES ('Alice', 'DB', 'A');
```

흐름:

```text
main
-> parse_insert()
-> execute_statement()
-> append_insert_row()
-> 새 id 생성: 1
-> 파일 저장 위치 ref 생성: 120
-> B+트리에 1 -> 120 저장
-> INSERT 1 출력
```

---

### SELECT by id 예시

```sql
SELECT * FROM demo.students WHERE id = 1;
```

흐름:

```text
main
-> parse_select()
-> execute_statement()
-> run_select_query()
-> run_select_by_id()
-> B+트리에서 1 찾기
-> ref = 120 얻기
-> 파일 120 위치의 row 읽기
-> 결과 출력
```

---

### SELECT by name 예시

```sql
SELECT * FROM demo.students WHERE name = 'Alice';
```

흐름:

```text
main
-> parse_select()
-> execute_statement()
-> run_select_query()
-> id 조건이 아님
-> run_select_linear()
-> 파일 전체 스캔
-> name이 Alice인 row만 결과에 추가
-> 결과 출력
```

---

## 핵심 정리

- `INSERT`
  `append_insert_row()`에서 처리하며, `id 자동 생성 -> 파일 저장 -> B+트리에 (id, ref) 추가` 순서로 동작한다

- `SELECT`
  `run_select_query()`에서 처리하며, `WHERE id` 조건이면 인덱스를 사용하고, 그 외 조건이면 선형 탐색을 한다

- `main`
  전체 과정을 묶는 입구 역할만 하고, 실제 저장과 조회는 다른 파일들이 처리한다
