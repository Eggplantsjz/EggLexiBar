# Runs the headless word-inbox checks (auto-snooze, keep-until-handled, restore).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-session.ps1
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\wltest.exe'
if (-not (Test-Path $exe)) {
    throw "wltest.exe not found - run scripts/build.ps1 first"
}

$env:Path = "D:/Qt/6.10.2/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$env:Path"
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "session checks failed (exit $LASTEXITCODE)"
}
