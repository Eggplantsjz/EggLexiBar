Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W3 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static string Dump() {
    var sb2 = new StringBuilder();
    EnumWindows((h, l) => {
      var sb = new StringBuilder(512);
      GetWindowText(h, sb, sb.Capacity);
      var t = sb.ToString();
      if (t.StartsWith("WordLens") || t.Contains("WordLens")) {
        RECT r; GetWindowRect(h, out r);
        sb2.AppendLine(t + " | vis=" + IsWindowVisible(h) + " | " + r.Left + "," + r.Top + " " + (r.Right-r.Left) + "x" + (r.Bottom-r.Top));
      }
      return true;
    }, IntPtr.Zero);
    return sb2.ToString();
  }
}
"@
[void][W3]::SetProcessDpiAwarenessContext([IntPtr](-4))
[W3]::Dump()
