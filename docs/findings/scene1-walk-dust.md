# HOUSE free-roam walk dust (records-A type 0xe)

The faint **white wispy smoke puff that kicks up at Recette's feet while she
walks** in HOUSE free-roam. Subtle — you must zoom her feet to see it (retail
ref: feed `20260601T122354_6f81`, and `runs/walkdust/verify_4965.png`). The port
floor was bare. Identified 2026-06-01.

## What it is

A **records-A particle, type 0xe**, emitted at the player's feet while grounded
and moving, drawn as an effect.bmp billboard on the floor. NOT the overlay
system, NOT Tear's wing-glow (0x1f), NOT the daea4/da1bc emits (those are gated
OFF in HOUSE free-roam — verified by Frida watch: `daea4==daea0==dae9c==0`,
`da1bc==0` throughout the walk).

## Ground truth (how it was found)

- Visual: `tests/scenarios/house-walk-tables` golden-retail vs port, zoomed at
  feet. Confirmed dust present in retail, absent in port.
- Frida watch of the records-A per-slot **type** field during walk vs idle
  (`runs/walkdust-types`): type **0xe** jumps from ~277 (idle) to ~1645 (moving)
  — ~6× — while Tear's 0x1f stays flat. (Tooling: 16 `--watch tN=0x069b2fb0+N*0x94:s32`.)
- d3d-trace over the walk window (anchor-relative, see below): the 0xe group draws
  at **ret_va 0x41e97b** in `FUN_004176ff`, texture = effect.bmp (`0x17291e50`,
  same sheet as the wing-glow), blend **SRCALPHA / INVSRCCOLOR** (D3DRS 19=5, 20=4).

> **Tooling note:** added anchor-relative d3d-trace arming to the Frida agent —
> `segtraceOnSegmentEnter` now adds `base + {capture}±2` frames to
> `g_d3d_trace_frames` when d3d-trace is enabled with a (non-null) frame Set. The
> load jitter shifts the anchor by *hundreds* of frames run-to-run, so absolute
> `--d3d-trace-frames` miss the walk; drive with a segtrace + a dummy frame
> (`--d3d-trace-frames 999999`) so the Set is non-null and the captures populate it.

## The emit (FUN_0048b850, decomp 48b850.c L457-476, asm 0x48c758-0x48c821)

In the controller tail, after the physics integrator `FUN_00483170` and
`FUN_0048a833`, gated on **player at ground height** (`DAT_056da1dc ==
DAT_056daf88`) and **moving** (`sqrt(daac4² + daabc²) > 0.1`, the horizontal
velocity) and **not paused** (`DAT_0438b1a0 == 0`):

- every 16th frame (`(DAT_056db054 & 0xf) == 0`): emit one type-0xe particle.
- plus an EXTRA emit when running (`DAT_056db034 == 1`).

Each emit (Ghidra dropped the args; recovered from objdump — the engine reuses
the RNG arg slots as the `FUN_00447f4f` arg slots, which is why the RNG's first
arg reads `0xe` = the particle type):

```
xj = (rng() - 0.5) * 0.5            # ∈ [-0.25, 0.25]
zj = (rng() - 0.5) * 0.5
FUN_00447f4f(0,
             DAT_056da1d8 + xj,     # x = player.x + jitter
             DAT_056da1dc + 0.5,    # y = player.y + 0.5  (just above floor)
             DAT_056da1e0 + zj,     # z = player.z + jitter
             0xe,                   # type
             0.125,                 # scale field (0x3e000000)
             1)                     # param_7
```

Constants: `0x51935c = 0.5f`, `0x51998c = 0.125f`.

## Spawn + tick (ALREADY PORTED — dormant, nothing emitted 0xe in HOUSE)

- Spawn: `scene1_spawn.c` (FUN_00447f4f) covers type 0xe.
- Tick: `scene1_particles_tick.c` L794/L1942 — "types 0xe/0x2b/0x1b/0x3b/0x76,
  gated scaled-drift", **kill at age 0x20 (32)**.

## The renderer arm (FUN_004176ff, decomp 4176ff.c L4958-5089, draw ret_va 0x41e97b)

Shared arm for types **0xe / 0x2b / 0x1b / 0x3b / 0x59 / 0x67 / 0x76**. Per live
records-A slot of one of those types (`piVar13[1]=age`, `piVar13[2]=scale field`,
`piVar13[-0xc/-0xb/-10]=pos x/y/z`):

- **scale** `s = (age*0.0004 + 0.02) * scale_field`, then billboard scale `s*0.8`.
  (type 0x67 uses 0.00015 instead of 0.0004.)
- **brightness** `b = 0x80` base; if `age > 0x10` then `b += (0x10 - age)*8`
  (fades to 0 at age 26); clamped 0..0x7f-ish.
- **diffuse** for type 0xe: grey `0xff·b·b·b` (= 0xff808080 at b=0x80) — the white
  comes from the texture; the diffuse is a grey age-fade (same idea as the
  wing-glow 0x1f arm, different per-type color builders for 0x3b/0x59/0x67/0x76/0x2b).
- **texture** effect.bmp (engine DAT_073cc8c0 = g_sysassets.effect_bmp).
- **UV**: U cell `c = min(age/3, 4)` → animates through 5 cells over its life;
  `u0 = (c*32 + 0.5)/256`, `u1 = (c*32 + 31.5)/256`. V fixed `0.6269531 .. 0.7480469`
  (≈ texel row y 160..191). So the dust cells are effect.bmp **row y≈160, columns
  x 0..159** (five 32×32 cells).
- **blend** SRCALPHA / INVSRCCOLOR (soft additive-ish white; NOT the 0x1f ONE/ONE).
- billboard world matrix `DAT_0438cdf8` (same as 0x1f), DrawPrimitiveUP strip,
  2 tris, 4 verts × 0x18, vbuf `&DAT_0064bf68`.

## Port status (2026-06-01)

LANDED (in tree, faithful, no regressions):
1. Emit — `scene1_player_ctrl.c` `player_ctrl_b850_foot_dust()`, called from
   `player_ctrl_b850_move()` after the damp.
2. Render — `src/scene1_walk_dust.c` (type-0xe arm), wired in `scene1_render.c`
   right after the wing-glow (the engine-faithful FUN_004176ff position), with
   MAG/MINFILTER=LINEAR + **MIPFILTER=NONE** (full-state extract at the 0x41e97b
   draw — without it the tiny dust samples a mip-averaged grey square).
   Spawn + tick were already ported (`scene1_spawn` / `scene1_particles_tick`).

Verified 1:1: emit cadence (every 16 frames, ground-truth probe median 16),
spawn (4 RNG: vel + rot.z), tick (drift/damp/kill 0x20), render state (matches
the retail full-state extract exactly).

## Depth — b1acf7c REVERTED (it broke the glow + shadow); still OPEN

> **2026-06-01 UPDATE — commit b1acf7c was WRONG and is reverted.** Making the
> player/companion sprites write Z with `ALPHATEST ref 0 GREATEREQUAL` passes
> *every* texel, so the whole **transparent** sprite quad laid down a Z
> footprint — an invisible occluding RECTANGLE at the actor's depth. That
> rectangle (a) occluded **Tear's own wing-glow** (drawn later, z-tested) → the
> blue glow vanished, and (b) punched a rectangular hole in the dust/shadow
> **around Recette** → the "shadow bug" the user reported. Both confirmed
> visually (port-vs-retail walk-down-dense). Reverted to the pre-b1acf7c state
> (sprites don't write Z); the glow is restored.
>
> **The real fix (TODO):** retail almost certainly alpha-tests the sprite with
> `ref > 0` so **only the opaque silhouette** writes Z (the dust behind her body
> is occluded; the glow beside Tear and the floor around her are not). Re-capture
> the *exact* sprite ALPHAREF/ALPHAFUNC/ZWRITE at the retail draw **and** confirm
> the wing-glow's draw order/Z relative to the sprite via a fresh d3d-trace before
> re-attempting. Do NOT use ref 0.

### Retail free-roam draw order — GROUND TRUTH (runs/walkdust-d3d, frame 5495)

Mined from the existing retail d3d-trace (ret_va + live Z/alpha/blend state per
draw; add 0x400000 to the trace's ret_va):

| # | ret_va | function | Z | blend | note |
|---|--------|----------|---|-------|------|
| 1 | 0x45ae4a | FUN_0045aa36 player/companion shadow | ZEN1 **ZWR0** | ZERO/SRCCOLOR | multiply-darken (ported, Csh.1) |
| 2 | 0x46f722 ×6 | FUN_0046f648 **furniture/object shadows** | ZEN1 **ZWR0** | ZERO/SRCCOLOR | **STUBBED in the port** |
| 3 | 0x45aa31 ×7 | FUN_0045a56f player+companion+NPC sprites | ZEN1 **ZWR1**, AREF0 GE | SRCALPHA/INVSRCALPHA | sprites DO write Z |
| 4 | 0x41e165 ×8 | FUN_004176ff wing-glow | ZEN1 ZWR0 | ONE/ONE | z-tested |
| 5 | 0x41e97b ×2 | FUN_004176ff dust | ZEN1 ZWR0 | SRCALPHA/INVSRCCOLOR | z-tested |
| 6 | 0x405396 / 0x4063bc | 2D HUD | ZEN0 | — | last |

**So retail *does* write Z on the sprites with AREF 0 — b1acf7c's STATE reading
was correct.** The reason it broke the PORT but not retail is **Z values /
positions**, not the render state:
- The wing-glow (#4) is z-tested. In retail it survives because Tear's sprite Z
  (#3) sits *behind* the glow at her correct hover position. In the port Tear is
  **slightly mis-positioned** (the persistent companion bug, [[project_next_char_controller]]),
  so her quad's Z landed *in front of* the glow → occluded it.
- The dust (#5) reads behind the walker in retail because the player's opaque-ish
  sprite Z is in front of it; the port reproduced the clip but, with the full-quad
  AREF-0 Z + the mis-positioned actors, it rectangularly cut the dust around her.

**Faithful path:** the only thing the dust occlusion actually needs is the
**sprite Z-write** (#3, ZWR1 AREF0) — that is what makes the dust read behind the
walker. The shadow passes (#1/#2) are **ZWRITE=0** (multiply-darken), so they do
**NOT** write depth and are **irrelevant to the occlusion** (an earlier note here
wrongly listed "port furniture shadows first" as a prerequisite — corrected). The
real blocker is **Tear's position**: re-enabling the sprite Z-write occludes her
own wing-glow because her mis-positioned quad lands in front of the glow. So:
(a) fix Tear's hover position so her Z matches retail; then (b) re-enable the
sprite Z-write. Until (a), the sprite Z-write must stay OFF (current reverted
state) or the glow dies again. (Porting the furniture/object dynamic shadow-blob
pass FUN_00470385/FUN_0046f648 is a *separate, minor, independent* cosmetic chip —
the visible furniture shadows already match retail via the baked 3D meshes; see
[[confirmed-parity-ledger]].)

The original (mis-)diagnosis below is kept for the record.

> **Correction to an earlier misread.** I first labelled the draw `0x45aa31` in
> the walking d3d-trace as "shadow" and concluded the free-roam player was a late
> 2D sprite (`0x405396`). WRONG. Per Cchr.1's quad_hist+SetTransform identity
> (docs/findings/scene1-char-sprite-trace.md), **`0x45aa31` is `FUN_0045a56f`
> drawing the player + companion + object SPRITES** (it spans 0x45a56f-0x45aa35);
> `0x405396`/`0x4063bc` are the 2D **HUD**; the shadows are `0x45ae4a`
> (FUN_0045aa36) / `0x46f722` (FUN_0046f648). So the free-roam player IS the 3D
> `FUN_0045a56f` sprite the port already draws (via `sw_pass_light`), drawn before
> the dust — the lesson: attribute draw VAs via the quad_hist identity table, not
> by guessing from the address.

Retail's player draw (`0x45aa31`, full-state extract `runs/walkdust-d3d` f5495):
**ZENABLE=1, ZWRITEENABLE=1, ALPHATEST ref 0 GREATEREQUAL** (pass-all → the whole
sprite quad lays down a Z footprint), blend SRCALPHA/INVSRCALPHA. The port's
`sw_pass_light` set no Z state → inherited ZWRITE=0 → the player wrote no depth,
so the later z-tested dust (and wing-glow) weren't occluded. Fix: set
ZENABLE/ZWRITEENABLE/ALPHATEST around the `sw_pass_light` actor loop (restore
ZWRITE off after). The dust stays at its engine position (after the wing-glow)
and is now occluded by the player's Z. See [[feedback_full_path_call_graph]].

Related: `docs/findings/scene1-wing-glow.md` (the 0x1f sibling arm),
`docs/findings/scene1-char-sprite-render.md` (the FUN_004552d0 standing path),
`project_freeroam_smoke_effect` memory, engine-quirks.
