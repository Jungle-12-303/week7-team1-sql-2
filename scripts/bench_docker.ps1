param(
    [int]$Rows = 1000000,
    [int]$Runs = 5,
    [string]$Image = "week7-mini-sql"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp"
$shPath = Join-Path $tmpDir "bench_1m.sh"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force $tmpDir | Out-Null

$lines = @(
    "set -euo pipefail",
    "mkdir -p /tmp/bench/demo /tmp/sql",
    "echo ""id|student_no|name|major|grade"" > /tmp/bench/demo/students.schema",
    ": > /tmp/bench/demo/students.data",
    "",
    "now_ms() {",
    "  date +%s%3N",
    "}",
    "",
    "avg_ms() {",
    "  awk '{sum+=`$1; n+=1} END { if (n==0) print ""0.000""; else printf ""%.3f"", sum/n }' ""`$1""",
    "}",
    "",
    "p95_ms() {",
    "  file=""`$1""",
    "  n=`$(wc -l < ""`$file"")",
    "  if [ ""`$n"" -le 0 ]; then",
    "    echo ""0.000""",
    "    return",
    "  fi",
    "  idx=`$(( (`$n * 95 + 99) / 100 ))",
    "  sort -n ""`$file"" | sed -n ""`${idx}p""",
    "}",
    "",
    "for i in `$(seq 1 $Rows); do",
    "  student_no=`$((2026000000 + i))",
    "  grade=`$(( (i % 4) + 1 ))",
    "  printf ""INSERT INTO demo.students (student_no, name, major, grade) VALUES ('%s', 'U%s', 'M%s', '%s');\n"" ""`$student_no"" ""`$i"" ""`$((i%10))"" ""`$grade""",
    "done > /tmp/sql/insert.sql",
    "",
    "t0=`$(now_ms)",
    "/app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt",
    "t1=`$(now_ms)",
    "insert_ms=`$((t1-t0))",
    "",
    "target_row=$([Math]::Floor($Rows / 2))",
    "if [ ""`$target_row"" -lt 1 ]; then target_row=1; fi",
    "target_id=""`$target_row""",
    "target_student_no=`$((2026000000 + target_row))",
    "target_name=""U`${target_row}""",
    "",
    ": > /tmp/sql/id_times.txt",
    ": > /tmp/sql/student_no_times.txt",
    "",
    "for r in `$(seq 1 $Runs); do",
    "  echo ""SELECT name FROM demo.students WHERE id = `${target_id};"" > /tmp/sql/q_id.sql",
    "  echo ""SELECT name FROM demo.students WHERE student_no = '`${target_student_no}';"" > /tmp/sql/q_student.sql",
    "",
    "  t2=`$(now_ms)",
    "  /app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt",
    "  t3=`$(now_ms)",
    "  echo `$((t3-t2)) >> /tmp/sql/id_times.txt",
    "",
    "  t4=`$(now_ms)",
    "  /app/build/mini_sql /tmp/bench /tmp/sql/q_student.sql >/tmp/out_student.txt",
    "  t5=`$(now_ms)",
    "  echo `$((t5-t4)) >> /tmp/sql/student_no_times.txt",
    "",
    "  grep -q ""`${target_name}"" /tmp/out_id.txt",
    "  grep -q ""`${target_name}"" /tmp/out_student.txt",
    "done",
    "",
    "id_avg=`$(avg_ms /tmp/sql/id_times.txt)",
    "id_p95=`$(p95_ms /tmp/sql/id_times.txt)",
    "student_avg=`$(avg_ms /tmp/sql/student_no_times.txt)",
    "student_p95=`$(p95_ms /tmp/sql/student_no_times.txt)",
    "speedup=`$(awk -v a=""`$id_avg"" -v b=""`$student_avg"" 'BEGIN { if (a <= 0.0001) print ""0.00""; else printf ""%.2f"", b/a }')",
    "",
    "echo ""rows=$Rows""",
    "echo ""runs=$Runs""",
    "echo ""target_id=`$target_id""",
    "echo ""target_student_no=`$target_student_no""",
    "echo ""target_name=`$target_name""",
    "echo ""insert_total_ms=`$insert_ms""",
    "echo ""id_query_avg_ms=`$id_avg""",
    "echo ""id_query_p95_ms=`$id_p95""",
    "echo ""student_no_query_avg_ms=`$student_avg""",
    "echo ""student_no_query_p95_ms=`$student_p95""",
    "echo ""speedup_student_no_over_id=`$speedup""",
    "echo ""case_a_path=B+TREE_ID_INDEX""",
    "echo ""case_b_path=LINEAR_SCAN_STUDENT_NO""",
    "echo ""--- case A result (B+ tree id index) ---""",
    "tail -n 5 /tmp/out_id.txt",
    "echo ""--- case B result (linear scan student_no) ---""",
    "tail -n 5 /tmp/out_student.txt"
)

$script = [string]::Join("`n", $lines) + "`n"
[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m.sh"
