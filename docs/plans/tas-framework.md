# TAS framework — the vision (deterministic playthrough as ground truth)

> **Operational "how to do trace work" lives in `docs/trace-workflow.md`.** This
> doc is the **why / where it's going** — the long-game vision and the prioritised
> next features. The substrate is built and validated (anchors + recorder
> anchor-logging + `distill --anchor-segments` + per-anchor RNG pin; two runs
> bit-identical). What remains is *coverage* and a few force-multipliers.

## The ultimate goal

**One TAS trace drives the port and retail identically — assuming the port is
accurate.** The trace (+ pinned RNG, anchored timeline) is the single source of
truth; a faithful port is a deterministic function of it, bit-for-bit in lockstep
with retail. Two consequences:

1. **Anchors are a correctness signal, not just alignment.** If the same trace
   makes an anchor fire at port-frame X but retail-frame Y once both are accurate,
   that gap is a port bug or a determinism leak. As the port converges, anchor
   frames converge.
2. **Keep growing anchor coverage until an entire playthrough replays
   deterministically.** Today we can deterministically reach + capture free-roam
   (`FREEROAM_START`) and dialogue (`TEXT_ANIM_*`). The endgame: enough anchors
   (shop open, customer arrives, item sold, dungeon entered, floor cleared, …)
   that any moment of a full run is a named, jitter-immune sync point. That full
   trace then doubles as the ultimate behavioural regression test — replay on both
   targets, any anchor/state divergence is a regression.

## TOP-PRIORITY next feature — save-file snapshot in the trace

> User direction 2026-06-03: *"once we get past the first scene we'll want to hook
> save file loading: when recording a trace, the entire savefile being loaded is
> snapshotted into the trace; when playing the trace the savefile load is overridden
> with the snapshot."*

**Why it's top priority:** it lets a trace **start from any game state**, not just a
new game. We can jump straight to/test any part of the game (a specific day, a
dungeon, a stocked shop) without hand-fiddling save files — the trace carries the
exact world it was recorded against. This unblocks trace work for every scene past
the first, and makes the deterministic-playthrough goal tractable in segments.

Shape:
- **Record:** at savefile-load, snapshot the loaded bytes (the `save_io` /
  `save_bank` arena — see `src/save_io.c`, `src/save_bank.c`) into the recording
  (raw header field or a sidecar `.save` keyed to the trace; base64 or a side file
  since it's large).
- **Replay:** override the savefile load with the snapshot — a hook at the
  load path (or a `{savefile:...}` segtrace op / a `--inject-save <path>` flag)
  that installs the snapshot before scene tick 0, so the world is byte-identical to
  the recording.
- **Distill:** carry the snapshot reference through `--anchor-segments`.
- **Retail mirror (later):** Frida-write the same save arena so port↔retail start
  from identical state (this is the "state-injection at scenario start" idea —
  generalised from a per-field snapshot to the whole save).

This subsumes the old "inject engine-state snapshot" plan: the savefile *is* the
canonical serialized world; snapshot/restore it instead of enumerating globals.

## Autonomous incremental trace synthesis (the long game)

Beyond replaying hand/recorded traces: tooling for the agent to **autonomously
discover** an input trace, growing it in baby steps and verifying game state at each
step, until it can drive a full run. The loop:

1. **State oracle** — a readable snapshot of every probeable global at any frame
   (player pos/facing, scene id, menu cursor, inventory/gold, dialog state, RNG,
   day/phase). Subsumes the anchor `on: state` sources + `mem_watch`/call-trace probes.
2. **Waypoint library** — named target states (`AT_TITLE`, `FREEROAM_START`,
   `FIRST_CUSTOMER`, `ITEM_SOLD`, …); a full-game TAS = an ordered waypoint chain.
3. **Baby-step synthesis** — to advance waypoint k→k+1: propose a short input
   segment, replay from the k checkpoint, read the oracle, assert the result. Accept
   on match, revise on mismatch.
4. **Checkpointing** — the save-file snapshot above *is* the checkpoint primitive:
   resume iteration from the last accepted waypoint instead of replaying from boot.
5. **Regression value** — the synthesised chain replays on both targets; any
   anchor/state divergence is a port bug.

## Canonical-state forcing (for bit-exact clean diffs)

Anchor alignment gets the same *moment*; bit-exact diffs of a moving scene also need
the same *pose/camera*. Apply a named force profile symmetrically (player/camera
pose, sim-freeze, overlay parity). Feeds `tools/pixel_diff.py`. Largely covered by
RNG-pin + anchor-relative capture today; revisit if a specific diff needs a frozen
pose.

## Methodology rule (learned the hard way)

Never conclude a feature is absent from a static decode of a state gated behind
unported intro/dialogue — **drive to the live state with a trace and diff the call
graph.** See `docs/trace-workflow.md` and the RE-methodology memories.
