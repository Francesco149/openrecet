#!/usr/bin/env bash
# record-trace.sh — attach to the running (Steam-launched) retail game via the
# default Frida server and record a TAS trace by hand. The convenience wrapper
# for the frida_capture --record-trace incantation (see docs/trace-workflow.md
# workflow B / the retail recorder). The trace_studio "record" panel drives the
# same command from the browser.
#
# Usage:
#   ./tools/record-trace.sh [name]
#     1. Launch Recettear via Steam, get to the TITLE screen FIRST.
#     2. Run this. It attaches and starts observing your inputs.
#     3. Play (new game → ESC-then-Z to skip dialogues → walk → interact …).
#     4. Ctrl-C (or close the game) to finish — the trace is written to
#        runs/recordings/<name>.raw.jsonl. Distil it with
#        `tools/distill_trace.py <raw> --anchor-segments` then feed it to
#        `tools/trace_studio.py capture`.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Re-exec inside the dev shell if we're not already in it (frida lives there).
if [ -z "${IN_NIX_SHELL:-}" ]; then
    exec nix develop "$ROOT" --command bash "${BASH_SOURCE[0]}" "$@"
fi

NAME="${1:-rec-$(date +%Y%m%d-%H%M%S)}"
REMOTE="${OPENRECET_FRIDA_REMOTE:-cutestation.soy:27042}"
OUT="$ROOT/runs/recordings/$NAME.raw.jsonl"
RUNDIR="$ROOT/runs/recordings/_rt_$NAME"
mkdir -p "$ROOT/runs/recordings"

echo "record-trace: attaching to retail @ $REMOTE"
echo "record-trace: trace → $OUT"
echo "record-trace: play the game by hand; Ctrl-C (or close Recettear) to finish."
exec python3 "$ROOT/tools/frida_capture.py" --remote "$REMOTE" \
    --record-trace "$OUT" --run-dir "$RUNDIR"
