# save-roundtrip — save/load TAS reference trace

**The parity reference for porting the port's missing save/load + shop UI.** A
retail recording (attach to the live Steam game) of: **load a save → walk to the
wooden sword on display → move it to another cell → save → quit to title → load
that save back** (the sword is gone from its origin). Replays deterministically on
a sandboxed retail spawn with the real save untouched.

## Files
- `recording.raw.jsonl` — the raw F2-format retail recording (source of truth).
  Its `{savefile}`/`{save_write}` rows point at the 18 MB arena dumps, which are
  NOT committed (provenance only); the gz blobs below are the committed save data.
- `trace.jsonl` — distilled anchor-segmented trace (the replayable reference).
  Embeds the boot save via `{savefile}` → `_saves/fa7c82….sav.gz`.
- `trace.jsonl.saves.json` — the in-session save the game wrote after the move
  (`_saves/397e13….sav.gz`) — the GROUND-TRUTH save bytes the port must reproduce.
- `_saves/*.sav.gz` — content-addressed, gzip'd save arenas (the user's save +
  the post-move save). Committed (force-added past gitignore) because the trace
  needs them to replay (per the save-blob policy).

## Replay (retail, sandboxed — never touches the real save)
```
nix develop --command python3 tools/trace_space.py tests/traces/save-roundtrip/trace.jsonl \
    -o /tmp/sr.spaced.jsonl --gap 60   # idle-gap headroom for the menus (movement preserved)
nix develop --command python3 tools/frida_capture.py \
    --input-segtrace /tmp/sr.spaced.jsonl --run-dir runs/sr-replay \
    --remote cutestation.soy:27042 --hide-window --turbo --silent-audio \
    --capture-all --capture-stride 12 --max-frames 70000
```
Anchor sequence it reproduces: NEW_GAME → HOUSE_FREEROAM → PAUSE_OPEN×… (move
sword) → save → TITLE_RETURN (quit) → NEW_GAME → HOUSE_FREEROAM (load-back).

## Known residual (replay fidelity, not blocking)
The item-PLACEMENT sub-step is imperfect: the cursor moves LEFT twice, so it
lands on an already-occupied cell instead of the empty one, and the 2nd
placement Z is effectively missed. The save/load round-trip itself is CORRECT
(reloading shows the sword removed from its origin). The placement-grid
navigation is the kind of menu-cursor sub-state we'll anchor/RE when porting.

## Next session
Port the missing systems (save I/O end-to-end, the shop display-management UI,
the save/load/title menus) until the PORT reproduces this trace — validating the
port's written save bytes against `_saves/397e13….sav.gz`.
