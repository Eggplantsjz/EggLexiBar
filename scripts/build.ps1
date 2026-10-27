param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$qtRoot   = 'D:/Qt/6.10.2/mingw_64'
$mingwBin = 'D:/Qt/Tools/mingw1310_64/bin'
$cmakeBin = 'D:/Qt/Tools/CMake_64/bin'
$ninjaBin = 'D:/Qt/Tools/Ninja'

$env:Path = "$mingwBin;$cmakeBin;$ninjaBin;$env:Path"

$sourceDir = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $sourceDir 'build'

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

cmake -S "$sourceDir" -B "$buildDir" -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_PREFIX_PATH=$qtRoot" `
    "-DCMAKE_C_COMPILER=$mingwBin/gcc.exe" `
    "-DCMAKE_CXX_COMPILER=$mingwBin/g++.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Built: $(Join-Path $buildDir 'WordLens.exe')" -ForegroundColor Green
