param(
    [int]$X = 100,
    [int]$Y = 116,
    [string]$Class = "OP.MainWindow",
    [int]$Repeat = 1
)
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CL {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static IntPtr Find(string cls) {
    IntPtr f = IntPtr.Zero;
    EnumWindows((h,l) => { var sb=new StringBuilder(256); GetClassName(h,sb,256); if(sb.ToString()==cls){f=h;return false;} return true;}, IntPtr.Zero);
    return f;
  }
}
"@
$h = [CL]::Find($Class)
if ($h -eq [IntPtr]::Zero) { Write-Error "window not found"; exit 1 }
# 跨进程鼠标消息会按 窗口DPI/96 换算；窗口 DPI 从注册表读系统缩放值
$dpi = 96
try { $dpi = (Get-ItemProperty 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name AppliedDPI -ErrorAction Stop).AppliedDPI } catch { $dpi = 96 }
if (-not $dpi -or $dpi -lt 96) { $dpi = 96 }
$k = [double]$dpi / 96.0
$px = [int]($X / $k)
$py = [int]($Y / $k)
for ($i = 0; $i -lt $Repeat; $i++) {
    $lp = [IntPtr](($py -shl 16) -bor ($px -band 0xFFFF))
    [CL]::PostMessage($h, 0x0202, [IntPtr]::Zero, $lp) | Out-Null
    Start-Sleep -Milliseconds 150
}
Write-Output "click $X,$Y -> post $px,$py (scale $k)"
