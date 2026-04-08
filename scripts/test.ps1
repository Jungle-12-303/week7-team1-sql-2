$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$dbRoot = Join-Path $root "tests\tmp\functional_db"
$sqlDir = Join-Path $root "tests\tmp\functional_sql"

# 테스트는 항상 같은 바이너리 기준이어야 하므로 제일 먼저 다시 빌드한다.
& (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir

# C 단위 테스트로 파서/스토리지 핵심 로직부터 먼저 검증한다.
& (Join-Path $buildDir "test_runner.exe")

# 기능 테스트용 임시 DB와 SQL 작업 폴더를 새로 만든다.
New-Item -ItemType Directory -Force $dbRoot, $sqlDir, (Join-Path $dbRoot "demo") | Out-Null
Set-Content -Path (Join-Path $dbRoot "demo\students.schema") -Value "id|name|major" -NoNewline
Set-Content -Path (Join-Path $dbRoot "demo\students.data") -Value "" -NoNewline

# INSERT와 SELECT가 함께 들어간 시나리오를 만들어 CLI 전체 흐름을 검증한다.
@"
INSERT INTO demo.students (id, name, major) VALUES (1, 'Alice', 'DB');
INSERT INTO demo.students (id, name, major) VALUES (2, 'Bob', 'AI');
SELECT * FROM demo.students;
SELECT name FROM demo.students WHERE id = 2;
"@ | Set-Content -Path (Join-Path $sqlDir "workflow.sql") -NoNewline

# 실제 CLI를 돌린 뒤 출력 텍스트를 모아 기대 결과가 나왔는지 확인한다.
$output = & (Join-Path $buildDir "mini_sql.exe") $dbRoot (Join-Path $sqlDir "workflow.sql")
$outputText = ($output | Out-String)

if ($LASTEXITCODE -ne 0) {
    throw "mini_sql execution failed"
}

if ($outputText -notmatch "INSERT 0 1") {
    throw "Expected INSERT output was not found."
}

if ($outputText -notmatch "Alice") {
    throw "Expected SELECT output for Alice was not found."
}

if ($outputText -notmatch "Bob") {
    throw "Expected SELECT output for Bob was not found."
}

if ($outputText -notmatch "\(1 rows\)") {
    throw "Expected filtered SELECT row count was not found."
}

Write-Host "All functional tests passed."
