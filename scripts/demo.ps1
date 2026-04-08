$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$demoDb = Join-Path $root "tests\tmp\demo_db"
$sourceDb = Join-Path $root "examples\db"
$sqlFile = Join-Path $root "examples\sql\demo_workflow.sql"

# 먼저 현재 소스로 바이너리를 다시 빌드해 데모와 코드 상태를 맞춘다.
& (Join-Path $PSScriptRoot "build.ps1") -OutputDir $buildDir

if (Test-Path $demoDb) {
    # 이전 실행 흔적이 남아 있으면 결과가 섞이므로 임시 DB를 비운다.
    Remove-Item -Recurse -Force $demoDb
}

New-Item -ItemType Directory -Force $demoDb | Out-Null

# 예제 DB를 복사해 원본 샘플 데이터는 건드리지 않고 데모를 실행한다.
Copy-Item -Path (Join-Path $sourceDb "*") -Destination $demoDb -Recurse -Force

# 준비한 임시 DB에 예제 SQL 스크립트를 흘려보내 실제 실행 모습을 확인한다.
& (Join-Path $buildDir "mini_sql.exe") $demoDb $sqlFile
