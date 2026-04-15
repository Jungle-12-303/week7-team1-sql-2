param(
    [int]$Rows = 1000000,
    [int]$Runs = 50
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$dbRoot = Join-Path $root "tests\tmp\bench_db"
$sqlDir = Join-Path $root "tests\tmp\bench_sql"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Ensure-Build {
    & (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir
}

function New-CleanDir([string]$path) {
    if (Test-Path $path) {
        Remove-Item -Recurse -Force $path
    }
    New-Item -ItemType Directory -Force $path | Out-Null
}

function Measure-CommandMs([scriptblock]$work) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $work
    $sw.Stop()
    return $sw.Elapsed.TotalMilliseconds
}

function Write-Utf8NoBom([string]$path, [string]$content) {
    [System.IO.File]::WriteAllText($path, $content, $utf8NoBom)
}

function Invoke-MiniSql([string]$db, [string]$sqlPath) {
    $output = & (Join-Path $buildDir "mini_sql.exe") $db $sqlPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "mini_sql failed for '$sqlPath'`n$($output | Out-String)"
    }
    return $output
}

Ensure-Build
New-CleanDir $dbRoot
New-CleanDir $sqlDir
New-Item -ItemType Directory -Force (Join-Path $dbRoot "demo") | Out-Null
Write-Utf8NoBom (Join-Path $dbRoot "demo\students.schema") "id|name|major"
Write-Utf8NoBom (Join-Path $dbRoot "demo\students.data") ""

$insertSql = Join-Path $sqlDir "bulk_insert.sql"
$writer = New-Object System.IO.StreamWriter($insertSql, $false, $utf8NoBom)
for ($i = 1; $i -le $Rows; $i++) {
    $major = "M$($i % 10)"
    $writer.WriteLine("INSERT INTO demo.students (name, major) VALUES ('U$i', '$major');")
}
$writer.Flush()
$writer.Close()

$insertMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $insertSql | Out-Null }

$idBatchSql = Join-Path $sqlDir "query_id_batch.sql"
$linBatchSql = Join-Path $sqlDir "query_lin_batch.sql"
$idWriter = New-Object System.IO.StreamWriter($idBatchSql, $false, $utf8NoBom)
$linWriter = New-Object System.IO.StreamWriter($linBatchSql, $false, $utf8NoBom)
for ($r = 1; $r -le $Runs; $r++) {
    $targetId = ((($r * 7919) % $Rows) + 1)
    $idWriter.WriteLine("SELECT name FROM demo.students WHERE id = $targetId;")
    $linWriter.WriteLine("SELECT name FROM demo.students WHERE major = 'M5';")
}
$idWriter.Flush(); $idWriter.Close()
$linWriter.Flush(); $linWriter.Close()

# 케이스별로 프로세스를 한 번만 실행해 인덱스 재구축 반복을 줄인다.
$idTotalMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $idBatchSql | Out-Null }
$linTotalMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $linBatchSql | Out-Null }

$idAvg = [Math]::Round(($idTotalMs / [Math]::Max($Runs, 1)), 3)
$linAvg = [Math]::Round(($linTotalMs / [Math]::Max($Runs, 1)), 3)
$speedup = [Math]::Round(($linAvg / [Math]::Max($idAvg, 0.001)), 2)

$textInsertPath = Join-Path $sqlDir "text_insert_simulation.txt"
$textMs = Measure-CommandMs {
    $tw = New-Object System.IO.StreamWriter($textInsertPath, $false, [System.Text.Encoding]::UTF8)
    for ($i = 1; $i -le $Rows; $i++) {
        $tw.WriteLine("$i|U$i|M$($i % 10)")
    }
    $tw.Flush()
    $tw.Close()
}

$binaryVsTextGain = [Math]::Round(((($textMs - $insertMs) / [Math]::Max($textMs, 0.001)) * 100), 2)
$textMsRounded = [Math]::Round([double]$textMs, 3)
$insertMsRounded = [Math]::Round([double]$insertMs, 3)

Write-Host "Benchmark completed"
Write-Host "Rows: $Rows / Queries per case: $Runs"
Write-Host ""
Write-Host ("Case A (WHERE id = ? / index path): total={0}ms, avg={1}ms" -f ([Math]::Round($idTotalMs,3)), $idAvg)
Write-Host ("Case B (WHERE major = ? / linear path): total={0}ms, avg={1}ms" -f ([Math]::Round($linTotalMs,3)), $linAvg)
Write-Host "Speedup (B/A): ${speedup}x"
Write-Host ""
Write-Host "Case C (insert total time):"
Write-Host ("  text simulation: {0}ms" -f $textMsRounded)
Write-Host ("  binary engine:   {0}ms" -f $insertMsRounded)
Write-Host ("  improvement:     {0}%" -f $binaryVsTextGain)
