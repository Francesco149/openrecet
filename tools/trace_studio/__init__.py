"""trace_studio — the human-facing TAS trace iteration studio (v2 package).

Record-tweak-verify a long trace until it plays end-to-end 1:1 on BOTH the port
and retail. The CLI entry point is the thin launcher `tools/trace_studio.py`
(kept for the documented `python3 tools/trace_studio.py {capture,serve,apply}`
command); all logic lives here.

Layout (see docs/plans/trace-studio-v2.md):
  model/      ops, segments (align core), timeline, session  — PURE, unit-tested
  drive/      caps, port, retail, runner                      — wrap export_trace / frida_capture
  transport/  convert, encode                                 — frame conversion + video encode
  analysis/   pixeldiff, verdict, state                       — diff / phase-RNG verdict / state stream
  edits/      apply                                           — marks → pins + worklist
  record/     recover, controller                             — retail recorder + capture subprocess
  server/     ranged, app                                     — local http viewer + HTTP-Range
  cli         argparse → orchestrator
"""

__all__ = ["model", "drive", "transport", "analysis", "edits"]
