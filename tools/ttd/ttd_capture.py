"""Record a trace.  All subprocess stdout/stderr → log file.

Output to caller: a single line of JSON.  Never echoes verbose tool
banter to its own stdout.  Schema:

    {"status": "ok", "trace_path": "...", "size_mb": N, "elapsed_s": N,
     "log_path": "..."}
    {"status": "failed", "stage": "<short label>", "log_path": "..."}

Stages on failure (caller-actionable abstract labels):
    paths_discover    binary not found / env not set
    run_dir_create    couldn't mkdir the output dir
    record_spawn      driver couldn't start the recorder process
    record_kill       couldn't terminate the target after the wall clock
    record_finalize   recorder didn't exit cleanly after target kill
    no_trace_output   recorder exited but the .run file isn't on disk

The log file at log_path is yours to inspect manually — the harness
itself never reads it back, so nothing from it surfaces to a
downstream consumer.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
from pathlib import Path

# allow `python tools/ttd/ttd_capture.py …` without installing as a package
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ttd_paths   # noqa: E402


ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_TARGET = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
DEFAULT_ASSET_CWD = ROOT / "vendor" / "original"
ELEVATED_PS1 = Path(__file__).resolve().parent / "_run_elevated.ps1"
POWERSHELL_EXE = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"


def _wslpath_w(p: Path) -> str:
    return subprocess.run(
        ["wslpath", "-w", str(p)],
        check=True, capture_output=True, text=True).stdout.strip()


def _wslpath_u(win_path: str) -> str:
    """Translate a Windows-style path back to its WSL mount path so
    Python's subprocess.Popen can find the binary on the Linux side.
    Popen on WSL2 won't resolve `C:\\…\\foo.exe` directly — it needs
    `/mnt/c/…/foo.exe`."""
    return subprocess.run(
        ["wslpath", "-u", win_path],
        check=True, capture_output=True, text=True).stdout.strip()


def _fail(stage: str, log_path: Path | None = None, **extra) -> int:
    out = {"status": "failed", "stage": stage}
    if log_path is not None:
        out["log_path"] = str(log_path)
    out.update(extra)
    print(json.dumps(out))
    return 1


def _ok(**kw) -> int:
    out = {"status": "ok"}
    out.update(kw)
    print(json.dumps(out))
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default=str(DEFAULT_TARGET),
        help="WSL path to the .exe to record (default: %(default)s)")
    ap.add_argument("--target-args", default="",
        help="extra argv for the target, space-separated")
    ap.add_argument("--cwd", default=str(DEFAULT_ASSET_CWD),
        help="WSL path the target runs in (default: %(default)s)")
    ap.add_argument("--run-dir", default=None,
        help="WSL path where trace.run + cdb.log land. "
             "Default: runs/ttd-<scenario>-<ts>/")
    ap.add_argument("--scenario", default="adhoc",
        help="label baked into the default run-dir name")
    ap.add_argument("--wall-s", type=float, default=4.0,
        help="seconds to let the target run before killing it (default 4)")
    ap.add_argument("--finalize-timeout-s", type=float, default=120.0,
        help="seconds to wait for the recorder to finalize after the "
             "target is killed (default 120; finalization for a 5s "
             "recording is usually <30s but can spike)")
    args = ap.parse_args(argv)

    # --- binaries ---
    paths = ttd_paths.discover()
    if paths["status"] != "ok":
        return _fail("paths_discover", **{k: v for k, v in paths.items()
                                          if k != "status"})

    ttd_exe_win = paths["ttd_exe"]

    # --- run dir ---
    if args.run_dir:
        run_dir = Path(args.run_dir)
    else:
        ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        run_dir = ROOT / "runs" / f"ttd-{args.scenario}-{ts}"

    try:
        run_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        return _fail("run_dir_create", error_class=type(e).__name__)

    log_path = run_dir / "cdb.log"
    trace_path = run_dir / "trace.run"
    status_path = run_dir / "elev_status.json"

    # --- target check ---
    target_p = Path(args.target).resolve()
    if not target_p.exists():
        return _fail("target_missing", log_path=log_path,
                     looked_for=str(target_p))

    cwd_p = Path(args.cwd).resolve()
    if not cwd_p.exists():
        return _fail("cwd_missing", log_path=log_path,
                     looked_for=str(cwd_p))

    try:
        trace_win = _wslpath_w(trace_path)
        target_win = _wslpath_w(target_p)
        cwd_win = _wslpath_w(cwd_p)
        log_win = _wslpath_w(log_path)
        status_win = _wslpath_w(status_path)
        elevated_ps1_win = _wslpath_w(ELEVATED_PS1)
    except Exception as e:
        return _fail("wslpath", log_path=log_path,
                     error_class=type(e).__name__)

    # Inner PowerShell argv — the script that actually does the
    # recording.  PowerShell parses this when the outer Start-Process
    # spawns the elevated child, so it's a single string.
    inner_args = (
        f"-NoProfile -ExecutionPolicy Bypass -File \"{elevated_ps1_win}\" "
        f"-TtdExe \"{ttd_exe_win}\" "
        f"-OutPath \"{trace_win}\" "
        f"-TargetExe \"{target_win}\" "
        f"-WallSec {args.wall_s} "
        f"-LogPath \"{log_win}\" "
        f"-StatusPath \"{status_win}\" "
        f"-TargetCwd \"{cwd_win}\""
    )

    # Outer (non-elevated) PowerShell that spawns the inner one with
    # -Verb RunAs.  -Wait blocks until the elevated process exits;
    # -PassThru lets us harvest its exit code.
    elevation_cmd_str = (
        f"$p = Start-Process -FilePath powershell.exe "
        f"-ArgumentList '{inner_args}' "
        f"-Verb RunAs -Wait -PassThru; "
        f"exit $p.ExitCode"
    )
    outer_cmd = [
        POWERSHELL_EXE, "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-Command", elevation_cmd_str,
    ]

    # Pre-write the log header so a totally-failed elevation still
    # leaves something on disk for manual inspection.
    log_path.write_text(
        f"# inner_argv: {inner_args}\n"
        f"# elevation_cmd: {elevation_cmd_str}\n"
        f"# target: {target_win}\n"
        f"# wall_s: {args.wall_s}\n\n"
    )

    t0 = time.monotonic()
    try:
        # No stdout/stderr capture on the outer PS — the elevated
        # child appends to log_path on its own.  We only care about
        # the outer PS exit code + the status JSON the child writes.
        r = subprocess.run(
            outer_cmd,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            timeout=args.wall_s + args.finalize_timeout_s + 60)
    except subprocess.TimeoutExpired:
        return _fail("record_timeout", log_path=log_path)
    except Exception as e:
        return _fail("record_spawn", log_path=log_path,
                     error_class=type(e).__name__)

    elapsed_s = round(time.monotonic() - t0, 2)

    if r.returncode != 0:
        return _fail("elevation_failed", log_path=log_path,
                     outer_exit=r.returncode, elapsed_s=elapsed_s)

    # Read the status file the elevated child wrote.  Missing file →
    # UAC was cancelled or the script crashed before its first write.
    if not status_path.exists():
        return _fail("elevation_no_status", log_path=log_path,
                     elapsed_s=elapsed_s)
    try:
        elev_status = json.loads(status_path.read_text())
    except Exception as e:
        return _fail("elevation_status_parse", log_path=log_path,
                     error_class=type(e).__name__)
    if elev_status.get("status") != "ok":
        return _fail("elevation_child_failed", log_path=log_path,
                     child_stage=elev_status.get("stage", "?"),
                     elapsed_s=elapsed_s)

    if not trace_path.exists() or trace_path.stat().st_size == 0:
        return _fail("no_trace_output", log_path=log_path,
                     elapsed_s=elapsed_s,
                     ttd_exit=elev_status.get("ttd_exit"))

    return _ok(
        trace_path=str(trace_path),
        size_mb=round(trace_path.stat().st_size / (1024 * 1024), 1),
        elapsed_s=elapsed_s,
        log_path=str(log_path),
        ttd_exit=elev_status.get("ttd_exit", 0))


if __name__ == "__main__":
    sys.exit(main())
