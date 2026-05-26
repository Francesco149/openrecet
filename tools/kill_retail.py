#!/usr/bin/env python3
"""
tools/kill_retail.py — force-kill stray recettear.unpacked.exe processes.

When a Frida-driven test (frida_capture.py, bisect_call_trace_vas.py,
etc.) is killed mid-run, its `device.kill(pid)` shutdown never runs and
the engine survives.  These instances are usually elevated and invisible
to non-admin taskkill / Stop-Process — but `device.kill(pid)` from any
fresh Frida-server connection works even cross-handle.

Usage:
    nix develop --command python3 tools/kill_retail.py
    nix develop --command python3 tools/kill_retail.py --remote host:port

Exit codes:
    0  killed N (N may be 0)
    1  Frida-server unreachable or tasklist unavailable
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys


DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")
DEFAULT_IMAGE  = "recettear.unpacked.exe"


def list_pids_by_name(image: str) -> set[int]:
    """PIDs of `image` (case-insensitive name match) via Windows tasklist."""
    try:
        r = subprocess.run(
            ["/mnt/c/Windows/system32/tasklist.exe",
             "/fi", f"imagename eq {image}",
             "/fo", "csv", "/nh"],
            capture_output=True, text=True, timeout=10)
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"error: tasklist failed: {e}", file=sys.stderr)
        return set()
    pids: set[int] = set()
    for line in r.stdout.splitlines():
        parts = [p.strip('"') for p in line.split(",")]
        if len(parts) >= 2:
            try:
                pids.add(int(parts[1]))
            except ValueError:
                pass
    return pids


def frida_device(remote: str):
    import frida
    dm = frida.get_device_manager()
    try:
        return dm.add_remote_device(remote)
    except frida.InvalidArgumentError:
        return dm.get_device(remote)


def kill_pids(pids: set[int], remote: str) -> tuple[int, list[tuple[int, str]]]:
    """Returns (n_killed, [(pid, error_msg), ...])."""
    if not pids:
        return 0, []
    dev = frida_device(remote)
    killed = 0
    errors: list[tuple[int, str]] = []
    for pid in sorted(pids):
        try:
            dev.kill(pid)
            killed += 1
        except Exception as e:
            errors.append((pid, str(e)))
    return killed, errors


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
        help="frida-server host:port (default %(default)s)")
    ap.add_argument("--image", default=DEFAULT_IMAGE,
        help="image name to kill (default %(default)s)")
    ap.add_argument("--quiet", action="store_true",
        help="suppress output unless an error occurs")
    args = ap.parse_args(argv)

    pids = list_pids_by_name(args.image)
    if not pids:
        if not args.quiet:
            print(f"no {args.image} processes running")
        return 0

    if not args.quiet:
        print(f"found {len(pids)} {args.image} process(es): {sorted(pids)}")

    killed, errors = kill_pids(pids, args.remote)
    if errors:
        for pid, msg in errors:
            print(f"  kill({pid}) failed: {msg}", file=sys.stderr)
    if not args.quiet:
        print(f"killed {killed}/{len(pids)}")
    return 0 if killed == len(pids) else 1


if __name__ == "__main__":
    sys.exit(main())
