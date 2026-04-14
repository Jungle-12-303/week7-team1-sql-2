param(
    [int]$Rows = 1000000,
    [int]$Runs = 5
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$dbRoot = Join-Path $root "tests\tmp\bench_db"
$sqlDir = Join-Path $root "tests\tmp\bench_sql"

function Ensure-Build {
    & (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir
}

function New-CleanDir([string]$path) {
    if (Test-Path $path) {
        Remove-Item -Recurse -Force $path
    }
    New-Item -ItemType Directory -Force $path | Out-Null
}

function Get-Stats([double[]]$values) {
    $sorted = $values | Sort-Object
    $avg = ($values | Measure-Object -Average).Average
    $p95Index = [Math]::Ceiling($sorted.Count * 0.95) - 1
    if ($p95Index -lt 0) { $p95Index = 0 }
    return [pscustomobject]@{
        Average = [Math]::Round($avg, 3)
        P95 = [Math]::Round($sorted[$p95Index], 3)
    }
}

function Measure-CommandMs([scriptblock]$work) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $work
    $sw.Stop()
    return $sw.Elapsed.TotalMilliseconds
}

Ensure-Build
New-CleanDir $dbRoot
New-CleanDir $sqlDir
New-Item -ItemType Directory -Force (Join-Path $dbRoot "demo") | Out-Null
Set-Content -NoNewline -Encoding UTF8 (Join-Path $dbRoot "demo\students.schema") "id|name|major"
Set-Content -NoNewline -Encoding UTF8 (Join-Path $dbRoot "demo\students.data") ""

$insertSql = Join-Path $sqlDir "bulk_insert.sql"
$writer = New-Object System.IO.StreamWriter($insertSql, $false, [System.Text.Encoding]::UTF8)
for ($i = 1; $i -le $Rows; $i++) {
    $major = "M$($i % 10)"
    $writer.WriteLine("INSERT INTO demo.students (name, major) VALUES ('U$i', '$major');")
}
$writer.Flush()
$writer.Close()

$insertMs = Measure-CommandMs {
    & (Join-Path $buildDir "mini_sql.exe") $dbRoot $insertSql | Out-Null
}

$idQueryTimes = @()
$linearQueryTimes = @()

for ($r = 1; $r -le $Runs; $r++) {
    $targetId = Get-Random -Minimum 1 -Maximum ($Rows + 1)
    $idSql = Join-Path $sqlDir "query_id_$r.sql"
    $linSql = Join-Path $sqlDir "query_lin_$r.sql"

    Set-Content -NoNewline -Encoding UTF8 $idSql "SELECT name FROM demo.students WHERE id = $targetId;"
    Set-Content -NoNewline -Encoding UTF8 $linSql "SELECT name FROM demo.students WHERE major = 'M5';"

    $idMs = Measure-CommandMs {
        & (Join-Path $buildDir "mini_sql.exe") $dbRoot $idSql | Out-Null
    }
    $linMs = Measure-CommandMs {
        & (Join-Path $buildDir "mini_sql.exe") $dbRoot $linSql | Out-Null
    }

    $idQueryTimes += $idMs
    $linearQueryTimes += $linMs
}

$idStats = Get-Stats $idQueryTimes
$linearStats = Get-Stats $linearQueryTimes
$speedup = [Math]::Round(($linearStats.Average / [Math]::Max($idStats.Average, 0.001)), 2)

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

Write-Host "Benchmark completed"
Write-Host "Rows: $Rows / Runs per query case: $Runs"
Write-Host ""
Write-Host "Case A (WHERE id = ? / index path): avg=${($idStats.Average)}ms, p95=${($idStats.P95)}ms"
Write-Host "Case B (WHERE major = ? / linear path): avg=${($linearStats.Average)}ms, p95=${($linearStats.P95)}ms"
Write-Host "Speedup (B/A): ${speedup}x"
Write-Host ""
Write-Host "Case C (insert total time):"
Write-Host "  text simulation: ${([Math]::Round($textMs,3))}ms"
Write-Host "  binary engine:   ${([Math]::Round($insertMs,3))}ms"
Write-Host "  improvement:     ${binaryVsTextGain}%"
