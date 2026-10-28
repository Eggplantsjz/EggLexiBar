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
public static class W2 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
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
[void][W2]::SetProcessDpiAwarenessContext([IntPtr](-4))

$repo = Split-Path -Parent $PSScriptRoot
$wlocr = Join-Path $repo 'build\wlocr.exe'

function Bars {
    $out = @()
    foreach ($entry in [W2]::Snapshot()) {
        $p = $entry -split '\|'
        $r = $p[1] -split ','
        $w = [int]$r[2]
        $h = [int]$r[3]
        if ($w -gt 400 -and $h -gt 30 -and $h -le 90) {
            $out += [pscustomobject]@{ X = [int]$r[0]; Y = [int]$r[1]; W = $w; H = $h }
        }
    }
    return $out
}

$bar = $null
for ($i = 0; $i -lt 60; $i++) {
    $bar = @(Bars) | Select-Object -First 1
    if ($bar) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $bar) { throw 'bar not found' }

$chipX = $bar.X + 72
$chipY = $bar.Y + [int]($bar.H / 2)
Write-Output ("bar: {0},{1} {2}x{3}  hover: {4},{5}" -f $bar.X, $bar.Y, $bar.W, $bar.H, $chipX, $chipY)

[void][W2]::SetCursorPos($chipX, $chipY)
Start-Sleep -Milliseconds 1200
& $wlocr --capture (Join-Path $OutDir 'hover-card.png') | Out-Null

[void][W2]::SetCursorPos(120, 1200)
Start-Sleep -Milliseconds 800
& $wlocr --capture (Join-Path $OutDir 'hover-hidden.png') | Out-Null
Write-Output 'captured'
