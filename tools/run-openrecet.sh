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

# Inject a default --max-duration-ms if the caller didn't pass one. The
# supervisor timeout above is the kernel-side safety net; this lets
# openrecet's own message-pump take the graceful exit path most of the
# time. 3s = same default as historic ad-hoc smoke runs.
have_max_duration=0
for a in "$@"; do
    [[ "$a" == "--max-duration-ms" || "$a" == --max-duration-ms=* ]] \
        && { have_max_duration=1; break; }
done

set --   $(( have_max_duration ? 0 : 1 )) "$@"
if (( $1 )); then
    shift
    set -- --max-duration-ms 3000 "$@"
else
    shift
fi

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
