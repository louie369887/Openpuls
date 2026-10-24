param(
    [string]$Class = "OP.Row",
    [int]$Index = 0,
    [string]$Parent = "OP.MainWindow"
)
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class CC {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static IntPtr Find(string cls) {
    IntPtr f = IntPtr.Zero;
    EnumWindows((h,l) => { var sb=new StringBuilder(256); GetClassName(h,sb,256); if(sb.ToString()==cls){f=h;return false;} return true;}, IntPtr.Zero);
    return f;
  }
  public static List<IntPtr> Children(IntPtr p, string cls) {
    var list = new List<IntPtr>();
    EnumChildWindows(p, (h,l) => {
      var sb=new StringBuilder(256); GetClassName(h,sb,256);
      if (sb.ToString()==cls) list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@
$p = [CC]::Find($Parent)
if ($p -eq [IntPtr]::Zero) { Write-Error "parent not found"; exit 1 }
$kids = [CC]::Children($p, $Class)
Write-Output "found $($kids.Count) children of class $Class"
if ($Index -ge $kids.Count) { Write-Error "index out of range"; exit 1 }
$h = $kids[$Index]
$r = New-Object CC+RECT
[CC]::GetWindowRect($h, [ref]$r) | Out-Null
"target hwnd=$h rect=$($r.Left),$($r.Top),$($r.Right),$($r.Bottom) visible=$([CC]::IsWindowVisible($h))"
# 行内任意位置即可触发（程序只关心点击事件，不关心具体坐标）
$lp = [IntPtr][long](20 * 65536 + 60)
[CC]::PostMessage($h, 0x0202, [IntPtr]::Zero, $lp) | Out-Null
Start-Sleep -Milliseconds 400
"posted"
