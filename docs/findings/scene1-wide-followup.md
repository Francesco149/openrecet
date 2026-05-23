# Scene-1 wide-frustum followup (FUN_004161c7) — survey

**Status (2026-05-23):** Survey only. Intended to set up the next
session for a port chip.

## What FUN_004161c7 is

The second of two functions called inside C8a's `scene1_render_meshes`
under the WIDE-frustum projection (z_far=2000):

```c
scene1_push_projection(dev, 2000.0f);   // L216-L217
scene1_shop_walker(dev);                 // L218 (C8c, FUN_004552d0)
scene1_walk_wide_followup_TODO();        // L219 (FUN_004161c7) ← this doc
```

Structurally a sibling of the C8c shop walker: same wide projection,
same per-record table reads, but emits **2D billboard quads** via
`DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vbuf, 0x18)` instead of 3D
meshes via `ID3DXMesh::DrawSubset`. The 3D records' positions drive
each billboard's world transform; the billboard's UV coords come
from a per-record tile selector against a shared atlas.

4925 B of decomp. Structurally smaller per-loop than C8c (each pass
is a self-contained quad-emit) but more passes (6 vs C8c's 7).

## The six per-record passes

Line numbers are from `docs/decompiled/by-address/4161c7.c`.

| Pass | Lines | Table          | Stride | Count gate     | Type filter (raw float)           | Texture            | Atlas UV scale |
|------|-------|----------------|--------|----------------|-----------------------------------|--------------------|----------------|
| A    | L50-92  | DAT_06932548   | 0x49 dw | DAT_0076b964 | 1.66755e-43 (0x77) or 2.2701e-43 (0xa2) | DAT_073cc8e0 | 128 tex (0.0078125 / 0.9921875) |
| B    | L93-127 | DAT_06932514   | 0x49 dw | DAT_0076b964 | 1.16308e-43 (0x53)                | DAT_073d8620       | 256 tex (0.00390625 / 0.99609375) |
| C    | L142-203| DAT_06956cd8   | 0x25 dw | DAT_0076b968 | {0, 1.4013e-45, 2.8026e-45, 4.2039e-45} → {0,1,2,3} | DAT_073cc930 | 64 tex tile (tile_index from `r[1]/3 % 7` + type offset) |
| D    | L224-287| DAT_06956cd8   | 0x25 dw | DAT_0076b968 | `*r > 6`                          | per-record (via FUN_004681f6 + DAT_095d3808 lookup) | tile from `(&DAT_095d380c)[lookup*0xb3]` |
| E    | L293-416| DAT_069324b0   | 0x49 dw | DAT_0076b964 | {0x71,0x72,0x75} or {0x73,0x7e,0x78,0xa0,0x7a} | DAT_073cc940 (shared) | type-driven tile selector |
| F    | L423-481| DAT_069b2fb0   | 0x25 dw | DAT_0076b960 | type 0x92 && `r[1] >= 0`          | DAT_073cc8c0       | 9-color cycle (no texture sampling — vertex-colored quads) |

## Mid-pass injections

Two non-loop blocks interrupt the per-pass cadence:

- **L128-141** (between passes B and C): full state reset to 2D
  (FOGENABLE off, ZWRITEENABLE on, TSS COLOROP=7) + `SetVertexShader(0x142)`
  (FVF XYZRHW|DIFFUSE|TEX1) + `FUN_00414ee2(1, 0)` — engine's 2D
  overlay layer 1 dispatch. Same 4006-byte `FUN_00414ee2` that the
  C7h `scene1_render_overlay` brackets call. Confirms this followup
  IS the 2D HUD aggregation point, not a separate concept.

- **L204-221**: projection swap to z_far=350 + conditional inner
  loop (`DAT_0438b1c0 == 1 && palette+0 == 0`): nested 15×20 walk
  calling `FUN_00415fab()` per cell, then `FUN_00485f8c()`. Then
  projection back to z_far=2000 for pass D onward. This sub-block
  is the "shop floor cell highlight" path that lights up the tile
  the player is standing on; conditional on player-on-floor scene
  state.

## Per-pass quad emission shape

Every pass (A-E) follows the same template:

```c
// 1. Texture cache update (avoid re-binding same texture).
if (DAT_0076b95c != desired_tex) {
    DAT_0076b95c = desired_tex;
    SetTexture(0, desired_tex);
}

// 2. World transform from record fields.
Translation(M, r[-0xf], r[-0xe], r[-0xd]);     // position
Scaling(s, scale, scale, scale);                // computed per pass
M = M * s;
RotationX(rx, π/2);                              // make Y-up quad face camera
M = M * rx;
RotationY(ry, π - r[-2]);                       // per-record yaw
M = M * ry;
SetTransform(D3DTS_WORLDMATRIX(0), M);

// 3. Populate the persistent vbuf at DAT_0064bf68 (or sibling).
//    4 vertices × 6 floats (XYZ + DIFFUSE + UV).
//    Color = 0xffffffff per vertex (passes E may compute alpha-fade).
//    UV coords = per-pass atlas + per-record tile selector.

// 4. Draw.
DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &DAT_0064bf68, 0x18);
```

Pass F (DAT_069b2fb0 type 0x92) is the only variant: the vbuf
includes per-vertex per-channel color cycling (9-entry color rotation
indexed by `local_18 % 9`), and no texture is actively bound for
the draw — the COLOROP from L288 (= 4 = MODULATE2X) implies the
last-bound texture (DAT_073cc8c0 from L419-422) still applies but
the per-vertex color drives the look.

## Persistent vertex buffers

Three engine-side static vbufs are reused across passes (each is
8×24 = 192 bytes for a 4-vertex strip, but the engine writes 6
strips × 24 bytes = 144 bytes per emit):

| Buffer addr | Used by | Purpose |
|-------------|---------|---------|
| `DAT_0064bf68` (`DAT_0064bf74` writeable) | Passes A, B, E | Generic shop-billboard vbuf |
| `DAT_0064e5d8` (`DAT_0064e5e4` writeable) | Passes C, D    | Particle/effect vbuf |
| `DAT_00648698` (`DAT_006486a4` writeable) | Pass F          | Color-cycle vbuf |

Each vbuf is initialized once at scene boot (writes around
`0xc3800000` / `0xc2000000` etc. at lines 8603, 8824, 8849 of
`all.c`) and re-populated per emit. Layout: 4 vertices × 6 floats
(XYZ, DIFFUSE, UV) = 24 bytes per vertex, 96 bytes per quad. The
engine writes a 6-vertex strip (96 bytes effective).

## Where the records come from — FUN_0040fb3a (8071 B, sim-side)

> **Erratum (2026-05-23):** This section originally claimed
> `FUN_0040fb3a` writes "every per-record table the wide-frustum
> render passes read".  Survey work in
> `docs/findings/scene1-particles-tick.md` shows that's wrong:
> `FUN_0040fb3a` writes **only table A** (`DAT_069b2fb0`, 4096 ×
> 0x25 dw) — the particle table. Tables B
> (`DAT_069324b0` / `DAT_06932548` / `DAT_06932514`, stride 0x49)
> and C (`DAT_06956cd8`, stride 0x25), plus the 0x2e9-stride
> "people" table at `DAT_0076bd60+`, have **separate writers yet
> to be found**. The MVP recommendation below is left in place but
> see the particles-tick doc for a corrected, smaller MVP
> (direct-injection of one type-0x92 slot in table A — bypasses
> both the integrator and the spawn API).

The (corrected) scope: `FUN_0040fb3a` is the per-tick **integrator**
for the table-A particle records. Bigger than the 3D walker
`FUN_0040a765` (7558 B). Inside the wide-followup it serves Pass F
only (DAT_069b2fb0 type 0x92); Pass D of the C8c shop walker
(`src/scene1_shop_walker.c` L240) also reads table A.

Call sites for FUN_0040fb3a (sim-side, all unported today):

- FUN_00436f97 (710 B — sibling of FUN_00474a9a, in FUN_004547ab's
  case-1 dispatch)
- FUN_004427d3
- FUN_00442c86
- FUN_004536c2 (the C7e load-chain wakeup's sim-side counterpart)
- FUN_0048da71

The most direct path: **FUN_004536c2 → FUN_0040fb3a → DAT_069...
records populate → C8c/C8f wide-pass walkers paint pixels.**
FUN_004536c2 is the sim-side counterpart to FUN_004547ab — runs
once per tick in the INGAME state.

Reset side: FUN_0040fb3a does NOT zero the tables; the records hold
state across frames. Initial reset is by the per-record `[0] = -1`
sentinel writes in FUN_0040fb3a's own "release any stale records"
preamble. The count globals (DAT_0076b960/4/8) are computed by
FUN_00459dfd's L51-L81 scan over the same tables — also dormant
today since the tables are BSS-zero.

## Why this matters for HOUSE visibility

The C8c shop walker is fully scaffolded but every per-record loop
short-circuits because the records are empty. FUN_004161c7 has the
exact same dormancy condition. Porting FUN_004161c7's structure
without porting FUN_0040fb3a (or its callers) results in another
~30-40 device state writes per frame with no draws — no visible
difference.

**The dependency chain is:**

```
FUN_004547ab (state==1)        ← INGAME sim entry, partial port in
                                  main.c's render_dispatch
   ↓
FUN_004536c2                   ← INGAME tick (unported)
   ↓
FUN_0040fb3a (8071 B)          ← Record populator (UNPORTED — Mt. Everest)
   ↓
DAT_069b2fb0 etc. populate     ← Per-record fields filled
   ↓
FUN_00459dfd L51-L81 scan      ← Count globals computed (currently a
                                  TODO in C8a as
                                  scene1_walk_record_counter_scan_TODO)
   ↓
DAT_0076b960/4/8 reflect       ← Loop bounds non-zero
   ↓
FUN_004552d0 + FUN_004161c7    ← Walkers actually iterate records
   ↓
Per-record draw bodies fire    ← FUN_00455191 + DrawPrimitiveUP
   ↓
                                  ✦ Pixels on screen ✦
```

So a "make HOUSE visible" minimum-viable-port would skip
FUN_004161c7 entirely and go straight at FUN_0040fb3a + the
counter-scan. Adding FUN_004161c7's port to the dependency tree
doubles the render-side surface area without changing whether
anything paints.

## Recommended next steps (for the porter)

**If the goal is "draw something for HOUSE":**

1. ~~Port FUN_00459dfd L51-L81 record-counter scan~~ — landed
   2026-05-23 as C8g.1 (`src/scene1_records.{c,h}`).  Also ports
   FUN_0040f64b's 3-table sentinel preamble, called from
   `scene1_preload_house` at INGAME entry.  Counts (`g_scene1_records_
   a/b/c_count`, engine DAT_0076b960/4/8) now live data; lands 0/0/0
   until the populator fills records.

2. Survey FUN_0040fb3a (8071 B). Probably needs to fan into 4-6
   sub-chips. This is the climb.

3. Port FUN_00436f97 (710 B) first to populate one record type —
   gives a single billboard / mesh to validate the C8c/C8e draw
   path against.

4. Port FUN_004161c7 last (this chip's subject), once at least
   one walker pass actually paints from C8c.

**If the goal is "complete the render-side surface area":**

1. Port FUN_004161c7 structurally now (same shape as C8c — 6
   per-record loops + 2 mid-pass injections + 3 persistent vbufs).
   Estimated 600-800 lines of new C.

2. The dormancy assumptions match C8c exactly: all loops gated by
   the same BSS-zero count globals.

3. Outcome: render-side fully scaffolded; the next sim-side port
   immediately fires draws across BOTH walkers.

## Implementation hazards

- **Per-pass `__ftol()` calls (passes D/E) have lost their float
  source in Ghidra.** Each is a per-record alpha computation; the
  source is almost certainly a per-record fade counter or per-frame
  sin/cos. Port verbatim with placeholder constants; the side-by-
  side with retail will surface the right scale.

- **The persistent vbufs (`DAT_0064bf68` etc.) live in `.data`
  initialized at engine startup.** Need to either:
    (a) port the boot-side init (lines 8603 / 8824 / 8849) — small
        but undisciplined; they're scattered across multiple init
        helpers.
    (b) Allocate equivalent static-storage vbufs in the new module
        and emit on the same shape.
  Option (b) is cleaner — the engine's globally-allocated approach
  is partly historical (D3D7-era pattern).

- **The L142-203 pass C uses a `tile_index` formula that mixes
  type-coding into the index:** `tile = (r[1]/3) % 7 + type_offset`,
  where `type_offset` is +0 / +8 / +16 / +24 for types 0/1/2/3.
  Reproduce verbatim including the modulo — engine treats this as a
  256-entry atlas with 4 type-bands of 64 tiles each.

- **The L204-221 conditional inner loop is the only call site for
  FUN_00415fab + FUN_00485f8c in the wide-followup.** Both unported.
  Could be its own chip — survey first.

## Related files

- `src/scene1_shop_walker.{c,h}` — C8c port (sister walker, same
  dormancy condition).
- `src/scene1_render.{c,h}` — C8a `scene1_render_meshes` orchestrator;
  L51-L81 counter scan TODO lives in
  `scene1_walk_record_counter_scan_TODO`.
- `docs/findings/scene1-render.md` — C7/C8 chip ladder.
- `docs/decompiled/by-address/4161c7.c` — this function's Ghidra
  output (485 lines).
- `docs/decompiled/by-address/40fb3a.c` — the populator (very
  large, not yet read end-to-end).
