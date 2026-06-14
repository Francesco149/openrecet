# install_shortcut.ps1 — create/refresh the "OpenRecet Trace Studio" shortcut on
# the Windows Desktop + Start Menu. Run from WSL:
#   powershell.exe -ExecutionPolicy Bypass -File "$(wslpath -w tools/trace_studio_v3/install_shortcut.ps1)"
#
# The shortcut runs open_studio.sh (in WSL), which opens the native v3 viewer on
# the CURRENT working trace (the .studio_current pointer orv3_window.py keeps
# fresh). So the shortcut is STABLE — it always opens whatever window we last
# drove, no re-creating needed.
$ErrorActionPreference = "Stop"
$ws = New-Object -ComObject WScript.Shell
$targets = @(
    [Environment]::GetFolderPath("Desktop"),
    (Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs")
)
foreach ($dir in $targets) {
    $lnkPath = Join-Path $dir "OpenRecet Trace Studio.lnk"
    $lnk = $ws.CreateShortcut($lnkPath)
    $lnk.TargetPath   = Join-Path $env:SystemRoot "System32\wsl.exe"
    $lnk.Arguments    = "-d NixOS bash -lc /opt/src/openrecet/tools/trace_studio_v3/open_studio.sh"
    $lnk.Description   = "Open the OpenRecet v3 Trace Studio viewer on the current working trace"
    $lnk.WindowStyle   = 7   # minimized — no lingering console window
    $lnk.Save()
    Write-Host "created: $lnkPath"
}
