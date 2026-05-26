"""Run a JS query script against a recorded trace.

All cdb stdout/stderr → log file.  Caller sees only structured JSON
on the harness's own stdout.  Schema:

    {"status": "ok", "output_path": "...", "n_records": N,
     "elapsed_s": N, "log_path": "..."}
    {"status": "failed", "stage": "<short label>", "log_path": "..."}

Stages on failure:
    paths_discover    binary not found / env not set
    trace_missing     given trace path doesn't exist or is empty
    script_missing    given .js script doesn't exist
    cdb_spawn         couldn't start the debugger
    cdb_exit_nonzero  debugger exited with non-zero (consult log)
    no_output_file    debugger exited fine but the .js script didn't
                        write the expected output JSON
    output_parse      output JSON exists but isn't valid JSON

The .js script is invoked through a generated wrapper that pins the
output path at load time.  Scripts live under tools/ttd/scripts/.
Each script must define `invokeScript()` and write a single top-level
JSON array (or object containing a top-level "records": [...] key)
to the output file via the wrapper-supplied path.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ttd_paths   # noqa: E402


ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = Path(__file__).resolve().parent / "scripts"


def _wslpath_w(p: Path) -> str:
    return subprocess.run(
        ["wslpath", "-w", str(p)],
        check=True, capture_output=True, text=True).stdout.strip()


def _wslpath_u(win_path: str) -> str:
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


def _generate_wrapper(script_win: str, output_win: str,
                      extra_globals: dict) -> str:
    """Build the JS wrapper that pins per-invocation globals before
    sourcing the user script."""
    lines = ["// auto-generated; pins per-invocation globals"]
    lines.append(f"var TTD_OUTPUT_PATH = {json.dumps(output_win)};")
    for k, v in extra_globals.items():
        lines.append(f"var {k} = {json.dumps(v)};")
    # the .scriptload directive doesn't chain in a JS-source sense; we
    # rely on the test runner to .scriptload the wrapper alone, and the
    # wrapper-resident invokeScript() does its own host.namespace lookups
    # before delegating.  For now embed the user script literally.
    user_src = Path(script_win.replace("\\", "/").split(":", 1)[-1]).read_text() \
        if ":" in script_win else ""
    return "\n".join(lines) + "\n\n" + user_src


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--trace", required=True,
        help="WSL path to the trace.run file produced by ttd_capture.py")
    ap.add_argument("--script", required=True,
        help="JS script name under tools/ttd/scripts/, e.g. frame_calls.js")
    ap.add_argument("--output", default=None,
        help="WSL path for the script's output JSON. Default: "
             "<trace_dir>/<script_basename>.json")
    ap.add_argument("--extra-global",
        action="append", default=[], metavar="KEY=VALUE",
        help="pin an additional JS global before script load. "
             "Repeatable. Values are JSON-encoded; strings need quotes. "
             "For large values, see --extra-global-file.")
    ap.add_argument("--extra-global-file",
        action="append", default=[], metavar="KEY=PATH",
        help="like --extra-global but reads VALUE from a JSON file. "
             "Avoids shell-quoting pain for large arrays. Repeatable.")
    ap.add_argument("--timeout-s", type=float, default=600.0,
        help="seconds to wait for cdb to finish (default 600)")
    args = ap.parse_args(argv)

    paths = ttd_paths.discover()
    if paths["status"] != "ok":
        return _fail("paths_discover", **{k: v for k, v in paths.items()
                                          if k != "status"})
    cdb_exe_win = paths["cdb_exe"]
    try:
        cdb_exe_wsl = _wslpath_u(cdb_exe_win)
    except Exception as e:
        return _fail("wslpath", error_class=type(e).__name__)

    trace_p = Path(args.trace).resolve()
    if not trace_p.exists() or trace_p.stat().st_size == 0:
        return _fail("trace_missing", looked_for=str(trace_p))

    script_p = (SCRIPTS_DIR / args.script).resolve()
    if not script_p.exists():
        return _fail("script_missing", looked_for=str(script_p))

    run_dir = trace_p.parent
    log_path = run_dir / f"cdb-{script_p.stem}.log"
    output_p = Path(args.output).resolve() if args.output else \
        run_dir / f"{script_p.stem}.json"

    # remove stale output so we can reliably detect a fresh write
    try:
        output_p.unlink(missing_ok=True)
    except Exception:
        pass

    # build wrapper
    extras: dict = {}
    for kv in args.extra_global:
        if "=" not in kv:
            return _fail("bad_extra_global", arg=kv)
        k, v = kv.split("=", 1)
        try:
            extras[k.strip()] = json.loads(v)
        except json.JSONDecodeError:
            return _fail("bad_extra_global_value", key=k.strip(),
                         hint="value must be JSON; strings need quotes")
    for kp in args.extra_global_file:
        if "=" not in kp:
            return _fail("bad_extra_global_file", arg=kp)
        k, p = kp.split("=", 1)
        try:
            extras[k.strip()] = json.loads(Path(p.strip()).read_text())
        except FileNotFoundError:
            return _fail("extra_global_file_missing", path=p.strip())
        except json.JSONDecodeError as e:
            return _fail("extra_global_file_parse", path=p.strip(),
                         error_class=type(e).__name__)

    try:
        trace_win = _wslpath_w(trace_p)
        output_win = _wslpath_w(output_p)
    except Exception as e:
        return _fail("wslpath", error_class=type(e).__name__)

    # the wrapper inlines globals + the user script body so a single
    # .scriptload is sufficient.  cdb's scriptload requires Windows paths.
    wrapper_lines = ["// auto-generated wrapper"]
    wrapper_lines.append(f"var TTD_OUTPUT_PATH = {json.dumps(output_win)};")
    for k, v in extras.items():
        wrapper_lines.append(f"var {k} = {json.dumps(v)};")
    wrapper_lines.append("")
    wrapper_lines.append(script_p.read_text())
    wrapper_src = "\n".join(wrapper_lines)

    wrapper_p = run_dir / f"_run_{script_p.stem}.js"
    wrapper_p.write_text(wrapper_src)
    wrapper_win = _wslpath_w(wrapper_p)

    # cdb command: load trace, run script, quit.  Use .scriptrun
    # (which fires the script's invokeScript()) — .scriptload only
    # calls initializeScript() if present, not invokeScript().
    cmd_str = f".scriptrun {wrapper_win};q"
    cmd = [cdb_exe_wsl, "-z", trace_win, "-c", cmd_str]

    t0 = time.monotonic()
    with log_path.open("w") as logf:
        logf.write(f"# argv: {cmd}\n\n")
        logf.flush()
        try:
            r = subprocess.run(
                cmd, stdout=logf, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, timeout=args.timeout_s)
        except subprocess.TimeoutExpired:
            return _fail("cdb_timeout", log_path=log_path)
        except Exception as e:
            return _fail("cdb_spawn", log_path=log_path,
                         error_class=type(e).__name__)

    elapsed_s = round(time.monotonic() - t0, 2)

    if r.returncode != 0:
        return _fail("cdb_exit_nonzero", log_path=log_path,
                     returncode=r.returncode, elapsed_s=elapsed_s)

    if not output_p.exists() or output_p.stat().st_size == 0:
        return _fail("no_output_file", log_path=log_path,
                     elapsed_s=elapsed_s)

    try:
        data = json.loads(output_p.read_text())
    except Exception as e:
        return _fail("output_parse", log_path=log_path,
                     error_class=type(e).__name__, elapsed_s=elapsed_s)

    if isinstance(data, list):
        n_records = len(data)
    elif isinstance(data, dict) and isinstance(data.get("records"), list):
        n_records = len(data["records"])
    else:
        n_records = None

    return _ok(
        output_path=str(output_p),
        n_records=n_records,
        elapsed_s=elapsed_s,
        log_path=str(log_path))


if __name__ == "__main__":
    sys.exit(main())
