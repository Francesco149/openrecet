#!/usr/bin/env bash
# tools/ghidra-headless.sh — batch decompile & export static RE index for recettear.exe (CV-01).
#
# Imports vendor/unpacked/recettear.unpacked.exe into a Ghidra project,
# runs auto-analysis, and executes post-scripts:
#   1. ExportDecompiledC.java -> dumps decompiled C to docs/decompiled/
#   2. ExportGhidraIndex.java -> dumps CFG blocks, flows, calls, xrefs, switches, byte hashes
#
# Modes:
#   ./tools/ghidra-headless.sh --all        (run decompile + index export)
#   ./tools/ghidra-headless.sh --index      (run index export only)
#   ./tools/ghidra-headless.sh --decompile  (run decompile export only)
#
# Idempotent: project lives in ghidra/projects/openrecet/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UNPACKED="$ROOT/vendor/unpacked/recettear.unpacked.exe"
PROJ_DIR="$ROOT/ghidra/projects"
PROJ_NAME="openrecet"
OUT_DIR="$ROOT/docs/decompiled"
SCRIPT_DIR="$ROOT/tools/ghidra-scripts"
MODE="all"
for arg in "$@"; do
    case "$arg" in
        --index|-i) MODE="index" ;;
        --decompile|-d) MODE="decompile" ;;
        --all|-a) MODE="all" ;;
        *) ;;
    esac
done

bold()   { printf "\033[1m%s\033[0m\n" "$*"; }
green()  { printf "\033[32m%s\033[0m\n" "$*"; }
red()    { printf "\033[31m%s\033[0m\n" "$*" >&2; }
yellow() { printf "\033[33m%s\033[0m\n" "$*"; }
# ─── pre-flight ────────────────────────────────────────────────────────────
if [[ ! -f "$UNPACKED" ]]; then
    red "Missing $UNPACKED — run ./tools/setup.sh first."
    exit 1
fi

if ! command -v ghidra-analyzeHeadless >/dev/null 2>&1; then
    red "ghidra-analyzeHeadless not on PATH. Are you in the nix dev shell? (nix develop)"
    exit 1
fi

mkdir -p "$PROJ_DIR" "$OUT_DIR" "$SCRIPT_DIR"

bold "[1/3] Pre-flight"
green "  ✓ ghidra-analyzeHeadless: $(command -v ghidra-analyzeHeadless)"
green "  ✓ input binary:    $UNPACKED"
green "  ✓ project dir:     $PROJ_DIR/$PROJ_NAME"
green "  ✓ output dir:      $OUT_DIR"
green "  ✓ export mode:     $MODE"

# ─── post scripts ──────────────────────────────────────────────────────────
if [[ "$MODE" == "decompile" || "$MODE" == "all" ]]; then
    if [[ ! -f "$SCRIPT_DIR/ExportDecompiledC.java" ]]; then
        red "Missing $SCRIPT_DIR/ExportDecompiledC.java"
        exit 1
    fi
    green "  ✓ uses $SCRIPT_DIR/ExportDecompiledC.java"
fi
if [[ "$MODE" == "index" || "$MODE" == "all" ]]; then
    if [[ ! -f "$SCRIPT_DIR/ExportGhidraIndex.java" ]]; then
        red "Missing $SCRIPT_DIR/ExportGhidraIndex.java"
        exit 1
    fi
    green "  ✓ uses $SCRIPT_DIR/ExportGhidraIndex.java"
fi
# ─── run headless analysis ────────────────────────────────────────────────
bold "[2/3] Importing + analyzing (this is slow on first run)"

GPR="$PROJ_DIR/$PROJ_NAME.gpr"

if [[ ! -f "$GPR" ]]; then
    yellow "  first run — importing + analyzing + exporting (10–30 min)..."
    if [[ "$MODE" == "decompile" || "$MODE" == "all" ]]; then
        ghidra-analyzeHeadless "$PROJ_DIR" "$PROJ_NAME" \
            -import "$UNPACKED" \
            -scriptPath "$SCRIPT_DIR" \
            -postScript ExportDecompiledC.java "$OUT_DIR" 2>&1 | tail -40 \
            || { red "ghidra-analyzeHeadless ExportDecompiledC failed"; exit 1; }
    fi
    if [[ "$MODE" == "index" || "$MODE" == "all" ]]; then
        ghidra-analyzeHeadless "$PROJ_DIR" "$PROJ_NAME" \
            -process "$(basename "$UNPACKED")" \
            -noanalysis \
            -scriptPath "$SCRIPT_DIR" \
            -postScript ExportGhidraIndex.java "$OUT_DIR" 2>&1 | tail -40 \
            || { red "ghidra-analyzeHeadless ExportGhidraIndex failed"; exit 1; }
    fi
else
    yellow "  project exists — skipping import/analysis, re-running export..."
    if [[ "$MODE" == "decompile" || "$MODE" == "all" ]]; then
        ghidra-analyzeHeadless "$PROJ_DIR" "$PROJ_NAME" \
            -process "$(basename "$UNPACKED")" \
            -noanalysis \
            -scriptPath "$SCRIPT_DIR" \
            -postScript ExportDecompiledC.java "$OUT_DIR" 2>&1 | tail -20 \
            || { red "ghidra-analyzeHeadless ExportDecompiledC failed"; exit 1; }
    fi
    if [[ "$MODE" == "index" || "$MODE" == "all" ]]; then
        ghidra-analyzeHeadless "$PROJ_DIR" "$PROJ_NAME" \
            -process "$(basename "$UNPACKED")" \
            -noanalysis \
            -scriptPath "$SCRIPT_DIR" \
            -postScript ExportGhidraIndex.java "$OUT_DIR" 2>&1 | tail -20 \
            || { red "ghidra-analyzeHeadless ExportGhidraIndex failed"; exit 1; }
    fi
fi

# ─── summary ──────────────────────────────────────────────────────────────
bold "[3/3] Summary"

FUNC_COUNT="$(wc -l < "$OUT_DIR/functions.csv" 2>/dev/null || echo 0)"
ALLSZ="$(wc -c < "$OUT_DIR/all.c" 2>/dev/null || echo 0)"
if [[ -f "$OUT_DIR/functions.csv" ]]; then
    green "  ✓ $((FUNC_COUNT - 1)) functions decompiled"
    green "  ✓ docs/decompiled/all.c                ($ALLSZ bytes)"
    green "  ✓ docs/decompiled/by-address/*.c"
    green "  ✓ docs/decompiled/by-name/*.c"
    green "  ✓ docs/decompiled/functions.csv"
fi
if [[ -f "$OUT_DIR/manifest.json" ]]; then
    green "  ✓ docs/decompiled/manifest.json        (static index manifest)"
    green "  ✓ docs/decompiled/blocks.json          (CFG basic blocks)"
    green "  ✓ docs/decompiled/flows.json           (CFG flow edges)"
    green "  ✓ docs/decompiled/data_xrefs.json      (read/write xrefs)"
    green "  ✓ docs/decompiled/switch_cases.json    (jump tables)"
fi
cat <<'EOF'

Next:
  • Read docs/decompiled/functions.csv to find named functions.
  • grep -n 'WinMain\|wWinMain' docs/decompiled/all.c to locate entry.
  • Open Ghidra GUI on the same project for interactive analysis:
      ghidraRun &  # then File > Open Project > ghidra/projects/openrecet.gpr
EOF
