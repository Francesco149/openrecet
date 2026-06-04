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

## Lead C — IDLE-only RNG over-consumption (+1 LCG/frame) — OPEN (found 2026-06-04)

`phase_probe house-idle --window 120,80` → **`rngcalls DESYNC`: the port
over-consumes ~+1 LCG/frame** (net +79 over the 80-frame window, first divergence
db054=42). Per-frame pattern is a clean 4-cycle: **retail 6,0,0,0 vs port 7,1,1,1**
— the port burns exactly **1 extra LCG every frame**. `house-walk-down-dense` is
`rngcalls` ALIGNED, so this is **idle-specific**.

- The port's +1/frame is the **§95 dev-overlay consume** — the unconditional
  `(void)rng_next15()` at the `scene1_ingame_default_arm_tick` tail (`scene1_sim.c`).
- **Retail does NOT consume it on these idle frames:** an idle `--drill` lists
  retail's RNG consumers as `FUN_00447f4f` (spawn) + `FUN_0046f2a3` only — **no
  `FUN_00442cef`** (the overlay's enclosing fn). The playbook's *walk* drill DID
  list `FUN_00442cef` at 1/frame. So the overlay's LCG step fires on **walk but
  not idle** in retail → it is **conditional**, which §95 (and the port) modelled
  as unconditional.
- **Open mechanism (do NOT fix blind):** the decompile shows the overlay tail
  (`442cef.c` LAB_004435f7, L417-430) as *unconditional*, and the thunk at
  `0x471084` is a real `jmp 0x5041f6` (so the step IS counted by the retail hook).
  Yet retail skips it on idle; the only early return is L143 (`DAT_0438becc % 3`,
  not idle-related). The `--drill` counts didn't reconcile with the aligned window
  (FUN_00447f4f ~5.8/frame in the drill vs ~1.5/frame aligned), so the exact gate
  isn't nailed. **NEXT:** Frida-trace `FUN_00442cef` entry/exit + the overlay call
  site on a synced idle-vs-walk window to find what gates the tail consume; OR
  empirically gate the port consume on player-moving and confirm BOTH traces go
  `rngcalls` ALIGNED (then revise §95 "unconditional" → "moving-gated"). Affects
  only RNG-stream parity for TAS comparisons (idle sparkle/dust phase), NOT the
  anim-phase result above.

## Captures/diffs must be phase + RNG aligned

A clean port↔retail pixel diff needs BOTH pins (phase + rng). `phase_probe`
injects both by default; the sparkles only stop being diff-noise once `rngcalls`
is ALIGNED (Lead A) — until then, expect RNG-driven sparkle/dust divergence in any
diff regardless of phase.

Cross-refs: `docs/phase-debugging.md`, `tools/phase_probe.py`, engine-quirks §94,
`scene1-tear-visual-diffs.md`, [[reference_phase_probe_tool]],
[[project_freeroam_smoke_effect]].
