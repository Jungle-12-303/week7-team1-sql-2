# B+ Tree가 프로젝트에서 맡는 역할

이 문서는 B+ Tree가 우리 프로젝트에서 무엇을 하기 위해 적용되었는지 설명합니다.

01 문서가 B+ Tree의 개념을 설명했다면, 이 문서는 그 개념이 실제 mini SQL 프로젝트에서 어떤 문제를 해결하는지에 집중합니다.

## 1. 프로젝트의 핵심 목표

이 프로젝트의 핵심 목표 중 하나는 아래처럼 `id`를 기준으로 조회하는 SQL을 빠르게 처리하는 것입니다.

```sql
SELECT * FROM demo.students WHERE id = 2;
SELECT * FROM demo.students WHERE id >= 10;
```

id는 row를 식별하는 값입니다.
그래서 `WHERE id = ?` 조건은 보통 결과가 0개 또는 1개입니다.
또한 `WHERE id > ?`, `WHERE id >= ?`, `WHERE id < ?`, `WHERE id <= ?` 조건은 id 순서에 맞는 여러 row를 찾는 범위 조회입니다.

이런 조건을 처리하기 위해 파일 전체를 처음부터 끝까지 읽는 것은 비효율적입니다.

## 2. 인덱스가 없을 때

인덱스가 없으면 `WHERE id = 2`를 처리할 때도 모든 row를 확인해야 합니다.

예를 들어 데이터 파일에 100만 건이 있다면:

```text
1번 row 읽기 -> id 확인
2번 row 읽기 -> id 확인
3번 row 읽기 -> id 확인
...
1,000,000번 row 읽기 -> id 확인
```

원하는 id가 중간에 있어도, 일반적인 선형 스캔 방식에서는 파일을 순서대로 읽어야 합니다.

이 방식은 구현은 단순하지만 데이터가 많아질수록 느려집니다.

## 3. 인덱스가 있을 때

B+ Tree 인덱스가 있으면 id로 row 위치를 바로 찾을 수 있습니다.

우리 프로젝트의 인덱스는 아래 정보를 저장합니다.

```text
id -> row_ref
```

`row_ref`는 바이너리 `.data` 파일 안에서 해당 row가 시작되는 byte offset입니다.

예를 들어:

```text
id=1 -> row_ref=0
id=2 -> row_ref=37
id=3 -> row_ref=78
```

이 상태에서 `WHERE id = 2`가 들어오면:

```text
B+ Tree에서 id=2 검색
-> row_ref=37 획득
-> 파일 37번째 byte로 이동
-> row 하나만 읽기
```

즉, 전체 파일을 다 읽지 않아도 됩니다.

범위 조건에서도 비슷한 이점이 있습니다.

예를 들어 아래 SQL을 생각해 보겠습니다.

```sql
SELECT * FROM demo.students WHERE id >= 10;
```

B+ Tree는 먼저 `10`이 처음 나올 수 있는 leaf 위치를 찾습니다.
그 뒤에는 leaf 노드의 `next` 연결을 따라 오른쪽으로 이동하면서 조건에 맞는 row_ref를 읽습니다.

```text
B+ Tree에서 id >= 10 시작 위치 검색
-> 시작 leaf와 index 획득
-> 현재 leaf의 남은 key들을 순서대로 확인
-> leaf.next로 오른쪽 leaf 이동
-> 끝까지 row_ref를 읽어 row를 복원
```

`id <= 50`이나 `id < 50` 같은 조건은 가장 왼쪽 leaf부터 시작해 key가 범위를 벗어나는 순간 멈춥니다.
이처럼 B+ Tree의 leaf 연결 리스트는 범위 조회에서 특히 중요합니다.

## 4. INSERT에서 B+ Tree가 하는 일

INSERT가 실행되면 프로젝트는 id를 자동으로 생성합니다.

관련 함수:

- [`append_insert_row`](../../../../src/storage.c#L1415)
- [`next_id`](../../../../src/storage.c#L361)
- [`binary_writer_append_row`](../../../../src/storage.c#L726)
- [`index_insert`](../../../../src/storage.c#L320)

흐름은 다음과 같습니다.

```text
INSERT 요청
-> next_id로 새 id 생성
-> row를 바이너리 파일 끝에 저장
-> 저장 위치 row_ref 획득
-> B+ Tree에 id -> row_ref 등록
```

예를 들어 새 row가 id 4를 받고 파일 offset 120에 저장되었다면:

```text
4 -> 120
```

이 매핑이 B+ Tree에 추가됩니다.

## 5. SELECT에서 B+ Tree가 하는 일

SELECT에서는 먼저 WHERE 조건이 id 단건 조회인지, id 범위 조회인지 확인합니다.

관련 함수:

- [`run_select_query`](../../../../src/storage.c#L1546)
- [`is_id_equality_predicate`](../../../../src/storage.c#L1192)
- [`is_id_range_predicate`](../../../../src/storage.c#L1208)
- [`run_select_by_id`](../../../../src/storage.c#L1246)
- [`run_select_by_id_range`](../../../../src/storage.c#L1260)
- [`index_find`](../../../../src/storage.c#L348)
- [`binary_reader_read_row_at`](../../../../src/storage.c#L784)

단건 조회 흐름은 다음과 같습니다.

```text
SELECT 요청
-> WHERE id = 숫자 형태인지 확인
-> 맞으면 B+ Tree에서 id 검색
-> row_ref 획득
-> 해당 offset의 row만 읽기
```

아래 SQL은 B+ Tree 경로를 탑니다.

```sql
SELECT name FROM demo.students WHERE id = 2;
```

범위 조회 흐름은 다음과 같습니다.

```text
SELECT 요청
-> WHERE id >, >=, <, <= 숫자 형태인지 확인
-> 맞으면 B+ Tree leaf에서 시작 위치를 찾음
-> leaf.next 연결을 따라 조건에 맞는 row_ref 수집
-> 각 row_ref로 row를 읽기
```

아래 SQL도 B+ Tree 경로를 탑니다.

```sql
SELECT id, name FROM demo.students WHERE id >= 10;
SELECT id, name FROM demo.students WHERE id <= 50;
```

아래 SQL은 B+ Tree 경로가 아닙니다.

```sql
SELECT name FROM demo.students WHERE major = 'AI';
```

`major`에는 인덱스가 없기 때문에 전체 파일을 선형 스캔합니다.

## 6. 기존 데이터에서 B+ Tree를 다시 만드는 이유

우리 프로젝트의 B+ Tree는 메모리 기반입니다.
즉, 프로그램이 종료되면 인덱스도 사라집니다.

그래서 특정 테이블을 사용할 때마다 데이터 파일을 읽어 인덱스를 다시 구성합니다.

관련 함수:

- [`activate_storage_for_table`](../../../../src/storage.c#L1024)
- [`binary_reader_scan_all`](../../../../src/storage.c#L813)
- [`build_index_callback`](../../../../src/storage.c#L995)

흐름은 다음과 같습니다.

```text
테이블 활성화
-> index_init
-> 바이너리 파일 전체 scan
-> 각 row의 id와 row_ref 추출
-> index_insert(id, row_ref)
```

이 과정을 거쳐 기존 데이터도 `WHERE id = ?` 인덱스 조회 대상이 됩니다.

## 7. B+ Tree가 적용되지 않는 경우

현재 프로젝트에서 B+ Tree는 모든 SELECT에 적용되지 않습니다.

적용되는 경우:

```sql
WHERE id = 2
WHERE id > 10
WHERE id >= 10
WHERE id < 50
WHERE id <= 50
```

적용되지 않는 경우:

```sql
WHERE major = 'AI'
WHERE name = 'Alice'
WHERE id = 'abc'
조건 없음
```

이유는 현재 구현이 id 컬럼 하나만 인덱싱하기 때문입니다.
또한 id 값은 숫자로 파싱되어야 합니다.

## 8. 우리 프로젝트에서 B+ Tree의 정확한 역할

이 프로젝트에서 B+ Tree는 SQL 데이터를 직접 저장하는 저장소가 아닙니다.

실제 row 데이터는 바이너리 `.data` 파일에 저장됩니다.
B+ Tree는 그 row가 어디에 있는지 알려주는 지도 역할을 합니다.

```text
바이너리 파일 = 실제 데이터 저장소
B+ Tree = id로 row 위치를 찾는 지도
```

그래서 B+ Tree 안에는 이름, 전공, 학점 같은 전체 row 데이터가 들어가지 않습니다.
들어가는 것은 오직 다음 매핑입니다.

```text
id -> row_ref
```

## 9. 이 문서에서 기억할 것

B+ Tree는 이 프로젝트에서 다음 일을 하기 위해 적용되었습니다.

1. `INSERT` 시 자동 생성된 id와 row 위치를 저장한다.
2. `WHERE id = ?` 조회 시 row 위치를 빠르게 찾는다.
3. `WHERE id >, >=, <, <= ?` 조회 시 leaf 연결을 따라 범위 row 위치를 찾는다.
4. 찾은 row 위치로 바이너리 파일에서 row를 직접 읽는다.
5. id가 아닌 조건은 기존처럼 선형 스캔하게 둔다.

한 문장으로 요약하면:

```text
B+ Tree는 id를 보고 바이너리 파일 안의 row 위치를 찾고, id 범위 조건에서는 leaf 연결을 따라 여러 row 위치를 순서대로 찾기 위한 메모리 기반 인덱스이다.
```
