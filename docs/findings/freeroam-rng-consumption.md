# Free-roam RNG-consumption desync + the missing ambient emitter (2026-06-04)

Status at session end. Two leads, both surfaced by the new phase/RNG tooling
(`tools/phase_probe.py`, `docs/phase-debugging.md`). **Pick up here next session.**

## How to reproduce / re-check (one command)

```sh
nix develop --command python3 tools/phase_probe.py house-walk-down-dense --drill
```

Drives port + retail on the synced trace, pins phase (`{phasepin}`) + RNG seed
(`{rngseed}`), aligns by `db054`, and prints a per-counter verdict + retail's RNG
consumers by function. Data: `runs/phase-probe/house-walk-down-dense/`.

## Lead A — RNG-consumption desync = a missing every-16-frame ambient emitter

**Verdict from the probe:** companion anim counters (cframe/ccnt/coct/canim) are
**bit-exact** vs retail, but `rngcalls DESYNC` — the port under-consumes the LCG
by **~40 calls over the 157-frame window** (diverges first at db054≈37). Even with
the same seed pinned at the same frame, the streams drift → the targets *consume*
RNG differently.

**`--drill` localization** (retail LCG callers, by enclosing function):
| function | LCG calls / window | port status |
|---|---|---|
| `FUN_00447f4f` (`scene1_spawn`) | 276 (spawn jitter) | ported — but called fewer times |
| `FUN_00442cef` (sim-arm dispatch) | 157 (1/frame) | matched |
| `FUN_0048b850` (player ctrl) | 18 | matched |

Per-frame call-site cadence (`rng_callsites.json`, db054 = frame − phasepin):
- `u:0x44a750…86b` (6-read jitter) every **4** frames → the **companion
  wing-sparkle** (db054%4). **Ported + matched.**
- `u:0x44a3dd / 0x44a418 / 0x44a449` (3-read jitter) every **16** frames
  (db054 = 16,32,48,…) → **an emitter the port LACKS** (~27 of the −40 deficit).
- `0x443606` (1/frame) + the rest are matched.

So the deficit is a **specific every-16-frame ambient-particle emitter** that calls
`scene1_spawn` with a 3-read jitter. This is the "faint ambient particles" gap
(cf. `scene1-bg-npc.md`, `reference_parity_trace_walk_down_dense`). It is **NOT**
`FUN_00483e7b` (that pump uses db054 %4/%2/%15/%20/%100 — no %16; and only its
`db054++` is modeled in `scene1_companion_ctrl.c`, the pump body is unported but
its moduli don't match this cadence either).

**NEXT STEP (the rabbit-hole drill the user pre-authorized):** add a hook on
`FUN_00447f4f` itself that records *its* caller VA on the every-16 frames (one
level up from the LCG) → names the emitter function + the particle type + its
stage gate. Then either (a) port that emitter, or (b) per the user's tactic,
insert matching **dummy `rng_next15()` calls** (3 reads every 16th db054 frame)
tagged `PORT-DEBT(...)` to re-align the stream, retiring them when the real
emitter lands. **Re-run `phase_probe` after — `rngcalls` must return to ALIGNED.**

## Lead B — Recette (player) anim is NOT phase-normalized

`{phasepin}` resets only the **companion** phase (db054 + companion anim cycle
`DAT_056dab40/48/4c/50`). The **player's** anim cycle is still load-offset, which
is why the user saw Recette in a mismatched anim frame in the phase-fixed diff
(feed `20260603T232805_45c4`). `phase_probe` also only verifies companion
counters today.

**NEXT STEP:** find the player (actor 0) anim FRAME/COUNTER engine VA — verify
empirically (Frida `--watch` candidates correlated with the port pos-log's
`aframe`, OR the player anim writer in the decompile; note `DAT_056dab00` is the
player *facing* octant, and the companion's record is the special `dab40` block,
so the dab00-as-array assumption is unverified). Then extend `{phasepin}` to reset
**all live actors'** anim cycles (the chosen "auto-settling phase-sync —
normalizes ALL scene phase counters") and add the player row to
`phase_probe.STD_WATCHES`. If her FRAME counter then matches but she still renders
differently → a real anim-code/cell-mapping bug.

## Captures/diffs must be phase + RNG aligned

A clean port↔retail pixel diff needs BOTH pins (phase + rng). `phase_probe`
injects both by default; the sparkles only stop being diff-noise once `rngcalls`
is ALIGNED (Lead A) — until then, expect RNG-driven sparkle/dust divergence in any
diff regardless of phase.

Cross-refs: `docs/phase-debugging.md`, `tools/phase_probe.py`, engine-quirks §94,
`scene1-tear-visual-diffs.md`, [[reference_phase_probe_tool]],
[[project_freeroam_smoke_effect]].
