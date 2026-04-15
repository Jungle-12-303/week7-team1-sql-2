$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$dbRoot = Join-Path $root "tests\tmp\functional_db"
$sqlDir = Join-Path $root "tests\tmp\functional_sql"

# 테스트는 현재 소스 기준으로 실행해야 하므로 먼저 빌드한다.
& (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir

# C 단위 테스트를 먼저 실행해 파서/저장소 핵심 로직을 확인한다.
& (Join-Path $buildDir "test_runner.exe")

# 기능 테스트용 임시 DB와 SQL 작업 폴더를 준비한다.
New-Item -ItemType Directory -Force $dbRoot, $sqlDir, (Join-Path $dbRoot "demo") | Out-Null
Set-Content -Path (Join-Path $dbRoot "demo\students.schema") -Value "id|student_no|name|major" -NoNewline
Set-Content -Path (Join-Path $dbRoot "demo\students.data") -Value "" -NoNewline

# INSERT와 SELECT가 함께 동작하는 워크플로 SQL을 생성한다.
@"
INSERT INTO demo.students (student_no, name, major) VALUES ('2026000001', 'Alice', 'DB');
INSERT INTO demo.students (student_no, name, major) VALUES ('2026000002', 'Bob', 'AI');
SELECT * FROM demo.students;
SELECT name FROM demo.students WHERE id = 2;
SELECT name FROM demo.students WHERE student_no = '2026000002';
"@ | Set-Content -Path (Join-Path $sqlDir "workflow.sql") -NoNewline

# CLI를 실행하고 출력 텍스트를 모아서 기대값과 비교한다.
$output = & (Join-Path $buildDir "mini_sql.exe") $dbRoot (Join-Path $sqlDir "workflow.sql")
$outputText = ($output | Out-String)

if ($LASTEXITCODE -ne 0) {
    throw "mini_sql execution failed"
}

if ($outputText -notmatch "INSERT 1") {
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

# 중복 학번 INSERT는 실패해야 한다.
@"
INSERT INTO demo.students (student_no, name, major) VALUES ('2026000002', 'Eve', 'Math');
"@ | Set-Content -Path (Join-Path $sqlDir "duplicate_student_no.sql") -NoNewline

$dupOutput = & (Join-Path $buildDir "mini_sql.exe") $dbRoot (Join-Path $sqlDir "duplicate_student_no.sql") 2>&1
if ($LASTEXITCODE -eq 0) {
    throw "Duplicate student_no INSERT should fail, but succeeded."
}

$dupText = ($dupOutput | Out-String)
if ($dupText -notmatch "duplicate student_no") {
    throw "Expected duplicate student_no message was not found."
}

Write-Host "All functional tests passed."
