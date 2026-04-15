# B+ Tree 성능과 벤치마크 이해

이 문서는 B+ Tree가 왜 빠른지, 그리고 이 프로젝트의 성능 테스트가 어떤 데이터를 만들고 어떤 쿼리를 비교하는지 이해하기 위한 문서입니다.

01 문서는 B+ Tree 개념, 02 문서는 프로젝트 적용 목적, 03 문서는 구현 흐름을 다뤘습니다.
04 문서는 성능 관점에서 B+ Tree 인덱스를 봅니다.

## 1. B+ Tree 조회는 O(1)이 아니다

B+ Tree 인덱스가 있다고 해서 `WHERE id = ?` 조회가 O(1)이 되는 것은 아닙니다.
id를 찾으려면 root에서 시작해서 internal node를 지나 leaf node까지 내려가야 합니다.

```text
root
-> internal node
-> leaf node
-> leaf 안에서 id 확인
```

그래서 B+ Tree 탐색은 보통 아래처럼 이해합니다.

```text
O(tree height)
= O(log n)
```

다만 B+ Tree는 한 node가 여러 key와 child를 가지는 다분기 트리입니다.
그래서 이진 탐색 트리보다 높이가 낮게 유지되고, 대량 데이터에서도 비교적 적은 단계로 leaf까지 도달할 수 있습니다.

## 2. 그럼 무엇이 빨라지는가

인덱스가 없으면 `WHERE id = ?` 조건도 파일을 처음부터 끝까지 읽어야 합니다.

```text
row 1 확인
row 2 확인
row 3 확인
...
원하는 id를 찾을 때까지 반복
```

이 방식은 선형 탐색입니다.

```text
O(n)
```

B+ Tree 인덱스가 있으면 전체 row를 모두 확인하지 않습니다.
먼저 B+ Tree에서 id에 해당하는 `row_ref`를 찾습니다.

```text
id -> row_ref
```

그 다음 `row_ref`를 사용해 바이너리 `.data` 파일의 특정 위치로 바로 이동합니다.

```text
B+ Tree에서 row_ref 찾기
-> binary_reader_read_row_at(row_ref)
-> 해당 위치의 row 하나 읽기
```

따라서 전체 흐름은 아래처럼 볼 수 있습니다.

```text
B+ Tree에서 id 찾기:
O(log n)

row_ref 위치의 row 읽기:
파일 offset을 알고 있으므로 직접 접근에 가까움
```

즉 B+ Tree는 조회를 O(1)로 만드는 것이 아니라, **전체 파일을 훑는 O(n) 탐색을 트리 높이만큼의 O(log n) 탐색으로 줄여줍니다.**

## 3. `row_ref`가 성능에서 중요한 이유

이 프로젝트에서 B+ Tree는 row 전체를 저장하지 않습니다.
대신 아래 매핑을 저장합니다.

```text
key   = id
value = row_ref
```

`row_ref`는 바이너리 `.data` 파일에서 해당 row가 시작되는 byte offset입니다.

예를 들어 아래처럼 저장되어 있다고 가정할 수 있습니다.

```text
id=1  -> row_ref=0
id=2  -> row_ref=37
id=10 -> row_ref=240
```

`SELECT ... WHERE id = 10`을 실행하면 B+ Tree는 `10`을 찾아 `240`을 반환합니다.
그 다음 저장소 계층은 `.data` 파일의 240 byte 위치로 이동해 row 하나만 읽습니다.

그래서 `row_ref`는 B+ Tree와 실제 데이터 파일을 연결하는 주소 역할을 합니다.

## 4. id 조회와 일반 컬럼 조회의 차이

현재 프로젝트에서 B+ Tree 인덱스는 `id`에만 적용됩니다.

인덱스를 타는 쿼리:

```sql
SELECT name FROM demo.students WHERE id = 500000;
```

인덱스를 타지 않는 쿼리:

```sql
SELECT name FROM demo.students WHERE student_no = '2026500000';
SELECT name FROM demo.students WHERE major = 'M5';
```

`id` 조건은 B+ Tree에서 `row_ref`를 찾을 수 있습니다.
하지만 `student_no`, `major` 같은 컬럼에는 별도 인덱스가 없으므로 파일을 순서대로 읽으며 조건을 확인해야 합니다.

```text
WHERE id = ?
-> B+ Tree 인덱스 경로

WHERE student_no = ?
WHERE major = ?
-> 선형 스캔 경로
```

이 차이가 성능 비교의 핵심입니다.

## 5. 범위 조회는 왜 B+ Tree에 유리한가

단건 조회는 id 하나를 찾아 row 하나를 읽습니다.
범위 조회는 여러 id를 순서대로 읽어야 합니다.

```sql
SELECT id, name FROM demo.students WHERE id >= 10;
```

B+ Tree에서는 먼저 시작 위치가 되는 leaf를 찾습니다.
그 다음부터는 leaf의 `next`를 따라 오른쪽 leaf들을 순서대로 읽습니다.

```text
root/internal 탐색으로 시작 leaf 찾기
-> 현재 leaf에서 조건에 맞는 key 읽기
-> leaf.next로 다음 leaf 이동
-> 범위를 벗어나면 중단
```

이 구조 덕분에 범위 조회는 매 key마다 root부터 다시 탐색하지 않아도 됩니다.

## 6. 성능 테스트 데이터는 어디서 만들어지는가

대용량 데이터 파일이 repository에 그대로 들어 있는 것은 아닙니다.
대신 성능 테스트용 데이터를 생성하는 스크립트가 있습니다.

관련 파일:

- [`scripts/bench_docker.ps1`](../../../../scripts/bench_docker.ps1)
- [`scripts/benchmark.ps1`](../../../../scripts/benchmark.ps1)

`scripts/bench_docker.ps1`는 기본적으로 100만 건 데이터를 생성하는 Docker 기반 벤치마크 스크립트입니다.

생성되는 INSERT는 대략 아래 형태입니다.

```sql
INSERT INTO demo.students (student_no, name, major, grade)
VALUES ('2026000001', 'U1', 'M1', '2');
```

데이터의 특징은 다음과 같습니다.

```text
id:
INSERT 시 자동 생성됨

student_no:
2026000000 + i 형태로 생성됨

name:
U1, U2, U3 ... 형태로 생성됨

major:
M0 ~ M9가 반복됨

grade:
1 ~ 4가 반복됨
```

## 7. 벤치마크는 무엇을 비교하는가

`scripts/bench_docker.ps1`는 대표적으로 아래 두 조회를 비교합니다.

```sql
SELECT name FROM demo.students WHERE id = 500000;
SELECT name FROM demo.students WHERE student_no = '2026500000';
```

두 쿼리는 같은 row를 찾을 수 있지만, 접근 경로가 다릅니다.

```text
WHERE id = 500000
-> B+ Tree 인덱스 사용

WHERE student_no = '2026500000'
-> 인덱스 없음
-> 선형 스캔
```

또 조회 위치를 앞, 중간, 뒤로 나누어 비교합니다.

```text
id = 1
id = 500000
id = 1000000
```

선형 스캔은 찾는 row가 뒤에 있을수록 더 오래 걸립니다.
반면 B+ Tree 인덱스 조회는 트리 높이를 따라 내려가므로 데이터 위치에 따른 차이가 상대적으로 작습니다.

## 8. README 결과를 어떻게 읽어야 하는가

README에는 100만 건 기준 성능 비교 결과가 정리되어 있습니다.

핵심 해석은 다음과 같습니다.

```text
앞쪽 데이터:
선형 스캔도 빨리 찾을 수 있어 차이가 작다.

중간 데이터:
선형 스캔은 절반 정도를 읽어야 하므로 느려진다.

뒤쪽 데이터:
선형 스캔은 거의 전체 파일을 읽어야 하므로 차이가 커진다.
```

즉 B+ Tree의 장점은 데이터가 많아질수록, 그리고 찾는 데이터가 뒤쪽에 있을수록 더 분명하게 드러납니다.

## 9. 이 문서에서 기억할 것

1. B+ Tree 조회는 O(1)이 아니라 O(log n)이다.
2. 인덱스가 없으면 조건에 맞는 row를 찾기 위해 O(n) 선형 스캔을 해야 한다.
3. B+ Tree는 `id -> row_ref`를 저장해 파일의 특정 위치로 바로 이동할 수 있게 한다.
4. 이 프로젝트에서 인덱스는 `id`에만 적용된다.
5. `student_no`, `major` 같은 컬럼 조건은 선형 스캔 경로를 탄다.
6. 벤치마크 데이터는 repository에 대용량 파일로 들어 있는 것이 아니라 스크립트로 생성된다.
7. 성능 비교의 핵심은 B+ Tree 인덱스 조회와 비인덱스 선형 탐색의 차이를 보는 것이다.
