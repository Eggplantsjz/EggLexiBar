# Minimal UI smoke test for WordLens.
# Drives the always-on-top bar with synthetic mouse/keyboard input and saves screenshots.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui-smoke.ps1 [-OutDir <dir>]
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
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
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
        list.Add(t + "|" + h.ToInt64() + "|" + r.Left + "," + r.Top + "," + (r.Right - r.Left) + "," + (r.Bottom - r.Top));
      }
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@

# Work in physical pixels so that clicks land where the screenshots say they are.
[void][W]::SetProcessDpiAwarenessContext([IntPtr](-4))

$repo = Split-Path -Parent $PSScriptRoot
$wlocr = Join-Path $repo 'build\wlocr.exe'
$env:Path = "D:/Qt/6.10.2/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$env:Path"

function Parse([string]$entry) {
    $p = $entry -split '\|'
    $r = $p[2] -split ','
    [pscustomobject]@{
        Title  = $p[0]
        Handle = [IntPtr][long]$p[1]
        X      = [int]$r[0]
        Y      = [int]$r[1]
        W      = [int]$r[2]
        H      = [int]$r[3]
    }
}
function Find-Bar {
    $best = $null
    foreach ($entry in [W]::Snapshot()) {
        $o = Parse $entry
        if ($o.Title -eq 'WordLens' -and $o.W -gt 400 -and $o.H -gt 30 -and $o.H -le 90) {
            if (-not $best -or $o.W -gt $best.W) { $best = $o }
        }
    }
    return $best
}
function Wait-Bar([int]$timeoutSeconds = 40) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $bar = Find-Bar
        if ($bar) { return $bar }
        if ($DebugSnapshot) {
            foreach ($entry in [W]::Snapshot()) {
                $o = Parse $entry
                Write-Output ("debug window title=[{0}] x={1} y={2} w={3} h={4}" -f $o.Title, $o.X, $o.Y, $o.W, $o.H)
            }
        }
        Start-Sleep -Milliseconds 500
    }
    return $null
}
function Find-Others {
    $list = @()
    foreach ($entry in [W]::Snapshot()) {
        $o = Parse $entry
        if (-not ($o.Title -eq 'WordLens' -and $o.H -lt 200)) { $list += $o }
    }
    return $list
}
function Find-Card {
    $list = @()
    foreach ($entry in [W]::Snapshot()) {
        $o = Parse $entry
        if ($o.Title -like 'WordLens*' -and $o.H -gt 100 -and $o.W -gt 250) { $list += $o }
    }
    return $list | Select-Object -First 1
}
function Find-ChipX($bar) {
    # Freeze the bar first so the probe walks a stable layout.
    [void][W]::SetCursorPos(($bar.X + [int]($bar.W / 2)), ($bar.Y + [int]($bar.H / 2)))
    Start-Sleep -Milliseconds 300
    $fresh = Find-Bar
    if ($fresh) { $bar = $fresh }
    for ($x = $bar.X + 330; $x -lt ($bar.X + $bar.W - 180); $x += 30) {
        [void][W]::SetCursorPos($x, $bar.Y + [int]($bar.H / 2))
        Start-Sleep -Milliseconds 460
        if (Find-Card) { return $x }
    }
    return $null
}
function Click-BarButton([string]$name) {
    # Park the pointer on the bar first: that freezes the bar's position, so the
    # coordinates cannot go stale between measuring and clicking.
    $bar = Find-Bar
    if (-not $bar) { throw 'bar not found' }
    [void][W]::SetCursorPos(($bar.X + [int]($bar.W / 2)), ($bar.Y + [int]($bar.H / 2)))
    Start-Sleep -Milliseconds 300
    $bar = Find-Bar
    $b = Buttons $bar
    $xy = $b[$name]
    Click $xy[0] $xy[1]
}
function Click([int]$x, [int]$y, [string]$button = 'left') {
    [void][W]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 150
    $down = if ($button -eq 'right') { 0x0008 } else { 0x0002 }
    $up = if ($button -eq 'right') { 0x0010 } else { 0x0004 }
    [W]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}
function Key([byte]$vk) {
    [W]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [W]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
}
function Capture([string]$name) {
    $path = Join-Path $OutDir $name
    & $wlocr --capture $path | Out-Null
    if (-not (Test-Path $path)) { Write-Output "capture FAILED: $path" }
    else { Write-Output "capture: $path" }
}
function Report([string]$label, $value) { Write-Output ("{0}: {1}" -f $label, $value) }
function Close-All([object[]]$windows) {
    foreach ($o in $windows) {
        [void][W]::PostMessage($o.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 800
}

$bar = Wait-Bar 40
if (-not $bar) { throw 'WordLens bar not found (restart the app first)' }
Report 'bar' ("x={0} y={1} w={2} h={3}" -f $bar.X, $bar.Y, $bar.W, $bar.H)

# Park the pointer away from the bar so tooltips do not confuse window discovery.
[void][W]::SetCursorPos(60, 900)
Start-Sleep -Milliseconds 600

function Buttons($b) {
    $right = $b.X + $b.W
    $midY = $b.Y + [int]($b.H / 2)
    $scale = [W]::GetDpiForWindow($b.Handle) / 96.0
    if ($scale -le 0) { $scale = 1.0 }
    @{
        pause    = @(($right - [int](118 * $scale)), $midY)
        list     = @(($right - [int](86 * $scale)), $midY)
        settings = @(($right - [int](54 * $scale)), $midY)
        hide     = @(($right - [int](22 * $scale)), $midY)
    }
}

# 1. Settings dialog
Click-BarButton 'settings'
Start-Sleep -Milliseconds 1500
$others = Find-Others
Report 'windows after settings click' (($others | ForEach-Object { "{0} [{1},{2} {3}x{4}]" -f $_.Title, $_.X, $_.Y, $_.W, $_.H }) -join ' ; ')
Capture 'ui-settings.png'
Close-All $others

# 2. Details panel
Click-BarButton 'list'
Start-Sleep -Milliseconds 1500
$others = Find-Others
Report 'windows after details click' (($others | ForEach-Object { "{0} [{1},{2} {3}x{4}]" -f $_.Title, $_.X, $_.Y, $_.W, $_.H }) -join ' ; ')
Capture 'ui-details.png'
Close-All $others

# 3. Context menu on a chip
$bar = Find-Bar
$chipX = Find-ChipX $bar
$bar = Find-Bar
Report 'chip x' $chipX
if ($chipX) {
    Click $chipX ($bar.Y + [int]($bar.H / 2)) 'right'
    Start-Sleep -Milliseconds 1000
    Capture 'ui-menu.png'
    Key 0x1B
}
[void][W]::SetCursorPos(60, 900)
Start-Sleep -Milliseconds 700

# 4. Click a chip -> clipboard
$bar = Find-Bar
$chipX = Find-ChipX $bar
$bar = Find-Bar
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Clipboard]::Clear()
if ($chipX) {
    Click $chipX ($bar.Y + [int]($bar.H / 2))
    Start-Sleep -Milliseconds 900
}
Report 'clipboard after chip click' ("'" + [System.Windows.Forms.Clipboard]::GetText() + "'")

# 5. Hide button
Click-BarButton 'hide'
Start-Sleep -Milliseconds 1500
Report 'bar after hide click' ($(if (Find-Bar) { 'STILL VISIBLE' } else { 'hidden ok' }))
Capture 'ui-hidden.png'

# 6. Toggle back by sending the hotkey message to the app (the hotkey itself is a real
#    key press; synthetic keybd_event input does not always reach RegisterHotKey).
$proc = Get-Process WordLens -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
    $null = $proc
}

Write-Output 'done'
