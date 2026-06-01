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
`FUN_0046f621()` (ambient motes) **before** `FUN_0048b850()` (the controller
whose tail emits the dust). So the motes churn the shared LCG *ahead of* the
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
| `0x46cf81` (int) | `FUN_0046c9a2` (3800 B) | ~0.27 / frame, steady | ❌ UNPORTED — called from the **render root** (`FUN_004547ab → FUN_0046c090 → FUN_0046c9a2`). **IDENTIFIED 2026-06-01: this is the dialogue text-box DRAW** (per-char reveal + the `TEXT_ANIM_END` flag `DAT_073a3e04`) — see `opening-prologue.md` §RESOLVED. Porting it closes BOTH the dialogue front and this dust-RNG front. (Not the hikari/ambient-glow — that guess is retired.) |
| `0x46f56b…0x46f5dc` (int+float) | `FUN_0046f2a3` (894 B) | sporadic (bound-cross respawns) | ❌ STUBBED — the **ambient motes** (`FUN_0046f621` no-op'd as `player_ctrl_prologue_churn`). 6 motes live in HOUSE (`DAT_005c7dd4==6`). |
| `0x49018c` / `0x490e56` cluster | `FUN_0049001c` / `FUN_00490e56` | **intro-only** (absent from free-roam windows) | n/a — new-game save/news/order generation; not a steady-state desync source. |

`0xbb21033` in the float-hooked run is a Frida-relocation artifact (the
`FUN_00471089` body's internal `call FUN_005041f6` after Interceptor relocated
the adjacent `0x471084` thunk) — it equals the float total, **not** a real
consumer; ignore it.

## Port vs retail — the measured gap

Port instrumented with a per-frame `g_rng_seed` dump (`--player-pos-log` now
carries `"rng"`); same trace; per-frame LCG-step counts:

- **Retail steady free-roam:** every frame consumes ≥1 LCG step; the wing-sparkle
  adds 6 every 4th frame → a `7,1,1,1` cadence, plus sporadic mote/effect spikes.
- **Port steady free-roam:** `{0: most frames, 6: every 4th frame}` — the sparkle
  matches, but the port consumes **0** on the in-between frames where retail
  consumes ≥1.

The missing per-frame consumption = **`FUN_0046c9a2`** (steady) + **the motes**
(`FUN_0046f2a3`, sporadic). Both must be ported, RNG-faithful and in the engine
call order, before the foot-dust jitter can phase-match retail.

## Implication / plan

Foot-dust *position* parity is an **RNG-stream-completeness** problem, not a
dust-renderer bug. Closing it = porting the remaining free-roam RNG consumers
faithfully (each is also a *visible* gap):

1. **Ambient motes** — `FUN_0046f621` + `FUN_0046f2a3` (sim) + `FUN_0046f648`
   (render). Self-contained, visible (6 floating motes), in plan P3.
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
  --player-pos-log runs/mote-probe/port_pos.jsonl --max-frames 8000 --turbo --silent-audio --hide-window
```

Related: `scene1-walk-dust.md` (the dust chip), `scene1-wing-glow.md` (the 0x1f
sibling), `project_freeroam_smoke_effect` / `openrecet_house_brightness_resolved`
memories, `docs/plans/freeroam-structural-parity.md` P3.
