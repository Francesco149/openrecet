#!/usr/bin/env bash
# tools/run-openrecet.sh — supervised launcher for ad-hoc bash runs.
#
# Wraps build/openrecet.exe inside build/openrecet-supervisor.exe (a
# Win32 Job Object). When this shell command exits — clean exit,
# timeout, Ctrl+C, terminal closed, anything — the kernel kills the
# openrecet child unconditionally. No more orphan Windows windows; no
# more `taskkill /F /IM` racing other parallel runs.
#
# The supervisor also blocks the terminal until the child exits or
# --timeout-ms fires, so an ad-hoc smoke run can't escape into the
# background by accident.
#
# Usage:
#     tools/run-openrecet.sh [--timeout-ms <N>] [--debug] [openrecet args...]
#
#     --timeout-ms N    hard ceiling enforced by the supervisor
#                       (default 30000 = 30s). 0 = wait forever.
#                       The supervisor's timeout is the safety net;
#                       --max-duration-ms (forwarded to openrecet) is
#                       the in-engine graceful-shutdown path. Tune both.
#     --debug           launch build/openrecet-debug.exe instead of
#                       openrecet.exe (console subsystem, stdio wired).
#     --visible         suppress the default --hidden injection (watch live).
#
# Fast-probe defaults: this wrapper auto-injects --turbo, --silent-audio, and
# --hidden when absent, so ad-hoc runs are quick + quiet and don't pop a
# window (results are surfaced via --capture-to screenshots).  Pass the flag
# explicitly to override, or --visible to keep the window on screen.
#
# Frame capture: pass `--capture-to <dir> --capture-frames a,b,c` (or
# `--capture-every-ms N`).  <dir> may be repo-relative ("runs/foo") or an
# absolute Unix path — this wrapper resolves it against the repo, mkdir -p's
# it, and converts it to the Windows path the exe's fopen() needs (the exe
# runs Windows-side under WSLInterop, so a bare Unix/relative path would
# otherwise resolve against the game asset cwd or fail silently).  Frames land
# as <dir>/frame_NNNNN.bmp (NNNNN = sim-frame index under --capture-frames).
# Convert to PNG with e.g. `magick`/`ffmpeg`; view with eog.
#
# Always cd's to vendor/original/ first so asset paths resolve (see
# memory/feedback_openrecet_run.md). --max-duration-ms is forwarded
# from a default of 3000ms if not present in the argv — historically
# the source of the orphan-window class of bugs.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SUPERVISOR="$ROOT/build/openrecet-supervisor.exe"
ASSET_CWD="$ROOT/vendor/original"

timeout_ms=30000
debug=0

# Pull off our wrapper flags before the openrecet pass-through.
while (( $# )); do
    case "$1" in
        --timeout-ms)
            timeout_ms="$2"; shift 2;;
        --timeout-ms=*)
            timeout_ms="${1#--timeout-ms=}"; shift;;
        --debug)
            debug=1; shift;;
        --help|-h)
            sed -n '3,30p' "$0"
            exit 0;;
        --)
            shift; break;;
        *)
            break;;
    esac
done

exe_name="openrecet.exe"
(( debug )) && exe_name="openrecet-debug.exe"
EXE="$ROOT/build/$exe_name"

if [[ ! -x "$SUPERVISOR" ]]; then
    echo "run-openrecet: missing $SUPERVISOR — build it with:" >&2
    echo "  nix develop --command make -C tools/supervisor" >&2
    exit 1
fi
if [[ ! -x "$EXE" ]]; then
    echo "run-openrecet: missing $EXE — build it with:" >&2
    echo "  nix develop --command make -C src" >&2
    exit 1
fi
if [[ ! -d "$ASSET_CWD" ]]; then
    echo "run-openrecet: missing asset cwd $ASSET_CWD — run tools/setup.sh" >&2
    exit 1
fi

# ── Default-inject fast-probe flags + a deterministic exit bound ─────────
# Scan argv once for what the caller already set.  --max-duration-ms is a
# WALL-CLOCK timer (engine SetTimer), so on --turbo it idles long past the
# capture; --max-frames is a SIM-FRAME bound (PostQuitMessage at frame N) and
# exits the instant the shot is taken, turbo-speed-independent.  So when
# capturing we bound by frame, not wall time.  (See feedback_fast_captures.md.)
want_visible=0; have_max_duration=0; have_max_frames=0; cap_spec=""; prev=""
for a in "$@"; do
    case "$a" in
        --visible)                                  want_visible=1 ;;
        --max-duration-ms|--max-duration-ms=*)      have_max_duration=1 ;;
        --max-frames|--max-frames=*)                have_max_frames=1 ;;
        --capture-frames=*)                         cap_spec="${a#*=}" ;;
    esac
    [[ "$prev" == "--capture-frames" ]] && cap_spec="$a"
    prev="$a"
done
cap_max=0
if [[ -n "$cap_spec" ]]; then
    IFS=',' read -ra _cf <<< "$cap_spec"
    for f in "${_cf[@]}"; do [[ "$f" =~ ^[0-9]+$ ]] && (( f > cap_max )) && cap_max=$f; done
fi

# Strip the wrapper-only --visible token before pass-through.
if (( want_visible )); then
    keep=(); for a in "$@"; do [[ "$a" == "--visible" ]] || keep+=( "$a" ); done
    set -- "${keep[@]}"
fi

# Exit bound: capture runs exit a few sim-frames after the last shot; plain
# smoke runs keep the historic 3s wall default.  Don't add the wall timer when
# capturing — --max-frames governs and --timeout-ms (supervisor) is the net.
if (( cap_max > 0 )); then
    (( have_max_frames )) || set -- "$@" --max-frames $(( cap_max + 8 ))
elif (( ! have_max_duration )); then
    set -- --max-duration-ms 3000 "$@"
fi

# Quiet + quick + windowless unless overridden (each added only if absent).
inject_if_absent() {  # $1 = flag to add when not already present in "$@"
    local flag="$1"; shift
    local a
    for a in "$@"; do [[ "$a" == "$flag" ]] && { printf '%s\0' "$@"; return; }; done
    printf '%s\0' "$flag" "$@"
}
mapfile -d '' -t _aw < <(inject_if_absent --turbo "$@");        set -- "${_aw[@]}"
mapfile -d '' -t _aw < <(inject_if_absent --silent-audio "$@"); set -- "${_aw[@]}"
(( want_visible )) || { mapfile -d '' -t _aw < <(inject_if_absent --hidden "$@"); set -- "${_aw[@]}"; }

# Rewrite the path-valued output flag(s) so a repo-relative or Unix path Just
# Works: resolve against the repo, mkdir -p, and hand the exe the Windows path
# its (Windows-side) fopen() needs.  Without this, --capture-to runs/foo lands
# in the game asset dir, and an absolute /opt/... path fails fopen() silently.
args=()
while (( $# )); do
    case "$1" in
        --capture-to|--house-preview-dump)
            flag="$1"; dir="$2"; shift 2
            [[ "$dir" = /* ]] || dir="$ROOT/$dir"
            mkdir -p "$dir"
            args+=( "$flag" "$(wslpath -w "$dir")" )
            ;;
        --capture-to=*|--house-preview-dump=*)
            flag="${1%%=*}"; dir="${1#*=}"; shift
            [[ "$dir" = /* ]] || dir="$ROOT/$dir"
            mkdir -p "$dir"
            args+=( "$flag" "$(wslpath -w "$dir")" )
            ;;
        *)
            args+=( "$1" ); shift
            ;;
    esac
done
set -- "${args[@]}"

EXE_WIN="$(wslpath -w "$EXE")"
cd "$ASSET_CWD"
exec "$SUPERVISOR" "$timeout_ms" "$EXE_WIN" "$@"
