param(
    [int]$Rows = 1000000,
    [int]$Runs = 50,
    [int]$TargetId = 777777,
    [string]$Image = "week7-mini-sql"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp"
$shPath = Join-Path $tmpDir "bench_1m.sh"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force $tmpDir | Out-Null

$scriptTemplate = @'
set -euo pipefail
mkdir -p /tmp/bench/demo /tmp/sql
echo "id|name|major|grade" > /tmp/bench/demo/students.schema
: > /tmp/bench/demo/students.data

now_ms() {
  date +%s%3N
}

for i in $(seq 1 __ROWS__); do
  printf "INSERT INTO demo.students (name, major, grade) VALUES ('U%s', 'M%s', 'A');\n" "$i" "$((i%10))"
done > /tmp/sql/insert.sql

t0=$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt
t1=$(now_ms)
insert_ms=$((t1-t0))

: > /tmp/sql/q_id_batch.sql
: > /tmp/sql/q_linear_batch.sql
for r in $(seq 1 __RUNS__); do
  id=$(( ((r * 7919) % __ROWS__) + 1 ))
  echo "SELECT name FROM demo.students WHERE id = $id;" >> /tmp/sql/q_id_batch.sql
  echo "SELECT name FROM demo.students WHERE major = 'M5';" >> /tmp/sql/q_linear_batch.sql
done

# 케이스별로 프로세스를 한 번만 실행해 인덱스 재구축 반복을 줄인다.
t2=$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/q_id_batch.sql >/tmp/out_id.txt
t3=$(now_ms)
id_total_ms=$((t3-t2))

t4=$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/q_linear_batch.sql >/tmp/out_lin.txt
t5=$(now_ms)
linear_total_ms=$((t5-t4))

id_avg=$(awk -v t="$id_total_ms" -v r="__RUNS__" 'BEGIN { if (r<=0) print "0.000"; else printf "%.3f", t/r }')
linear_avg=$(awk -v t="$linear_total_ms" -v r="__RUNS__" 'BEGIN { if (r<=0) print "0.000"; else printf "%.3f", t/r }')
speedup=$(awk -v a="$id_avg" -v b="$linear_avg" 'BEGIN { if (a <= 0.0001) print "0.00"; else printf "%.2f", b/a }')

echo "rows=__ROWS__"
echo "runs=__RUNS__"
echo "target_id_example=__TARGET_ID__"
echo "insert_total_ms=$insert_ms"
echo "id_query_total_ms=$id_total_ms"
echo "linear_query_total_ms=$linear_total_ms"
echo "id_query_avg_ms=$id_avg"
echo "linear_query_avg_ms=$linear_avg"
echo "speedup_linear_over_id=$speedup"
echo "case_a_path=B+TREE_ID_INDEX"
echo "case_b_path=LINEAR_SCAN_MAJOR"
echo "--- case A result (B+ tree id index) ---"
tail -n 5 /tmp/out_id.txt
echo "--- case B result (linear scan) ---"
tail -n 5 /tmp/out_lin.txt
'@

$script = $scriptTemplate.Replace("__ROWS__", [string]$Rows).Replace("__RUNS__", [string]$Runs).Replace("__TARGET_ID__", [string]$TargetId)
[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m.sh"
