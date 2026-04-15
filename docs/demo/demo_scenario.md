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
- 벤치 표에 `Target ID`가 `1`, `500000`, `1000000`으로 3줄 출력된다.
- 각 줄에서 `ID Index avg/p95`, `StudentNo avg/p95`, `Speedup`가 함께 출력된다.
- 각 케이스에서 `Target Name`이 `U1`, `U500000`, `U1000000`으로 검증된다.
- `Legend`에 `ID Index = B+Tree`, `StudentNo = Linear Scan`이 표시된다.

### 실제 측정 예시 (2026-04-16)
- Rows: `1000000`, Runs: `5`
- Insert Total: `12249 ms`
- `id=1`      -> `ID 997.000/1055`, `StudentNo 1493.200/1530`, `Speedup 1.50x`
- `id=500000` -> `ID 970.800/976`,  `StudentNo 1494.600/1513`, `Speedup 1.54x`
- `id=1000000`-> `ID 991.000/1016`, `StudentNo 1534.000/1564`, `Speedup 1.55x`

### 비교 해석 (발표용)
| 타깃 id | B+Tree(`WHERE id = ?`) avg | 선형(`WHERE student_no = ?`) avg | 해석 |
| --- | ---: | ---: | --- |
| 1 | 997.000ms | 1493.200ms | 동일 대상 비교에서 B+Tree가 빠름 |
| 500000 | 970.800ms | 1494.600ms | 중간 위치에서도 B+Tree 우위 유지 |
| 1000000 | 991.000ms | 1534.000ms | 끝 위치에서도 B+Tree 우위 유지 |

### 시각 자료 (텍스트 바 차트)
```text
Demo Targets (lower is better)

id=1       : ID Index 997.000/1055 | StudentNo 1493.200/1530 | 1.50x
id=500000  : ID Index 970.800/976  | StudentNo 1494.600/1513 | 1.54x
id=1000000 : ID Index 991.000/1016 | StudentNo 1534.000/1564 | 1.55x
```

### 발표 멘트
- "동일한 3개 타깃(id 1, 500000, 1000000)을 기준으로 B+Tree(id)와 선형(student_no)을 비교했습니다."
- "조회 대상을 고정해서, 경로 차이에 따른 성능 차이만 비교되도록 설계했습니다."
<!-- - "PowerShell quoting 이슈를 피하기 위해 벤치는 `scripts/bench_docker.ps1` 단일 명령으로 실행합니다." -->

## Q&A 빠른 답변
- 왜 id만 인덱스 최적화했나요?
  - 과제 핵심 요구사항을 우선 충족하기 위해 id equality 경로를 명시적으로 최적화했습니다.
- 벤치와 테스트 실행 기준은 무엇인가요?
  - 본 프로젝트는 Docker 실행 경로를 기준으로 동일하게 재현합니다.
- 마이그레이션 실패 시 데이터는?
  - 기존 텍스트 파일을 백업으로 남기고 전환합니다.
