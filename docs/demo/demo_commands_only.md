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

```bash
# 4) 100만 건 벤치마크 (Docker)
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"set -e; \
 mkdir -p /tmp/bench/demo /tmp/sql; \
 printf 'id|name|major' > /tmp/bench/demo/students.schema; \
 : > /tmp/bench/demo/students.data; \
 seq 1 1000000 | awk '{printf \"INSERT INTO demo.students (name, major) VALUES (\\047U%s\\047, \\047M%s\\047);\\n\", $1, $1%10}' > /tmp/sql/insert.sql; \
 t0=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt; t1=$(date +%s); \
 echo \"SELECT name FROM demo.students WHERE id = 777777;\" > /tmp/sql/q_id.sql; \
 echo \"SELECT name FROM demo.students WHERE major = 'M5';\" > /tmp/sql/q_lin.sql; \
 t2=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt; t3=$(date +%s); \
 t4=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/q_lin.sql >/tmp/out_lin.txt; t5=$(date +%s); \
 echo \"insert_total_sec=$((t1-t0))\"; \
 echo \"id_query_sec=$((t3-t2))\"; \
 echo \"linear_query_sec=$((t5-t4))\"; \
 tail -n 2 /tmp/out_id.txt; \
 tail -n 2 /tmp/out_lin.txt"
```

```powershell
# 4) 100만 건 벤치마크 (Docker, PowerShell에서 실행할 때)
$cmd = @'
set -e
mkdir -p /tmp/bench/demo /tmp/sql
printf 'id|name|major' > /tmp/bench/demo/students.schema
: > /tmp/bench/demo/students.data
seq 1 1000000 | awk '{printf "INSERT INTO demo.students (name, major) VALUES (\047U%s\047, \047M%s\047);\n", $1, $1%10}' > /tmp/sql/insert.sql
t0=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt; t1=$(date +%s)
echo "SELECT name FROM demo.students WHERE id = 777777;" > /tmp/sql/q_id.sql
echo "SELECT name FROM demo.students WHERE major = 'M5';" > /tmp/sql/q_lin.sql
t2=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt; t3=$(date +%s)
t4=$(date +%s); /app/build/mini_sql /tmp/bench /tmp/sql/q_lin.sql >/tmp/out_lin.txt; t5=$(date +%s)
echo "insert_total_sec=$((t1-t0))"
echo "id_query_sec=$((t3-t2))"
echo "linear_query_sec=$((t5-t4))"
tail -n 2 /tmp/out_id.txt
tail -n 2 /tmp/out_lin.txt
'@

docker run --rm --entrypoint /bin/bash week7-mini-sql -lc "$cmd"
```

- 2번에서 볼 것: `INSERT 1` 2회, `WHERE id = 2` 결과 1건
- 3번에서 볼 것: `students.data`(바이너리), `students.data.text.bak`(텍스트 백업)
- 4번에서 볼 것: `insert_total_sec`, `id_query_sec`, `linear_query_sec` 비교
