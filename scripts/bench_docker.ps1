param(
    [ValidateSet("prepare", "query")]
    [string]$Mode = "query",
    [int]$Rows = 1000000,
    [int]$Runs = 5,
    [int]$QueryRepeats = 30,
    [switch]$ForceRebuild,
    [string]$Image = "week7-mini-sql"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp"
$cacheRoot = Join-Path $tmpDir "bench_cache\$Rows\demo"
$schemaPath = Join-Path $cacheRoot "students.schema"
$dataPath = Join-Path $cacheRoot "students.data"
$shPath = Join-Path $tmpDir "bench_1m_demo.sh"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force $tmpDir | Out-Null

function New-BenchmarkDatasetBinary {
    param(
        [string]$SchemaPath,
        [string]$DataPath,
        [int]$Rows
    )

    New-Item -ItemType Directory -Force (Split-Path -Parent $SchemaPath) | Out-Null
    [System.IO.File]::WriteAllText($SchemaPath, "id|student_no|name", $utf8NoBom)

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $fs = [System.IO.File]::Open($DataPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $buffered = New-Object System.IO.BufferedStream($fs, 1048576)
        $writer = New-Object System.IO.BinaryWriter($buffered, [System.Text.Encoding]::UTF8)
        try {
            for ($i = 1; $i -le $Rows; $i++) {
                $id = $i.ToString()
                $studentNo = (2026000000 + $i).ToString()
                $name = "U$i"

                $idBytes = [System.Text.Encoding]::UTF8.GetBytes($id)
                $studentNoBytes = [System.Text.Encoding]::UTF8.GetBytes($studentNo)
                $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($name)

                $writer.Write([uint32]3)

                $writer.Write([uint32]$idBytes.Length)
                $writer.Write($idBytes)

                $writer.Write([uint32]$studentNoBytes.Length)
                $writer.Write($studentNoBytes)

                $writer.Write([uint32]$nameBytes.Length)
                $writer.Write($nameBytes)
            }
            $writer.Flush()
            $buffered.Flush()
        } finally {
            $writer.Dispose()
            $buffered.Dispose()
        }
    } finally {
        $fs.Dispose()
    }

    $sw.Stop()
    $size = (Get-Item $DataPath).Length
    Write-Host "Prepare completed"
    Write-Host ("rows={0}" -f $Rows)
    Write-Host ("prepare_mode=binary_writer")
    Write-Host ("prepare_total_ms={0}" -f [int][Math]::Round($sw.Elapsed.TotalMilliseconds))
    Write-Host ("cache_dir={0}" -f (Split-Path -Parent $SchemaPath))
    Write-Host ("data_size_bytes={0}" -f $size)
}

function Invoke-QueryBenchmarkDocker {
    param(
        [int]$Rows,
        [int]$Runs,
        [int]$QueryRepeats,
        [string]$Image
    )

    $scriptTemplate = @'
set -euo pipefail

ROWS=__ROWS__
RUNS=__RUNS__
QUERY_REPEATS=__QUERY_REPEATS__

WORK_ROOT=/work/tests/tmp
CACHE_DIR="$WORK_ROOT/bench_cache/$ROWS"
BENCH_DIR=/tmp/bench

mkdir -p "$BENCH_DIR/demo" /tmp/sql

if [ ! -f "$CACHE_DIR/demo/students.schema" ] || [ ! -f "$CACHE_DIR/demo/students.data" ]; then
  echo "Cache not found: $CACHE_DIR/demo"
  echo "Run prepare first: .\scripts\bench_docker.ps1 -Mode prepare -Rows $ROWS"
  exit 1
fi

cp "$CACHE_DIR/demo/students.schema" "$BENCH_DIR/demo/students.schema"
cp "$CACHE_DIR/demo/students.data" "$BENCH_DIR/demo/students.data"

now_ms() {
  date +%s%3N
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

prepare_mode="cache_reused"
insert_ms=0

echo
echo "============================================================"
echo " Mini SQL Benchmark Demo (Docker)"
echo "============================================================"
echo "Rows: $ROWS / Runs(batch groups) per case: $RUNS / Query repeats per group: $QUERY_REPEATS"
echo "Insert Total: ${insert_ms} ms"
echo "Dataset Source: ${prepare_mode} (cache: $CACHE_DIR)"
echo
printf "%-10s | %-14s | %-13s | %-13s | %-8s\n" "Target ID" "Target Name" "ID Index ms" "StudentNo ms" "Speedup"
echo "-----------+----------------+---------------+---------------+---------"

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
  speedup=$(awk -v a="$id_avg" -v b="$student_avg" 'BEGIN { if (a <= 0.0001) print "0.00x"; else printf "%.2fx", b/a }')

  printf "%-10s | %-14s | %-13s | %-13s | %-8s\n" "$target" "$target_name" "$id_avg" "$student_avg" "$speedup"
done

echo
echo "Legend:"
echo "- ID Index: WHERE id = ? (B+Tree), per-query average ms"
echo "- StudentNo: WHERE student_no = ? (Linear Scan), per-query average ms"
echo "- Speedup: StudentNo ms / ID Index ms"
echo "- Measurement: each case runs one mini_sql process; repeated count = RUNS x QUERY_REPEATS"
echo "============================================================"
'@

    $script = $scriptTemplate.
        Replace("__ROWS__", $Rows.ToString()).
        Replace("__RUNS__", $Runs.ToString()).
        Replace("__QUERY_REPEATS__", $QueryRepeats.ToString())
    $script = $script.Replace("`r`n", "`n")
    [System.IO.File]::WriteAllText($shPath, $script, $utf8NoBom)

    $wd = (Get-Location).Path
    docker run --rm -v "${wd}:/work" --entrypoint /bin/bash $Image -lc "bash /work/tests/tmp/bench_1m_demo.sh"
}

if ($Mode -eq "prepare") {
    if ((-not $ForceRebuild.IsPresent) -and (Test-Path $dataPath) -and (Test-Path $schemaPath) -and ((Get-Item $dataPath).Length -gt 0)) {
        Write-Host "Prepare skipped: cache already exists."
        Write-Host ("rows={0}" -f $Rows)
        Write-Host ("cache_dir={0}" -f $cacheRoot)
        Write-Host ("data_size_bytes={0}" -f (Get-Item $dataPath).Length)
        exit 0
    }

    New-BenchmarkDatasetBinary -SchemaPath $schemaPath -DataPath $dataPath -Rows $Rows
    exit 0
}

Invoke-QueryBenchmarkDocker -Rows $Rows -Runs $Runs -QueryRepeats $QueryRepeats -Image $Image
