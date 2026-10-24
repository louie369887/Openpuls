param(
    [int]$X = 100,
    [int]$Y = 116,
    [string]$Class = "OP.MainWindow",
    [switch]$Double
)
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class RC {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static IntPtr Find(string cls) {
    IntPtr f = IntPtr.Zero;
    EnumWindows((h,l) => { var sb=new StringBuilder(256); GetClassName(h,sb,256); if(sb.ToString()==cls){f=h;return false;} return true;}, IntPtr.Zero);
    return f;
  }
}
"@
$h = [RC]::Find($Class)
if ($h -eq [IntPtr]::Zero) { Write-Error "window not found"; exit 1 }
[RC]::SetForegroundWindow($h) | Out-Null
$pt = New-Object RC+POINT
$pt.X = $X; $pt.Y = $Y
[RC]::ClientToScreen($h, [ref]$pt) | Out-Null
[RC]::SetCursorPos($pt.X, $pt.Y) | Out-Null
Start-Sleep -Milliseconds 200
[RC]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 60
[RC]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
if ($Double) {
    Start-Sleep -Milliseconds 120
    [RC]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [RC]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}
Write-Output "real click at screen $($pt.X),$($pt.Y)"
