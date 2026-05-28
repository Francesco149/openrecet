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

# --force / OPENRECET_FORCE_UNPACK=1 — required to re-unpack over an existing
# vendor/unpacked/recettear.unpacked.exe. The unpacked exe is the analysis
# target the entire findings/port-ledger corpus references by VA, AND the
# frida-spawn target; a stray overwrite (e.g. an analysis tool dumping a
# memory image to that path) silently broke it once. The installed exe is
# also chmod'd read-only as a second line of defence. See the guard below.
FORCE_UNPACK="${OPENRECET_FORCE_UNPACK:-0}"
for arg in "$@"; do
    case "$arg" in
        --force|-f) FORCE_UNPACK=1 ;;
        *) ;;
    esac
done

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

# Activate the repo's tracked git hooks (co-author trailer, pre-commit ledger
# + test gate). Idempotent. Without this a fresh clone uses .git/hooks (empty).
git -C "$(dirname "$0")/.." config core.hooksPath tools/git-hooks
green "  ✓ git hooks:    core.hooksPath → tools/git-hooks"

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

# Overwrite guard. If a previous unpacked exe is already in place, refuse to
# clobber it unless --force / OPENRECET_FORCE_UNPACK=1. Re-running setup.sh
# for symlink/hook validation must NOT silently replace the analysis target.
DEST="$UNPACKED/recettear.unpacked.exe"
SHAFILE="$UNPACKED/.unpacked.sha256"
if [[ -f "$DEST" && "$FORCE_UNPACK" != "1" ]]; then
    green "  ✓ unpacked exe already present — skipping Steamless (overwrite-guarded)."
    EXISTING_SHA="$(sha256sum "$DEST" | awk '{print $1}')"
    if [[ -f "$SHAFILE" ]]; then
        RECORDED_SHA="$(awk '{print $1}' "$SHAFILE")"
        if [[ "$EXISTING_SHA" != "$RECORDED_SHA" ]]; then
            red "  ! integrity drift: $DEST sha256 $EXISTING_SHA"
            red "    does not match recorded $RECORDED_SHA — the exe was"
            red "    overwritten out-of-band. Re-run with --force to regenerate."
        fi
    else
        # First adoption: record the current exe's sha as canonical so a
        # later out-of-band overwrite is detectable.
        printf '%s  recettear.unpacked.exe\n' "$EXISTING_SHA" > "$SHAFILE"
    fi
    yellow "    sha256 $EXISTING_SHA"
    yellow "    (re-unpack with: ./tools/setup.sh --force)"
    # Re-assert the read-only bit in case something cleared it.
    chmod a-w "$DEST" 2>/dev/null || true
    bold "[4/4] Summary"
    green "Setup OK (unpack skipped). vendor/original -> $GAME_DIR"
    exit 0
fi

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

# Clear any prior read-only bit so the move can replace it (we only reach
# here when --force or the dest is absent), then install + re-protect.
chmod u+w "$DEST" 2>/dev/null || true
rm -f "$DEST"
mv "$WORK/recettear.exe.unpacked.exe" "$DEST"
UNPACK_SHA="$(sha256sum "$DEST" | awk '{print $1}')"
# Record the sha so a future setup.sh / integrity check can detect an
# out-of-band overwrite, and make the file read-only so a stray write
# (e.g. an analysis tool dumping to this path) fails loudly instead of
# silently swapping in a non-loadable image.
printf '%s  recettear.unpacked.exe\n' "$UNPACK_SHA" > "$SHAFILE"
chmod a-w "$DEST"
green "  ✓ unpacked: vendor/unpacked/recettear.unpacked.exe (read-only)"
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
