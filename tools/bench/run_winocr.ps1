# Run the Windows.Media.Ocr variants of the OCR benchmark.
#
# Windows OCR is the OCR engine bundled with Windows 10/11. It runs in-process,
# needs no external executable, and exposes one engine per recogniser language.
# This script prefers en-US and falls back to the language packs that are
# actually installed (a zh-Hans pack also recognises Latin script, but its
# dictionary model is tuned for bilingual text).
#
# Output shape matches run_tesseract.py so score.py can treat every backend the
# same way.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench/run_winocr.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench/run_winocr.ps1 -Reps 3

param(
    [int]$Reps = 2,
    [string]$Out = "",
    [string]$Manifest = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null

[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and
    $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($operation, $resultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($resultType)
    $task = $asTask.Invoke($null, @($operation))
    $null = $task.Wait(-1)
    $task.Result
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Manifest) { $Manifest = Join-Path $root "tools\bench\corpus\manifest.json" }
if (-not $Out) { $Out = Join-Path $root "tools\bench\results\winocr.json" }

$pages = Get-Content $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json

# --- pick a recogniser ------------------------------------------------------
$installed = @([Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages | ForEach-Object { $_.LanguageTag })
Write-Output ("installed recognisers: " + ($installed -join ", "))

$engine = $null
$usedTag = ""
foreach ($tag in @("en-US", "en-GB", "en")) {
    if ($installed -notcontains $tag) { continue }
    $lang = New-Object Windows.Globalization.Language $tag
    $candidate = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
    if ($null -ne $candidate) { $engine = $candidate; $usedTag = $tag; break }
}
if ($null -eq $engine) {
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
    if ($null -ne $engine) { $usedTag = $engine.RecognizerLanguage.LanguageTag }
}
if ($null -eq $engine) { throw "no OCR recogniser is available on this machine" }
if ($usedTag -ne "en-US") {
    Write-Output "WARNING: en-US is not installed; using $usedTag (accuracy is not comparable)"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
$warm = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Join-Path $root $pages[0].png))) ([Windows.Storage.StorageFile])
$warmStream = Await ($warm.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$warmDecoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($warmStream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$warmBitmap = Await ($warmDecoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$null = Await ($engine.RecognizeAsync($warmBitmap)) ([Windows.Media.Ocr.OcrResult])
$warmStream.Dispose()
$warmMs = $sw.Elapsed.TotalMilliseconds
Write-Output ("warm-up: {0:N0} ms" -f $warmMs)

# --- run --------------------------------------------------------------------
$runs = New-Object System.Collections.ArrayList

foreach ($page in $pages) {
    $png = Join-Path $root $page.png

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($png)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $prepMs = $sw.Elapsed.TotalMilliseconds

    for ($rep = 0; $rep -lt $Reps; $rep++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
        $ocrMs = $sw.Elapsed.TotalMilliseconds

        $words = New-Object System.Collections.ArrayList
        $lines = New-Object System.Collections.ArrayList
        foreach ($line in $result.Lines) {
            $null = $lines.Add($line.Text)
            foreach ($word in $line.Words) {
                $rect = $word.BoundingRect
                $null = $words.Add([ordered]@{
                    text = [string]$word.Text
                    conf = 0.0
                    box  = @([int]$rect.X, [int]$rect.Y,
                             [int]($rect.X + $rect.Width), [int]($rect.Y + $rect.Height))
                })
            }
        }

        $null = $runs.Add([ordered]@{
            page     = [string]$page.page
            variant  = "win-" + $usedTag
            scale    = 100
            rep      = $rep
            prep_ms  = [math]::Round($prepMs, 1)
            ocr_ms   = [math]::Round($ocrMs, 1)
            total_ms = [math]::Round($prepMs + $ocrMs, 1)
            tokens   = $words.Count
            words    = $words.ToArray()
            lines    = $lines.ToArray()
        })
        Write-Output ("{0,-9} rep{1} prep={2,6:N0}ms ocr={3,7:N0}ms words={4,4}" -f $page.page, $rep, $prepMs, $ocrMs, $words.Count)
    }

    $stream.Dispose()
    $bitmap.Dispose()
}

$payload = [ordered]@{
    backend = "winocr"
    engine  = "Windows.Media.Ocr " + $usedTag
    warm_ms = [math]::Round($warmMs, 1)
    runs    = $runs.ToArray()
}

$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }
$json = $payload | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($Out, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "wrote $Out"
