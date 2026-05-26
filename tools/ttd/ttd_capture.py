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


def _wslpath_w(p: Path) -> str:
    return subprocess.run(
        ["wslpath", "-w", str(p)],
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

    ttd_exe = paths["ttd_exe"]

    # --- run dir ---
    if args.run_dir:
        run_dir = Path(args.run_dir)
    else:
        ts = datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")
        run_dir = ROOT / "runs" / f"ttd-{args.scenario}-{ts}"

    try:
        run_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        return _fail("run_dir_create", error_class=type(e).__name__)

    log_path = run_dir / "cdb.log"
    trace_path = run_dir / "trace.run"

    # --- recorder argv ---
    target_p = Path(args.target).resolve()
    cwd_p = Path(args.cwd).resolve()
    if not target_p.exists():
        return _fail("target_missing", log_path=log_path,
                     looked_for=str(target_p))

    try:
        trace_win = _wslpath_w(trace_path)
        target_win = _wslpath_w(target_p)
        cwd_win = _wslpath_w(cwd_p)
    except Exception as e:
        return _fail("wslpath", log_path=log_path,
                     error_class=type(e).__name__)

    cmd = [ttd_exe, "-out", trace_win, target_win]
    if args.target_args.strip():
        cmd += args.target_args.split()

    # --- record ---
    t0 = time.monotonic()
    with log_path.open("w") as logf:
        logf.write(f"# argv: {cmd}\n# cwd: {cwd_win}\n# wall_s: {args.wall_s}\n\n")
        logf.flush()
        try:
            proc = subprocess.Popen(
                cmd, cwd="/mnt/c",  # cdb-style invocation; cwd controls
                                    #   nothing the recorder cares about
                stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        except Exception as e:
            return _fail("record_spawn", log_path=log_path,
                         error_class=type(e).__name__)

        # let the target run, then kill it.  The recorder finalizes
        # the trace on target exit.
        target_basename = os.path.basename(args.target)
        try:
            time.sleep(args.wall_s)
        except KeyboardInterrupt:
            pass

        try:
            subprocess.run(
                ["/mnt/c/Windows/System32/taskkill.exe", "/F", "/IM",
                 target_basename],
                stdout=logf, stderr=subprocess.STDOUT, check=False, timeout=15)
        except Exception as e:
            return _fail("record_kill", log_path=log_path,
                         error_class=type(e).__name__)

        # wait for the recorder process itself to finalize the trace
        try:
            proc.wait(timeout=args.finalize_timeout_s)
        except subprocess.TimeoutExpired:
            proc.kill()
            return _fail("record_finalize", log_path=log_path)
        except Exception as e:
            return _fail("record_finalize", log_path=log_path,
                         error_class=type(e).__name__)

    elapsed_s = round(time.monotonic() - t0, 2)

    if not trace_path.exists() or trace_path.stat().st_size == 0:
        return _fail("no_trace_output", log_path=log_path,
                     elapsed_s=elapsed_s)

    return _ok(
        trace_path=str(trace_path),
        size_mb=round(trace_path.stat().st_size / (1024 * 1024), 1),
        elapsed_s=elapsed_s,
        log_path=str(log_path))


if __name__ == "__main__":
    sys.exit(main())
