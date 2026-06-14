#!/usr/bin/env bash
# open_studio.sh — open the native v3 viewer on the CURRENT working trace.
#
# This is what the desktop / Start-Menu "OpenRecet Trace Studio" shortcut runs
# (via wsl.exe). It reads the `.studio_current` pointer that orv3_window.py
# rewrites on every window build, so it always opens the latest trace we drove —
# no manual updating. See CLAUDE.md "Trace Studio shortcut".
#
# Pointer format (.studio_current, next to this script):
#   line 1: the WSL path to the window's view.json
#   line 2: a human label (scenario + anchor+window)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PTR="$HERE/.studio_current"
VIEWER="$HERE/viewer/viewer.exe"

die() { printf 'OpenRecet Trace Studio: %s\n' "$1" >&2; sleep 4; exit 1; }

[[ -f "$VIEWER" ]] || die "viewer.exe not built — run: nix develop --command make -C tools/trace_studio_v3/viewer"
[[ -f "$PTR" ]]    || die "no working trace yet. Drive one first, e.g.:
  nix develop --command python3 tools/trace_studio_v3/orv3_window.py <scenario> --window OFF:COUNT --anchor <ANCHOR> --launch
(orv3_window writes the pointer this shortcut reads.)"

VIEW_WSL="$(sed -n '1p' "$PTR")"
LABEL="$(sed -n '2p' "$PTR")"
[[ -n "$VIEW_WSL" && -f "$VIEW_WSL" ]] || die "the pointed-at view.json is gone ($VIEW_WSL). Re-drive the window with orv3_window --launch."

VIEW_WIN="$(wslpath -w "$VIEW_WSL")"
printf 'OpenRecet Trace Studio → %s\n  %s\n' "${LABEL:-?}" "$VIEW_WIN"

# Launch the Windows GUI viewer fully detached so it outlives this shell + wsl.exe.
setsid "$VIEWER" "$VIEW_WIN" >/dev/null 2>&1 < /dev/null &
disown || true
