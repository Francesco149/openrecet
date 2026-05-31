#!/usr/bin/env python3
"""
tools/feed_push_run.py — push a capture run's frames (+ any diff PNGs) to the
llm-feed (localhost:8777), so every test / input-trace run shows up there
automatically as it happens.

This is the worker behind the auto-push hook (see .claude/settings.json
PostToolUse): the hook spots a `--capture-to <dir>` Bash command and calls this
on <dir> once the command finishes.  It is also safe to run by hand.

Design:
  - Best-effort.  If the feed is down (no /healthz), this is a SILENT no-op so
    it can never break a run or a hook.
  - Idempotent.  A `.feed_pushed` marker in the run dir records a fingerprint of
    the pushed frame/diff set (names + sizes + mtimes).  Re-running on the same,
    unchanged dir does nothing — so the hook firing plus an accidental manual
    call won't double-push.  Pass --force to override.
  - Frames are montaged (flip-through) via `feed.py montage`.

NOTE: this pushes a target's own frame captures, NOT a retail-vs-port diff.
The amplified pixel `comparison` (click-to-reveal) is a separate thing, built
from a deterministic `--target both` run and pushed by tools/push_comparison.py
(auto-invoked at the end of `scenario-test.py --target both`).

Usage:
    feed_push_run.py <run-dir> [--note N] [--title T] [--label L] [--force]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

FEED_PY = Path(os.environ.get("LLM_FEED_PY", "/opt/src/llm-feed/feed.py"))
FEED_PORT = int(os.environ.get("LLM_FEED_PORT", "8777"))
MARKER = ".feed_pushed"


def feed_up() -> bool:
    try:
        with urllib.request.urlopen(
            f"http://localhost:{FEED_PORT}/healthz", timeout=2
        ) as r:
            return r.status == 200
    except Exception:
        return False


def find_frames_dir(run_dir: Path) -> Path:
    """Prefer <run>/frames if it holds frame BMPs, else <run> itself."""
    sub = run_dir / "frames"
    if sub.is_dir() and any(sub.glob("frame_*.bmp")):
        return sub
    return run_dir


def fingerprint(paths: list[Path]) -> str:
    h = hashlib.sha1()
    for p in sorted(paths):
        try:
            st = p.stat()
            h.update(p.name.encode())
            h.update(str(st.st_size).encode())
            h.update(str(st.st_mtime_ns).encode())
        except OSError:
            continue
    return h.hexdigest()


def run_feed(*args: str) -> int:
    cmd = [sys.executable, str(FEED_PY), *args]
    try:
        return subprocess.call(cmd)
    except Exception as e:  # noqa: BLE001
        print(f"feed_push_run: feed call failed: {e}", file=sys.stderr)
        return 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--note", default="")
    ap.add_argument("--title", default="")
    ap.add_argument("--label", default="", help="extra tag prepended to the title")
    ap.add_argument("--force", action="store_true",
                    help="push even if the fingerprint is unchanged")
    args = ap.parse_args(argv)

    run_dir = args.run_dir
    if not run_dir.is_dir():
        # Not an error worth failing a hook over — the dir may simply not exist.
        return 0

    if not FEED_PY.is_file():
        return 0  # feed not installed here; silent no-op
    if not feed_up():
        return 0  # feed down; silent no-op (best-effort)

    frames_dir = find_frames_dir(run_dir)
    frames = sorted(frames_dir.glob("frame_*.bmp"))
    if not frames:
        return 0  # nothing capture-like here

    fp = fingerprint(frames)
    marker = run_dir / MARKER
    if not args.force and marker.is_file():
        try:
            if marker.read_text().strip() == fp:
                return 0  # already pushed this exact set
        except OSError:
            pass

    tag = (args.label + " ") if args.label else ""
    title = args.title or f"{tag}{run_dir.name}"
    note = args.note or f"auto-push from {run_dir} ({len(frames)} frames)"

    rc = run_feed(
        "montage",
        "--frames-dir", str(frames_dir),
        "--glob", "frame_*.bmp",
        "--title", title,
        "--note", note,
    )

    # Record the fingerprint even on partial failure so we don't spin re-pushing
    # a flaky frame; --force is the escape hatch.
    try:
        marker.write_text(fp)
    except OSError:
        pass
    return 0 if rc == 0 else 0  # never propagate a non-zero to a hook


if __name__ == "__main__":
    raise SystemExit(main())
