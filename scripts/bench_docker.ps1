param(
    [int]$Rows = 1000000,
    [int]$Runs = 5,
    [string]$Image = "week7-mini-sql"
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

mkdir -p /tmp/bench/demo /tmp/sql
echo "id|student_no|name|major|grade" > /tmp/bench/demo/students.schema
: > /tmp/bench/demo/students.data

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

for i in $(seq 1 "$ROWS"); do
  student_no=$((2026000000 + i))
  grade=$(( (i % 4) + 1 ))
  printf "INSERT INTO demo.students (student_no, name, major, grade) VALUES ('%s', 'U%s', 'M%s', '%s');\n" "$student_no" "$i" "$((i%10))" "$grade"
done > /tmp/sql/insert.sql

t0=$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt
t1=$(now_ms)
insert_ms=$((t1-t0))

echo
echo "============================================================"
echo " Mini SQL Benchmark Demo (Docker)"
echo "============================================================"
echo "Rows: $ROWS / Runs per case: $RUNS"
echo "Insert Total: ${insert_ms} ms"
echo
printf "%-10s | %-14s | %-13s | %-13s | %-8s\n" "Target ID" "Target Name" "ID Index avg/p95" "StudentNo avg/p95" "Speedup"
echo "-----------+----------------+---------------+---------------+---------"

for target in 1 500000 1000000; do
  if [ "$target" -gt "$ROWS" ]; then
    printf "%-10s | %-14s | %-13s | %-13s | %-8s\n" "$target" "(not loaded)" "-" "-" "-"
    continue
  fi

  target_student_no=$((2026000000 + target))
  target_name="U${target}"

  id_times="/tmp/sql/id_${target}.times"
  student_times="/tmp/sql/student_${target}.times"
  : > "$id_times"
  : > "$student_times"

  for r in $(seq 1 "$RUNS"); do
    echo "SELECT name FROM demo.students WHERE id = ${target};" > /tmp/sql/q_id.sql
    echo "SELECT name FROM demo.students WHERE student_no = '${target_student_no}';" > /tmp/sql/q_student.sql

    t2=$(now_ms)
    /app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt
    t3=$(now_ms)
    echo $((t3-t2)) >> "$id_times"

    t4=$(now_ms)
    /app/build/mini_sql /tmp/bench /tmp/sql/q_student.sql >/tmp/out_student.txt
    t5=$(now_ms)
    echo $((t5-t4)) >> "$student_times"

    grep -q "$target_name" /tmp/out_id.txt
    grep -q "$target_name" /tmp/out_student.txt
  done

  id_avg=$(avg_ms "$id_times")
  id_p95=$(p95_ms "$id_times")
  student_avg=$(avg_ms "$student_times")
  student_p95=$(p95_ms "$student_times")
  speedup=$(awk -v a="$id_avg" -v b="$student_avg" 'BEGIN { if (a <= 0.0001) print "0.00x"; else printf "%.2fx", b/a }')

  id_cell="${id_avg}/${id_p95}"
  student_cell="${student_avg}/${student_p95}"
  printf "%-10s | %-14s | %-13s | %-13s | %-8s\n" "$target" "$target_name" "$id_cell" "$student_cell" "$speedup"
done

echo
echo "Legend:"
echo "- ID Index: WHERE id = ? (B+Tree)"
echo "- StudentNo: WHERE student_no = ? (Linear Scan)"
echo "- Speedup: StudentNo avg / ID avg"
echo "============================================================"
'@

$script = $scriptTemplate.Replace("__ROWS__", $Rows.ToString()).Replace("__RUNS__", $Runs.ToString())
[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m_demo.sh"
