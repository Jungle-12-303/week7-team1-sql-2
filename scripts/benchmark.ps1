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
    return ($output | Out-String)
}

Ensure-Build
New-CleanDir $dbRoot
New-CleanDir $sqlDir
New-Item -ItemType Directory -Force (Join-Path $dbRoot "demo") | Out-Null
Write-Utf8NoBom (Join-Path $dbRoot "demo\students.schema") "id|student_no|name|major|grade"
Write-Utf8NoBom (Join-Path $dbRoot "demo\students.data") ""

$insertSql = Join-Path $sqlDir "bulk_insert.sql"
$writer = New-Object System.IO.StreamWriter($insertSql, $false, $utf8NoBom)
for ($i = 1; $i -le $Rows; $i++) {
    $studentNo = (2026000000 + $i).ToString()
    $name = "U$i"
    $major = "M$($i % 10)"
    $grade = (($i % 4) + 1).ToString()
    $writer.WriteLine("INSERT INTO demo.students (student_no, name, major, grade) VALUES ('$studentNo', '$name', '$major', '$grade');")
}
$writer.Flush()
$writer.Close()

$insertMs = Measure-CommandMs { Invoke-MiniSql $dbRoot $insertSql | Out-Null }

# 모든 런에서 동일한 조회 대상을 사용해 비교 신뢰도를 고정한다.
$targetRow = [Math]::Floor($Rows / 2)
if ($targetRow -lt 1) { $targetRow = 1 }
$targetId = $targetRow
$targetStudentNo = (2026000000 + $targetRow).ToString()
$targetName = "U$targetRow"

$idQueryTimes = @()
$studentNoLinearTimes = @()
$nameLinearTimes = @()

for ($r = 1; $r -le $Runs; $r++) {
    $idSql = Join-Path $sqlDir "query_id_$r.sql"
    $studentNoSql = Join-Path $sqlDir "query_student_no_$r.sql"
    $nameSql = Join-Path $sqlDir "query_name_$r.sql"

    Write-Utf8NoBom $idSql "SELECT name FROM demo.students WHERE id = $targetId;"
    Write-Utf8NoBom $studentNoSql "SELECT name FROM demo.students WHERE student_no = '$targetStudentNo';"
    Write-Utf8NoBom $nameSql "SELECT name FROM demo.students WHERE name = '$targetName';"

    $idOutput = $null
    $studentOutput = $null
    $nameOutput = $null

    $idMs = Measure-CommandMs { $idOutput = Invoke-MiniSql $dbRoot $idSql }
    $studentMs = Measure-CommandMs { $studentOutput = Invoke-MiniSql $dbRoot $studentNoSql }
    $nameMs = Measure-CommandMs { $nameOutput = Invoke-MiniSql $dbRoot $nameSql }

    if ($idOutput -notmatch [Regex]::Escape($targetName)) {
        throw "Indexed id query did not return fixed target name: $targetName"
    }
    if ($studentOutput -notmatch [Regex]::Escape($targetName)) {
        throw "Linear student_no query did not return fixed target name: $targetName"
    }
    if ($nameOutput -notmatch [Regex]::Escape($targetName)) {
        throw "Linear name query did not return fixed target name: $targetName"
    }

    $idQueryTimes += [double]$idMs
    $studentNoLinearTimes += [double]$studentMs
    $nameLinearTimes += [double]$nameMs
}

$idStats = Get-Stats $idQueryTimes
$studentStats = Get-Stats $studentNoLinearTimes
$nameStats = Get-Stats $nameLinearTimes

$idAvg = [double]$idStats[0]
$idP95 = [double]$idStats[1]
$studentAvg = [double]$studentStats[0]
$studentP95 = [double]$studentStats[1]
$nameAvg = [double]$nameStats[0]
$nameP95 = [double]$nameStats[1]

$speedupStudent = [Math]::Round(($studentAvg / [Math]::Max($idAvg, 0.001)), 2)
$speedupName = [Math]::Round(($nameAvg / [Math]::Max($idAvg, 0.001)), 2)
$insertMsRounded = [Math]::Round([double]$insertMs, 3)

Write-Host "Benchmark completed"
Write-Host "Rows: $Rows / Runs per query case: $Runs"
Write-Host ("Fixed target: id={0}, student_no={1}, name={2}" -f $targetId, $targetStudentNo, $targetName)
Write-Host ""
Write-Host ("Case A (index, WHERE id = ?): avg={0}ms, p95={1}ms" -f $idAvg, $idP95)
Write-Host ("Case B (linear, WHERE student_no = ?): avg={0}ms, p95={1}ms" -f $studentAvg, $studentP95)
Write-Host ("Case C (linear, WHERE name = ?): avg={0}ms, p95={1}ms" -f $nameAvg, $nameP95)
Write-Host ("Speedup (B/A): {0}x" -f $speedupStudent)
Write-Host ("Speedup (C/A): {0}x" -f $speedupName)
Write-Host ""
Write-Host ("Insert total time (binary, {0} rows): {1}ms" -f $Rows, $insertMsRounded)
Write-Host "Note: student_no query is intentionally measured on linear scan path (non-B+Tree)."
