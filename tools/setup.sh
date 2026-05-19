#!/usr/bin/env bash
# tools/setup.sh — one-shot bootstrap for a fresh clone.
#
#   1. Symlinks the user's Steam install of Recettear into vendor/original/.
#   2. Runs Steamless via WSLInterop (it's .NET — Windows already has the
#      runtime, no wine needed for this step).
#   3. Stashes the unpacked exe in vendor/unpacked/.
#   4. Prints SHA256s of inputs and outputs for reproducibility.
#
# Idempotent. Re-running re-validates symlinks and re-checks unpacker output.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GAME_DIR="${OPENRECET_GAME_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/Recettear}"
STEAMLESS_DIR="${OPENRECET_STEAMLESS_DIR:-/mnt/c/Users/headpats/Documents/_devtools/Steamless.v3.1.0.5.-.by.atom0s}"

VENDOR="$ROOT/vendor"
ORIGINAL="$VENDOR/original"
UNPACKED="$VENDOR/unpacked"

bold()  { printf "\033[1m%s\033[0m\n" "$*"; }
green() { printf "\033[32m%s\033[0m\n" "$*"; }
red()   { printf "\033[31m%s\033[0m\n" "$*" >&2; }
yellow(){ printf "\033[33m%s\033[0m\n" "$*"; }

# ─── pre-flight ────────────────────────────────────────────────────────────
bold "[1/4] Pre-flight"

if [[ ! -d "$GAME_DIR" ]]; then
    red "Game directory not found: $GAME_DIR"
    red "Set OPENRECET_GAME_DIR to the absolute path of your Recettear install."
    exit 1
fi
if [[ ! -f "$GAME_DIR/recettear.exe" ]]; then
    red "recettear.exe not found inside $GAME_DIR"
    exit 1
fi
if [[ ! -f "$STEAMLESS_DIR/Steamless.CLI.exe" ]]; then
    red "Steamless.CLI.exe not found at $STEAMLESS_DIR"
    red "Set OPENRECET_STEAMLESS_DIR or download from https://github.com/atom0s/Steamless/releases"
    exit 1
fi
green "  ✓ game dir:     $GAME_DIR"
green "  ✓ steamless:    $STEAMLESS_DIR"

# ─── symlink game files ────────────────────────────────────────────────────
bold "[2/4] Linking game files into vendor/original/"

mkdir -p "$VENDOR"
# Replace the symlink each run so a moved game install doesn't go stale.
if [[ -L "$ORIGINAL" || -e "$ORIGINAL" ]]; then
    rm -f "$ORIGINAL"
fi
ln -s "$GAME_DIR" "$ORIGINAL"
green "  ✓ vendor/original -> $GAME_DIR"

# ─── steamless unpack ─────────────────────────────────────────────────────
bold "[3/4] Running Steamless to remove DRM"

mkdir -p "$UNPACKED"

# Copy (not symlink) the exe into a writable dir, since Steamless writes its
# output next to the input. The Steam install dir may also be read-only or
# under Windows ACLs that confuse Steamless when writing.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$GAME_DIR/recettear.exe" "$WORK/recettear.exe"

ORIG_SHA="$(sha256sum "$WORK/recettear.exe" | awk '{print $1}')"
yellow "  original recettear.exe: sha256 $ORIG_SHA"

# Convert the Linux temp path to a Windows path for Steamless (running native).
WORK_WIN="$(wslpath -w "$WORK")"
STEAMLESS_WIN="$(wslpath -w "$STEAMLESS_DIR/Steamless.CLI.exe")"

# Run Steamless via WSLInterop — it's .NET, your Windows already has the
# runtime. We pass --quiet so the output is greppable.
yellow "  invoking: $STEAMLESS_WIN --quiet $WORK_WIN\\recettear.exe"
"$STEAMLESS_DIR/Steamless.CLI.exe" --quiet "$WORK_WIN\\recettear.exe" || {
    red "Steamless failed. Try without --quiet to see what went wrong:"
    red "  '$STEAMLESS_DIR/Steamless.CLI.exe' '$WORK_WIN\\recettear.exe'"
    exit 1
}

# Steamless writes alongside input as `<name>.unpacked.exe`
if [[ ! -f "$WORK/recettear.exe.unpacked.exe" ]]; then
    red "Steamless ran but output missing: $WORK/recettear.exe.unpacked.exe"
    ls -la "$WORK"
    exit 1
fi

mv "$WORK/recettear.exe.unpacked.exe" "$UNPACKED/recettear.unpacked.exe"
UNPACK_SHA="$(sha256sum "$UNPACKED/recettear.unpacked.exe" | awk '{print $1}')"
green "  ✓ unpacked: vendor/unpacked/recettear.unpacked.exe"
yellow "    sha256 $UNPACK_SHA"

# ─── summary / next steps ─────────────────────────────────────────────────
bold "[4/4] Summary"

cat <<EOF

Inputs:
  recettear.exe (packed) sha256:   $ORIG_SHA

Outputs:
  vendor/original/                 -> $GAME_DIR
  vendor/unpacked/recettear.unpacked.exe
  sha256:                          $UNPACK_SHA

Next:
  • ./tools/ghidra-headless.sh         # batch decompile the unpacked exe
  • python ./tools/extract/xfile.py vendor/original/xfile/city/dun_city00.x
EOF
