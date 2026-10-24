# 打开「输入延迟实测」小窗并截图
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class BT2 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static IntPtr Find(string cls) {
    IntPtr f = IntPtr.Zero;
    EnumWindows((h,l) => { var sb=new StringBuilder(256); GetClassName(h,sb,256); if(sb.ToString()==cls){f=h;return false;} return true;}, IntPtr.Zero);
    return f;
  }
  public static List<IntPtr> VisibleChildren(IntPtr p, string cls) {
    var list = new List<IntPtr>();
    EnumChildWindows(p, (h,l) => { var sb=new StringBuilder(256); GetClassName(h,sb,256); if(sb.ToString()==cls && IsWindowVisible(h)) list.Add(h); return true;}, IntPtr.Zero);
    return list;
  }
}
"@
$main = [BT2]::Find("OP.MainWindow")
if ($main -eq [IntPtr]::Zero) { Write-Error "main window not found"; exit 1 }
$btns = [BT2]::VisibleChildren($main, "OP.Btn")
$target = [IntPtr]::Zero
foreach ($b in $btns) {
    $r = New-Object BT2+RECT
    [BT2]::GetWindowRect($b, [ref]$r) | Out-Null
    if (($r.Top -gt 140) -and (($r.Bottom - $r.Top) -gt 10)) { $target = $b; break }
}
if ($target -eq [IntPtr]::Zero) { Write-Error "latency button not found (switch to the touch page first)"; exit 1 }
[BT2]::PostMessage($target, 0x0202, [IntPtr]::Zero, [IntPtr][long](20*65536+40)) | Out-Null
Start-Sleep -Milliseconds 900
$lat = [BT2]::Find("OP.Latency")
"latency window hwnd = $lat"
if ($lat -ne [IntPtr]::Zero) {
    $r = New-Object BT2+RECT
    [BT2]::GetWindowRect($lat, [ref]$r) | Out-Null
    # 在窗口中间点一下，让它产生一次采样
    $cx = [int](($r.Left + $r.Right) / 2)
    $cy = [int](($r.Top + $r.Bottom) / 2)
    [BT2]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 150
    [BT2]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [BT2]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}

