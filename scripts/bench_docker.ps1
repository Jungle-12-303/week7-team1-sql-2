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

for i in `$(seq 1 $Rows); do
  printf "INSERT INTO demo.students (name, major, grade) VALUES ('U%s', 'M%s', 'A');\n" "`$i" "`$((i%10))"
done > /tmp/sql/insert.sql

t0=`$(date +%s)
/app/build/mini_sql /tmp/bench /tmp/sql/insert.sql >/tmp/out_insert.txt
t1=`$(date +%s)

echo "SELECT name FROM demo.students WHERE id = $TargetId;" > /tmp/sql/q_id.sql
echo "SELECT name FROM demo.students WHERE major = 'M5';" > /tmp/sql/q_lin.sql

t2=`$(date +%s)
/app/build/mini_sql /tmp/bench /tmp/sql/q_id.sql >/tmp/out_id.txt
t3=`$(date +%s)

t4=`$(date +%s)
/app/build/mini_sql /tmp/bench /tmp/sql/q_lin.sql >/tmp/out_lin.txt
t5=`$(date +%s)

echo "insert_total_sec=`$((t1-t0))"
echo "id_query_sec=`$((t3-t2))"
echo "linear_query_sec=`$((t5-t4))"
echo "--- id result ---"
tail -n 5 /tmp/out_id.txt
echo "--- linear result ---"
tail -n 5 /tmp/out_lin.txt
"@

[System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

$wd = (Get-Location).Path
docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m.sh"
