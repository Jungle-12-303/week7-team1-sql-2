param(
    [int]$Rows = 1000000,
    [int]$Runs = 5,
    [int]$QueryRepeats = 30,
    [string]$Image = "week7-mini-sql",
    [switch]$PrepareOnly
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp"
$shPath = Join-Path $tmpDir "bench_1m_demo.sh"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force $tmpDir | Out-Null

$scriptTemplate = @'
set -euo pipefail

ROWS=__ROWS__
RUNS=__RUNS__
QUERY_REPEATS=__QUERY_REPEATS__
PREPARE_ONLY=__PREPARE_ONLY__

WORK_ROOT=/work/tests/tmp
CACHE_DIR="$WORK_ROOT/bench_cache/$ROWS"
BENCH_DIR=/tmp/bench

mkdir -p "$BENCH_DIR/demo" /tmp/sql

now_ms() {
  date +%s%3N
}

avg_ms() {
  awk '{sum+=$1; n+=1} END { if (n==0) print "0.000"; else printf "%.3f", sum/n }' "$1"
}

p95_ms() {
  file="$1"
  n=$(wc -l < "$file")
  if [ "$n" -le 0 ]; then
    echo "0.000"
    return
  fi
  idx=$(( (n * 95 + 99) / 100 ))
  sort -n "$file" | sed -n "${idx}p"
}

emit_repeated_query_file() {
  out_file="$1"
  query="$2"
  repeats="$3"
  : > "$out_file"
  for _ in $(seq 1 "$repeats"); do
    printf "%s\n" "$query" >> "$out_file"
  done
}

for i in $(seq 1 "$ROWS"); do
  student_no=$((2026000000 + i))
  grade=$(( (i % 4) + 1 ))
  printf "INSERT INTO demo.students (student_no, name, major, grade) VALUES ('%s', 'U%s', 'M%s', '%s');\n" "$student_no" "$i" "$((i%10))" "$grade"
done > /tmp/sql/insert.sql

prepare_mode="cache_reused"
insert_ms=0

if [ -f "$CACHE_DIR/demo/students.schema" ] && [ -f "$CACHE_DIR/demo/students.data" ]; then
  cp "$CACHE_DIR/demo/students.schema" "$BENCH_DIR/demo/students.schema"
  cp "$CACHE_DIR/demo/students.data" "$BENCH_DIR/demo/students.data"
else
  prepare_mode="fresh_insert"
  echo "id|student_no|name|major|grade" > "$BENCH_DIR/demo/students.schema"
  : > "$BENCH_DIR/demo/students.data"
  t0=$(now_ms)
  /app/build/mini_sql "$BENCH_DIR" /tmp/sql/insert.sql >/tmp/out_insert.txt
  t1=$(now_ms)
  insert_ms=$((t1-t0))
  mkdir -p "$CACHE_DIR/demo"
  cp "$BENCH_DIR/demo/students.schema" "$CACHE_DIR/demo/students.schema"
  cp "$BENCH_DIR/demo/students.data" "$CACHE_DIR/demo/students.data"
fi

if [ "$PREPARE_ONLY" -eq 1 ]; then
  echo "Prepare completed"
  echo "rows=$ROWS"
  echo "prepare_mode=$prepare_mode"
  echo "insert_total_ms=$insert_ms"
  echo "cache_dir=$CACHE_DIR"
  exit 0
fi

echo
echo "============================================================"
echo " Mini SQL Benchmark Demo (Docker)"
echo "============================================================"
echo "Rows: $ROWS / Runs(batch groups) per case: $RUNS / Query repeats per group: $QUERY_REPEATS"
echo "Insert Total: ${insert_ms} ms"
echo "Dataset Source: ${prepare_mode} (cache: $CACHE_DIR)"
echo
printf "%-10s | %-14s | %-19s | %-19s | %-8s\n" "Target ID" "Target Name" "ID Index avg/p95" "StudentNo avg/p95" "Speedup"
echo "-----------+----------------+---------------------+---------------------+---------"

for target in 1 500000 1000000; do
  if [ "$target" -gt "$ROWS" ]; then
    printf "%-10s | %-14s | %-19s | %-19s | %-8s\n" "$target" "(not loaded)" "-" "-" "-"
    continue
  fi

  target_student_no=$((2026000000 + target))
  target_name="U${target}"
  case_queries=$((RUNS * QUERY_REPEATS))

  emit_repeated_query_file /tmp/sql/q_id.sql "SELECT name FROM demo.students WHERE id = ${target};" "$case_queries"
  emit_repeated_query_file /tmp/sql/q_student.sql "SELECT name FROM demo.students WHERE student_no = '${target_student_no}';" "$case_queries"

  t2=$(now_ms)
  /app/build/mini_sql "$BENCH_DIR" /tmp/sql/q_id.sql >/tmp/out_id.txt
  t3=$(now_ms)
  id_total_ms=$((t3-t2))

  t4=$(now_ms)
  /app/build/mini_sql "$BENCH_DIR" /tmp/sql/q_student.sql >/tmp/out_student.txt
  t5=$(now_ms)
  student_total_ms=$((t5-t4))

  grep -q "$target_name" /tmp/out_id.txt
  grep -q "$target_name" /tmp/out_student.txt

  id_avg=$(awk -v total_ms="$id_total_ms" -v repeats="$case_queries" 'BEGIN { if (repeats<=0) print "0.000"; else printf "%.3f", total_ms / repeats }')
  student_avg=$(awk -v total_ms="$student_total_ms" -v repeats="$case_queries" 'BEGIN { if (repeats<=0) print "0.000"; else printf "%.3f", total_ms / repeats }')
  id_p95="n/a"
  student_p95="n/a"
  speedup=$(awk -v a="$id_avg" -v b="$student_avg" 'BEGIN { if (a <= 0.0001) print "0.00x"; else printf "%.2fx", b/a }')

  id_cell="${id_avg}/${id_p95}"
  student_cell="${student_avg}/${student_p95}"
  printf "%-10s | %-14s | %-19s | %-19s | %-8s\n" "$target" "$target_name" "$id_cell" "$student_cell" "$speedup"
done

echo
echo "Legend:"
echo "- ID Index: WHERE id = ? (B+Tree), per-query avg in one process"
echo "- StudentNo: WHERE student_no = ? (Linear Scan), per-query avg in one process"
echo "- Speedup: StudentNo avg / ID avg"
echo "- Measurement: each case runs one mini_sql process; repeated count = RUNS x QUERY_REPEATS"
echo "============================================================"
'@

$script = $scriptTemplate.
    Replace("__ROWS__", $Rows.ToString()).
    Replace("__RUNS__", $Runs.ToString()).
    Replace("__QUERY_REPEATS__", $QueryRepeats.ToString()).
    Replace("__PREPARE_ONLY__", $(if ($PrepareOnly) { "1" } else { "0" }))
# Force LF line endings for bash script to avoid `set: pipefail` errors from CRLF.
$script = $script.Replace("`r`n", "`n")
[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m_demo.sh"
