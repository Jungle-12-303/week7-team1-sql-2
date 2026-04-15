param(
    [int]$Rows = 1000000,
    [int]$TargetId = 777777,
    [string]$Image = "week7-mini-sql"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp"
$shPath = Join-Path $tmpDir "bench_1m.sh"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force $tmpDir | Out-Null

$script = @"
set -e
mkdir -p /tmp/bench/demo /tmp/sql
echo "id|name|major|grade" > /tmp/bench/demo/students.schema
: > /tmp/bench/demo/students.data

now_ms() {
  date +%s%3N
}

for i in `$(seq 1 $Rows); do
  printf "INSERT INTO demo.students (name, major, grade) VALUES ('U%s', 'M%s', 'A');\n" "`$i" "`$((i%10))"
done > /tmp/sql/insert.sql

t0=`$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt
t1=`$(now_ms)

echo "SELECT name FROM demo.students WHERE id = $TargetId;" > /tmp/sql/q_id.sql
echo "SELECT name FROM demo.students WHERE major = 'M5';" > /tmp/sql/q_lin.sql

t2=`$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt
t3=`$(now_ms)

t4=`$(now_ms)
/app/build/mini_sql /tmp/bench /tmp/sql/q_lin.sql >/tmp/out_lin.txt
t5=`$(now_ms)

echo "insert_total_ms=`$((t1-t0))"
echo "id_query_ms=`$((t3-t2))"
echo "linear_query_ms=`$((t5-t4))"
echo "case_a_path=B+TREE_ID_INDEX"
echo "case_b_path=LINEAR_SCAN_MAJOR"
echo "--- case A result (B+ tree id index) ---"
tail -n 5 /tmp/out_id.txt
echo "--- case B result (linear scan) ---"
tail -n 5 /tmp/out_lin.txt
"@

[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m.sh"
