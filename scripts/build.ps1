param(
    [string]$OutputDir = "build"
)

$ErrorActionPreference = "Stop"

function Get-ZigExe {
    if ($env:ZIG_EXE -and (Test-Path $env:ZIG_EXE)) {
        return $env:ZIG_EXE
    }

    if ($zigCommand = Get-Command zig -ErrorAction SilentlyContinue) {
        if ($zigCommand.Source -and (Test-Path $zigCommand.Source)) {
            return $zigCommand.Source
        }
    }

    if ($zigExeCommand = Get-Command zig.exe -ErrorAction SilentlyContinue) {
        if ($zigExeCommand.Source -and (Test-Path $zigExeCommand.Source)) {
            return $zigExeCommand.Source
        }
    }

# PATH에 없을 때는 winget 설치 경로까지 확인해 마지막으로 한 번 더 찾는다.
    $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe"
    if (Test-Path $wingetRoot) {
        $wingetPath = Get-ChildItem -Path $wingetRoot -Directory -Filter "zig-*" |
            Sort-Object LastWriteTime -Descending |
            ForEach-Object { Join-Path $_.FullName "zig.exe" } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1

        if ($wingetPath) {
            return $wingetPath
        }
    }

    throw "zig.exe not found. Set ZIG_EXE or install Zig."
}

$zig = Get-ZigExe
New-Item -ItemType Directory -Force $OutputDir | Out-Null

# 캐시를 build 폴더 아래로 모아 두면 생성물 관리와 정리가 쉬워진다.
$zigGlobalCacheDir = Join-Path $OutputDir ".zig-global-cache"
$zigLocalCacheDir = Join-Path $OutputDir ".zig-local-cache"
New-Item -ItemType Directory -Force $zigGlobalCacheDir, $zigLocalCacheDir | Out-Null

$env:ZIG_GLOBAL_CACHE_DIR = $zigGlobalCacheDir
$env:ZIG_LOCAL_CACHE_DIR = $zigLocalCacheDir

$sources = @(
    "src/common.c",
    "src/parser.c",
    "src/storage.c",
    "src/executor.c",
    "src/main.c"
)

$testSources = @(
    "src/common.c",
    "src/parser.c",
    "src/storage.c",
    "src/executor.c",
    "tests/test_runner.c"
)

# 사용자 CLI와 테스트 실행 파일을 각각 따로 빌드한다.
& $zig cc -std=c11 -Wall -Wextra -Werror -Iinclude @sources -o "$OutputDir/mini_sql.exe"
& $zig cc -std=c11 -Wall -Wextra -Werror -Iinclude @testSources -o "$OutputDir/test_runner.exe"

Write-Host "Build completed:"
Write-Host "  $OutputDir/mini_sql.exe"
Write-Host "  $OutputDir/test_runner.exe"
