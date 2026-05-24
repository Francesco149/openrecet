# Scene-1 per-frame open (FUN_00414929) — survey

**Status (2026-05-24):** Survey only. Reads end-to-end; identifies
existing typed storage already covered by chip O.2, plus the one
new sibling table (DAT_00730c20) that needs to land before the tick
can port.

## TL;DR

`FUN_00414929` (1465 B) is the **first** function called by the
scene-1 particle integrator `FUN_0040fb3a` (L1 of the integrator).
Despite the "per-frame open" naming used in the integrator survey,
it is **not** integrator-private — it ticks two unrelated entity
tables before the per-type particle handlers run:

1. **Spawn-request queue** at `DAT_00730c20` — 256 entries × 11 dw
   (44 B), small fixed table.  Each entry holds a queued
   `scene1_overlay_spawn` invocation (type + xyz + scale + ...) that
   fires once per tick against a **parent template** (selected by a
   per-frame counter `entry[9]`, 0..6 — 7 distinct sub-records per
   spawn-request).  After 300 ticks the slot self-clears.

2. **Overlay particle integrator** over `g_scene1_overlay_slots` —
   the SAME 4096 × 55 dw table the chip O.2 already lays down for the
   overlay dispatcher (engine `DAT_0064e810 .. 0072a8a0`, sentinel
   field at offset 32 dw / `&DAT_0064e890`).  Type dispatch on
   slot dw 31; updates anim-cell counters, applies pos+=vel and
   matrix-driven displacement, drag, gravity, and (for type 4
   "shop walker") an aim-toward-(11, -9) physics body with random
   half-kill at terminal velocity.

So the work of porting `FUN_00414929` is:

- New storage for **Table A** (the 256-slot spawn-request queue) +
  its **parent template table** at `DAT_007444e0` (≤999 entries ×
  0x5f dw = 380 B = parsed from a text file by `FUN_0041276e`).
- New port for `FUN_00414902` — sentinel-init both Table A
  (DAT_00730c20 slots' sentinel field at +4 dw) AND the overlay
  slots (already covered by `scene1_overlay_reset`, but at a
  different call site).
- Port the per-type integrator bodies into a new TU
  (`scene1_per_frame_open.c`?) or extend `scene1_overlay.c` since
  Table B already lives there.

## Function inventory

| Engine fn         | Size  | Role                                                          | Ported?               |
|-------------------|-------|---------------------------------------------------------------|-----------------------|
| `FUN_00414929`    | 1465 B | Per-frame open (this survey)                                  | TODO stub (scene1_particles_tick.c) |
| `FUN_00414902`    |   39 B | Sentinel-init both tables                                     | scene1_overlay_reset covers Table B; Table A NOT inited |
| `FUN_004132c1`    |   92 B | Table A allocator (10-arg)                                    | unported              |
| `FUN_0041331d`    |   89 B | Table A allocator (9-arg)                                     | unported              |
| `FUN_0041276e`    |  795 B | Parent template table parser (text-file → DAT_007444e0)       | unported              |
| `FUN_00414345`    | 1057 B | `scene1_overlay_spawn` (Table B allocator)                    | scene1_overlay.c (O.2) |
| `FUN_005031e4`    |    9 B | FPU `sqrt` wrapper                                            | sqrtf (libm)          |
| `FUN_0040656e`    |   22 B | `DAT_00648280 = 4; FUN_00499519(0x29d);` — terminal-velocity SE | stand-in (no consumer) |

## Table A — DAT_00730c20 spawn-request queue

**Address layout** (256 entries, stride 0xb dw = 0x2c bytes):

- Entry base: `DAT_00730c20 + k * 0x2c`, k = 0..255
- Total span: `0x730c20 .. 0x733820` (= 256 × 44 B = 11264 B)
- Sentinel field at entry+4 dw — Ghidra aliases this as `DAT_00730c30`
- End-of-table marker (loop bound) at `DAT_00733830` (= base + 256*0x2c
  + 0x10 = sentinel of past-the-end entry)
- Init walk also uses sentinel anchor: `DAT_00730c30 .. DAT_00733830`
  stepping 0xb dw

The tick (FUN_00414929 L1-43) walks at the sentinel anchor:

```c
piVar2 = &DAT_00730c30;   // entry+4 dw
do {
    if (*piVar2 != -1) {  // slot alive
        // inner 7-iteration sub-record walk against parent template
        // pointed to by sentinel value (iVar10 = parent template id)
        for (sub = 0; sub < 7; sub++) {
            if (parent[sub].sentinel != -1 && parent[sub].extra == age) {
                // build (pos, scale, type, color, flag) from
                // (entry[1..3, -1..-4], parent[sub].xyz)
                FUN_00414345(...);  // scene1_overlay_spawn
            }
        }
        if (++entry.age == 300) entry.sentinel = -1;  // self-clear
    }
    piVar2 += 0xb;
} while (piVar2 != &DAT_00733830);
```

**Field offsets** (relative to piVar2, i.e. sentinel at +0):

| dw     | Engine alias    | Allocator FUN_004132c1 | Allocator FUN_0041331d | Tick reads          |
|--------|-----------------|-----------------------|------------------------|---------------------|
| -4 / [0]  | DAT_00730c20 | =0                    | =param_1               | as int (iVar5)      |
| -3 / [1]  | +1 dw        | =param_1              | =param_2               | as float (fVar6)    |
| -2 / [2]  | +2 dw        | =param_2              | =param_3               | as float (fVar7)    |
| -1 / [3]  | +3 dw        | =0xc4020000 (-520.0)  | =param_4               | as float (pos.z anchor) |
|  0 / [4]  | DAT_00730c30 | =param_3 (sentinel)   | =param_5 (sentinel)    | parent_id           |
|  1 / [5]  | +5 dw        | =param_4              | =param_6               | scale mul           |
|  2 / [6]  | +6 dw        | =param_5              | =param_7               | color (iVar9)       |
|  3 / [7]  | +7 dw        | =0                    | =param_8               | flag (iVar10 in mode 0) |
|  4 / [8]  | +8 dw        | =param_6              | =param_9               | (not read in tick)  |
|  5 / [9]  | +9 dw        | =0                    | =0                     | age counter         |
|  6 / [10] | +10 dw       | =1                    | =0                     | mode flag (1=projected, 0=passthrough) |

**Sentinel convention:** `entry[4] = -1` means empty.  Init writes
this to -1 for all 256 slots.  Allocators set it to the parent
template id (a non-negative int).

**Parent template table** at `DAT_007444e0` (stride 0x5f dw = 0x17c
bytes = 380 B), populated by `FUN_0041276e` from a text file
(format `ef/effect%d.dat` per the FUN_00412a89 init at L76536).
Used by the tick at:

- `parent[id].entry[0]`  = sentinel (read via `*piVar1`; non-(-1) → live)
- `parent[id].entry[7]`  = extra/age-match key (read via `piVar1[7]`)
- `parent[id].entry[0x2a]` = scale_mul (read via `piVar1[0x2a]`)
- `parent[id].entry[74..76]` (float view at +0xcc) = xyz offset (`pfVar3[-1..1]`)

**Capacity:** the parent table region spans `0x744580..0x769740` per
the init walk in `FUN_00412a89` — that's `0x251c0 / 0x17c = 400`
entries.  Each file (`effect%d.dat`) holds up to 100 entries (parser
reads 38000 B = 100 × 380 B per file), and the table holds up to 4
files concatenated (4 × 100 = 400).  Storage: ~149 KB.

## Table B — g_scene1_overlay_slots (existing storage)

This is the **scene1_overlay slot table** already laid down by chip
O.2 (`src/scene1_overlay.{c,h}`):

- 4096 entries × 55 dw (= 0x37 dw / 220 B)
- Engine slot base: `DAT_0064e820` (engine pointer aliases at this
  base are skewed: init `FUN_00414902` walks at sentinel address
  `DAT_0064e890` (= slot+28 dw); tick `FUN_00414929` walks at
  `DAT_0064e8a0` (= slot+32 dw = sentinel+4 dw); allocator
  `FUN_00414345` walks at sentinel too)
- Sentinel: dw 28 (`SCENE1_OVERLAY_OFF_ACTIVE` in O.2) == -1 means empty

The tick's `piVar2` indexes off slot+32 dw.  Mapping (slot dw =
piVar2 index + 32):

| piVar2 idx | slot dw | O.2 alias                       | Tick role                                              |
|------------|---------|----------------------------------|--------------------------------------------------------|
| -0x20  | 0  | TEXTURE_TYPE                    | overlay shape index (= scene1_overlay_shapes_table key)  |
| -0x1f  | 1  | TYPE_SHAPE                      | type 8/9/10 sub-dispatch (= renderer's "shape ID")       |
| -0x1e  | 2  | POS_X                           | pos.x (type 1/6 + default integrator)                    |
| -0x1d  | 3  | POS_Y                           | pos.y                                                    |
| -0x1c  | 4  | POS_Z                           | pos.z                                                    |
| -0x1b  | 5  | VEL_X                           | accum.x (type 1/6 only)                                  |
| -0x1a  | 6  | VEL_Y                           | accum.y                                                  |
| -0x19  | 7  | VEL_Z                           | accum.z (type 1/6 only)                                  |
| -0x18  | 8  | POS_X_COPY                      | base_x for type 1/6 matrix add                           |
| -0x17  | 9  | POS_Y_COPY                      | base_y                                                   |
| -0x16  | 10 | POS_Z_COPY                      | base_z                                                   |
| -0x15  | 11 | BEND_X                          | **vel.x** (read+write: drag, gravity additive)           |
| -0x14  | 12 | BEND_Y                          | **vel.y** (read+write)                                   |
| -0x13  | 13 | BEND_Z                          | **vel.z** (read+write)                                   |
| -0x11  | 15 | ROT_Y                           | type 8/9/10's `entry[15] += entry[12]` advance           |
| -0xf   | 17 | TEMPLATE5_COPY                  | drag multiplier (vel *= entry[17])                       |
| -0xe   | 18 | UNK_48                          | type 4 only: aim-toward-(11, -9, -520) "shop walker" force |
| -0xd   | 19 | FADE_OUT_OFFSET                 | kill threshold (-1 = no age-kill)                        |
| -0xc   | 20 | SCALE_X                         | energy counter                                           |
| -0xb   | 21 | TEMPLATE11_COPY                 | energy delta per frame                                   |
| -8     | 24 | AGE_BIRTH                       | init_age base (age limit = entry[24] + entry[19])        |
| -5     | 27 | SHAPE_MODE                      | **integrator TYPE** (1/4/6/8/9/10/default) — note: NOT renderer's "shape" |
| -4     | 28 | **ACTIVE**                      | **SENTINEL** (== -1 → empty)                             |
| -3     | 29 | AGE                             | AGE counter (incremented every active tick)              |
| -1     | 31 | UNK_7C                          | **anim_frame_counter** (per-cell tick)                   |
|  0     | 32 | RNG_SEED                        | **anim_cell_counter** (loops/clamps per shape table)     |
| +4     | 36 | OWNER_A                         | matrix-row pointer (type 1; reads at +0x20/+0x24/+0x28)  |
| +5     | 37 | OWNER_B                         | matrix-row pointer (type 6; reads at +0x3f0/+0x3f4/+0x3f8) |

**Tick / renderer field consistency:** field meanings align cleanly
between renderer and tick.  O.2's `ACTIVE`/`AGE` ARE the sentinel and
age; `SHAPE_MODE` is the integrator TYPE; `BEND_X/Y/Z` are vel.x/y/z
(the renderer reads them as a fade offset, the integrator writes
them via drag).  Two O.2 names should be revised when PFO.3 lands:

- `OFF_UNK_7C` (dw 31) → `OFF_ANIM_FRAME_COUNTER`
- `OFF_RNG_SEED` (dw 32) → `OFF_ANIM_CELL_INDEX` (not actually an
  RNG seed; init zeroes it, tick increments it per-cell)

## FUN_00414929 — per-type dispatch summary

After the sentinel gate (`if slot[28] != -1`), each slot runs:

1. **Anim-cell tick** (always; not type-gated):
    ```
    counter = ++slot[31]                       // anim_frame
    shape_frames = g_scene1_overlay_shapes[slot[0] * 8 + 6]
    if (0 < shape_frames && shape_frames <= counter) {
        slot[31] = 0
        slot[32]++                             // anim_cell
        shape_max_cell = g_scene1_overlay_shapes[slot[0] * 8 + 5]
        if (shape_max_cell <= slot[32]) {
            if (g_scene1_overlay_shapes[slot[0] * 8 + 7] == 1)
                slot[32] = 0                   // loop
            else
                slot[32] = shape_max_cell - 1  // clamp
        }
    }
    ```

2. **Type-dispatched integrator** (only when `slot[29] >= 0` — the
   spawn API plants negative ages for staggered bursts):

    | TYPE (slot[27]) | Body |
    |-----------------|------|
    | 1    | accum += vel; pos = matrix_row[0..2] (from slot[36]+0x20..+0x28) + base + accum |
    | 6    | same as type 1 but matrix is at slot[37]+0x3f0..+0x3f8 |
    | 8/9/10 | only `slot[15] += slot[12]` — pos NOT updated via vel |
    | default | pos += vel (3D)                                                                |

3. **Drag + gravity** (always after step 2):
    ```
    vel.x *= slot[17]    // drag
    vel.y *= slot[17]
    vel.z *= slot[17]
    vel.y += slot[18]    // gravity-like additive
    ```

4. **Type 4 + slot[18] != 0** (= "shop walker physics"):
    - After age `30 + (frame_count % 4)`, aim toward target
      `(11 * factor, -9 * factor, -520)` where factor is the capped
      lerp `[0.1, 1.2]` based on (age - 30).
    - Decay drag (`slot[18] *= 0.8`).
    - Speed-clamp: if `|vel| > 1`, normalize (vel /= |vel|).
    - At terminal lerp (factor == 1.2), 50% random chance to
      self-kill + call `FUN_0040656e` (plays SE 0x29d "thud").

5. **Energy decay** (always): `slot[20] += slot[21]`.

6. **Age advance + kill check** (always):
    ```
    age = ++slot[29]
    if (!(type == 4 && slot[18] != 0) && slot[19] != -1) {
        if (slot[19] <= age - slot[24] || slot[20] <= 0)
            slot[28] = -1  // kill
    }
    ```

## Dependencies

### Already ported
- `scene1_overlay_spawn` (FUN_00414345) — Table B allocator.
- `scene1_overlay_reset` — covers Table B sentinel init.
- `g_scene1_overlay_shapes` table — read in step 1 anim-cell tick.
- `rng_next15` / `rng_next_unit` — not directly read by the tick (only
  used by FUN_00414345 and FUN_0040656e).

### Stand-in needed
- `DAT_074b2ee4` — single int gate, NO writers in decompile.  Read
  twice in tick: Table A inner (sets pos.z to -520 when nonzero) and
  passed to `FUN_00414345` calls as the `param_10` arg.  Likely a
  "scene is dungeon" or "alt-mode" flag set by per-stage init code.
  Default 0; expose as `g_scene1_pfo_alt_mode` stand-in.

### New typed storage needed
- **Table A storage**: 256 × 11 dw `int32_t` array.  ~11 KB.
- **Parent template table** at `DAT_007444e0`: 400 entries × 0x5f dw
  = ~149 KB.  Loaded from up to 4 × `ef/effect%d.dat` files via the
  parser `FUN_0041276e` (PFO.7).
- **Allocators** FUN_004132c1 + FUN_0041331d.  Tiny (≤92 B each).

## Cross-references

- `docs/findings/scene1-particles-tick.md` § "Per-frame open
  FUN_00414929 — 1465 B sibling" — names the function but says
  "separate concern" for the integrator chip.
- `docs/findings/scene1-overlay-dispatcher.md` — same 4096 × 55 dw
  table as the dispatcher consumer.  Field name table is renderer-
  centric.
- `src/scene1_overlay.{c,h}` chip O.2 — typed storage + spawn API
  already cover Table B.

## Pending human checks

### 1. DAT_074b2ee4 semantics (no writers in decompile)
Read at:
- L11667 `if (DAT_074b2ee4 == 0)` — FUN_00413376 (overlay-side fade tick)
- L11678 — passed as 10th arg to a FUN_00414345 call (DAT_074b2ee4 != 0)
- L11883 / L11893 — same: gate + spawn-arg in another caller
- L12589 — **tick Table A inner**: pos.z = -520 when nonzero

Hypothesis: per-stage "alt projection" flag (HOUSE/DUNGEON or first-
person/third-person). Stand-in default 0 leaves the tick in "normal"
mode. Verify via Frida read of `*(int*)0x74b2ee4` when sub-scene
transitions fire.

### 2. Parent template table source file (RESOLVED)
`FUN_0041276e` parses `ef/effect%d.dat` files into `DAT_007444e0`.
Caller identified: `FUN_00412a89` at L76536 (same tables-loader
`FUN_00475270` that runs the overlay table parser from chip O.10).
Vendor `ef/` contains `effect1.dat`, `effect2.dat`, `effect3.dat`,
`effect4.dat` — 4 files × 100 entries each = 400 entries in the
400-slot table.

**File format peek:** binary-with-embedded-text (each record has a
text "name" field at the head plus fixed-offset floats/ints
afterward).  PFO.7 will need to RE the parser carefully — the
Ghidra decomp uses `\r`/`\n` line-break logic that doesn't trivially
match binary files; possibly the records are newline-padded.  Out of
scope for this survey.

### 3. Two O.2 field-name labels are wrong; resolve in PFO.3
O.2's `OFF_UNK_7C` (dw 31) is actually the per-frame anim-cell tick
counter; O.2's `OFF_RNG_SEED` (dw 32) is the anim-cell index (not a
seed — it's initialized to 0 by the spawner and incremented by the
tick per shape-table threshold).  PFO.3 (the integrator body port)
should rename these to `OFF_ANIM_FRAME_COUNTER` and
`OFF_ANIM_CELL_INDEX` and update the (small handful of) renderer
references to match.  Field offsets stay the same.

## Chip ladder

The full FUN_00414929 port lands in sub-chips.  Recommended order:

| Sub-chip | What lands                                                                                | Approx LoC | Dormancy gate |
|----------|--------------------------------------------------------------------------------------------|------------|---------------|
| PFO.0    | **This survey doc** + memory updates                                                       | doc only   | n/a |
| PFO.1    | Table A storage + FUN_00414902 Table A init wired into scene1_records_reset; tick stub for Table A keeps a no-op | ~150 | BSS-zero parent table → tick body never spawns |
| PFO.2    | Parent template table storage + (skeleton) tests for the storage layout                    | ~100 | parser not ported; table stays BSS-zero |
| PFO.3    | Port Table B tick (anim-cell + per-type integrator + drag/gravity/age-kill); skip type-4 physics body | ~300 | overlay slots stay sentinel-empty in HOUSE |
| PFO.4    | Type 4 "shop walker physics" body + SE 0x29d at terminal velocity                         | ~120 | same as PFO.3 |
| PFO.5    | Wire FUN_00414929 into particles_per_frame_open (replace stub) + extend scene1_records_reset to also call Table A init | ~30 | dormant on all gates |
| PFO.6    | Allocators FUN_004132c1 + FUN_0041331d                                                    | ~80 | no caller until a consumer ports |
| PFO.7    | Parent table parser FUN_0041276e + boot wiring                                            | ~200 | tick is structurally complete after this |

PFO.1-5 are all dormant-in-HOUSE per current data.  Goldens must stay
bit-exact across each landing.

## How to pick up next session

1. Re-read this doc + `docs/findings/scene1-particles-tick.md`.
2. Confirm Table B field-name aliasing decision (PHC #3) — either
   extend `src/scene1_overlay.h` with parallel `OFF_INT_*` aliases or
   rename the renderer-centric ones.
3. Land PFO.1 first (Table A storage + init).  Smallest, lowest-risk
   chip; unblocks PFO.5 wiring.
