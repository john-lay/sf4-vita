# Run an installed app in Vita3K and screenshot just its window contents.
#   powershell -File tools\vita3k-shot.ps1 SFIV00001 out.png [-Seconds 12] [-Count 1] [-Interval 0.5]
# With -Count N > 1 it takes N shots -Interval seconds apart: out_1.png .. out_N.png.
# Install the VPK first (e.g. with vita3k-run.ps1). The window's own contents
# are captured with PrintWindow, so other windows on the desktop never end up
# in the image, even if they cover Vita3K.
param(
    [Parameter(Mandatory)] [string] $TitleId,
    [Parameter(Mandatory)] [string] $Out,
    [int] $Seconds = 12,
    [int] $Count = 1,
    [double] $Interval = 0.5,
    [string] $Vita3K = "D:\vita-3k"
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$p = Start-Process -FilePath (Join-Path $Vita3K "Vita3K.exe") -ArgumentList "--frontend", "sdl", "-r", $TitleId `
    -WorkingDirectory $Vita3K -PassThru
Start-Sleep -Seconds $Seconds
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; throw "Vita3K window not found" }

$r = New-Object Win+RECT; [void][Win]::GetClientRect($h, [ref]$r)
for ($i = 1; $i -le $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # PW_CLIENTONLY (1) | PW_RENDERFULLCONTENT (2): client area, including GPU-rendered content
    $ok = [Win]::PrintWindow($h, $hdc, 3)
    $g.ReleaseHdc($hdc)
    if (-not $ok) { $g.Dispose(); $bmp.Dispose(); Stop-Process -Id $p.Id -Force; throw "PrintWindow failed" }
    $file = $Out
    if ($Count -gt 1) { $file = [IO.Path]::ChangeExtension($Out, $null).TrimEnd('.') + "_$i" + [IO.Path]::GetExtension($Out) }
    $bmp.Save($file)
    $g.Dispose(); $bmp.Dispose()
    if ($i -lt $Count) { Start-Sleep -Milliseconds ([int]($Interval * 1000)) }
}
Stop-Process -Id $p.Id -Force
"saved $Count shot(s), $($r.R - $r.L)x$($r.B - $r.T), to $Out"
