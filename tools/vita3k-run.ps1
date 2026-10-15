# Install a VPK into Vita3K, run it for a while, then print the useful part of the log.
#   powershell -File tools\vita3k-run.ps1 game\sf4_vita_vita3k.vpk SFIV00001 [-Seconds 20] [-Vita3K D:\vita-3k]
# The VPK is extracted straight into ux0\app\<TitleId> (the GUI installer asks
# for confirmation), and the app is run with the plain SDL frontend.
param(
    [Parameter(Mandatory)] [string] $Vpk,
    [Parameter(Mandatory)] [string] $TitleId,
    [int] $Seconds = 20,
    [string] $Vita3K = "D:\vita-3k",
    [string] $Filter = '\|[IWEC]\|'   # drop trace/debug lines by default
)

$app = Join-Path $env:APPDATA "Vita3K\Vita3K\ux0\app\$TitleId"
if (Test-Path $app) { Remove-Item -Recurse -Force $app }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path $Vpk).Path, $app)

$p = Start-Process -FilePath (Join-Path $Vita3K "Vita3K.exe") -ArgumentList "--frontend", "sdl", "-r", $TitleId `
    -WorkingDirectory $Vita3K -PassThru
Start-Sleep -Seconds $Seconds
if ($p.HasExited) { "Vita3K exited by itself (code $($p.ExitCode))" } else { Stop-Process -Id $p.Id -Force; "Vita3K still running after $Seconds s; stopped it" }
Start-Sleep -Seconds 1

Get-Content (Join-Path $Vita3K "vita3k.log") |
    Select-String -Pattern $Filter |
    Select-String -NotMatch 'renderer::|shader::|overlay::|taiInject|create_inline_hook|load_var_exports' |
    ForEach-Object { $_.Line -replace '\x1b\[[0-9;]*m', '' }
