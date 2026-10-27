$src = Get-Content -Raw 'D:\code\life\tools\ui-smoke.ps1'
$cut = $src.IndexOf('$bar = Wait-Bar 40')
$head = $src.Substring(0, $cut)
$head += @'

$snap = [W]::Snapshot()
Write-Output ("debug count=" + $snap.Count)
$parsed = @()
foreach ($entry in $snap) { $parsed += Parse $entry }
foreach ($o in $parsed) {
    Write-Output ("debug title=[{0}] x={1} y={2} w={3} h={4}" -f $o.Title, $o.X, $o.Y, $o.W, $o.H)
}
$fb = Find-Bar
Write-Output ("debug Find-Bar: " + $(if ($fb) { "x=$($fb.X) w=$($fb.W)" } else { 'NULL' }))
'@
Invoke-Expression $head
