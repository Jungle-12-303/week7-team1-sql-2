# Week7 Mini SQL (Binary + Auto ID + B+ Tree Index)

이 프로젝트는 기존 텍스트 기반 `.data` 저장을 바이너리 포맷으로 전환하고,
`INSERT` 시 자동 ID를 부여한 뒤 해당 ID를 메모리 기반 B+ 트리에 등록하여
`WHERE id = ?`를 인덱스 경로로 빠르게 처리하도록 확장한 버전입니다.

실행/검증 기준 환경은 Docker입니다.

## 1. 목표와 범위
- `.data` 텍스트 저장 -> 바이너리 저장 전환
- `INSERT` 시 `id` 자동 부여
- 자동 부여된 `id -> row_ref(byte offset)`를 B+ 트리에 등록
- `WHERE id = ?` 인덱스 조회 분기
- 텍스트 데이터 자동 마이그레이션(일회성)
- 테스트/벤치/데모 문서 반영

## 2. 저장 포맷
바이너리 레코드 포맷(v1):
- `uint32 field_count`
- 각 필드: `uint32 byte_length + raw bytes(UTF-8)`

`row_ref`는 레코드 시작 바이트 오프셋입니다.

## 3. 실행 흐름
- INSERT: `auto id 생성 -> binary append -> bptree_insert`
- SELECT (`WHERE id = ?`): `index_find -> row_ref direct read`
- SELECT (그 외): 바이너리 파일 선형 스캔

## 3-1. B+ 트리 인덱스
- 구현 방식: 디스크 기반이 아닌 메모리 기반 B+ 트리
- 키: `id(uint64_t)`
- 값: `row_ref(byte offset)`
- 리프 노드 분할 시 오른쪽 리프의 최소 키를 부모에 승격
- 내부 노드 분할 시 중앙 키를 부모에 승격
- 중복 ID는 삽입 거부

## 4. 마이그레이션
실행 시 `.data`가 텍스트 포맷으로 감지되면:
1. `students.data.bin.tmp`에 바이너리 변환
2. 기존 텍스트를 `students.data.text.bak`로 백업
3. 바이너리 파일을 `students.data`로 교체

검증 스크립트:
- `scripts/verify_migration.ps1`

## 5. 테스트
단위 테스트(`tests/test_runner.c`)에서 다음을 검증합니다.
- 파서 INSERT/SELECT-WHERE 파싱
- `index_init/index_insert/index_find`
- 자동 ID 증가와 `WHERE id` 조회
- 일반 조건 선형 스캔(`WHERE major = ?`)
- 없는 id 조회 빈 결과
- 텍스트->바이너리 마이그레이션 후 조회 일치

실행:
```bash
docker build -t week7-mini-sql .
```

`docker build` 단계에서 `make`와 `make test`가 함께 수행됩니다.

## 6. 벤치마크 (100만 건)
벤치마크는 Docker 기준으로 실행합니다.
실행 명령은 `docs/demo/demo_commands_only.md`의 "100만 건 벤치마크 (Docker)" 섹션을 사용하세요.

측정 항목:
- Case A: `WHERE id = ?` (B+ 트리 인덱스)
- Case B: `WHERE major = ?` (선형 탐색)
- Case C: 텍스트 삽입 시뮬레이션 대비 바이너리 삽입 시간

### 예시 결과 (2026-04-15)
- `insert_total_ms=16000`
- `id_query_ms=1000`
- `linear_query_ms=2000`
- `case_a_path=B+TREE_ID_INDEX`
- `case_b_path=LINEAR_SCAN_MAJOR`

### A/B 비교 표
| 항목 | 측정값 | 해석 |
| --- | ---: | --- |
| Case A: `WHERE id = ?` (B+ 트리) | 1000ms | 단건 키 조회가 빠르게 수행됨 |
| Case B: `WHERE major = ?` (선형) | 2000ms | 전체 레코드 스캔으로 시간이 더 소요됨 |
| 속도비 (B/A) | 2.0x | B+ 트리 경로가 약 2배 빠름 |

### 시각 자료 (텍스트 바 차트)
```text
Query Latency (lower is better)

Case A (B+ tree id index) : 1000 ms |████████████████████
Case B (linear scan)      : 2000 ms |████████████████████████████████████████

Relative speedup: Case A is 2.0x faster than Case B
```

## 7. 실행 방법
빌드:
```bash
docker build -t week7-mini-sql .
```

인터랙티브 모드:
```bash
docker run -it --rm week7-mini-sql
```

프롬프트에서 SQL을 직접 입력:
```sql
SELECT * FROM demo.students;
SELECT name, grade FROM demo.students WHERE id = 2;
```

파일 실행 모드(기존 방식):
```bash
docker run --rm week7-mini-sql examples/db examples/sql/demo_workflow.sql
```

## 8. 4분 데모 스크립트 + Q&A
데모(4분):
- 0:00~0:40: 문제/목표(텍스트->바이너리, 자동ID, B+ 트리 인덱스)
- 0:40~1:20: 저장 포맷과 B+ 트리 인덱스 설명
- 1:20~2:20: INSERT + WHERE id + WHERE major 시연
- 2:20~3:20: 테스트/마이그레이션 검증
- 3:20~4:00: 벤치 결과 요약

Q&A(예상):
1. 왜 `id`만 인덱스 최적화했나?
- 과제 핵심 경로를 먼저 확실히 검증하기 위해서입니다.
2. 인덱스 실패 시 처리?
- 오류를 반환하고 질의는 실패 처리합니다.
3. 텍스트 대비 바이너리 장점?
- 파싱 비용 감소, row_ref 기반 직접 접근, 인덱스 경로 성능 개선.
