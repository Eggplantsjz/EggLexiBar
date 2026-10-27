param([Parameter(Mandatory=$true)][string]$Path)

Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null

[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null

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

$sw = [Diagnostics.Stopwatch]::StartNew()
$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Path)) ([Windows.Storage.StorageFile])
$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$loadMs = $sw.ElapsedMilliseconds

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if (-not $engine) {
    $lang = New-Object Windows.Globalization.Language 'zh-Hans-CN'
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
}
"engine language: $($engine.RecognizerLanguage.LanguageTag)"

$sw2 = [Diagnostics.Stopwatch]::StartNew()
$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
$sw2.Stop()
"load: $loadMs ms   ocr: $($sw2.ElapsedMilliseconds) ms   lines: $($result.Lines.Count)"
"--- first 15 lines ---"
$result.Lines | Select-Object -First 15 | ForEach-Object { $_.Text }
"--- words sample ---"
$words = @()
foreach ($line in $result.Lines) { foreach ($w in $line.Words) { $words += $w } }
"word count: $($words.Count)"
$words | Select-Object -First 20 | ForEach-Object { "'$($_.Text)' [$($_.BoundingRect.X),$($_.BoundingRect.Y) $($_.BoundingRect.Width)x$($_.BoundingRect.Height)]" }
