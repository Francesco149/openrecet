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

## OPEN: depth — needs the free-roam 2D player-sprite path (NEXT CHIP)

The dust renders in FRONT of the walking player; retail draws it behind. Root
cause is **structural, not in the dust**: a d3d-trace of a free-roam *walking*
frame (`runs/walkdust-d3d`, frame 5495) shows the player+companion are the
**final draws** — the 2D quad batch flush **`0x405396`/`0x4063bc`** (vb
`0x605208`, **ZENABLE=0, ZWRITE=0, blend 5/6**, drawn LAST, after the
FUN_004176ff particle pass incl. the dust at 0x41e97b). There is **no
`FUN_004552d0`/`0045a56f` draw** in the free-roam walking frame. So in free-roam
the player is a late 2D sprite (always on top → occludes the dust); the
`FUN_004552d0` 3D path the port uses (`sw_pass_light`) only draws the player in
the standing/cutscene state (the Cchr.2f/2h pixel validation was frame 17544, a
standing pose). The port draws the free-roam player through the wrong path.

**Fix (full path, user-approved):** port the free-roam 2D character-sprite path
(emitter → `FUN_00404efc` render_quad_add → `FUN_00405354` flush @0x405396,
ZENABLE=0), and gate off the `FUN_004552d0` player draw in free-roam. First step:
identify the 2D character-quad EMITTER VA — quad_hist (`tools/frida_capture.py
--quad-hist`, currently armed only via the dump_records_b drive, not the
segtrace — needs wiring) or static call-graph from the FUN_00404efc caller set
(`404e61/404e98/405b1a/405d70/406c64/406d50/409925/407cac/...`). Then the dust's
engine position (after wing-glow) is correct and depth resolves naturally.
See [[feedback_full_path_call_graph]].

Related: `docs/findings/scene1-wing-glow.md` (the 0x1f sibling arm),
`docs/findings/scene1-char-sprite-render.md` (the FUN_004552d0 standing path),
`project_freeroam_smoke_effect` memory, engine-quirks.
