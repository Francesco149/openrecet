# Free-roam RNG-consumption desync + the missing ambient emitter (2026-06-04)

## ✅ Lead A RESOLVED 2026-06-04 — it was an invisible dev coordinate overlay, NOT a missing emitter

`phase_probe house-walk-down-dense` now reports **`rng` AND `rngcalls` ALIGNED**
(bit-exact LCG state at every db054). Fix: `src/scene1_sim.c` consumes one
`rng_next15()` at the tail of `scene1_ingame_default_arm_tick` (commit below).

The original hypothesis (a missing every-16-frame ambient emitter through
`scene1_spawn`) was **WRONG**. The numeric per-frame `rngcalls` diff showed a
steady **−1 read/frame on EVERY frame** (a flat consumer), not a +N step every 16
frames (a periodic emitter). The every-16 foot-dust (`FUN_0048b850` → `scene1_spawn`
type 0xe) and the every-4 wing-sparkle were already **bit-exact in the port**.

Root cause (drilled with a `--call-trace` hook on `0x447f4f` + the per-frame
`rngcalls` numeric diff): the **dev coordinate overlay** at the tail of
`FUN_00442cef` (decompile L421, asm `0x443601`) calls the raw LCG once per frame
to print `"%d"` (the rng value) + `"X/Y/Z:%f"` (player pos) into an **unrendered**
debug text grid (`DAT_06a47aac` via `FUN_00451874`). Invisible, but it still
advances `DAT_006023a0` every frame and is the tick's LAST rng consumer — so
omitting it desynced the whole downstream stream by 1 step/frame (first visible at
db054≈37, the frame the trace's walk begins). Written up as **engine-quirks §95**;
regression test `test_scene1_ingame_default_arm_consumes_debug_overlay_rng`.

Lead B (player-anim phase normalization) is still open — see below.

---

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

## Lead B — Recette (player) anim phase — ✅ RESOLVED 2026-06-04

The player (actor 0) anim record is the `i*0x2c`-byte mirror of the companion in
`&DAT_056daae8[i*0xb]` (verified vs the engine writer at all.c:84565+):
ANIM `0x56daae8` / TIMER `0x56daaf0` / COUNTER `0x56daaf4` / FRAME `0x56daaf8` /
FACING `0x56dab00`. (`0x56dab00` IS the player facing — because it's the FACING
field of actor 0, which *confirms* the array layout rather than refuting it.)

- Added the player row to `phase_probe.STD_WATCHES` (`p.*` vs the companion `c.*`);
  names match the port pos-log's actor-0 fields (commit a79f8b0).
- Extended `{phasepin}` to also reset the player anim cycle (`player_ctrl_phasepin`
  + the Frida agent; commit cb9f465) so an **idle** comparison normalizes the
  player origin (a walk trace already self-aligns via the idle↔walk transition
  reset).
- **Verified bit-exact:** `phase_probe house-walk-down-dense` AND
  `house-idle --window 120,80` → all eight `p.*`/`c.*` counters ALIGNED, incl. the
  walk-cycle wrap landing on the same frame. **Character anim phase is 1:1 for
  Recette AND Tear, walk AND idle.** The earlier "mismatched anim frame" diff was
  the load-dependent ORIGIN (now harness-normalized), not a logic/cell bug.

NPC anim phase is the remaining `{phasepin}` follow-up.

## Lead C — IDLE-only RNG over-consumption — ✅ RESOLVED 2026-06-04 (§95 overlay was movement-gated)

`phase_probe house-idle --window 120,80` showed **`rngcalls DESYNC`: the port
over-consumed +1 LCG/frame** (net +79 over the window; per-frame 4-cycle **retail
6,0,0,0 vs port 7,1,1,1** — exactly 1 extra LCG every frame). `house-walk-down-dense`
was ALIGNED, so it was **idle-specific**.

**Root cause:** the **§95 dev-overlay consume** at the `scene1_ingame_default_arm_tick`
tail was **unconditional**, but retail's overlay LCG step is **movement-gated** —
ground truth from raw per-render-frame `rngcalls`:
- **walk:** both targets consume +1 on EVERY render frame (overlay fires).
- **idle:** retail consumes **0** on every non-sim frame AND no extra step on the
  sim-tick frames (the burst is pure spawn work); the un-gated port burned +1/frame.
- idle `--drill` lists retail's consumers as `FUN_00447f4f` + `FUN_0046f2a3` only —
  **no `FUN_00442cef`**; the walk drill DID list it at 1/frame.

So the overlay step fires every render frame **only while the player is moving**.
(The decompile tail reads unconditional and the thunk `0x471084` is a real
`jmp 0x5041f6`, so the precise in-engine gate isn't pinned — but the observed
retail behaviour is unambiguous. §95 corrected with a CORRECTION banner.)

**Fix:** `scene1_sim.c` gates the consume on the player's walk-intent
(`scene1_debug_overlay_consume_rng()` → `if (player_ctrl_is_moving()) rng_next15();`).
Regression test `test_scene1_ingame_default_arm_consumes_debug_overlay_rng`
rewritten to assert idle→0 / moving→1.

**Validated:** post-fix `phase_probe` → `house-walk-down-dense` ALIGNED (the gate
is a no-op when always-moving); `house-idle` steady +1/frame leak gone (net +79 →
±5). The residual **±5 single-frame spike is retail run-to-run capture variance**,
not a port bug: across re-runs it flips sign and frame (+5 @ db054=49 → −5 @
db054=61; the walk trace likewise showed a one-off +5 that vanished on re-run). It
is one ~5-LCG spawn (foot-dust jitter) landing one frame inside vs outside the
window — a non-deterministic-in-retail spawn phase the TAS harness can't pin, and
the same class as the deferred foot-dust position/phase divergence. NOT chased
further.

## Captures/diffs must be phase + RNG aligned

A clean port↔retail pixel diff needs BOTH pins (phase + rng). `phase_probe`
injects both by default; the sparkles only stop being diff-noise once `rngcalls`
is ALIGNED (Lead A) — until then, expect RNG-driven sparkle/dust divergence in any
diff regardless of phase.

Cross-refs: `docs/phase-debugging.md`, `tools/phase_probe.py`, engine-quirks §94,
`scene1-tear-visual-diffs.md`, [[reference_phase_probe_tool]],
[[project_freeroam_smoke_effect]].
