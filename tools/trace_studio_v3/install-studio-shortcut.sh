#!/usr/bin/env bash
# tools/trace_studio_v3/install-studio-shortcut.sh — install/refresh the
# "OpenRecet Trace Studio" desktop + Start-Menu shortcut so ONE click opens the
# native v3 viewer on the CURRENT working trace.
#
# Why a native Windows batch (not wsl.exe → bash → setsid &):
#   The old shortcut ran `wsl.exe -d NixOS bash -lc open_studio.sh`, which
#   launched the GUI via `setsid viewer.exe & disown` from INSIDE a transient
#   `wsl.exe -c bash`.  When that bash exits and the WSL instance has no other
#   live process, the kernel tears the session down and kills the just-spawned
#   viewer — so the shortcut "sometimes does nothing" (it only survived when the
#   WSL instance happened to be alive for another reason).  The robust shape
#   (same as OpenSummoners' osr_view launcher) is a first-class Windows process:
#
#     Desktop / Start-Menu "OpenRecet Trace Studio.lnk"
#       -> C:\openrecet-studio\open-studio.bat
#           reads C:\openrecet-studio\studio-current.txt  (line 1 = the viewer
#           arg: the \\wsl.localhost UNC path to the window's view.json)
#           -> start C:\openrecet-studio\viewer.exe <that view.json>
#
#   `start` launches the viewer detached from cmd, so it outlives the click and
#   has zero dependency on any WSL session.  The shortcut + batch never change;
#   orv3_window.py keeps studio-current.txt pointed at the latest window it drove
#   (write_current_pointer).  The viewer is static (-static-libgcc/-libstdc++) so
#   the lone copy on C:\ runs standalone; it reads view.json + the dedup'd
#   containers over UNC (view.json stores absolute \\wsl.localhost paths), so
#   nothing big is copied to C:\.
#
# Re-run this after rebuilding the viewer (it refreshes the C:\ copy).  Run from
# the repo inside the dev shell:
#   nix develop --command bash tools/trace_studio_v3/install-studio-shortcut.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STUDIO_DIR="/mnt/c/openrecet-studio"
PTR="$HERE/.studio_current"          # the WSL pointer orv3_window also writes
EXE_SRC="$HERE/viewer/viewer.exe"

[ -f "$EXE_SRC" ] || {
    echo "viewer.exe missing ($EXE_SRC) — build it first:" >&2
    echo "  nix develop --command make -C tools/trace_studio_v3/viewer" >&2
    exit 1
}

mkdir -p "$STUDIO_DIR"
cp -f "$EXE_SRC" "$STUDIO_DIR/viewer.exe"
echo "[studio-shortcut] copied viewer.exe -> C:\\openrecet-studio\\viewer.exe"

# The launcher batch.  %~dp0 = this batch's dir (C:\openrecet-studio\), so
# viewer.exe + studio-current.txt sit next to it.  `start` detaches the viewer
# (a first-class Windows process — survives regardless of WSL).
cat > "$STUDIO_DIR/open-studio.bat" <<'BAT'
@echo off
REM OpenRecet Trace Studio launcher.  Opens the native v3 viewer on the CURRENT
REM working trace, read from studio-current.txt (line 1 = the view.json path, a
REM \\wsl.localhost UNC path).  orv3_window.py rewrites studio-current.txt on
REM every window it drives, so this batch + the .lnk never change.
setlocal
set "DIR=%~dp0"
REM Pin our own dir as the cwd (a real C:\ path) so `start` never inherits a
REM \\wsl.localhost UNC cwd — which cmd can't use ("UNC paths are not supported,
REM Defaulting to Windows directory" + an Access-denied on the child launch). The
REM .lnk already sets WorkingDirectory here, but this makes the batch robust no
REM matter how it's invoked (e.g. `cmd /c open-studio.bat` from a WSL shell).
cd /d "%DIR%"
if not exist "%DIR%viewer.exe" (
  echo viewer.exe missing in %DIR% -- re-run install-studio-shortcut.sh & timeout /t 8 & exit /b 1
)
set "VIEW="
if exist "%DIR%studio-current.txt" set /p VIEW=<"%DIR%studio-current.txt"
if "%VIEW%"=="" (
  echo No working trace yet.  Drive one first, e.g.:
  echo   orv3_window.py ^<scenario^> --window OFF:COUNT --anchor ^<ANCHOR^> --view
  timeout /t 8 & exit /b 1
)
start "OpenRecet Trace Studio" "%DIR%viewer.exe" "%VIEW%"
BAT
echo "[studio-shortcut] wrote C:\\openrecet-studio\\open-studio.bat"

# Seed studio-current.txt: prefer the live WSL pointer (.studio_current line 1,
# converted to its UNC path) so the shortcut is usable immediately post-install;
# else leave whatever's there (orv3_window owns it), else an empty placeholder.
# No trailing newline — cmd's `set /p` reads it clean.
if [ -f "$STUDIO_DIR/studio-current.txt" ] && [ -s "$STUDIO_DIR/studio-current.txt" ]; then
    echo "[studio-shortcut] studio-current.txt exists -> $(cat "$STUDIO_DIR/studio-current.txt")"
elif [ -f "$PTR" ] && [ -n "$(sed -n '1p' "$PTR")" ]; then
    VIEW_WSL="$(sed -n '1p' "$PTR")"
    if [ -f "$VIEW_WSL" ]; then
        wslpath -w "$VIEW_WSL" | tr -d '\n' > "$STUDIO_DIR/studio-current.txt"
        echo "[studio-shortcut] seeded studio-current.txt from .studio_current -> $(cat "$STUDIO_DIR/studio-current.txt")"
    else
        : > "$STUDIO_DIR/studio-current.txt"
        echo "[studio-shortcut] seeded empty studio-current.txt (the .studio_current view.json is gone)"
    fi
else
    : > "$STUDIO_DIR/studio-current.txt"
    echo "[studio-shortcut] seeded empty studio-current.txt (drive a window with --view to populate)"
fi

# Create the .lnk shortcuts (Desktop + Start Menu) via PowerShell (WScript.Shell).
# WindowStyle 7 = minimized, so the cmd window that runs the batch doesn't linger.
powershell.exe -NoProfile -Command '
$ws = New-Object -ComObject WScript.Shell
$targets = @(
  (Join-Path $env:USERPROFILE "Desktop\OpenRecet Trace Studio.lnk"),
  (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\OpenRecet Trace Studio.lnk")
)
foreach ($t in $targets) {
  $lnk = $ws.CreateShortcut($t)
  $lnk.TargetPath       = "C:\openrecet-studio\open-studio.bat"
  $lnk.WorkingDirectory = "C:\openrecet-studio"
  $lnk.IconLocation     = "C:\openrecet-studio\viewer.exe,0"
  $lnk.Description       = "Open the OpenRecet v3 Trace Studio viewer on the current working trace"
  $lnk.WindowStyle       = 7
  $lnk.Save()
  Write-Host "[studio-shortcut] created $t"
}
'
echo "[studio-shortcut] done — click \"OpenRecet Trace Studio\" on the desktop / Start Menu"
