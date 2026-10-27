# Live check for the auto-snooze path: temporarily lowers the thresholds, shows a
# page with stable English text, waits for several scans, then opens the word list
# and screenshots it so the "疑似已掌握" rows can be inspected.
#
# The ini file is backed up before the run and restored afterwards.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-snooze.ps1
param(
    [string]$OutDir = (Join-Path $env:TEMP 'wordlens-shots'),
    [int]$WaitSeconds = 35
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class WS {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static List<string> Snapshot() {
    var list = new List<string>();
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      var sb = new StringBuilder(512); GetWindowText(h, sb, sb.Capacity);
      RECT r; GetWindowRect(h, out r);
      list.Add(sb + "|" + h.ToInt64() + "|" + r.Left + "," + r.Top + "," + (r.Right-r.Left) + "," + (r.Bottom-r.Top));
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@
[void][WS]::SetProcessDpiAwarenessContext([IntPtr](-4))

$repo = Split-Path -Parent $PSScriptRoot
$wlocr = Join-Path $repo 'build\wlocr.exe'
$env:Path = "D:/Qt/6.10.2/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$env:Path"

function Windows {
    foreach ($entry in [WS]::Snapshot()) {
        $p = $entry -split '\|'
        $r = $p[2] -split ','
        [pscustomobject]@{
            Title = $p[0]; Handle = [IntPtr][long]$p[1]
            X = [int]$r[0]; Y = [int]$r[1]; W = [int]$r[2]; H = [int]$r[3]
        }
    }
}
function Find-Bar {
    Windows | Where-Object { $_.Title -eq 'WordLens' -and $_.W -gt 400 -and $_.H -gt 30 -and $_.H -le 90 } |
        Sort-Object W -Descending | Select-Object -First 1
}
function Buttons($b) {
    $right = $b.X + $b.W
    $midY = $b.Y + [int]($b.H / 2)
    $scale = [WS]::GetDpiForWindow($b.Handle) / 96.0
    if ($scale -le 0) { $scale = 1.0 }
    @{
        pause    = @(($right - [int](118 * $scale)), $midY)
        list     = @(($right - [int](86 * $scale)), $midY)
        settings = @(($right - [int](54 * $scale)), $midY)
        hide     = @(($right - [int](22 * $scale)), $midY)
    }
}
function Click([int]$x, [int]$y) {
    [void][WS]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 150
    [WS]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [WS]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 600
}
function Capture([string]$name) {
    $path = Join-Path $OutDir $name
    & $wlocr --capture $path | Out-Null
    Write-Output "capture: $path"
}

# --- 1. raise the app, keep the original configuration --------------------
Get-Process WordLens -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700

$ini = Join-Path $env:LOCALAPPDATA 'WordLens\WordLens\wordlens.ini'
$backup = "$ini.snooze-test.bak"
if (-not (Test-Path $ini)) { throw "config not found: $ini" }
Copy-Item $ini $backup -Force

try {
    $text = Get-Content $ini -Raw
    $text = $text -replace 'autoSnoozeScans=\d+', 'autoSnoozeScans=2'
    $text = $text -replace 'autoSnoozeSeconds=\d+', 'autoSnoozeSeconds=6'
    $text = $text -replace 'keepUntilHandled=\w+', 'keepUntilHandled=true'
    $text = $text -replace 'autoSnooze=\w+', 'autoSnooze=true'
    Set-Content $ini $text -NoNewline
    Write-Output 'config: autoSnoozeScans=2 autoSnoozeSeconds=6'

    # --- 2. a page whose text stays put while the screen still changes -----
    $page = Join-Path $env:TEMP 'wordlens-snooze.html'
    @'
<!doctype html><html><head><meta charset="utf-8"><style>
body{background:#101418;color:#e8eef7;font:44px 'Segoe UI',sans-serif;padding:38px;line-height:1.5}
b{color:#7aa2f7}span{color:#9ece6a}
</style></head><body>
<div>boundary conformal decimator</div>
<div>cofactor mapper neutron</div>
<div>oscillator waveguide <b id="tick">0</b></div>
<p><span>keep this sentence steady while the counter moves</span></p>
<script>let n=0;setInterval(()=>{n++;document.getElementById('tick').textContent=n},400)</script>
</body></html>
'@ | Set-Content $page -Encoding UTF8
    Start-Process $page
    Start-Sleep -Seconds 3

    # --- 3. run the recogniser long enough to snooze -----------------------
    Start-Process -FilePath (Join-Path $repo 'build\WordLens.exe') `
                  -WorkingDirectory (Join-Path $repo 'build')
    Start-Sleep -Seconds $WaitSeconds

    $bar = Find-Bar
    if (-not $bar) { throw 'bar not found' }
    Write-Output ("bar after {0}s: {1},{2} {3}x{4}" -f $WaitSeconds, $bar.X, $bar.Y, $bar.W, $bar.H)
    Capture 'snooze-bar.png'

    # --- 4. look at the word list -----------------------------------------
    # Park on the bar first so its geometry is frozen while we aim.
    [void][WS]::SetCursorPos(($bar.X + [int]($bar.W / 2)), ($bar.Y + [int]($bar.H / 2)))
    Start-Sleep -Milliseconds 300
    $bar = Find-Bar
    $b = Buttons $bar
    Click $b.list[0] $b.list[1]
    Start-Sleep -Milliseconds 1800
    Capture 'snooze-details.png'
    Windows | Where-Object { $_.Title -like 'WordLens*' -and $_.H -gt 200 } |
        ForEach-Object { Write-Output ("window: {0} [{1},{2} {3}x{4}]" -f $_.Title, $_.X, $_.Y, $_.W, $_.H) }
}
finally {
    Get-Process WordLens -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    Copy-Item $backup $ini -Force
    Remove-Item $backup -Force -ErrorAction SilentlyContinue
    Get-Process msedge, chrome, firefox -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle -like '*wordlens-snooze*' } |
        ForEach-Object { $_.CloseMainWindow() | Out-Null }
    Write-Output 'config restored'
}
Write-Output 'done'
