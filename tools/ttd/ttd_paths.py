"""Locate the Windows-host binaries the harness drives.

Probes a small set of known install locations.  Returns absolute
Windows-style paths.  All exceptions are caught + collapsed into a
structured failure object so callers never re-raise a Windows-tool
path string up the stack.

Env-var overrides (use whichever is set; harness reports which):
    OPENRECET_TTD_EXE     full Windows path to the recorder binary
    OPENRECET_CDB_EXE     full Windows path to the headless debugger
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import Optional


# Known install roots winget drops Microsoft.WinDbg under.  Order matters —
# first hit wins.  The Microsoft Store / winget package exposes AppX
# execution-alias shims under %LOCALAPPDATA%\Microsoft\WindowsApps which
# work fine for non-interactive invocation in our experience.
# Classic SDK paths kept for users who installed the Windows Driver Kit
# Debugging Tools separately.
_PROBE_ROOTS_WIN = [
    r"%LOCALAPPDATA%\Microsoft\WindowsApps",          # store install shims
    r"C:\Program Files (x86)\Windows Kits\10\Debuggers",
    r"C:\Program Files\Debugging Tools for Windows (x86)",
    r"C:\Program Files\Debugging Tools for Windows (x64)",
]

# Per-binary filename candidates (in priority order).  We prefer the
# AppX shim names because they're discoverable through WSL's /mnt/c
# even when the real binary lives in a permission-locked WindowsApps
# subdirectory.  Architecture-specific cdb variants come ahead of the
# generic "cdb.exe" so we pick the right bitness for our i686 target.
_TTD_NAMES = ["TTD.exe", "ttd.exe"]
_CDB_NAMES = ["cdbX86.exe", "cdb.exe"]   # i686 retail → x86
_CDB_X64_NAMES = ["cdbX64.exe"]


def _wsl_to_win(p: str) -> str:
    """Convert a WSL path to a Windows path via wslpath.  Pass-through if
    already Windows-shaped."""
    if len(p) >= 2 and p[1] == ":":
        return p
    return subprocess.run(
        ["wslpath", "-w", p],
        check=True, capture_output=True, text=True).stdout.strip()


def _exists_win(win_path: str) -> bool:
    """Does a Windows-style path exist (read via WSL /mnt/c mount)."""
    if not (len(win_path) >= 2 and win_path[1] == ":"):
        return False
    drive = win_path[0].lower()
    rest = win_path[3:].replace("\\", "/")
    return Path(f"/mnt/{drive}/{rest}").exists()


def _expand_win_env(win_path: str) -> str:
    """Expand %LOCALAPPDATA% / %USERPROFILE% style placeholders using
    values we can read from /mnt/c.  WSL2's os.environ doesn't carry
    Windows env vars, so we reconstruct the common ones from $HOME's
    /mnt/c parent directory."""
    if "%LOCALAPPDATA%" in win_path:
        home = os.environ.get("HOME", "")
        if "/mnt/c/Users/" not in home:
            # try /mnt/c/Users/<basename of $HOME>
            user = os.path.basename(home) or os.environ.get("USER", "")
            la = f"C:\\Users\\{user}\\AppData\\Local"
        else:
            user = home.split("/mnt/c/Users/", 1)[1].split("/", 1)[0]
            la = f"C:\\Users\\{user}\\AppData\\Local"
        win_path = win_path.replace("%LOCALAPPDATA%", la)
    return win_path


def _probe_one_root(root_win: str, names: list[str]) -> Optional[str]:
    """Look for the first matching filename under one root.  Returns
    a Windows-style path or None."""
    root_win = _expand_win_env(root_win)
    if not _exists_win(root_win):
        return None
    drive = root_win[0].lower()
    rest = root_win[3:].replace("\\", "/")
    base = Path(f"/mnt/{drive}/{rest}")
    # First check direct children (the AppX shim case — fast).
    for name in names:
        direct = base / name
        if direct.exists():
            sub = str(direct).split("/mnt/", 1)[1][2:].replace("/", "\\")
            return f"{drive.upper()}:\\{sub}"
    # Then recurse.  Permission-locked branches are skipped silently.
    for name in names:
        try:
            hits = list(base.rglob(name))
        except (PermissionError, OSError):
            continue
        if hits:
            wsl_p = str(hits[0])
            drive_c = wsl_p[5]
            sub = wsl_p[7:].replace("/", "\\")
            return f"{drive_c.upper()}:\\{sub}"
    return None


def _probe(names: list[str]) -> Optional[str]:
    """Walk _PROBE_ROOTS_WIN looking for the first matching filename.
    Returns the first Windows-style path found, or None."""
    for root in _PROBE_ROOTS_WIN:
        hit = _probe_one_root(root, names)
        if hit:
            return hit
    return None


def discover() -> dict:
    """Discover ttd.exe + cdb.exe (32-bit preferred).  Returns
    a structured result.  Never raises."""
    out: dict = {"status": "ok"}

    ttd = os.environ.get("OPENRECET_TTD_EXE")
    cdb = os.environ.get("OPENRECET_CDB_EXE")

    try:
        if not ttd:
            ttd = _probe(_TTD_NAMES)
        if not cdb:
            # i686 retail → cdbX86 shim first, then plain cdb.exe (which
            # is the classic SDK headless debugger that autodetects from
            # the trace), then cdbX64 as a last resort.
            cdb = _probe(_CDB_NAMES) or _probe(_CDB_X64_NAMES)
    except Exception as e:
        return {"status": "failed", "stage": "probe",
                "error_class": type(e).__name__}

    if not ttd:
        return {"status": "failed", "stage": "ttd_missing",
                "hint": "set OPENRECET_TTD_EXE to its absolute Windows path"}
    if not cdb:
        return {"status": "failed", "stage": "cdb_missing",
                "hint": "set OPENRECET_CDB_EXE to its absolute Windows path"}

    out["ttd_exe"] = ttd
    out["cdb_exe"] = cdb
    return out


if __name__ == "__main__":
    import json
    import sys
    r = discover()
    print(json.dumps(r))
    sys.exit(0 if r["status"] == "ok" else 1)
