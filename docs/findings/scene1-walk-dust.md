# HOUSE free-roam walk dust (records-A type 0xe)

The faint **white wispy smoke puff that kicks up at Recette's feet while she
walks** in HOUSE free-roam. Subtle — you must zoom her feet to see it (retail
ref: feed `20260601T122354_6f81`, and `runs/walkdust/verify_4965.png`). The port
floor was bare. Identified 2026-06-01.

## ✅ OCCLUSION RESOLVED 2026-06-04 — it was the records-A z_far (2000→500), NOT a 3D-mesh occluder

The long-open "dust draws in front of the walker" bug — and the §2026-06-03f
conclusion that "the char is ruled out, the real occluder is a 3D mesh (Phase 4)"
— were **both wrong about the cause**. The real cause is the **per-pass projection
`z_far`** the dust draws under (the same mechanism as the Tear wing-glow occlusion,
[[feedback_zfar_depth_footgun]], engine-quirks §93):

- Retail's records-A render (`FUN_004176ff`) builds its **own** projection with a
  hardcoded **`z_far = 500.0`** (`.rdata DAT_005199d8`, objdump-confirmed at the
  `0x419c5c` `D3DXMatrixPerspectiveFovRH` build; near 1.0, aspect 4:3). All its
  effects — the wing-glow **sparkles** (`0x41e165`) AND the foot **dust**
  (`0x41e97b`) — draw at `z_far = 500` (synced d3d-trace, exact).
- The port (`scene1_render.c` L862) pushed **`z_far = 2000`** before
  `wing_glow_render` + `walk_dust_render` (it assumed `FUN_004176ff` inherited the
  wide chr-walker projection). At 2000 the dust's NDC depth is compressed **nearer**
  than the walker's body (which writes Z at `z_far = 1450`), so **every** puff
  passed `ZFUNC=LE` and drew in front.
- Fix: push `z_far = 500.0f` for the records-A effects. The dust depth now spreads
  so puffs that have drifted **behind the walker's feet-plane** fall behind her body
  (ndcz > body) and are **occluded by the dress hem**, while fresh dust at the feet
  stays in front — matching retail. Verified: at cap_03, port dust(Y0.48) ndcz
  0.95432 > Recette body 0.95419 → behind; dust(Y0.56) ndcz 0.95415 < body → in
  front. User-verified in-game 2026-06-04. The dust ZWRITE is **not** involved (the
  b1acf7c full-quad-Z-write hunt was a dead end).
- **Exact 1:1** of *which* puffs occlude still needs the dust spawn **phase/RNG**
  matched to retail (deferred, same baseline as the Tear flap/bob phase).

## Reproducible reference for the OCCLUSION bug (2026-06-03, pre-fix)

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

## 2026-06-03d — INSTRUMENTED both sides at the stationary wall; authoritative vel+age+pos

Pulled per-particle ground truth on BOTH engines at the stationary bottom-wall
(player pinned at **(-0.3,0,9.5)** on both — `--watch px/py/pz` confirms retail
is NOT moving, killing the earlier "trailing dust from a moving player" theory).

**Tools built this session:**
- Port `--dust-log <file>` (`src/scene1_walk_dust.c` + `main.c` +
  `tools/run-openrecet.sh` + `tools/export_trace.py --dust-log`): per live
  type-0xe slot, dumps world pos + projected NDC-z + each actor anchor's NDC-z.
- Retail authoritative dump: `distill_trace --anchor-segments
  runs/recordings/retail-bottomwall.raw.jsonl` → `frida_capture --dump-records-b
  --dump-records-b-offsets <off…>` (offsets are relative to records-A
  first-populate ≈ HOUSE entry; the stationary tail is off ~370–460). The
  `records_a` rows carry **type, age, pos, vel, scale** — the real spawn state.
  Also `--d3d-trace` over the same anchor-pinned replay gives the char+dust draw
  WORLD matrices (→ NDC-z). Runs: `runs/retail-bw-d3d3/4`, `runs/retail-bw-recA/2`,
  `runs/trace-export/bw-dustlog`.

**Confirmed facts:**
- **Char Z-write is a NO-OP here.** Z-ON vs a hard `#if 0` Z-OFF build (only that
  block differs) are **pixel-identical on 0/219 frames** (feet AND glow). The
  uncommitted `sw_pass_light` Z-write does nothing in this scene — it is NOT the
  dust-occlusion fix. (Earlier "it clips the glow" was stale pre-session exports.)
- **Spawn velocity MATCHES** the port formula: retail age-1 vel e.g.
  (+0.18,-0.23,+0.30); vy≈(u-0.5)*0.5, vxz≈trig*u*0.5; damp≈0.95/frame; kill age
  0x20 — all consistent with `init_type_shared_unit_half` + the type-0xe tick.
- **Emit Y=0.5 matches** (retail age-1 Y=0.500 exactly = no Y jitter, = port).
- **The gap is the DUST OCCLUSION (behind-fraction), and it is a POSITION effect,
  not render:** retail dust is **~62% behind the foot** (d3d, 87 draws); port
  **1.9%** (7532) — even projecting PORT positions through the RETAIL matrices.

**Still OPEN — the exact divergent parameter.** The raw Z/Y distributions look
*similar* (below-player-Z: retail 20% vs port 28%; Z median retail 9.57 vs port
9.66; Y median retail 0.61 vs port 0.50 — retail is if anything HIGHER-Y = nearer),
yet the behind-fraction is 62% vs 2%. Sampling (102 retail vs 7532 port) explains
some spread but not a 30× behind-gap. Candidate causes still to pin against the
disassembly + this GT data:
  1. **Emit XZ jitter** — port `xj=zj=(u-0.5)*0.5` (±0.25 each). Retail emit jitter
     (6 samples) xj∈[-0.23,+0.17], zj∈[-0.07,+0.38] — looks ASYMMETRIC/larger; verify
     the real emit math (FUN_0048b850 emit site, the `FUN_00447f4f` arg order).
  2. **The foot/char reference depth** used in the behind test — re-verify the char
     DRAW anchor Z (FUN_00456f56 actor.z+0.02 vs player pos) on both sides; a small
     char-Z offset flips many marginal dust draws.
  3. **RNG-stream / velocity-distribution** parity (the port's per-frame RNG may
     desync, biasing the vxz direction distribution).
Next: a multi-angle disassembly verification of the emit (FUN_0048b850 tail) +
spawn (FUN_00447f4f type-0xe) + tick (type-0xe drift) against the captured retail
vel/age/pos, recomputing the behind-test with a consistent char-anchor on both
sides. (See [[project_freeroam_smoke_effect]] §2026-06-03d.)

### Seed-sweep diff: the SPAWN is BIT-EXACT (2026-06-03d)

Per the user's suggestion — call the particle-param code with a forced RNG seed on
BOTH engines, sweep seeds, diff. Built it for `FUN_00447f4f` (the type-0xe spawn):
- Port: host program seeds `g_rng_seed`, calls `scene1_spawn(0,-0.3,0.5,9.5,0xe,0.125,1)`,
  reads slot 0 vel/rot/scale (`/tmp` sweep; links rng.c+scene1_spawn.c+scene1_records.c).
- Retail: new agent RPC `runRetailDustSpawn` (tools/frida/openrecet-agent.js) — forces
  `DAT_006023a0`, snapshots+empties slot 0, calls `FUN_00447f4f` via NativeFunction
  (cdecl, 7 args), reads back the slot, restores. Driven by a small diff_test.open_frida
  harness (retail spawned suspended, frozen-process call).
- **Result: BIT-EXACT across all 12 seeds** (vel.x/y/z, rot.z, scale, age all identical
  to 6 dp). So the spawn's velocity/rotation math AND its RNG-consumption order are
  faithful — the dust POSITION divergence is NOT in the spawn.
=> Remaining suspect for port=front/retail=behind: the **EMIT jitter** in FUN_0048b850
   (the xj/zj added to the spawn position, 2 rng calls BEFORE the spawn — can't be
   exercised by the spawn-call diff since it lives in the controller), or the type-0xe
   TICK drift. Verify the emit site's exact per-axis jitter math from the disassembly.

## 2026-06-03e — NARROWING (READ THIS FIRST): emit/spawn/tick are bit-exact; the gap is the CHAR DRAW path

Triple-confirmed (disassembly = captured retail GT = port C) — do NOT re-investigate these:
- **EMIT jitter** (FUN_0048b850 tail, asm 0x48c758-0x48c821): `X = px + (R2-0.5)*0.5`,
  `Y = py + 0.5` (NO jitter), `Z = pz + (R1-0.5)*0.5`; both XZ jitters are (rng-0.5)*0.5,
  symmetric **±0.25**; RNG order **Z-first (R1) then X (R2)**. Port
  `player_ctrl_b850_foot_dust` matches bit-exact.
- **SPAWN** (FUN_00447f4f type-0xe = init_type_shared_unit_half): vel.x=sin(a)·u1·0.5,
  vel.z=cos(a)·u1·0.5, vel.y=(u3-0.5)·0.5, rot.z=u4·2π, age=0, scale=param6 verbatim,
  pos=spawn(x,y,z) verbatim (no anchor-back / no ×3 for 0xe). 4 rng draws. Confirmed BOTH
  by disasm AND a **seed-sweep diff** (port host `scene1_spawn` vs retail
  `runRetailDustSpawn` RPC, 12 seeds → vel/rot/scale/age identical to 6dp).
- **TICK** (handle_type_group_drift_e_2b / FUN_0040fb3a type-0xe @ 0x41113d): `pos +=
  vel * SLOT_SCALE(0.125)`, vel `*= 0.95`, age++ unconditional, kill at age 0x20, no rot
  bump. Port matches bit-exact.
- **Dust WORLD position** therefore matches: retail dust mean (z−9.5)=+0.16 (genuinely
  ~0.16 *behind* the foot in world Z), port +0.157 — identical.

Measurement correction:
- The earlier "**retail 62% behind vs port 1.9%**" was an **artifact of an inconsistent
  char-anchor reference**. Recomputed with the char DRAW WORLD-translation as the occluder
  on both sides (= foot z=9.5, projected per-frame): retail **0%** behind the foot anchor,
  port 1.9% — i.e. against the FOOT-ANCHOR depth, BOTH engines draw the dust *in front*
  (the +0.5 Y lift dominates the +0.16 Z push in this projection). So a translation-point
  NDC-z compare does NOT reproduce the visible bug.
- The **char Z-write A/B in `sw_pass_light` was a NO-OP (0/219 frames)** — but that was the
  WRONG draw path. The free-roam char is NOT drawn by sw_pass_light. scene1_chr_sprite_render
  has callers in: **scene1_chr_walker.c** (player+companion), **scene1_chr_prepass.c** (a
  PREPASS), scene1_shop_walker.c (sw_pass_light), scene1_bg_npc.c, main.c. Find which one
  draws the free-roam player (ret_va 0x45aa31) and whether it writes Z.

OPEN — the real chain to dig (render/depth, NOT the particle math):
1. The free-roam char draw is at **ret_va 0x45aa31** and in retail carries **ZWRITE=1,
   ZFUNC=LESSEQUAL, ALPHATEST ref0 GREATEREQUAL** (d3d trace runs/retail-bw-d3d4); it draws
   as a **14-prim fan** (multi-cell sprite body, NOT a 2-tri billboard), BEFORE every dust
   draw. Does the PORT char draw write Z? (scene1_chr_sprite_render sets NO depth state.)
2. **CRUX — char per-vertex depth:** does the char quad have VERTICAL depth extent (body
   pixels above the foot at a NEARER depth → can occlude the +0.16-behind dust), or is it a
   camera-facing CONSTANT-depth billboard (engine-quirks §92 — then a Z-write occludes
   nothing, since the dust is nearer than the whole quad)? §92 says constant-depth; the
   workflow assumed vertical-extent. RESOLVE THIS before adding any char Z-write: transform
   the char DrawPrimitiveUP verts (foot vs head local-Y) through WORLD·VIEW·PROJ from the
   retail d3d trace and compare NDC-z. If constant-depth, the occluder is something else
   (2D painter order, a Z-prepass, or per-vertex Z on the sprite quad).
3. **scene1_chr_prepass.c** — what is this prepass and does retail run a char Z-prepass that
   establishes the body depth the dust later tests against?

Tooling/data: spawn seed-sweep harness (`runRetailDustSpawn` RPC + port host prog), records
dump runs/retail-bw-recA2 (type 0xe vel/age/pos), runs/retail-bw-d3d4 (char+dust matrices),
runs/trace-export/bw-dustlog. Workflow: dust-emit-disasm-verify (wf_5947468d-e69).

### 2026-06-03e CRUX RESOLVED: char is CONSTANT-depth → char Z-write is NOT the fix

Transformed the char quad local verts through the RETAIL char WORLD·VIEW·PROJ
(runs/retail-bw-d3d4): ndcz = **0.956269 for every local-Y from −400 to +400** — the
whole sprite body (foot→head) is at ONE depth (the foot anchor). The local-Y world
direction (0,0.5547,−0.8321) dotted with the VIEW pitch rows = 0 (engine-quirks §92).
The dust (−0.3,0.5,9.66) projects to ndcz **0.955150 — NEARER than the entire char**.
So a char Z-write writes 0.9563 across the body and the nearer dust passes LESSEQUAL →
draws in front regardless. **The char-sprite Z-write CANNOT occlude this dust** (the
workflow synthesis was wrong on this point; it assumed a vertical-extent quad). This also
fully explains the sw_pass_light Z-write no-op.

=> The retail "dust-behind-boot" is therefore NOT a char-depth occlusion. Remaining real
   occluder candidates (next dig): (a) the **3D bottom-wall / floor / furniture MESH**
   (real per-pixel depth, written before the sprites) occluding the dust at world Z≈9.66
   — check whether the PORT's wall mesh depth or the dust's Z-test against it differs;
   (b) **scene1_chr_prepass** — a possible char Z-PREPASS writing per-pixel body depth at
   the true (non-constant) sprite footprint; (c) verify the retail draw ORDER in the
   bottomwall capture (char-before-dust vs dust-before-char) — painter's order with the
   alpha-blended char.

### 2026-06-03e chain dig: real char path = scene1_chr_walker, draws with ZWRITE=FALSE

- The free-roam char (Recette+companion) is drawn by **scene1_chr_walker_render**
  (scene1_render.c L855, the DEFAULT path — `--force-chr-walker` is now a no-op). NOT
  sw_pass_light — which is why the earlier sw_pass_light Z-write A/B was a no-op (wrong path).
- chr_walker sets **D3DRS_ZWRITEENABLE=FALSE** at L195 for the whole char/wing pass and only
  re-enables ZWRITE=TRUE *after* the player draw (L311). So the PORT char draws with
  **ZWRITE=0**. Retail's char draw (ret_va 0x45aa31) has **ZWRITE=1** (d3d trace, all frames).
  => concrete render-state divergence on the real path.
- BUT the char is constant-depth (above), so this Z-write only occludes dust that is FARTHER
  than the char's single depth — and dust↔char are a **near-tie** (~0.001 ndcz; the sign even
  flipped between two captures, char anchor 0.9542 vs 0.9563 vs dust 0.95515). So whether
  matching retail's ZWRITE=1 actually pushes "most" dust behind is knife-edge and must be
  tested EMPIRICALLY, not by offline ndcz.
- Retail bottomwall DRAW ORDER (runs/retail-bw-d3d4): 3D-mesh… → pcShadow → furnShadow×6 →
  CHAR×7 → 3D-mesh… → CHAR×1 → GLOW×8 → DUST×2 → HUD. So the dust z-tests against BOTH the
  char (if it writes Z) AND the 3D meshes drawn before it. A 3D bottom-wall/furniture mesh
  occluder is still a live alternative to the char Z-write.

NEXT (a judgement call — re-enters the known Tear-glow entanglement):
  (1) Empirically A/B: set ZWRITE=TRUE around the chr_walker player draw (match retail) +
      restore after, re-render the bottomwall, and check if the dust now reads behind like
      retail — AND whether Tear's wing-glow survives (the b1acf7c-class regression; Tear is
      still not-1:1, [[project_confirmed_parity_ledger]]). The char being CONSTANT-depth and
      the glow being a constant-depth billboard at Tear's anchor means the glow should pass
      LESSEQUAL if Tear's quad depth == glow depth — verify.
  (2) If the char ZWRITE doesn't reproduce "mostly behind", the occluder is the 3D mesh —
      capture per-pixel mesh depth at the dust footprint (bottom wall/counter) port vs retail.

## 2026-06-03f — RESOLVED: render contract is now faithful; char Z-write is the real retail state (committed) and a confirmed dust no-op; the remaining gap is TEAR

Built the **Phase-0 per-draw render contract** end to end (`tools/d3d_state_diff.py`
replays the d3d-trace state machine → per-Draw ZENABLE/ZWRITE/ZFUNC/ALPHATEST*/blend,
keyed by ret_va) and ran it on BOTH sides at the bottomwall (retail `runs/retail-bw-d3d4`,
port `runs/port-bw-d3d` via the new `export_trace.py --d3d-trace` + d3d-trace caprange
windowing). This finally ground-truths the whole chain instead of theorising:

**The earlier "free-roam char = chr_walker, NOT sw_pass_light" conclusion was WRONG.**
Per-draw VA attribution (port `nm`):
- Free-roam **player + companion** draw at `0x441d7a` = **`scene1_shop_walker` (sw_pass_light
  = the FUN_004552d0 port)** — *this* is the retail free-roam char path. The retail char
  render-state (ZWRITE=1 / ZFUNC=LE / ALPHATEST ref0 GREATER / SRCALPHA·INVSRCALPHA) is set
  inside **FUN_004552d0** itself (asm 0x4552f0–0x4553fb), NOT in the leaf FUN_0045a56f (which
  sets only SetTransform + SetTextureStageState + DrawPrimitiveUP) and NOT in the prepass
  FUN_0045672a (which sets ZWRITE=0).
- `scene1_chr_walker_render` only draws the **additive companion-glow billboard** (`0x41e751`,
  ONE/ONE, ZWRITE=0) — which is why the chr_walker Z-write A/B was a no-op (wrong path).
- bg-NPCs `0x41c347` (ZWRITE=1, landed 2c96b97), shadows `0x41d858`/`0x41c085`, glow
  `0x448ee9`, **dust `0x44673f`** all map 1:1 to the retail draw order.

**Port now matches retail's contract** (the uncommitted `sw_pass_light` ZWRITE=1 + ALPHATEST
GREATER ref0 is exactly FUN_004552d0's state). Verified by `d3d_state_diff.py` port-vs-retail.

**Char Z-write is a pixel-exact dust no-op — CONFIRMED empirically, not just by §92.** A clean
pixel-aligned A/B (same port sim, only the `OPENRECET_NO_CHAR_ZWRITE` block toggled,
`runs/port-bw-d3d` vs `runs/port-bw-d3d-noz`) is **0/24 frames different** — feet AND glow.
The char is constant-depth (§92): player ndcz 0.9540, live dust ndcz 0.9530/0.9533 (NEARER) →
dust passes ZFUNC=LE and draws in front regardless. **This proves the CHAR is not the dust
occluder — it does NOT prove the dust is fixed.** The Z-write is still the correct retail state
(committed for structural fidelity) and is regression-free (no glow kill, no square cutouts —
GREATER, not GREATEREQUAL), but it is orthogonal to the occlusion bug.

**THE DUST-OCCLUSION BUG IS STILL OPEN (user, 2026-06-03f).** Both the port AND the retail
*snapshot I diffed* show the dust drawing in front — but those two captures are **unsynced**
(different recordings, different anim phase) and neither is a confirmed occluded-dust frame, so
they prove nothing. Retail DOES occlude this dust (user ref `20260603T022400_3466` cap_14 — the
dress hem covers the puff top). Since the char is ruled out as the occluder (constant-depth,
A/B no-op), the real occluder is **something drawn with ZWRITE=1 before the dust that is NOT the
char** — i.e. a **3D mesh** (bottom wall / floor / furniture / counter), Phase 4. Next: get a
confirmed occluded-dust retail frame + its synced-state port frame, then compare per-pixel mesh
depth at the dust footprint. **Do NOT mark resolved.**

**Tear position is NOT off (correction).** Retail's companion world pos (d3d-trace char#2/#8,
frame 538) = **(+0.600, +3.056, +9.350)**, and the port actor[2] = (0.6, 3.08, 9.35) — they
match. Recette matches too (retail char#1 = port player = (-0.3, 0, 9.5)). The apparent Tear
"tilt/float" in the unsynced `feed_feet_3p` crop is **animation-phase**, not a position bug. Any
real Tear divergence (anim phase / orientation) must be checked on SYNCED state, not screen
crops of two different recordings.

Minor residual contract deltas (`d3d_state_diff.py diff`, non-blocking): port glow/dust/HUD
carry **ALPHAFUNC=GREATEREQUAL** where retail keeps **GREATER** (ref0 → only differs for
exactly-α0 texels; benign for ONE/ONE glow + INVSRCCOLOR dust), and the 2D HUD draws with
**ZENABLE=1** where retail has ZENABLE=0 (HUD is last + near-depth, no visible effect). Log
these in the contract checklist; fix opportunistically.

Tooling kept: `tools/d3d_state_diff.py` (dump/diff per-draw contract), `export_trace.py
--d3d-trace` (+ `d3d_trace_set_window` caprange arming in main.c/d3d_trace.c — port d3d-trace
is now anchor-relative + jitter-immune), `runs/port-bw-d3d{,-noz}`.
