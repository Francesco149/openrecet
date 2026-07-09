#!/usr/bin/env bash
# tools/ghidra-mcp.sh — launch the pyghidra-mcp server (stdio) against the
# EXISTING, already-analyzed openrecet Ghidra project.
#
# The RE-query companion to the static docs/decompiled/ export: serves Ghidra's
# live analysis (decompile-any-function, precise xrefs incl. indexed/computed
# struct accesses grep misses, data types, disassembly, call-graphs) over MCP
# so an agent can query on demand instead of grepping all.c. REUSES the
# analysis in ghidra/projects/openrecet.rep — does NOT re-analyze
# (single-digit-second startup).
#
# Registered as the `ghidra` MCP (stdio) in .mcp.json. Run standalone for a
# quick smoke test:
#     nix develop --command bash tools/ghidra-mcp.sh --help
#
# pyghidra-mcp (Apache-2.0, github.com/clearbluejar/pyghidra-mcp) lives in a
# dedicated venv under ~/.local/state/openrecet/ghidra-mcp-venv:
#     python -m venv ~/.local/state/openrecet/ghidra-mcp-venv
#     ~/.local/state/openrecet/ghidra-mcp-venv/bin/pip install pyghidra-mcp
# Requires the nix dev shell's Ghidra 12 + JDK 21 (mirror of OpenLords2's
# tools/ghidra-mcp.sh — same venv layout, same flags).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- nix-provided Ghidra 12 (bundles pyghidra 3.x) + its JDK 21 -------------
# Resolve from the dev-shell PATH so store-hash bumps don't break the wrapper.
GHIDRA_BIN="$(command -v ghidra-analyzeHeadless 2>/dev/null || true)"
if [[ -z "$GHIDRA_BIN" ]]; then
    echo "ghidra-analyzeHeadless not on PATH — run inside 'nix develop $ROOT'." >&2
    exit 1
fi
# .../lib/ghidra/support/analyzeHeadless -> GHIDRA_INSTALL_DIR = .../lib/ghidra
export GHIDRA_INSTALL_DIR="$(dirname "$(dirname "$(readlink -f "$GHIDRA_BIN")")")"

# JDK 21 that Ghidra ships with (a reference in the ghidra store path).
_GHIDRA_STORE="$(readlink -f "$GHIDRA_BIN")"; _GHIDRA_STORE="${_GHIDRA_STORE%%/lib/*}"
JDK="$(nix-store -q --references "$_GHIDRA_STORE" 2>/dev/null | grep -m1 -i openjdk || true)"
if [[ -n "$JDK" && -x "$JDK/bin/java" ]]; then
    export JAVA_HOME="$JDK"
    export PATH="$JDK/bin:$PATH"
fi

# jpype's native _jpype.so needs a real libstdc++.so.6 (absent from the pure
# nix shell's default loader path). Pull one from the gcc-lib in the closure.
GCCLIB="$(dirname "$(find /nix/store -maxdepth 3 -name libstdc++.so.6 -path '*gcc-*-lib*' 2>/dev/null | head -1)")"
[[ -n "$GCCLIB" ]] && export LD_LIBRARY_PATH="$GCCLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

VENV="$HOME/.local/state/openrecet/ghidra-mcp-venv"
if [[ ! -x "$VENV/bin/pyghidra-mcp" ]]; then
    echo "pyghidra-mcp venv missing at $VENV — recreate:" >&2
    echo "  python -m venv \"$VENV\" && \"$VENV/bin/pip\" install pyghidra-mcp" >&2
    exit 1
fi

PROJECT="$ROOT/ghidra/projects/openrecet.gpr"

# --wait-for-analysis: reuse the existing .rep analysis and flip pyghidra-mcp's
#   own analysis-complete flag (it detects "already analyzed" and just indexes;
#   without this flag, tool calls error with "Analysis incomplete").
exec "$VENV/bin/pyghidra-mcp" \
    --project-path "$PROJECT" \
    --wait-for-analysis \
    "$@"
