# 데모 시연 시나리오 (발표자용)

## 목적
이 시연은 다음 4가지를 눈으로 확인시키는 데 목적이 있습니다.
- INSERT 시 id 자동 부여
- WHERE id = ? 경로의 인덱스 조회
- 텍스트 `.data` -> 바이너리 `.data` 자동 전환
- Docker 기준 테스트/벤치 재현

## 시연 전 준비
- 터미널은 Docker가 가능한 환경에서 실행한다.
- 현재 폴더는 프로젝트 루트(`Week7`)여야 한다.

## 1단계: 이미지 빌드 + 내부 테스트 통과 확인
### 명령어
```bash
docker build -t week7-mini-sql .
```

### 화면에서 기대할 결과
- build 로그 중 `RUN make && make test` 단계가 성공한다.
- 최종적으로 이미지 빌드 성공 메시지가 나온다.

### 발표 멘트
- "도커 빌드 안에서 컴파일과 테스트를 같이 수행해서, 운영체제 차이 없이 동일하게 검증됩니다."

## 2단계: 기본 워크플로 시연 실행
### 명령어
```bash
docker run --rm week7-mini-sql
```

### 화면에서 기대할 결과
- `INSERT 1` 출력이 5번 나타난다.
- `SELECT *` 결과 테이블에 기존 데이터 + 신규 데이터가 함께 보인다.
- `WHERE id = 2` 조건 결과가 1건으로 출력된다.
- `WHERE id >= 4` 조건 결과가 다건으로 출력된다.

### 발표 멘트
- "INSERT 시 id를 입력하지 않아도 자동으로 부여됩니다."
- "id 조건은 인덱스 경로로, 일반 조건은 선형 스캔 경로로 분기됩니다."

## 2-1단계: Docker Only 인터랙티브 CLI 실행 (PowerShell)
### 명령어
```powershell
# 1) 이미지 빌드
docker build -t week7-mini-sql .

# 2) 인터랙티브(우리 SQL CLI)
docker run --rm -it -v "${PWD}:/work" week7-mini-sql -d /work/examples/db -i
```

### 인터랙티브 안에서 입력
```sql
SELECT * FROM demo.students;
```

### 화면에서 기대할 결과
- `mini_sql>` 프롬프트가 표시된다.
- SQL 실행 결과 테이블이 출력된다.
- `exit` 또는 `quit`으로 정상 종료된다.

### 발표 멘트
- "PowerShell 프롬프트에서 SQL을 직접 치는 것이 아니라, mini_sql 프롬프트 안에서 SQL을 입력합니다."

## 3단계: CLI 범위 조회 시연
### 명령어
```bash
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_range_workflow.sql"
```

### 화면에서 기대할 결과
- `WHERE id >= 4` 결과가 연속 id 구간으로 출력된다.
- `WHERE id <= 5` 결과가 낮은 id 구간으로 출력된다.
- `WHERE id > 6`, `WHERE id < 8` 결과가 각각 기대 범위만 출력된다.

### 발표 멘트
- "B+ 트리 인덱스를 point 조회뿐 아니라 range 조회에도 적용했습니다."
- "CLI에서 `>=`, `<=`, `>`, `<` 조건을 바로 실행해 범위 필터링을 검증할 수 있습니다."

## 4단계: 마이그레이션 증거 확인
### 명령어
```bash
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_workflow.sql && \
 ls -l /tmp/demo/db/demo && \
 od -An -tx1 -N 32 /tmp/demo/db/demo/students.data && \
 echo '--- backup ---' && cat /tmp/demo/db/demo/students.data.text.bak"
```

### 화면에서 기대할 결과
- `students.data`와 `students.data.text.bak`가 함께 보인다.
- `students.data`는 사람이 읽기 어려운 바이너리 바이트(hex)로 보인다.
- `students.data.text.bak`는 사람이 읽을 수 있는 텍스트 row로 보인다.

### 발표 멘트
- "초기 텍스트 데이터는 백업(`.text.bak`)으로 보존하고, 활성 데이터 파일은 바이너리로 전환합니다."
- "이 방식으로 기존 데이터 손실 위험을 줄이면서 저장 포맷을 바꿉니다."

## 5단계: 100만 건 벤치마크 시연 (Docker/PowerShell)
### 명령어
```powershell
.\scripts\bench_docker.ps1
```

### 화면에서 기대할 결과
- `insert_total_ms`, `id_query_ms`, `linear_query_ms` 숫자가 출력된다.
- `case_a_path=B+TREE_ID_INDEX`, `case_b_path=LINEAR_SCAN_MAJOR` 라벨이 출력된다.
- `WHERE id = 777777` 결과가 1건 출력된다.
- `WHERE major = 'M5'` 결과가 100000건 출력된다.

### 실제 측정 예시 (2026-04-15)
- `insert_total_ms=16000`
- `id_query_ms=1000`
- `linear_query_ms=2000`
- `case_a_path=B+TREE_ID_INDEX`
- `case_b_path=LINEAR_SCAN_MAJOR`

### 비교 해석 (발표용)
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

### 발표 멘트
- "동일한 Docker 환경에서 100만 건 삽입 후 B+ 트리 경로(Case A)와 선형 경로(Case B)를 분리 측정했습니다."
- "동일 조건에서 id 경로가 선형 스캔 대비 약 2배 빨랐고, 출력 라벨로 경로 자체를 함께 검증했습니다."
<!-- - "PowerShell quoting 이슈를 피하기 위해 벤치는 `scripts/bench_docker.ps1` 단일 명령으로 실행합니다." -->

## Q&A 빠른 답변
- 왜 id만 인덱스 최적화했나요?
  - 과제 핵심 요구사항을 우선 충족하기 위해 id equality 경로를 명시적으로 최적화했습니다.
- 벤치와 테스트 실행 기준은 무엇인가요?
  - 본 프로젝트는 Docker 실행 경로를 기준으로 동일하게 재현합니다.
- 마이그레이션 실패 시 데이터는?
  - 기존 텍스트 파일을 백업으로 남기고 전환합니다.
