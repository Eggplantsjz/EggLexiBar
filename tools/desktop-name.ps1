Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Desk {
  [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
  [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetUserObjectInformation(IntPtr h, int index, StringBuilder info, int len, out int needed);
}
"@
$h = [Desk]::OpenInputDesktop(0, $false, 0x0001)
if ($h -eq [IntPtr]::Zero) { "cannot open input desktop: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
else {
  $sb = New-Object System.Text.StringBuilder 256
  $needed = 0
  [void][Desk]::GetUserObjectInformation($h, 2, $sb, 512, [ref]$needed)
  "input desktop: $($sb.ToString())"
  [void][Desk]::CloseDesktop($h)
}
