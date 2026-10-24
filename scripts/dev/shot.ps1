param(
    [string]$Out = "F:\openpuls\docs\shots\main.png",
    [string]$Class = "OP.MainWindow",
    [int]$X = 20,
    [int]$Y = 20
)
Add-Type -AssemblyName System.Drawing
if (-not ("WP" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Text;
using System.Drawing;
using System.Runtime.InteropServices;
public class WP {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static IntPtr Find(string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h,l) => {
      var sb = new StringBuilder(256);
      GetClassName(h, sb, 256);
      if (sb.ToString() == cls) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@
}
$h = [WP]::Find($Class)
if ($h -eq [IntPtr]::Zero) { Write-Error "window not found: $Class"; exit 1 }

# PowerShell 是 DPI 无关进程，坐标会被按 96/dpi 虚拟化；这里换算回物理像素再截图
$scale = [double][WP]::GetDpiForWindow($h) / 96.0
if ($scale -lt 1.0) { $scale = 1.0 }

[WP]::ShowWindow($h, 9) | Out-Null
[WP]::SetWindowPos($h, [IntPtr](-1), [int]($X / $scale), [int]($Y / $scale), 0, 0, 0x0001 -bor 0x0040) | Out-Null
[WP]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900

$r = New-Object WP+RECT
[WP]::GetWindowRect($h, [ref]$r) | Out-Null
$left = [int]($r.Left * $scale)
$top = [int]($r.Top * $scale)
$w = [int](($r.Right - $r.Left) * $scale)
$ht = [int](($r.Bottom - $r.Top) * $scale)

$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($left, $top, 0, 0, $bmp.Size)
New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
[WP]::SetWindowPos($h, [IntPtr](-2), [int]($X / $scale), [int]($Y / $scale), 0, 0, 0x0001) | Out-Null
Write-Output "saved $Out ($w x $ht) at $left,$top scale=$scale"
