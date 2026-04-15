# 데모 명령어 치트시트 (현장 참고용)

```bash
# 0) 프로젝트 루트로 이동
cd Week7

# 1) 이미지 빌드 (내부 make/test 포함)
docker build -t week7-mini-sql .

# 2) 기본 데모 실행 (INSERT/SELECT 출력 확인)
docker run --rm week7-mini-sql

# 3) 마이그레이션 증거 확인 (binary + text backup)
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_workflow.sql && \
 ls -l /tmp/demo/db/demo && \
 od -An -tx1 -N 32 /tmp/demo/db/demo/students.data && \
 echo '--- backup ---' && cat /tmp/demo/db/demo/students.data.text.bak"
```

```powershell
# 4) 100만 건 벤치마크 (PowerShell/Docker)
.\scripts\bench_docker.ps1
```

```powershell
# 옵션 사용 예시
.\scripts\bench_docker.ps1 -Rows 1000000 -TargetId 777777 -Image week7-mini-sql
```

```text
# 실제 실행 예시(2026-04-15)
insert_total_sec=18
id_query_sec=1
linear_query_sec=2
--- id result ---
| name    |
+---------+
| U777777 |
+---------+
(1 rows)
--- linear result ---
...
(100000 rows)
```

- 2번에서 볼 것: `INSERT 1` 2회, `WHERE id = 2` 결과 1건
- 3번에서 볼 것: `students.data`(바이너리), `students.data.text.bak`(텍스트 백업)
- 4번에서 볼 것: `insert_total_sec`, `id_query_sec`, `linear_query_sec` 비교
- 참고: PowerShell에서 `docker ... -lc "긴 문자열"` 형태는 quoting 깨짐으로 실패할 수 있으므로 `scripts/bench_docker.ps1`를 사용
