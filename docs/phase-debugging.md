# Debugging port↔retail PHASE / determinism divergences (the playbook)

When a free-roam **animation / hover-bob / sparkle / spawn phase** looks wrong vs
retail — Tear's wings flapping at the wrong cell, her eyes/hands a frame off, the
dust or sparkles out of step — the first question is always the same:

> **Is this a real per-frame LOGIC error, or just a load-dependent phase OFFSET
> (the counters/RNG start from a different origin, but the laws are bit-exact)?**

Don't eyeball sprites to answer it. Run the tool.

## The one command

```sh
nix develop --command python3 tools/phase_probe.py house-walk-down-dense
```

It drives the PORT and RETAIL on the same synced trace, logs the companion phase
counters per sim-frame on both, aligns them by `db054` value, and prints a
verdict per counter:

```
  counter    verdict       detail
  cframe     ALIGNED       bit-exact
  ccnt       ALIGNED       bit-exact
  coct       ALIGNED       bit-exact
  canim      ALIGNED       bit-exact
  rng        UNPINNED      0/37 match — add a {rngseed} op for a clean RNG comparison

  VERDICT: ✅ PHASE-CLEAN — all counters bit-exact vs retail.
```

Verdicts:
- **ALIGNED / PHASE-CLEAN** — counters bit-exact. Any remaining visual diff is
  RNG (sparkles) or render-side, **not** phase. Stop chasing phase.
- **CONST-OFFSET / PHASE-SYNC OFFSET** — a counter is off by a single constant
  (or constant-mod-cycle) with zero drift → the **law is correct**, only the
  phase *origin* differs. This is load-dependent, not a logic bug (see below).
- **DRIFT / LOGIC** — a counter's offset *grows* frame-to-frame → a real port
  logic error; the tool points at the counter.

`--no-pin` shows the RAW offset first (diagnosis); the default injects a
`{phasepin}` to normalize it (proof). It ALSO injects `{rngseed:[pin,seed]}` at
the same frame by default (`--seed`, default the recorder seed; `--seed 0`
disables) so the captures + diffs are **phase- AND rng-aligned** — the RNG
sparkles stop being diff noise, so a residual diff is a real render/logic gap.
A `DRIFT` on the `rng` row after pinning means the targets *consume* RNG
differently (a consumption desync — itself a finding). `--reuse` re-analyzes
existing run dirs. Data lands in `runs/phase-probe/<scenario>/{port,retail}/`.

## Why a phase OFFSET is usually NOT a bug — the load-jitter rule

The per-scene counter `DAT_056db054` (companion bob `sin(db054·0.04)`, every-4th
sparkle, §83/§81 spawns) is **frozen at 0 through retail's `recet_op.wmv` intro
video** and only ticks once the HOUSE per-frame open starts (db054 == 43 at
HOUSE_FREEROAM — engine-quirks §94). The **port skips the video** (§13), so its
counter accumulates the skipped frames: ~+1518 at free-roam. The increment + the
facing law are bit-exact (constant offset, zero drift), so it is a **load-time
phase ORIGIN**, not a logic gap. Such a frame count is **allowed to be overwritten
at an anchor when running traces** (it's not part of game logic; retail's own
free-roam phase is equally load-dependent).

## How the normalization works — `{phasepin}`

A segtrace op (mirrored in `src/input_segtrace.c` + the Frida agent, passed
through `frida_capture.py`):

```jsonc
{"wait": "HOUSE_FREEROAM"}
{"phasepin": 1552}     // at base+1552, zero db054 + the companion anim cycle on BOTH targets
```

It zeros `db054` (`0x056db054`) and the companion anim cycle
(`FRAME/TIMER/COUNTER` = `0x056dab50/48/4c`) so both sides share one clock from
that frame. `phase_probe.py` injects it automatically just before the capture
window. **Fire it AFTER the post-anchor load settles**: there is a residual
~47-frame `db054` freeze right after HOUSE_FREEROAM (the load tail) — pinning at
`base+0` leaves a +47 residual; pinning late (the tool defaults `window_start−40`)
gives a clean 0/N. Trace-comparison only — the shipped game keeps the
engine-faithful free-running counter.

Alternative in the toolbox (the user's "wait-to-sync"): idle at the anchor until
a counter reaches a high common target (a `wait_until db054 >= N`, N above the
load-inflated max, retry on a slow load). The agent has `wait_until`; the port
does not yet.

## Worked result (2026-06-04)

Tear's anim-phase (`scene1-tear-visual-diffs.md` #3/#4): raw offset **+1518
db054 / 140/140 cframe-mismatch**; with `{phasepin}` after the load tail,
**cframe/ccnt/facing 0/139** — bit-exact. The visible cap_03 diff that remained
was the **unpinned RNG wing-sparkles** (#5), not phase. Proof that #3/#4 were a
deterministic phase-origin offset, not a logic bug.

## RNG consumption (the `rngcalls` row + `--drill`)

Pinning the same seed is not enough if the two targets *consume* RNG differently
— the streams desync from a shared seed. The probe always captures a cumulative
LCG-call count (`rngcalls`, port `src/rng.c` `g_rng_call_count` ↔ retail hook on
`FUN_005041f6`) and reports per-frame consumption:

- `rngcalls ALIGNED` — same per-frame consumption; a seeded RNG diff is then a
  genuine value bug, and sparkle/dust diffs are clean.
- `rngcalls DESYNC` — the targets consume different amounts; the row names the
  first divergent db054 + the net port−retail delta. A **negative** delta = the
  port under-consumes = a missing/under-emitting consumer (often an unported
  particle emitter through `FUN_00447f4f` `scene1_spawn`).

`--drill` then captures retail's LCG **caller VAs** for `count+120` frames after
the phasepin (single run, load-jitter immune) and rolls them up by enclosing
function — the who-consumed-RNG list. Cross-check each `FUN_` against the port's
ported consumers; a function retail uses more is the lead. Worked example
(`house-walk-down-dense`): retail consumes via `FUN_00447f4f` (spawn jitter, the
dominant), `FUN_00442cef` (1/frame), `FUN_0048b850` (player ctrl); the port runs
−40 over the window → fewer particle spawns (the missing ambient particles).

**Pragmatic alignment:** if porting a missing consumer is its own rabbit hole,
insert matching **dummy `rng_next15()` calls** tagged `PORT-DEBT(...)` so the
stream stays aligned for comparison, and retire them when the real emitter lands.

### DISCIPLINE: re-check determinism after EVERY rng-touching change

Any change that adds/removes/reorders an `rng_next15` call — porting a consumer,
a dummy stub, a spawn tweak — can desync the stream. **Re-run
`tools/phase_probe.py <scenario>` after it** and confirm `rngcalls` stays
`ALIGNED` (or improves). Treat a new `rngcalls DESYNC` as a regression.

## The draw-side twin

This tool reads the SIM-side counters. For the DRAW side — per-pass NDC-z, anim
cell fingerprint, a measured frame-shift between two unsynced runs — use
`tools/d3d_state_diff.py phase|depth|depthdiff` and
**`docs/render-depth-debugging.md`**. Phase issue → this doc; "billboard occluded
when it shouldn't be" / wrong layer → that one.

## Extending

Companion-focused today (`STD_WATCHES` in `phase_probe.py` + the port
`--player-pos-log` fields). To probe the player or an NPC's anim phase, add its
record VAs to `STD_WATCHES` and the matching field to the port pos-log
(`src/main.c`). The `{phasepin}` reset is likewise companion-only on the agent
side — generalizing it to all live actors is the open follow-up
([[project_next_char_controller]] "auto-settling phase-sync").

Cross-refs: `docs/findings/scene1-tear-visual-diffs.md`, engine-quirks §94,
`docs/trace-workflow.md`, [[reference_phase_divergence_method]].
