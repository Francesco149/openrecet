# HOUSE free-roam walk dust (records-A type 0xe)

The faint **white wispy smoke puff that kicks up at Recette's feet while she
walks** in HOUSE free-roam. Subtle — you must zoom her feet to see it (retail
ref: feed `20260601T122354_6f81`, and `runs/walkdust/verify_4965.png`). The port
floor was bare. Identified 2026-06-01.

## Reproducible reference for the OCCLUSION bug (2026-06-03)

The dust is rendering but **not occluded by Recette's body** (it draws over her
feet/legs instead of behind). Canonical reproducible frame to debug against, from
the anchor-segmented + RNG-pinned trace `dlg-skip-8604` (bit-exact across runs):

- Feed trace `20260603T125428_c63b`, frame **f=828** (`frame_0186.png`), crop
  **`box=600,449,635,486`** (1024×768) shows the dust NOT occluded.
- Saved: `docs/findings/refs/dust-not-occluded_8604_f828.png`.
- Reproduce: `distill_trace.py runs/recordings/dlg-skip-8604.raw.jsonl
  --anchor-segments` → `export_trace.py --caprange 0,505`. See `docs/trace-workflow.md`.

The uncommitted GREATER sprite Z-write in `src/scene1_shop_walker.c` is the
candidate fix to verify against this frame. Occlusion is a DEPTH-relationship gap
(dust emit Y vs body), not a Z-state bug — see the §2026-06-03 note below.

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
  (#3) sits *behind* the glow. In the port, re-enabling the sprite Z-write put
  Tear's quad Z *in front of* the glow → occluded it. WHY her quad differs from
  retail is **not isolated** — Tear is a persistent not-1:1 ([[project_next_char_controller]])
  but it could be a **position** error or an **animation-phase** error, and the
  wing-flap not being exactly 1:1 per frame adds comparison noise.
- The dust (#5) reads behind the walker in retail because the player's opaque-ish
  sprite Z is in front of it; the port reproduced the clip but, with the full-quad
  AREF-0 Z + Tear's off appearance, it rectangularly cut the dust around her.

**Faithful path:** the only thing the dust occlusion actually needs is the
**sprite Z-write** (#3, ZWR1 AREF0) — that is what makes the dust read behind the
walker. The shadow passes (#1/#2) are **ZWRITE=0** (multiply-darken), so they do
**NOT** write depth and are **irrelevant to the occlusion** (an earlier note here
wrongly listed "port furniture shadows first" as a prerequisite — corrected). The
real blocker is **Tear's not-yet-1:1 appearance** (position and/or animation phase,
**not isolated** — investigate closely later, once everything else is spot on, per
the user 2026-06-01): re-enabling the sprite Z-write occludes her own wing-glow.
So: (a) isolate + fix Tear's divergence so her Z matches retail; (b) THEN re-enable
the sprite Z-write. Until (a), the sprite Z-write must stay OFF (current reverted
state) or the glow dies again. (Porting the furniture/object dynamic shadow-blob
pass FUN_00470385/FUN_0046f648 is a *separate, minor, independent* cosmetic chip —
the visible furniture shadows already match retail via the baked 3D meshes; see
[[confirmed-parity-ledger]].) (Porting the furniture/object dynamic shadow-blob
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

## 2026-06-03 — Z-write re-attempted with GREATER; depth, not state, is the gap

Re-enabled the sprite Z-write in `sw_pass_light` but with **ALPHAFUNC=GREATER**
(ref 0 → alpha>0, opaque-silhouette only), not GREATEREQUAL. The bg-NPC session
(2c96b97) re-read the SAME frame-5495 trace as **AFUNC=5 = D3DCMP_GREATER** (the
b1acf7c row table above says "GREATEREQUAL" — that was the misread that punched
the square cut-outs). GREATER is **glow-safe**: an intra-port baseline-vs-fixed
A/B (`house-walk-down-dense`) is **bit-identical on every frame where Tear's glow
is visible** — the b1acf7c regression (transparent quad writing Z) is gone.

**But the fix is a visual no-op so far, and the reason is DEPTH, not render
state.** Two confirmations:
- The emit Y offset is **decompile-confirmed faithful** (not eyeballed):
  `FUN_0048b850` L463-475 spawns `FUN_00447f4f(0, X+jit, DAT_056da1dc+0.5,
  Z+jit, 0xe, 0.125)` — i.e. the dust sits at **player_y + 0.5**. The port matches
  (`scene1_player_ctrl.c player_ctrl_b850_foot_dust`).
- With ZFUNC **LESSEQUAL** and the dust at `py+0.5` (higher in world-Y → nearer a
  down-tilted camera than the character billboard at `py`), the dust depth is ≤
  the sprite depth where they overlap, so it **passes the z-test and draws in
  front regardless of the sprite Z-write**. User-confirmed: the faint foot-dust
  wisps over Recette's body are NOT occluded even with the fix
  (`20260603T024835_2a6c`). Retail occludes the same dust (the dress hem covers
  its top, `20260603T022400_3466` cap_14) — so retail's character body Z must be
  *nearer* than the dust at the overlap, which the port doesn't reproduce yet.

So the occlusion needs the **character/dust DEPTH relationship** fixed (why retail
occludes at `py+0.5`), not just the Z-write. The Z-write is kept (faithful state,
glow-safe) but **uncommitted** pending a reproducible dense-dust frame. That frame
now exists via the new **`{rngseed}` segtrace op** (commit d553861,
[[scene1-rng-stream-parity]]): record a dense-dust walk, distill, and the recorded
dust reproduces on playback (and on retail, same LCG stream) so the depth can be
pixel-diffed. Next: capture the dust + character vertex Z (the existing
`runs/walkdust-d3d` only logs the vb pointer) and find why retail's body occludes
the `py+0.5` dust.

## 2026-06-03b — RESOLVED (geometry): fresh feet-dust is *supposed* to draw in front; only TRAILING dust is occluded

Mined the WORLD/VIEW/PROJ matrices straight out of the retail d3d trace
(`runs/walkdust-d3d`, the `SetTransform` rows it already logs) instead of guessing.
Result, now in [[engine-quirks]] §92: the char and dust billboards share the
camera-facing orientation `DAT_0438cdf8`, which makes **each quad perpendicular to
the view axis → a single constant view-depth = the depth of its anchor**. The char
anchor is the **feet** (`FUN_004552d0` `(px,py,pz)`; `FUN_00456f56`
`(actor.x,actor.y,actor.z+0.02)` — both Y≈0 at the floor, confirmed: retail char
anchor Y = 0.000 every frame); the dust anchor is **`py+0.5`**. Transforming both
anchors through the retail VIEW (pitch rows `0.5547/0.8321`):

- A point `0.5` higher in world-Y is ~`0.42` view-units **nearer** the down-tilted
  camera. So **fresh dust at the current feet is geometrically NEARER than the
  whole (constant-depth) char quad → it draws in front, with the Z-write ON or OFF.**
  This is exactly what the port shows at the repro frame, and it is **correct retail
  behaviour** — not a bug. Projecting char-anchor vs dust-anchor to NDC-z per frame:
  fresh-dust frames (f5495–5528) read **dust in front**; only after the dust drifts
  **behind the walker's feet-plane** (f5597+, walking toward camera) does it read
  **behind** and get occluded by the body's feet-depth Z footprint.

**So the occlusion the Z-write actually buys is for TRAILING dust** (puffs left
behind as she walks toward the camera), NOT the fresh puff at her feet. The earlier
"dust not occluded" repro (`frame_0186`, standing/just-moving, fresh dust over the
lower dress) is the *expected* in-front case — verifying the Z-write against it was
testing the wrong frame. **Open / next:** A/B the Z-write ON vs OFF on a
**trailing-dust** frame (she walking toward camera, an older puff overlapping her
torso) to confirm the body occludes it — that is the frame that proves whether the
uncommitted `sw_pass_light` Z-write should land. Reproduce via the now-stable
anchor-gated export (`runs/trace-export/dust-fixed`, `frame_NNNNN` = anchor-relative).

> **Tooling fixed alongside (2026-06-03):** `export_trace.py` now auto anchor-gates
> raw recordings and renumbers frames to anchor-relative 0-based, so a `frame_NNNNN`
> reference is jitter-immune (the documented `frame_0186` reproduces bit-exactly from
> a fresh raw replay). `feed.py montage` refuses trace-export dirs → use the `trace`
> card. See `docs/trace-workflow.md`.
