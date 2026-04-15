# 데모 명령어 치트시트 (현장 참고용)

## 0) 프로젝트 루트 이동
```bash
cd Week7
```

## 1) 이미지 빌드 (내부에서 make + make test 수행)
```bash
docker build -t week7-mini-sql .
```

## 2) 기본 데모 실행 (INSERT/SELECT 출력 확인)
```bash
docker run --rm week7-mini-sql
```

## 3) 범위 조회 데모 실행 (CLI)
```bash
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_range_workflow.sql"
```

## 4) 마이그레이션 증거 확인 (binary + text backup)
```bash
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_workflow.sql && \
 ls -l /tmp/demo/db/demo && \
 od -An -tx1 -N 32 /tmp/demo/db/demo/students.data && \
 echo '--- backup ---' && cat /tmp/demo/db/demo/students.data.text.bak"
```

## 5) 100만 건 벤치마크 (PowerShell/Docker)
```powershell
.\scripts\bench_docker.ps1
```

옵션 사용 예시:
```powershell
.\scripts\bench_docker.ps1 -Rows 1000000 -Runs 5 -Image week7-mini-sql
```

SQL 오타/예외 처리 데모:
```powershell
.\scripts\typo_demo_docker.ps1
```

## 실행 결과에서 볼 포인트
- 2번: `INSERT 1` 출력, `WHERE id = 2` 결과 1건 확인
- 3번: `WHERE id >=`, `<=`, `>`, `<` 범위 조회 결과 확인
- 4번: `students.data`(바이너리), `students.data.text.bak`(텍스트 백업) 확인
- 5번: 벤치 표에서 `Target ID`가 `1`, `500000`, `1000000`으로 3줄 출력되는지 확인
- 5번: 각 줄의 `ID Index avg/p95` vs `StudentNo avg/p95` 비교
- 5번: `Target Name`이 `U1`, `U500000`, `U1000000`으로 정확히 출력되는지 확인

참고:
- PowerShell에서 `docker ... -lc "긴 문자열"` 형태는 quoting 깨짐이 날 수 있으므로
  가능하면 `scripts/bench_docker.ps1` 사용을 권장합니다.

---

## mini_sql 실행 방법 (빠른 참고)

```powershell
# 1) 이미지 빌드
docker build -t week7-mini-sql .

# 2) 기본 데모 SQL 실행(컨테이너 기본 CMD)
docker run --rm week7-mini-sql

# 3) 파일 모드로 직접 실행
docker run --rm week7-mini-sql /app/examples/db /app/examples/sql/demo_workflow.sql

# 4) 옵션 모드로 실행(short/long option)
docker run --rm week7-mini-sql -d /app/examples/db -f /app/examples/sql/demo_workflow.sql
docker run --rm week7-mini-sql --db /app/examples/db --file /app/examples/sql/demo_workflow.sql

# 5) 인터랙티브 모드 실행
docker run -it --rm week7-mini-sql -d /app/examples/db -i
```

```sql
-- 인터랙티브 모드 안에서 입력
SELECT * FROM demo.students;
```

주의:
- PowerShell 프롬프트(`PS ...>`)에 SQL을 직접 입력하면 `Select-Object`로 해석되어 실패할 수 있습니다.
- SQL은 반드시 `mini_sql` 인터랙티브 안에서 입력하거나, SQL 파일/파이프 입력으로 전달하세요.
