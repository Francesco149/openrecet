#!/usr/bin/env bash
# PostToolUse(Bash) hook — auto-push capture frames to the llm-feed.
#
# Fires after every Bash tool call, but fast-exits unless the command captured
# frames (contains `--capture-to`).  When it did, it montages + pushes that run
# dir to the feed (localhost:8777) via tools/feed_push_run.py, so test / input-
# trace captures show up there automatically without the agent remembering to.
#
# The push worker is best-effort + idempotent (see feed_push_run.py): feed down
# → silent no-op; same frames already pushed → no double-push.  So it's safe for
# this to fire even when the agent also pushed by hand.
#
# Registered in .claude/settings.json under hooks.PostToolUse (matcher: Bash).

input=$(cat)

# Fast path: no capture in this command → do nothing (no python, no nix).
case "$input" in
  *--capture-to*) ;;
  *) exit 0 ;;
esac

# Pull the capture dir out of the JSON-embedded command string. Handles both
# `--capture-to DIR` and `--capture-to=DIR`; stops at the first whitespace/quote.
dir=$(printf '%s' "$input" \
      | grep -oE -- '--capture-to[=[:space:]]+[^"[:space:]]+' \
      | head -1 \
      | sed -E 's/--capture-to[=[:space:]]+//')
[ -n "$dir" ] || exit 0

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo" || exit 0

# python3 isn't on the bare WSL PATH (see memory: feedback_nix_develop), so go
# through the dev shell. Bounded so a cold nix eval can't hang the hook.
timeout 90 nix develop --command python3 tools/feed_push_run.py "$dir" \
    >/dev/null 2>&1

exit 0
