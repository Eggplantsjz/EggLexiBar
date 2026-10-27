param(
    [string[]]$ArgumentList = @()
)

$ErrorActionPreference = 'Stop'

$sourceDir = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $sourceDir 'build/WordLens.exe'
if (-not (Test-Path $exe)) {
    throw "WordLens.exe not found, run scripts/build.ps1 first."
}

$env:Path = "D:/Qt/6.10.2/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$env:Path"

$startParams = @{
    FilePath         = $exe
    WorkingDirectory = (Split-Path $exe)
}
if ($ArgumentList -and $ArgumentList.Count -gt 0) {
    $startParams.ArgumentList = $ArgumentList
}
Start-Process @startParams
