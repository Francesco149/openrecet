#!/usr/bin/env bash
# Claude Code PreToolUse hook — hard guard around the project's single
# unbreakable constraint: never mutate vendor game assets or the Ghidra
# project DB.  The "never touch vendor/" rule lived only as prose in
# AGENT-WORKFLOW.md; a cold-start subagent could ignore it.  This makes it
# an enforced invariant.
#
# Reads the PreToolUse JSON event on stdin, extracts the target path from the
# tool input, and exits 2 (deny, with reason to Claude) if it lands under a
# protected prefix.  Any other path → exit 0 (allow).
#
# Wired from .claude/settings.json on Edit|Write|MultiEdit|NotebookEdit.

set -euo pipefail

event="$(cat)"

# file_path covers Edit/Write/MultiEdit; notebook_path covers NotebookEdit.
path="$(printf '%s' "$event" | jq -r '.tool_input.file_path // .tool_input.notebook_path // empty')"
[[ -z "$path" ]] && exit 0

# Normalise to a repo-relative comparison: match both absolute and relative.
case "$path" in
    */vendor/*|vendor/*|*/ghidra/projects/*|ghidra/projects/*)
        echo "BLOCKED: '$path' is under a protected path (vendor/ game assets or" \
             "ghidra/projects/ DB). These must never be mutated — see" \
             "AGENT-WORKFLOW.md and the never-redistribute-assets constraint." >&2
        exit 2
        ;;
esac

exit 0
