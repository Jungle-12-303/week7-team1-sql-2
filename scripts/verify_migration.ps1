$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$dbRoot = Join-Path $root "tests\tmp\migration_verify_db"
$sqlDir = Join-Path $root "tests\tmp\migration_verify_sql"

& (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir

if (Test-Path $dbRoot) {
    Remove-Item -Recurse -Force $dbRoot
}
if (Test-Path $sqlDir) {
    Remove-Item -Recurse -Force $sqlDir
}

New-Item -ItemType Directory -Force $dbRoot, $sqlDir, (Join-Path $dbRoot "demo") | Out-Null
Set-Content -NoNewline -Encoding UTF8 (Join-Path $dbRoot "demo\students.schema") "id|name|major"
Set-Content -Encoding UTF8 (Join-Path $dbRoot "demo\students.data") @"
1|LegacyA|DB
2|LegacyB|AI
3|LegacyC|SYS
"@

$lineCountBefore = (Get-Content (Join-Path $dbRoot "demo\students.data")).Count

$sqlPath = Join-Path $sqlDir "check.sql"
Set-Content -NoNewline -Encoding UTF8 $sqlPath "SELECT * FROM demo.students;"

$output = & (Join-Path $buildDir "mini_sql.exe") $dbRoot $sqlPath
$outputText = ($output | Out-String)

if ($outputText -notmatch "\(3 rows\)") {
    throw "Expected 3 rows after migration, but output was: $outputText"
}

$file = [System.IO.File]::OpenRead((Join-Path $dbRoot "demo\students.data"))
try {
    $firstByte = $file.ReadByte()
} finally {
    $file.Dispose()
}

if ($firstByte -eq [byte][char]'1') {
    throw "Migration did not switch to binary format."
}

$bakPath = Join-Path $dbRoot "demo\students.data.text.bak"
if (-not (Test-Path $bakPath)) {
    throw "Expected text backup file not found: $bakPath"
}

$lineCountAfterBak = (Get-Content $bakPath).Count
if ($lineCountBefore -ne $lineCountAfterBak) {
    throw "Backup row count mismatch: before=$lineCountBefore, backup=$lineCountAfterBak"
}

Write-Host "Migration verification passed."
