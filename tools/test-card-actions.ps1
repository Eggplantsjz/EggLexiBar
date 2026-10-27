# Verifies the hover card on the live overlay bar: finds a chip, opens its
# definition card, moves into the card, and clicks the "copy" button.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-card-actions.ps1
param(
    [string]$OutDir = (Join-Path $env:TEMP 'wordlens-shots')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class WC {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static List<string> Snapshot() {
    var list = new List<string>();
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      var sb = new StringBuilder(512);
      GetWindowText(h, sb, sb.Capacity);
      var t = sb.ToString();
      if (t.StartsWith("WordLens")) {
        RECT r; GetWindowRect(h, out r);
        list.Add(t + "|" + r.Left + "," + r.Top + "," + (r.Right - r.Left) + "," + (r.Bottom - r.Top));
      }
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@
[void][WC]::SetProcessDpiAwarenessContext([IntPtr](-4))

$repo = Split-Path -Parent $PSScriptRoot
$wlocr = Join-Path $repo 'build\wlocr.exe'
$env:Path = "D:/Qt/6.10.2/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$env:Path"

function Windows {
    $out = @()
    foreach ($entry in [WC]::Snapshot()) {
        $p = $entry -split '\|'
        $r = $p[1] -split ','
        $out += [pscustomobject]@{
            Title = $p[0]; X = [int]$r[0]; Y = [int]$r[1]; W = [int]$r[2]; H = [int]$r[3]
        }
    }
    return $out
}
function Bar {
    Windows | Where-Object { $_.Title -eq 'WordLens' -and $_.W -gt 400 -and $_.H -gt 30 -and $_.H -le 90 } |
        Sort-Object W -Descending | Select-Object -First 1
}
function Card { Windows | Where-Object { $_.Title -like 'WordLens*' -and $_.H -gt 100 -and $_.W -gt 250 } | Select-Object -First 1 }
function Click([int]$x, [int]$y) {
    [void][WC]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 150
    [WC]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [WC]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}
function Capture([string]$name) {
    $path = Join-Path $OutDir $name
    & $wlocr --capture $path | Out-Null
}

$bar = Bar
if (-not $bar) { throw 'bar not found' }
Write-Output ("bar: {0},{1} {2}x{3}" -f $bar.X, $bar.Y, $bar.W, $bar.H)

# Probe horizontally until a chip opens the card.
$found = $false
for ($x = $bar.X + 330; $x -lt ($bar.X + $bar.W - 150); $x += 34) {
    [void][WC]::SetCursorPos($x, $bar.Y + [int]($bar.H / 2))
    Start-Sleep -Milliseconds 480
    if (Card) { $found = $true; Write-Output ("chip hovering at x={0}" -f $x); break }
}
if (-not $found) { throw 'no definition card appeared while probing the bar' }
Start-Sleep -Milliseconds 300
$card = Card
Write-Output ("card: {0},{1} {2}x{3}" -f $card.X, $card.Y, $card.W, $card.H)
Capture 'card-live.png'

# Move into the card and stay there: it must not disappear.
[void][WC]::SetCursorPos($card.X + [int]($card.W / 2), $card.Y + [int]($card.H / 2))
Start-Sleep -Milliseconds 900
$still = Card
Write-Output ("card after entering: {0}" -f $(if ($still) { 'visible ok' } else { 'GONE (bad)' }))
Capture 'card-hover.png'

# Click the copy button (bottom right of the card) and check the clipboard.
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Clipboard]::Clear()
Click ($card.X + $card.W - 60) ($card.Y + $card.H - 38)
Start-Sleep -Milliseconds 400
$clip = [System.Windows.Forms.Clipboard]::GetText()
Write-Output ("clipboard after copy button: '{0}'" -f $clip)
Write-Output ("card after button: {0}" -f $(if (Card) { 'still visible (bad)' } else { 'hidden ok' }))

[void][WC]::SetCursorPos(80, 1100)
Start-Sleep -Milliseconds 800
Write-Output ("card after leaving: {0}" -f $(if (Card) { 'still visible (bad)' } else { 'hidden ok' }))
Capture 'card-after.png'
Write-Output 'done'
