param(
    [string]$Image = "week7-mini-sql"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $root "tests\tmp\typo_demo"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:WorkDbRel = "tests/tmp/typo_demo/db"

if (Test-Path $tmpDir) {
    Remove-Item -Recurse -Force $tmpDir
}
New-Item -ItemType Directory -Force $tmpDir | Out-Null

function Write-Utf8NoBom([string]$path, [string]$content) {
    [System.IO.File]::WriteAllText($path, $content, $utf8NoBom)
}

function Invoke-DockerMiniSql([string]$dbRel, [string]$sqlRel) {
    $wd = (Get-Location).Path

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "docker"
    $psi.Arguments = "run --rm -v ""${wd}:/work"" --entrypoint /bin/bash $Image -lc ""/app/build/mini_sql /work/$dbRel /work/$sqlRel"""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()

    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()

    return [PSCustomObject]@{
        ExitCode = $proc.ExitCode
        StdOut = $stdout
        StdErr = $stderr
    }
}

function Invoke-Case([string]$title, [string]$sqlPath, [string]$expected) {
    Write-Host ""
    Write-Host "=== $title ==="
    Write-Host "Expected: $expected"

    $result = Invoke-DockerMiniSql $script:WorkDbRel $sqlPath

    if ($result.StdOut) {
        $result.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host $_.TrimEnd("`r") }
    }
    if ($result.StdErr) {
        $result.StdErr.TrimEnd().Split("`n") | ForEach-Object { Write-Host $_.TrimEnd("`r") }
    }
    Write-Host "ExitCode: $($result.ExitCode)"
}

$schemaRel = "tests/tmp/typo_demo/db/demo/students.schema"
$dataRel = "tests/tmp/typo_demo/db/demo/students.data"
$seedRel = "tests/tmp/typo_demo/seed.sql"
$okSqlRel = "tests/tmp/typo_demo/ok.sql"
$parseErrRel = "tests/tmp/typo_demo/typo_parse.sql"
$colErrRel = "tests/tmp/typo_demo/typo_column.sql"
$unsupportedRel = "tests/tmp/typo_demo/typo_unsupported.sql"
$idTypeRel = "tests/tmp/typo_demo/typo_id_value.sql"

New-Item -ItemType Directory -Force (Join-Path $root "tests\tmp\typo_demo\db\demo") | Out-Null
Write-Utf8NoBom (Join-Path $root $schemaRel) "id|name|major|grade"
Write-Utf8NoBom (Join-Path $root $dataRel) ""

Write-Utf8NoBom (Join-Path $root $seedRel) @"
INSERT INTO demo.students (name, major, grade) VALUES ('Bob', 'AI', 'B');
INSERT INTO demo.students (name, major, grade) VALUES ('Choi', 'Data', 'A');
"@

Write-Utf8NoBom (Join-Path $root $okSqlRel) @"
SELECT name FROM demo.students WHERE id = 2;
"@

Write-Utf8NoBom (Join-Path $root $parseErrRel) @"
SELEC * FROM demo.students;
"@

Write-Utf8NoBom (Join-Path $root $colErrRel) @"
SELECT * FROM demo.students WHERE majro = 'AI';
"@

Write-Utf8NoBom (Join-Path $root $unsupportedRel) @"
UPDATE demo.students SET major='AI' WHERE id=1;
"@

Write-Utf8NoBom (Join-Path $root $idTypeRel) @"
SELECT name FROM demo.students WHERE id = 'abc';
"@

# Seed data so the normal id query has a deterministic hit.
$seedResult = Invoke-DockerMiniSql $script:WorkDbRel $seedRel
if ($seedResult.ExitCode -ne 0) {
    if ($seedResult.StdOut) { Write-Host $seedResult.StdOut.TrimEnd() }
    if ($seedResult.StdErr) { Write-Host $seedResult.StdErr.TrimEnd() }
    throw "Failed to seed typo demo database."
}

Invoke-Case "Case 0: Normal id query" $okSqlRel "1 row result"
Invoke-Case "Case 1: Typo in keyword" $parseErrRel "parse error"
Invoke-Case "Case 2: Typo in column" $colErrRel "execution error (unknown column)"
Invoke-Case "Case 3: Unsupported statement" $unsupportedRel "parse error (unsupported statement)"
Invoke-Case "Case 4: Invalid id literal type" $idTypeRel "0 rows"
