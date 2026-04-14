param(
    [int]$Rows = 1000000,
    [int]$Runs = 5
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

function Get-Stats([double[]]$values) {
    if ($null -eq $values -or $values.Count -eq 0) {
        return ,@(0.0, 0.0)
    }

    $sorted = $values | Sort-Object
    $sum = 0.0
    foreach ($v in $values) {
        $sum += [double]$v
    }
    $avg = $sum / [double]$values.Count
    $p95Index = [Math]::Ceiling($sorted.Count * 0.95) - 1
    if ($p95Index -lt 0) { $p95Index = 0 }
    return ,@([Math]::Round($avg, 3), [Math]::Round([double]$sorted[$p95Index], 3))
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

$idQueryTimes = @()
$linearQueryTimes = @()

for ($r = 1; $r -le $Runs; $r++) {
    $targetId = Get-Random -Minimum 1 -Maximum ($Rows + 1)
    $idSql = Join-Path $sqlDir "query_id_$r.sql"
    $linSql = Join-Path $sqlDir "query_lin_$r.sql"

    Write-Utf8NoBom $idSql "SELECT name FROM demo.students WHERE id = $targetId;"
    Write-Utf8NoBom $linSql "SELECT name FROM demo.students WHERE major = 'M5';"

    $idMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $idSql | Out-Null }
    $linMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $linSql | Out-Null }

    $idQueryTimes += [double]$idMs
    $linearQueryTimes += [double]$linMs
}

$idStats = Get-Stats $idQueryTimes
$linearStats = Get-Stats $linearQueryTimes
$idAvg = [double]$idStats[0]
$idP95 = [double]$idStats[1]
$linAvg = [double]$linearStats[0]
$linP95 = [double]$linearStats[1]
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
Write-Host "Rows: $Rows / Runs per query case: $Runs"
Write-Host ""
Write-Host ("Case A (WHERE id = ? / index path): avg={0}ms, p95={1}ms" -f $idAvg, $idP95)
Write-Host ("Case B (WHERE major = ? / linear path): avg={0}ms, p95={1}ms" -f $linAvg, $linP95)
Write-Host "Speedup (B/A): ${speedup}x"
Write-Host ""
Write-Host "Case C (insert total time):"
Write-Host ("  text simulation: {0}ms" -f $textMsRounded)
Write-Host ("  binary engine:   {0}ms" -f $insertMsRounded)
Write-Host ("  improvement:     {0}%" -f $binaryVsTextGain)
