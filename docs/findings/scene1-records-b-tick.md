# Scene-1 table B integrator survey — FUN_0043ae20

**Status (2026-05-25):** survey only.  No port yet.  Documents the
25.7 KB Mt. Everest at `0x43ae20` that ticks the 512-slot table B
once per INGAME frame.  This is the consumer side of the C8j allocator
ladder; without this port, every record an allocator commits sits
frozen at its initial pose.

## TL;DR

| Item | Value |
|------|-------|
| Engine address | `FUN_0043ae20 @ 0x43ae20` |
| Size | 25750 B (~3127 decompile lines, all.c L36379..L39509) |
| Callers | 2 — `FUN_004426a7` L40184 (sim default INGAME arm tail), `FUN_004427d3` L40603 (rare sub-arm) |
| Slot table | `DAT_069324b0` = `g_scene1_records_b` (512 × 0x49 dw) |
| Loop bound | `local_2c == 0x200` → 512 iterations (matches `SCENE1_RECORDS_B_COUNT`) |
| Slot dead sentinel | `slot[0] == 0` — same as C8j allocator preamble's claim convention |
| Type checks | 147 across ~86 unique TYPE values |
| Major helpers called | sinf / cosf (×90), `FUN_0043865e` (×73), `FUN_004147d5` (×43), `FUN_00447f4f` (×30), `FUN_00499519` SE (×26), `FUN_00471089` rng_unit (×17), `FUN_004a2a03` mat_mul (×11), `FUN_00432e50` ground (×11), `FUN_0044b219` (×9), `FUN_005031e4` atan2 (×7), `FUN_0044375e` seq (×6), `FUN_004a35d3/3537/3670` RotX/Y/Z, `FUN_0041331d` Table A alloc (×5), `FUN_00445a8c/0044376a` C8j allocators (3+2), `FUN_00433674` wall raycast (×2) |
| Globals written | DAT_069324b0..DAT_069325cc (table B fields), DAT_0076bd98 + DAT_007c8f98 + DAT_007ca4c4 (shop-walker record table), DAT_06a46f98 (per-tick flag), DAT_0438b8cc/c218/c3a8 (game-state), DAT_056da1d8/dc/e0, plus per-type fields |
| Globals read | DAT_073de39c (camera yaw), DAT_005c2434/8/c (type-enable tables), DAT_06956cb0 (table C), engine palette + game state |
| **Cross-cutting hot helper** | `FUN_0043865e` @ 0x43865e — **8059 B per-record body** (similar size to FUN_0040fb3a 8071 B which we already ported as `scene1_particles_tick`).  Called 73 times across many type branches.  Almost certainly the "main per-frame state-machine" for NPC/player-driven slots (collision + AI + animation drive). |

**This is a two-Mt-Everest port.**  FUN_0043ae20 (outer dispatch +
many type bodies) AND FUN_0043865e (the per-record state-machine
called from 73 sites) together total ~34 KB.  Compare:

- Particle tick (C8h ladder): FUN_0040fb3a (8071 B) + FUN_00447f4f
  spawn API (separate, ported as C8i ladder).
- Table B allocator ladder (C8j): FUN_0044376a (8538 B) + FUN_00445a8c
  (8952 B).

Magnitude matches the C8h or C8j ladder.  Sub-chip ladder proposed
below (~10 sub-chips, mirroring C8j.0..C8j.13 structure).

## Outer-loop structure

```c
local_2c = 0;
do {
  uVar6   = local_2c;
  iVar13  = local_2c * 0x124;                  /* byte stride */
  piVar14 = &DAT_069324b0 + local_2c * 0x49;   /* &slot[0] */

  if (*piVar14 == 0) goto LAB_0043fbbc;        /* dead — skip */

  DAT_06a46f98 = 0;                            /* per-tick flag clear */

  /* Preamble: pos += vel + age++ */
  slot[POS_X]    += slot[VEL_X];     /* +0x5c, +0x68 */
  slot[POS_Y]    += slot[VEL_Y];     /* +0x60, +0x6c */
  slot[POS_Z]    += slot[VEL_Z];     /* +0x64, +0x70 */
  slot[AGE]++;                       /* +0x98 (dw 38) */

  iVar15 = *piVar14;                  /* TYPE = slot[0] */

  /* Big nested if-else cascade dispatching ~86 types. */
  /* Each type body may:
   *   - update pos/vel/rot/aux fields
   *   - call sinf/cosf/atan2/sqrt for orientation
   *   - call FUN_0043865e (the per-record state machine)
   *   - call FUN_00432e50 (ground query) or FUN_00433674 (wall ray)
   *   - call FUN_00447f4f to spawn particles (death effect, etc.)
   *   - call FUN_004147d5 to spawn overlay slots
   *   - call FUN_0044376a / FUN_00445a8c to spawn new table-B records
   *   - call FUN_0041331d to spawn table-A entries (PFO.6 allocator)
   *   - mark slot dead by reaching LAB_004411e3 → *piVar14 = 0
   */

LAB_0043fbbc:
  local_2c++;
  if (local_2c == 0x200) return;
} while (true);
```

**Slot conventions** match the C8j allocator ladder (see
`SCENE1_RECORDS_B_OFF_*` in `src/scene1_records.h`):

| Offset (dw) | Engine `DAT_*` | Meaning |
|-------------|----------------|---------|
| 0 | DAT_069324b0 | TYPE (0 = dead, ≠0 = alive) |
| 1 | DAT_069324b4 | reserved/flag |
| 3 | DAT_069324bc | flag (engine "flag" arg) |
| 4 | DAT_069324c0 | OWNER_PTR (entity/NPC blob) |
| 5 | DAT_069324c4 | OWNER_PTR alias (Ghidra punning) |
| 9 | DAT_069324d4 | (per-type field) |
| 23..25 | DAT_0693250c/10/14 | POS_X / POS_Y / POS_Z |
| 26..28 | DAT_06932518/1c/20 | VEL_X / VEL_Y / VEL_Z |
| 29..38 | DAT_06932524..48 | per-type aux (rot, scale, AUX_C8, drag, etc.) |
| 38 | DAT_06932548 | AGE (per-tick counter, byte-low bit also used as a "kill-spawn parity gate") |
| 39 | DAT_0693254c | per-type flag |
| 44 | DAT_06932560 | rotor / direction index |
| 53..54 | DAT_06932584..8 | aux (per-type) |
| 0x60+ | DAT_069325b8..cc | per-type pos2/rot2 (extension fields) |

## Type dispatch overview

147 type checks visible in decomp; ~86 unique TYPE values handled.
Dispatch is NOT a clean switch — it's a deeply nested if-else cascade
with `goto LAB_*` re-entry points.  Following pattern observed:

1. **Anchor cascade** at L82-L268 — types 0x1e, 0x2f, 0x88, 0x9a, 0x9e,
   0x89 — each writes `slot[POS_*] = owner+0x3f0..0x3f8` (NPC owner
   pose, same offset as `scene1_people_entry_t.pos`) then re-projects
   along owner's `+0x420` orientation angle by ±π/2 with per-type
   distance multipliers.  Sets vel + iter bounds (iVar8/iVar11 = AGE
   start/end gates).
2. **Mid-cascade** at L408 (0x9c), L475 (0x34), L515 (0x69/0x74/0x79/
   0x68) — particle/NPC bridge types.
3. **Big body 1** at L689 — types {2, 0x54, 0x67, 0x22, 0x6d-0x70} +
   {3, 4} subset — particle-physics group sharing kill-on-ground +
   bounce logic.
4. **Big body 2** at L812 — types {0x71, 0x72, 0x7d, 0x85, 0x8a, 0x8b,
   0x5b, 0x5c, 0x5e, 0x86, 0x87} — chr-walker / shop-walker driven
   slots (touches DAT_0076bd98 + DAT_007c8f98 = shop-walker records).
5. **Body 3** at L1050 — types {0x5a, 0x98, 0x6c, 0x6b, 0x2d, 0x28} —
   uses FUN_00432e50 (ground query) repeatedly.
6. **Body 4** at L1187 onwards — types {0, 1} = dispatch families
   tied to a 0x49-stride sub-table (likely table B itself iterated).
7. **Body 5** at L1374 — types {0x21, 0x25, 0x31, 0x32}.
8. **Body 6** at L1492 — types {10, 0xb, 0x14, 0x13, 0x99} — share
   a small kill-on-fall body.
9. **Body 7** at L1524 — types {0x11, 0xc, 0xd, 0xf, 0x15, 0x12, 0xe,
   0x97, 0x46, 0x47, 0x44, 0x45, 0x43, 0x18, 0x3b, 0x3c, 0x3d, 0x3e,
   0x3f, 0x40, 0x41, 0x42, 0x25-0x28, 0x33}.  Big visibility/AI block
   — most calls into FUN_0043865e originate here.
10. **Big body 8** at L1723 (label LAB_0043e22b) — entity wall-bounce
    + spawn-on-impact group for types {0x9b, 0x24, 0x53, 0x58, 0x66,
    0x4d, 0x4e, 0x4f, 0x51, 0x52}.  Uses FUN_00433674 (wall raycast)
    + FUN_0041331d (Table A passthrough alloc) + FUN_00447f4f (particle
    spawn).
11. **Tail body** at L1953 onwards (LAB_0043f0f8 / LAB_0043f73a /
    LAB_0043e7c3 / LAB_0043ed34 / LAB_0043ed87) — handles {0x83, 0x84,
    0x87, 0xa0..0xa6} player/NPC AI types.
12. **Final cascade** at L2435 (LAB_004402a2) through L2843
    (LAB_00440176) and L3054 (LAB_0044056c..LAB_00440741) — death-effect
    spawns, sub-scene transitions, animation drive.
13. **Kill path** at L3117 (LAB_004411e3): `*piVar14 = 0` (mark slot
    dead), fall through to LAB_0043fbbc (advance slot index).

**Dispatch breakdown — 86 unique types observed:**

```
0x00, 0x01, 0x02, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18,
0x1e, 0x1f, 0x21, 0x22, 0x23, 0x24, 0x27, 0x28,
0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x31,
0x32, 0x33, 0x34, 0x36, 0x38, 0x3a, 0x3b, 0x3c,
0x3f, 0x41, 0x45, 0x47, 0x48, 0x4c, 0x4d, 0x4e,
0x51, 0x52, 0x53, 0x54, 0x58, 0x5b, 0x5c, 0x5f,
0x60, 0x61, 0x65, 0x66, 0x67, 0x68, 0x6a, 0x6b,
0x6c, 0x6e, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
0x77, 0x7a, 0x7c, 0x7d, 0x82, 0x83, 0x84, 0x87,
0x88, 0x89, 0x8b, 0x8c, 0x96, 0x97, 0x98, 0x99,
0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0xa0, 0xa1, 0xa2,
0xa3, 0xa4, 0xa6,
plus low-int {0, 1, 2, 4, 99, 100}.
```

**Set overlap with C8j allocators** — every TYPE in this dispatch
also appears in either FUN_0044376a (entity allocator) or
FUN_00445a8c (NPC allocator).  The integrator is the consumer side
of those allocator commits; types that spawn but never tick = stuck
records in HOUSE today.

## External call inventory

| Call | Count | Function | What it does | Port status |
|------|-------|----------|--------------|-------------|
| FUN_0043865e | 73 | `0x43865e` (8059 B) | **Mt. Everest #2** — per-record state machine (collision + AI + anim drive).  Almost certainly the engine's main player + NPC tick body.  Sees first arg = pointer-to-slot (`int *param_1`). | **unported — separate ladder** |
| FUN_00503a44 | 46 | sinf | sinf | named in port |
| FUN_00503994 | 44 | cosf | cosf | named in port |
| FUN_004147d5 | 43 | `0x4147d5` (62 B) | 9-arg wrapper around FUN_00414345 (= scene1_overlay_spawn) appending 10th-arg=0. | call site of `scene1_overlay_spawn(..., 0)` |
| FUN_00447f4f | 30 | `0x447f4f` | scene1_spawn (particle alloc) | ported (C8i ladder) |
| FUN_00499519 | 26 | `0x499519` | SE play (sound effect by id) | ported as `se_play` |
| FUN_00471089 | 17 | `0x471089` (34 B) | `rng_next15() / 32768.0` | named as `rng_next_unit` |
| FUN_004a2a03 | 11 | `0x4a2a03` | mat4_multiply (3-arg) | named in math3d.h |
| FUN_00432e50 | 11 | `0x432e50` (2084 B) | ground query (x,z → ground_y) | **unported** (PHC #15 hook only — used by C8j.1 with stub default) |
| FUN_0044b219 | 9 | `0x44b219` (60 B) | writes 6 dwords (param_1..4 + zeros) to DAT_0438cc14..cc2c.  Schedules a sub-scene transition target (item-pickup notification queue, likely). | **unported** — small wrapper |
| FUN_005031e4 | 7 | `0x5031e4` | atan2 (or sqrtf of dx²+dz²; ambiguous per PHC #6) | ported as needed |
| FUN_0044375e | 6 | `0x44375e` (12 B) | `seq_counter_next()` (DAT_06a46fb8++) | already exposed in port (g_scene1_record_b_seq_counter) |
| FUN_004a35d3 | 5 | `0x4a35d3` | mat4_rotation_x | named in math3d.h |
| FUN_004a3537 | 5 | `0x4a3537` | mat4_rotation_y | named in math3d.h |
| FUN_0041331d | 5 | `0x41331d` (89 B) | PFO Table A allocator passthrough | ported (PFO.6) — `scene1_pfo_table_a_alloc_passthrough` |
| FUN_004a3462 | 4 | `0x4a3462` | mat4_translation (D3DXMatrixTranslation) | named in math3d.h |
| FUN_00490820 | 4 | `0x490820` (348 B) | view-frustum cull / projection visibility test (vec3 × 4-row view-matrix vs radius).  Reads `_DAT_095d3770..95d37bc` (16-float matrix + 4 ints).  Returns iVar3 = depth bias or 0 (cull). | **unported** |
| FUN_00445a8c | 3 | `0x445a8c` | NPC table-B allocator | ported (C8j ladder) — `scene1_record_b_spawn_npc` |
| FUN_0043ab6e | 3 | `0x43ab6e` (690 B) | NPC sister-search (nearest-live-NPC scanner with frustum cull).  Iterates DAT_0076c478 (people-table sister alias) stride 0x1c9; uses FUN_00490820 + atan2 for distance. | **unported** |
| FUN_004a3670 | 2 | `0x4a3670` | mat4_rotation_z | named in math3d.h |
| FUN_004532bc | 2 | `0x4532bc` (29 B) | If `DAT_06a49998==0`: set `DAT_06a49994=1` + `DAT_005c5938=arg1`.  Conditional fade/transition trigger. | **unported** |
| FUN_0044376a | 2 | `0x44376a` | entity table-B allocator | ported (C8j ladder) — `scene1_record_b_spawn_entity` |
| FUN_00433674 | 2 | `0x433674` (2354 B) | wall raycast (3-azimuth, returns t + normal) | **unported** (PHC #13 — output args ambiguous) |
| FUN_005041f6 | 1 | rng | engine rng_next15 | named |
| FUN_00485979 | 1 | `0x485979` (731 B) | unknown (item-pickup/notify?) | **unported** |
| FUN_00482ae7 | 1 | `0x482ae7` (348 B) | unknown | **unported** |
| FUN_00482a51 | 1 | `0x482a51` (32 B) | unknown small wrapper | **unported** |
| FUN_0044b255 | 1 | `0x44b255` (1 B) | RET — no-op | trivial |
| FUN_004319d6 | 1 | `0x4319d6` (170 B) | Stage-transition gate check (reads DAT_0438b4c8/cc — current/next stage ID).  Returns 1 when in specific stage→stage transitions (0→4, 4→0x1d, 4→99). | **unported** |
| FUN_0042353c | 1 | `0x42353c` (330 B) | unknown helper | **unported** |
| FUN_00404bb8 | 1 | `0x404bb8` (84 B) | mat4_identity (zeros + sets [0,5,10,15]=1.0f) | trivial |
| FUN_0043ae20 | 1 | self-recursion at L36450 (do-while top) | not a real call | — |

## External global inventory

### Slot fields (table B, all confirmed by C8j allocator ladder)

`DAT_069324b0..DAT_069325cc` — see slot table above.

### Game-state / engine globals

| Engine global | Role | Port status |
|---------------|------|-------------|
| `DAT_06a46f94` | unknown counter | unported (alongside C8j-tracked DAT_06a46fb8) |
| `DAT_06a46f98` | per-tick flag (cleared at every slot iter top) | unported |
| `DAT_073de39c` | camera yaw | `g_scene1_camera_yaw` (Cc.1) |
| `DAT_005c2434` | type-enable table (per-type 0x68 bytes — same shape as Pass F's DAT_005c2410) | unported (same .rdata pattern as DAT_005c2410 — likely uninitialized in-binary, written by lnkdatas / .rdata table copy) |
| `DAT_005c2438` | type-enable table (sister) | unported |
| `DAT_005c243c` | type-enable table (sister) | unported |
| `DAT_0438b8cc` | game state (player input / sub-arm gate) | unported |
| `DAT_0438c218` | game state | unported |
| `DAT_0438c3a8` | game state | unported |
| `DAT_056da1d8/dc/e0` | scene state counters | unported (cousin of DAT_056dab58 / dae84 from PHC #8) |

### Shop-walker record overlap (NEW finding)

`DAT_0076bd98` (= `DAT_0076bd94 + 4` = shop-walker record offset +1 dw)
appears in body L812 (types 0x71, 0x72, 0x7d, 0x85).  This means
**FUN_0043ae20 writes into the shop-walker record table** (`DAT_0076bd94..DAT_007c8f94`,
128 × 0x2e9 dw = 372 KB, scanned by sw_pass_a / sw_pass_f / sw_pass_g
— `sw_pass_af_count()` stub returns 0 today).

Same observation for `DAT_007c8f98`, `DAT_007ca4c4` (stride/offset
variants of the same table).  When the integrator ports, the
shop-walker record table will be written FROM table B's per-tick logic
— i.e. table B IS the writer of the shop-walker records for these
types.

That gives a path to lighting up `sw_pass_a` / `sw_pass_f` /
`sw_pass_g` for real once FUN_0043ae20 ports far enough to cover the
types that write the shop-walker table.

### Table C overlap

`DAT_06956cb0` (table C slot pos.x base — engine `g_scene1_records_c`)
appears in late bodies — table B integrator reads table C state for
some pickup/world-drop interaction patterns.

## Pending human checks (new)

The following items will surface as the integrator ports.  Logging
here so the C8j-tick.* sub-chips can route them to
`openrecet_pending_human_checks.md` as they land.

### #19 (new) — DAT_005c2434/8/c type-enable tables

Pass F's analog `DAT_005c2410` was traced to "no in-binary writer
found, likely .rdata table copy" (C8c.F landing).  Same pattern
expected for these three siblings.  Port them as
`sw_records_b_type_enabled(table, type)` host-installable hooks with
default-returns-0 (BSS-zero match) to keep the bodies dormant in
HOUSE until the .rdata table copier ports.

**Will surface when:** the first per-type body that gates on
`DAT_005c243*[type * 0x68].byte0 == 1` lands.

### #20 (new) — FUN_0043865e per-record state machine (8059 B)

Cross-cutting helper called 73 times.  Almost certainly the player/
NPC body (collision + AI + animation drive).  Same magnitude as
`FUN_0040fb3a` (8071 B, ported as `scene1_particles_tick`) — needs
its own sub-chip ladder.

**Will surface when:** the first integrator body that dispatches into
FUN_0043865e lands (likely Body 7 / type 0xf or 0x97 — the AI / NPC
tracking types).

### #21 (new) — DAT_06a46f98 per-tick flag

Cleared at every slot iter top (L36455).  Almost certainly a "side
effect this tick" flag that some sub-call sets to short-circuit
collision retries within the same iteration.  No in-binary writer
visible from this function — written by one of the called helpers
(probably FUN_0043865e or FUN_00432e50 / FUN_00433674).

**Will surface when:** a body checks `DAT_06a46f98 != 0` to gate its
behavior — likely the wall-bounce + ground-snap bodies.

### #22 (new) — DAT_0438cc14..cc2c notification queue (FUN_0044b219 dst)

9 call sites to FUN_0044b219, which writes 6 dwords to
DAT_0438cc14..cc2c (4 args + 2 zeros).  Looks like a single-slot
notification queue (item-pickup popup, scene transition trigger,
etc.).  No in-binary reader of these addresses visible — consumer is
in unported code, likely the HUD overlay or a sub-arm of the sim
dispatch.

**Will surface when:** the first body that calls FUN_0044b219 lands
(scattered across L1844 onward).

## Sub-chip ladder proposal (C8j-tick.* family)

Mirroring the C8j allocator ladder (which took ~10 sub-chips for
~17 KB of code), FUN_0043ae20 (25.7 KB) plus FUN_0043865e (8 KB)
needs ~12-15 sub-chips.  Suggested decomposition:

| Sub-chip | Scope | Eng lines | Est LoC | Surface |
|----------|-------|-----------|---------|---------|
| C8j-tick.0 (this doc) | Survey | n/a | 0 | none |
| C8j-tick.1 | Skeleton: outer 512-slot loop + preamble (pos+=vel + age++ + per-tick flag clear) + LAB_0043fbbc tail + LAB_004411e3 kill path.  Wires `scene1_records_b_tick()` into `scene1_sim` default INGAME arm (currently STUB at scene1_sim.c L60). | L82, L3120 | ~100 | per-tick pos integration on live slots — Pass C/D/E walkers see slot drift |
| C8j-tick.2 | Anchor cascade (L82-L268): types 0x1e, 0x2f, 0x88, 0x9a, 0x9e, 0x89, 0x48, 0x4b, 0x4c | L82-L268 | ~250 | shop-walker NPC anchor rejoin (Pass C 0x9a, etc.) |
| C8j-tick.3 | Mid-cascade (L408-L649): types 0x9c, 0x34, 0x69, 0x74, 0x79, 0x68 — particle/NPC bridge | L408-L649 | ~250 | 0x34 chain spawn (pairs with C8i.3c 0x34/0x35 spawn) |
| C8j-tick.4 | Body 1 (L689-L812): types {2, 0x54, 0x67, 0x22, 0x6d-0x70, 3, 4} — kill-on-ground + bounce particles | L689-L812 | ~250 | particle-driven HUD effects + jem pickup arc |
| C8j-tick.5 | Body 2 (L812-L1050): types {0x71, 0x72, 0x7d, 0x85, 0x8a, 0x8b, 0x5b, 0x5c, 0x5e, 0x86, 0x87} — chr-walker/shop-walker driven.  **Writes shop-walker records** (DAT_0076bd98 etc.).  Will unblock sw_pass_a / sw_pass_f visible-pixel path. | L812-L1050 | ~300 | shop-walker record writes (Pass A/F count > 0) |
| C8j-tick.6 | Body 3 (L1050-L1187): types {0x5a, 0x98, 0x6c, 0x6b, 0x2d, 0x28} — ground-query loop | L1050-L1187 | ~250 | uses FUN_00432e50 ground query (PHC #15 hook) |
| C8j-tick.7 | Body 4 (L1187-L1374): types {0, 1} sub-table dispatch + L1197 sub-cascade with type-window slot scan | L1187-L1374 | ~250 | mixed |
| C8j-tick.8 | Body 5+6 (L1374-L1547): types {0x21, 0x25, 0x31, 0x32, 10, 0xb, 0x14, 0x13, 0x99, 0x11, 0xc} — kill-on-fall + small physics | L1374-L1547 | ~300 | particle / NPC death effects |
| C8j-tick.9 | Body 7a (L1547-L1700): types {0xd, 0xf, 0x15, 0x12, 0x97, 0x46, 0x47, 0x44, 0x45, 0x43, 0x25-0x28, 0x33} — first major FUN_0043865e dispatch.  **Requires FUN_0043865e stub** with host-installable hook (default no-op). | L1547-L1700 | ~300 | most slots become reachable; default hook keeps them frozen until FUN_0043865e ladder lands |
| C8j-tick.10 | Body 7b (L1700-L1900): types {0x18, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x9b} — wall-bounce + spawn-on-impact.  Uses FUN_00433674 (wall raycast, PHC #13). | L1700-L1900 | ~300 | scattered effects |
| C8j-tick.11 | Big body 8 (L1900-L2435): types {0x24, 0x53, 0x58, 0x66, 0x4d, 0x4e, 0x4f, 0x51, 0x52} — entity body cluster | L1900-L2435 | ~400 | C8j allocator entity types start moving |
| C8j-tick.12 | Tail bodies (L2435-L3054): types {0x83, 0x84, 0x87, 0xa0-0xa6} — player/NPC AI types | L2435-L3054 | ~400 | NPC AI (mostly Pass A/F visible) |
| C8j-tick.13 | Final cascade + LAB_0043f39b death-effect spawn (L3054-end) + verify kill paths | L3054-L3127 | ~150 | death effects, sub-scene transitions |
| **C8j-tick.fin** | Wire into `scene1_sim` default INGAME arm (replaces stub at L60).  Connect Pass A/F shop-walker record table.  Verify canaries bit-exact. | n/a | ~20 | C8j allocator ladder + integrator both live → first real walker pixels from spawned NPCs |

**Total estimate:** ~3-4 KLoC across 13 sub-chips, ~2x the C8j allocator
ladder (which was ~17 KB engine → ~5 KLoC port).

**FUN_0043865e sub-ladder** (separate, parallel): treat as its own
C8jb.* family (the player/NPC state machine).  Likely 6-10 sub-chips
of its own.  Until that ports, the integrator's L1547+ bodies call
through a host-installable stub hook (default no-op) — slots are
reachable but frozen.

## Why this is the unlock for HOUSE visible pixels

The C8e session diagnosed Pass D visible-pixel blockage at frame 92+
even with all smoke flags on.  Working hypothesis at that landing was
"surfaces naturally when real production data lands" — this is that
real production data.

Specifically:

1. **Pass C** (jems/coins drops) — table C populator + tick already
   land (C8j.1/.2/.3).  Pass C body lands (C8c.C).  Visible pixels
   gated on item resolver hook (`wf_pass_d_set_item_resolver`), which
   needs item-DB port (`data/item.txt` parser).  Not unblocked by
   this survey.
2. **Pass D** (world pickups) — same gate as Pass C.  Not unblocked
   by this survey.
3. **Pass A** (shop-walker NPC faces) — gated on `sw_pass_af_count()`
   stub returning 0.  **C8j-tick.5** (Body 2, shop-walker record
   writes) **directly unlocks this** — once that sub-chip lands, the
   shop-walker record table starts seeing real allocations from
   integrator-driven table B entries.
4. **Pass F** (shop-walker scene-tree dispatch) — same gate as Pass A.
   Also unlocked by C8j-tick.5.
5. **Pass B/C/E wide-followup walker** (entity/NPC billboards) — gated
   on `g_scene1_records_b_count > 0` AND per-type filter.  The C8j
   allocator ladder put records there; the **integrator makes them
   visible** by moving their pos + setting their pose.  Without the
   tick, records spawn at their initial pose and never animate, which
   means Pass A/B/C/D/E walker bodies might still draw them, just
   frozen at frame 0.

In other words: the C8j allocator ladder gave us slots; this survey
identifies the unblock-path that makes those slots **animate** AND
**get pose written into the shop-walker record table** so Pass A/F
get records to draw.

The first ~5 sub-chips (tick.1 through tick.5) probably surface the
biggest visible-progress wins.  After that it's filling in the long
tail of per-type bodies.

## Cross-links

- **Allocators (C8j ladder)**:
  `docs/findings/scene1-table-b-allocators.md` and
  `docs/findings/scene1-record-populators.md`.
- **Particle tick (parallel ladder)**:
  `docs/findings/scene1-particles-tick.md` (FUN_0040fb3a) — same outer
  loop + dispatch shape, half the size.
- **Per-frame open (parallel)**: `docs/findings/scene1-per-frame-open.md`
  (FUN_00414929) — overlay slot table consumer.
- **Sim arm dispatch**: `docs/findings/sim-step-a-dispatch.md` —
  identifies FUN_0043ae20 as the largest unported function and
  predicts it's the player+NPC+world tick.
- **Shop-walker record table**: `src/scene1_shop_walker.c` —
  `sw_pass_af_count()` stub at L75 is the gate this survey identifies
  as unlocked by sub-chip C8j-tick.5.
- **Sim stub**: `src/scene1_sim.c` L60 — the
  `/* scene1_records_b_tick();  — FUN_0043ae20, stubbed */` comment
  is the wire point for C8j-tick.fin.

## Risk + open questions

- **Q1 — Is `local_2c == 0x200` (512 slots) authoritative?**  Yes,
  matches `SCENE1_RECORDS_B_COUNT = 512` in port + the engine's
  allocator scan bound (C8j.5 confirmation).
- **Q2 — Does FUN_0043865e take a slot pointer or an owner pointer?**
  Decomp shows `int *param_1` and the function locals suggest
  per-record state operations.  Probably `&slot[0]`.  Confirm during
  C8j-tick.9 first-call landing.
- **Q3 — RESOLVED** — `FUN_004a3462` is `mat4_translation`
  (D3DXMatrixTranslation) per math3d.h:53.  All 4 calls are
  position-into-matrix writes.
- **Q4 — Does the kill path at LAB_004411e3 fire a death-effect
  particle?**  LAB_0043f39b (just before the kill) reads
  `slot[AGE] & 1` and spawns particle type 0x21.  This is a "kill
  with parity-gated death effect" — sub-chip C8j-tick.13 captures it.
- **Q5 — Are there callers we missed?**  Only `FUN_004426a7` L40184
  and `FUN_004427d3` L40603.  Both are sim INGAME arms.
- **Q6 — Should we port FUN_0043865e first (treat as prereq) or stub
  it inline (treat as parallel)?**  Recommend stub-first: the C8j-tick
  ladder lands "skeleton + integrator-only bodies", with FUN_0043865e
  starting as a no-op hook (host-installable, default returns 0).
  Each sub-chip can then exercise its TYPE bodies WITHOUT needing
  Mt. Everest #2 to be done.  FUN_0043865e gets its own C8jb.* ladder
  in parallel.

## Recommended next chip: C8j-tick.1

Skeleton sub-chip.  Land:

1. New file `src/scene1_records_b_tick.{c,h}`.
2. Public `void scene1_records_b_tick(void)` — outer 512-slot loop
   + preamble (pos += vel + age++ + DAT_06a46f98 clear) + skip dead
   slots (`*piVar14 == 0`) + LAB_0043fbbc tail.
3. Kill helper `scene1_records_b_kill_slot(int slot_idx)` — sets
   `slot[0] = 0` (matches LAB_004411e3).
4. Type dispatch wrapper that calls a per-type body table — every
   type body initially a TODO stub (no-op) tagged with its sub-chip
   number.
5. Wire into `scene1_sim.c::scene1_sim_tick` replacing the existing
   commented stub at L60.
6. Host tests: skeleton preamble verification (pos integration, age
   increment, dead-slot skip, kill helper).
7. Smoke flag `--debug-record-b-tick` (optional) to log per-tick
   counts of "ticked / skipped / killed" — diagnostic only, default
   off.

Once C8j-tick.1 lands, every C8j allocator commit will see at least
the position-integration preamble fire each tick — slots will drift
by their velocity (the C8j allocators write VEL_* during init).
That alone makes table B records VISIBLY animate in any walker pass
they pass the filter on.  Smoke validation: `--force-b-npc <type>`
fires an allocator that writes a non-zero VEL_*; with the integrator
preamble running, the slot's pos.x should be observably different
between frame 0 and frame 60.

## Estimated total effort

~13 sub-chips × ~250 LoC each = ~3.2 KLoC across ~2-3 sessions of
focused chip landings.  Compare:

- C8j allocator ladder: ~10 sub-chips, ~17 KB engine code → ~5 KLoC
  port, landed across ~3 sessions (2026-05-23 to 2026-05-24).
- C8h+C8i particle ladder: ~12 sub-chips, ~16 KB engine code →
  ~4 KLoC port, landed across ~4 sessions.

So C8j-tick.* + C8jb.* (FUN_0043865e) together are roughly twice the
C8j allocator effort and dwarf the particle ladder.  However, the
incremental visible-progress payoff is large: every sub-chip lands a
new set of type bodies and surfaces more walker pixels.
