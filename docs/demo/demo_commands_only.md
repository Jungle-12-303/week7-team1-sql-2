# 데모 명령어 치트시트 (현장 참고용)

```bash
# 0) 프로젝트 루트로 이동
cd Week7

# 1) 이미지 빌드 (내부 make/test 포함)
docker build -t week7-mini-sql .

# 2) 기본 데모 실행 (INSERT/SELECT 출력 확인)
docker run --rm week7-mini-sql

# 3) 범위 조회 데모 실행 (CLI)
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_range_workflow.sql"

# 4) 마이그레이션 증거 확인 (binary + text backup)
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_workflow.sql && \
 ls -l /tmp/demo/db/demo && \
 od -An -tx1 -N 32 /tmp/demo/db/demo/students.data && \
 echo '--- backup ---' && cat /tmp/demo/db/demo/students.data.text.bak"
```

```powershell
# 5) 100만 건 벤치마크 (PowerShell/Docker)
.\scripts\bench_docker.ps1
```

```powershell
# 옵션 사용 예시
.\scripts\bench_docker.ps1 -Rows 1000000 -TargetId 777777 -Image week7-mini-sql
```

```powershell
# SQL 오타/예외 처리 데모
.\scripts\typo_demo_docker.ps1
```

```text
# 실제 실행 예시(2026-04-15)
insert_total_ms=18012
id_query_ms=1042
linear_query_ms=2133
case_a_path=B+TREE_ID_INDEX
case_b_path=LINEAR_SCAN_MAJOR

--- case A result (B+ tree id index) ---
| name    |
+---------+
| U777777 |
+---------+
(1 rows)
--- case B result (linear scan) ---
...
(100000 rows)
```

- 2번에서 볼 것: `INSERT 1` 5회, `WHERE id = 2` 결과 1건, `WHERE id >= 4` 결과 다건
- 3번에서 볼 것: `WHERE id >=`, `<=`, `>`, `<` 범위 조회 결과 테이블
- 4번에서 볼 것: `students.data`(바이너리), `students.data.text.bak`(텍스트 백업)
- 5번에서 볼 것: `insert_total_ms`, `id_query_ms`, `linear_query_ms`와 `case_a_path/case_b_path` 라벨 비교
- 참고: PowerShell에서 `docker ... -lc "긴 문자열"` 형태는 quoting 깨짐으로 실패할 수 있으므로 `scripts/bench_docker.ps1`를 사용
