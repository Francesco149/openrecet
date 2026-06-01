# Tear companion wing-glow renderer (FUN_004176ff records-A type-0x1f)

**Chip P0.1** (`plans/freeroam-structural-parity.md`), landed 2026-06-01.
Ported in `src/scene1_wing_glow.c`.

## What it is

The bright translucent-blue sparkle that trails Tear (the flying fairy
companion) in HOUSE free-roam. Emitted every 4th frame by the companion
controller (`scene1_companion_ctrl.c` → `scene1_spawn`, type 0x1f, scale
0.1) and aged/killed by `scene1_particles_tick.c` (grav −0.001, damp 0.97,
kill at age 0x20). Those two halves were already ported; this chip adds the
missing **draw**.

## Where it lives in the engine

`FUN_004176ff` (30 KB, the "chr mesh walker", the single biggest scene-1
render function) has a **records-A sweep** (decompile L2608+, base
`&DAT_069b2f80`, count `DAT_0076b960`) with ~30 per-type arms. The type-0x1f
billboard is the **catch arm `{0x1f, 0x64, 0x6d, 0x65, 0x68, 0x6c}`**
(L3818-3921), which draws at **ret_va 0x41e165** via `DrawPrimitiveUP`.

> **Correction to the survey's first guess.** The freeroam plan originally
> pointed at the records-A sweep around L1422 and theorised 0x1f fell into a
> "catch-all else" at ~L1708 with a B=full, R=G=half *diffuse* blue. Both were
> wrong: L1422 is a *different, earlier* records-A pass (additive types
> 0x58/0x93/0x5a/0x56/0x42/0x41/0x61/0x72/0x62 only — 0x1f is not among them and
> falls through to no-draw there); L1708 is a sub-switch *inside* the
> 0x41/0x61/0x72/0x62 arm. The real 0x1f draw is the **main** records-A sweep
> (L2608+), arm L3818, and its diffuse is a **grey age-fade** — the *blue comes
> from the texture*, not the vertex colour.

## The draw recipe (retail ground truth)

Recovered with `tools/dump_wingglow_groundtruth.py` (vbuf read) +
`tools/frida_capture.py --d3d-trace` driven by `traces/house_walk.jsonl`
(state replay at the 0x41e165 draw). Runs: `runs/wingglow-gt*`,
`runs/wingglow-d3d`.

| Property | Value |
|---|---|
| Geometry | static ±256 quad, `D3DPT_TRIANGLESTRIP`, prim_count 2 |
| FVF | `0x142` (XYZ \| DIFFUSE \| TEX1), stride 24 |
| Vbuf | BSS template `&DAT_0064b548` — **never written in code**; geometry+UVs are a fixed template read live from retail |
| UVs | u ∈ [0.251953125, 0.373046875], v ∈ [0.501953125, 0.623046875] — a 32×32 cell of `bmp/effect.bmp` at atlas (64,128); cell centre is bright pale blue (215,233,249) |
| Texture | `bmp/effect.bmp` (engine `DAT_073cc8c0` → `g_sysassets.effect_bmp`), bound at L2013 (ret_va 0x41a25f) before the sweep |
| Blend | `ALPHABLENDENABLE=1`, `SRCBLEND=ONE`, `DESTBLEND=ONE` → **additive** |
| Alpha test | `ENABLE=1`, `REF=0`, `FUNC=GREATEREQUAL` (passes everything) |
| Depth | `ZENABLE=1`, `ZWRITEENABLE=0`; `CULLMODE=NONE`; `LIGHTING=0` |
| Tex stage 0 | `COLOROP=MODULATE(COLORARG1=DIFFUSE, COLORARG2=TEXTURE)`, `ALPHAOP=MODULATE(ALPHAARG1=TEXTURE, ALPHAARG2=DIFFUSE)` |
| Per-slot scale | `SCALE_field * 0.005` (emit passes 0.1 → 0.0005) |
| Per-slot world | `RotZ(rot.z) · billboard(DAT_0438cdf8 = g_scene1_camera_orient) · Scale · Translate(pos)`; rot.z is 0 for the wing-glow |
| Per-slot diffuse | `0xFF<<24 \| i<<16 \| i<<8 \| i` (grey), `i = (age>0) ? (0x7f − 4·age) : 0x7f` — fades with age; stays in [3,0x7f] over the sim's age range so the channel-pack never overflows |

So the colour the eye sees = `diffuse(grey age-fade) × texture(blue glow)`,
additively accumulated over the ~8 live sparkles.

## Why the vbuf had to be read live

`&DAT_0064b548` is in **BSS** (VA 0x64b548 sits past `.data`'s
0x603e00 end), zero at load, and **no code writes its xyz/uv** — it is a
fixed billboard template initialised by machinery not present in the
decompile. The only runtime write is the per-slot diffuse. So the geometry
+ UVs were dumped from a live retail HOUSE free-roam frame rather than
guessed.

## Validation

`tests/scenarios/house-movement` cap_00 (idle free-roam), port vs
retail golden zoomed on Tear (`runs/wingglow-cmp/`). Port now draws the
glow (absent before); brightest glow pixel adds **+67,+100,+140 RGB** —
the expected additive-blue magnitude (`diffuse 0x73 × texel ~249`).

**Open follow-up:** the port's *aggregate* glow is smaller/dimmer than
retail at the matched anchor frame. Per-sparkle rendering is correct, so the
suspects are upstream/confounding, not the renderer: (a) the player
controller is unported → frozen player → Tear (spring-follow) hovers at a
different spot than retail → glow lands offset; (b) Tear's character sprite
palette is unported (renders grey vs retail's blue-tinted), which dominates
the raw pixel diff. Sparkle **count** parity at the frame is not yet
verified (would need a port-side records-A dump). Revisit once the movement
controller (`FUN_0048b850`, plan P4) lands and positions align.

## Two distinct effects: trail sparkles (this chip) vs. the wing billboard (NOT this chip)

User feedback after landing (2026-06-01): the type-0x1f trail sparkles now render
("3-4 faint blue dots at Tear's back") but **Tear's big glowing wings are still
missing**. Investigation (`runs/wingglow-d3d` d3d-trace, retail wing crop
`runs/wingglow-cmp/retail_wing_tight.png`) shows the wings are a **separate
effect**, NOT a particle:

- Retail's "wings" = a large, **solid translucent-cyan triangular billboard** at
  Tear's body — not a cluster of sparkles.
- It is **not a records-A/B particle**: the GT dump shows all live records-A slots
  are type-0x1f sparkles (scale 0.1), and records-B is empty in free-roam.
- Near Tear, `FUN_0045a56f` (the chr-sprite leaf, ret_va 0x45aa31) draws **two**
  billboards: the body (alpha-blend `src5/dst6`, tex 0x172bc130) **and an additive
  one** (`src2/dst2`=ONE/ONE, tex 0x172bc650, separate vb). The additive draw is
  the wing.
- The engine callers confirm it: `FUN_00456f56` (chr walker) invokes
  `FUN_0045a56f` per actor with different tints — the body pass uses grey
  `0xff7f7f7f` (L68), the **wing pass uses blue `…| 0x7f7fff`** (L120, sprite index
  `DAT_056da1cc`). So the wing is the **additive blue second pass of the
  character-sprite renderer**.

**Why the port doesn't show it:** the chr-sprite per-actor / people passes
(`scene1_chr_walker.c` FUN_00456f56, `scene1_chr_prepass.c` Section C) are
**dormant** — gated on the actor/people-table populator **FUN_00436f97** (people
base returns NULL today). Tear's *body* renders via the companion-controller path
(§71), but that path does not yet issue the additive blue wing pass.

**Next chip (separate, not the particle wing-glow):** port the additive blue
chr-sprite pass for the companion — either by wiring `FUN_00456f56`'s pass-2
(L120, blue tint + the glow sprite) into the companion render, or by un-dormanting
the chr_walker/chr_prepass actor draws once FUN_00436f97 populates the tables.
Needs: the wing sprite index (`DAT_056da1cc`), its texture, the `0x7f7fff` tint,
and the additive envelope (already set up in `chr_prepass_ab_setup`). Tracked
separately from this records-A 0x1f chip, which is correct + complete.

## PORT-DEBT

`PORT-DEBT(partial, FUN_004176ff)` — only the records-A 0x1f arm is ported;
the other ~30 type arms + the records-B passes + the function's full per-pass
state sequencing remain in the `scene1_walk_chr_TODO` stub.
`PORT-DEBT(simplified, FUN_004176ff L3876)` — the boosted-glow sub-branch
(`if (DAT_0438b8f8 == 2) { intensity*=2; scale*=3; }`) is not ported
(`DAT_0438b8f8` unexposed, 0 in free-roam → dormant).
