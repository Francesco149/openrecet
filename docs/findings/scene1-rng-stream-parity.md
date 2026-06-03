# HOUSE free-roam RNG-stream parity (foot-dust position/phase divergence)

> Started 2026-06-01. The walk-dust chip (records-A 0xe) landed bit-faithful
> emit/spawn/tick/render, but the **dust jitter positions still diverge from
> retail** (commit 3182f80 noted "RNG-stream desync"). This doc nails the cause
> with a live retail probe and maps the work needed to close it.

## The generator

One global LCG drives **all** gameplay/particle randomness:

```
FUN_005041f6:  DAT_006023a0 = DAT_006023a0*0x343fd + 0x269ec3;  return (DAT_006023a0>>16)&0x7fff   // int, "rng_next15"
FUN_00471089:  return (FUN_005041f6() & 0x7fff) / 32768.0                                          // float [0,1), "rng_next_unit"
```

So `rng_next15` (int, via the `0x471084` jmp-thunk) and `rng_next_unit` (float)
**share one state** `DAT_006023a0`. The port mirrors this exactly (`rng.c`,
`g_rng_seed`). The foot-dust emit reads `rng_next_unit()` ×2 (z then x jitter).
For the dust positions to match retail, `DAT_006023a0` must be at the **same
phase** when the dust reads it — i.e. **every prior per-frame consumer must
match**, cumulatively.

## The call order (why upstream consumers matter)

In `FUN_0048670f` (the free-roam controller dispatcher), every arm calls
`FUN_0046f621()` (background-window NPCs) **before** `FUN_0048b850()` (the controller
whose tail emits the dust). So the NPCs churn the shared LCG *ahead of* the
dust read, each frame.

## Live retail probe — the per-frame consumer map

Tooling added this session: **`frida_capture.py --rng-callers`** hooks
`FUN_005041f6` (+ `FUN_00471089` for the float breakdown, recorded under a `u:`
prefix) and tallies the immediate caller VA → `<run_dir>/rng_callers.json` +
per-200-frame snapshots in `rng_callers.jsonl` (so windows can be diffed).
Run over `tests/scenarios/house-walk-down-dense/trace.jsonl`
(`runs/rng-callers/`).

Diffing a **steady free-roam window** (cumulative snapshot deltas) isolates the
per-frame consumers from one-time intro work:

| caller | function | rate | port status |
|--------|----------|------|-------------|
| `0x44a750…0x44a86b` (float ×6) | `FUN_00447f4f` (`scene1_spawn`) | **6 calls / 4 frames** | ✅ MATCHES — the wing-sparkle emit (type 0x1f, every 4th frame). Port does exactly this. |
| `0x46cf81` (int) | `FUN_0046c9a2` (3800 B) | ~0.27 / frame, steady | ⚠️ PARTIAL — `FUN_0046c9a2` (the dialogue DRAW) is now ported (`scene1_dialogue_draw.c`, 2026-06-02), **but `0x46cf81` is NOT the per-char reveal**. It is the return addr of the **standee-shake** rng read (`call 0x471084` @ `0x46cf7c`, decompiled line 191), gated on `DAT_073a6d9c != 0` — the `rmb:` chr-shake countdown. The bg-shake sibling read @ `0x46cc51` (gated `DAT_073a6d98`) does NOT appear in the free-roam window because the bedroom/HOUSE bg is static (the read is in the scroll branch `DAT_073a6d84|DAT_073a6d94 != 0`). **STILL UNPORTED:** the `IVE_OP_RMB` exec handler (set `shake_bg/shake_chr`), the per-step decay (`FUN_0046c320` 101-105), and the two gated rng reads in the draw. These are the remaining consumer — see the rmb note below. |
| `0x46f56b…0x46f5dc` (int+float) | `FUN_0046f2a3` (894 B) | sporadic (bound-cross respawns) | ✅ PORTED — the **background-window NPCs** (`scene1_bg_npc`, FUN_0046f621/2a3/648/737). 6 NPCs live in HOUSE (`DAT_005c7dd4==6`). See [[scene1-bg-npc]]. |
| `0x49018c` / `0x490e56` cluster | `FUN_0049001c` / `FUN_00490e56` | **intro-only** (absent from free-roam windows) | n/a — new-game save/news/order generation; not a steady-state desync source. |

`0xbb21033` in the float-hooked run is a Frida-relocation artifact (the
`FUN_00471089` body's internal `call FUN_005041f6` after Interceptor relocated
the adjacent `0x471084` thunk) — it equals the float total, **not** a real
consumer; ignore it.

## The `rmb` screen-shake consumer (scoped 2026-06-02)

The `0x46cf81` consumer is the **standee-shake** rng read inside the dialogue
draw. It only fires while `DAT_073a6d9c > 0`, which the `rmb:a,b` command sets
(handler `0x46d926` → `shake_bg = atoi(a)+1`, `shake_chr = atoi(b)+1`), decayed
one-per-step by `FUN_0046c320`. Read offset = `(rng() & 0x1f) - 0x10` → ±16 px
of **Y jitter** per active standee per frame (x is untouched).

Confirmed by extracting the scripts (`tools/extract/data-bin.py`):
- **`iv1_1.ivt:117`** has a live `rmb:40,40`, fired right before the
  **"WAKE UP, PLEASE!"** line (`intro-dialogue-lines` **cap_05**). So 41 frames
  of bg+chr shake straddle that line; since the line reveals in ~16 frames the
  shake is *still active* at the cap_05 TEXT_ANIM_END anchor.
- **`iv1_2.ivt:23`** has it **commented out** (`//rmb:40,40`).

**Why the bedroom doesn't visibly shake the bg:** the bg-shake read (`0x46cc51`,
`DAT_073a6d98`) lives in the scroll branch (`else` of
`DAT_073a6d84==0 && DAT_073a6d94==0`). iv1_1's bg is static → that branch is
skipped → `shake_bg` decays unused. Only the **standee** Y-jitter is visible.

**Verification blocker for the prologue path:** at cap_05 the standee-shake
Y-offset can't be cleanly isolated in a port↔retail diff because the standee
**tween** PORT-DEBT (settled-pose-only; see `opening-prologue.md`) already
diverges the character silhouettes there. So porting the rmb reads is **not
prologue-anchor-verifiable** until the tweens land. Its clean verification stays
the free-roam **foot-dust phase** check (the Reproduce recipe below). Sequencing:
either (a) port standee tweens first to deconfound cap_05, or (b) verify rmb via
the free-roam `--rng-callers` pipeline against the retail host. Left unimplemented
this session to avoid landing an unverified RNG-consuming render change.

## Port vs retail — the measured gap

Port instrumented with a per-frame `g_rng_seed` dump (`--player-pos-log` now
carries `"rng"`); same trace; per-frame LCG-step counts:

- **Retail steady free-roam:** every frame consumes ≥1 LCG step; the wing-sparkle
  adds 6 every 4th frame → a `7,1,1,1` cadence, plus sporadic NPC/effect spikes.
- **Port steady free-roam:** `{0: most frames, 6: every 4th frame}` — the sparkle
  matches, but the port consumes **0** on the in-between frames where retail
  consumes ≥1.

The missing per-frame consumption = **`FUN_0046c9a2`** (steady) + **the NPCs**
(`FUN_0046f2a3`, sporadic). Both must be ported, RNG-faithful and in the engine
call order, before the foot-dust jitter can phase-match retail.

## Implication / plan

Foot-dust *position* parity is an **RNG-stream-completeness** problem, not a
dust-renderer bug. Closing it = porting the remaining free-roam RNG consumers
faithfully (each is also a *visible* gap):

1. **Ambient NPCs** — `FUN_0046f621` + `FUN_0046f2a3` (sim) + `FUN_0046f648`
   (render). Self-contained, visible (6 floating NPCs), in plan P3.
2. **`FUN_0046c9a2`** (via `FUN_0046c090`, render root) — the steady per-frame
   consumer; likely the hikari/ambient-glow effect (also a candidate cause of the
   dust's *brightness* divergence — "counter-shadow lightening" — if it darkens
   the floor the dust composites over).

The wing-sparkle (`scene1_spawn` 0x1f) is already faithful and need not change.

## Reproduce

```
nix develop --command python3 tools/frida_capture.py --remote cutestation.soy:27042 \
  --run-dir runs/rng-callers --input-segtrace tests/scenarios/house-walk-down-dense/trace.jsonl \
  --rng-callers --max-frames 6000 --duration-ms 150000 --turbo --silent-audio --hide-window --no-montage
# port side (per-frame g_rng_seed):
tools/run-openrecet.sh --input-segtrace tests/scenarios/house-walk-down-dense/trace.jsonl \
  --player-pos-log runs/NPC-probe/port_pos.jsonl --max-frames 8000 --turbo --silent-audio --hide-window
```

## Reproducible RNG for recorded traces — the `{rngseed}` segtrace op (2026-06-03)

The RNG-stream work above (and any future RNG parity, e.g. bg-NPC frame-exact
timing) needs a recorded trace to **reproduce its RNG-driven behaviour on
playback** — otherwise the `distill --house-segtrace` rebase onto a standard
intro leaves the LCG at a different phase than the live recording, and the dense
occludable-dust frame you recorded won't recur. Solved by a new segtrace op
(commit d553861):

- `src/main.c trace_rec_start()` snapshots the live `g_rng_seed` at F2; the raw
  header carries `rng_seed_at_start`.
- `tools/distill_trace.py` emits `{"rngseed":[frame,value]}` at the recorded
  segment's first frame (house: `[1565, S]`; flat: `[0, S]`).
- On playback the op forces the global LCG to `S` **before** that frame's sim
  consumers (port: `input_segtrace_tick` rng_cb → `rng_seed()`; retail agent:
  `DAT_006023a0` write in `segtraceTick`) — so the recorded segment replays
  against the identical LCG state regardless of the prepended intro, and **port +
  retail share one LCG stream from the anchor** (cross-target RNG parity).

So the canonical RNG-parity workflow is now: record (F2…walk…F2) → `distill_trace
--house-segtrace` → `scenario-test --target both`; the recorded window's RNG
positions match frame-for-frame (modulo the load-frame anchor shift, which does
not desync the LCG). Op grammar in `src/input_segtrace.h`.

Related: `scene1-walk-dust.md` (the dust chip), `scene1-wing-glow.md` (the 0x1f
sibling), `project_freeroam_smoke_effect` / `openrecet_house_brightness_resolved`
memories, `docs/plans/freeroam-structural-parity.md` P3.
