#!/usr/bin/env python3
"""test_trace_studio_cancel.py — the JobTray ✕ (POST /capture/cancel).

Guards `_argv_is_capture`, the cmdline predicate `_orphan_capture_pgids()` uses to
reap a capture left running by a prior `serve` instance (it's detached, so a server
restart can't re-track it). The load-bearing invariant: it matches `capture`
(and the `drill`/`recapture` subprocesses, which run as `capture`) but NEVER the
`serve` process — else cancel would kill the studio server itself.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.record.controller import (  # noqa: E402
    _argv_is_capture, _orphan_capture_pgids,
)

PY = "/nix/store/x/bin/python3"
TS = "/opt/src/openrecet/tools/trace_studio.py"


def main() -> int:
    # a real capture invocation (what CaptureController.start spawns)
    assert _argv_is_capture(
        [PY, TS, "capture", "runs/x.raw.jsonl", "--session", "x",
         "--target", "both", "--remote", "h:1", "--call-trace"]), "capture must match"

    # drill + recapture both run as `capture` subprocesses → must match
    assert _argv_is_capture(
        [PY, TS, "capture", "edit.jsonl", "--session", "x",
         "--capstride", "1", "--reset-trace"]), "drill (capture subproc) must match"

    # the server process must NEVER match (else cancel kills the studio)
    assert not _argv_is_capture(
        [PY, TS, "serve", "--session", "x", "--port", "8779"]), "serve must NOT match"

    # other subcommands / unrelated processes
    assert not _argv_is_capture([PY, TS, "apply", "x"]), "apply must not match"
    assert not _argv_is_capture([PY, TS]), "bare launcher must not match"
    assert not _argv_is_capture([PY, "-m", "pytest"]), "unrelated python must not match"
    assert not _argv_is_capture([]), "empty argv must not match"
    # `capture` as a trace-name argument, not the subcommand, must not match
    assert not _argv_is_capture(
        [PY, TS, "serve", "capture"]), "capture as arg (not subcmd) must not match"

    # the live scanner must run cleanly and return a sorted list of ints
    pg = _orphan_capture_pgids()
    assert isinstance(pg, list) and all(isinstance(x, int) for x in pg), "pgids list"
    assert pg == sorted(pg), "pgids sorted"

    print("OK: trace_studio cancel (capture/drill/recapture match; serve never; "
          "live /proc scan clean)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
