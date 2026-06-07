"""cli.py — thin argparse → orchestrator for the trace_studio package.

  capture   drive port(+retail) → aligned scrub videos + a v2 segmented session
  serve     local http viewer (scrub videos in lockstep, mark/apply loop)
  apply     turn viewer marks (edits.jsonl) into trace pins + a worklist

The user-facing entry point is the launcher tools/trace_studio.py (so the documented
`python3 tools/trace_studio.py …` command + the server's subprocess spawn keep
working); it just calls main() here.
"""
from __future__ import annotations

import argparse
import sys

from .capture import CaptureConfig, run_capture
from .paths import DEFAULT_REMOTE, SESS_ROOT, WEB_DIR


def cmd_capture(args) -> int:
    return run_capture(CaptureConfig(
        trace=args.trace, session=args.session, target=args.target,
        call_trace=args.call_trace, amp=args.amp, caprange=args.caprange,
        port_max_frames=args.port_max_frames, retail_max_frames=args.retail_max_frames,
        remote=args.remote, prune_frames=args.prune_frames,
        reset_trace=args.reset_trace, only=args.only, anchors=args.anchors,
        suppress_loads=args.suppress_loads, capture_local=args.capture_local))


def cmd_serve(args) -> int:
    # server/edits are lifted into the package in the next step; until then use the
    # still-present flat modules so `serve`/`apply` keep working at every commit.
    from trace_studio_serve import serve
    sess_dir = SESS_ROOT / args.session if args.session else None
    if sess_dir and not sess_dir.exists():
        raise SystemExit(f"trace_studio: no session {args.session} under {SESS_ROOT}")
    serve(SESS_ROOT, WEB_DIR, host=args.host, port=args.port,
          default_session=args.session, remote=args.remote)
    return 0


def cmd_apply(args) -> int:
    from pathlib import Path

    from trace_studio_apply import apply
    sess_dir = SESS_ROOT / args.session
    if not sess_dir.exists():
        raise SystemExit(f"trace_studio: no session {args.session}")
    apply(sess_dir, trace_override=Path(args.trace) if args.trace else None,
          auto_pin=args.auto_pin, dry_run=args.dry_run)
    return 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="trace_studio",
        description="Record-tweak-verify a TAS trace until it plays 1:1 on port + retail.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="drive port+retail → aligned scrub videos")
    c.add_argument("trace", help="trace.jsonl path OR a scenario name")
    c.add_argument("--caprange", help="START,COUNT (default: from the trace)")
    c.add_argument("--target", choices=("both", "openrecet"), default="both")
    c.add_argument("--session", help="session name (default: <trace>-<ts>)")
    c.add_argument("--call-trace", action="store_true",
                   help="capture the flow-trace + store the phase/RNG verdict "
                        "(needs a {calltrace} op in the trace)")
    c.add_argument("--amp", type=float, default=6.0, help="white-diff amplification")
    c.add_argument("--port-max-frames", type=int, default=4000)
    c.add_argument("--retail-max-frames", type=int, default=22000,
                   help="retail turbo load-stretches anchors late; ≥22000 for HOUSE")
    c.add_argument("--remote", default=DEFAULT_REMOTE)
    c.add_argument("--prune-frames", action="store_true",
                   help="drop bulk PNGs after encoding (long traces)")
    c.add_argument("--reset-trace", action="store_true",
                   help="rebuild the session's working trace from the source "
                        "(discards applied pins)")
    c.add_argument("--only", choices=("both", "port"), default="both",
                   help="'port' re-runs only the port and REUSES the cached retail "
                        "capture (the fast port-fixing loop); 'both' is a full capture")
    c.add_argument("--anchors", action="store_true",
                   help="distil a raw recording with every recorded anchor as a {wait} "
                        "sync point (default: FLAT — boot-synced, captured from frame 0)")
    c.add_argument("--suppress-loads", action=argparse.BooleanOptionalAction,
                   default=True,
                   help="D1: drop captures during loads so the turbo load span "
                        "collapses to a zero-frame seam (default on; --no-suppress-loads "
                        "to capture load frames). Auto-degrades on a pre-D1 exe.")
    c.add_argument("--capture-local", action="store_true",
                   help="D2: exe-side local-disk capture (degrades if the exe lacks it)")
    c.set_defaults(func=cmd_capture)

    s = sub.add_parser("serve", help="open the scrubbing editor")
    s.add_argument("--session", help="session to open by default")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=8778)
    s.add_argument("--remote", default=DEFAULT_REMOTE,
                   help="frida host:port the record panel attaches to")
    s.set_defaults(func=cmd_serve)

    a = sub.add_parser("apply", help="apply edits.jsonl pins + emit worklist")
    a.add_argument("session")
    a.add_argument("--trace", help="trace to edit (default: the session's trace)")
    a.add_argument("--auto-pin", action="store_true",
                   help="also propose pins from the stored verdict")
    a.add_argument("--dry-run", action="store_true")
    a.set_defaults(func=cmd_apply)
    return ap


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
