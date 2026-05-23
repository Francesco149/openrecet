# Scene-1 particle integrator (FUN_0040fb3a) — survey

**Status (2026-05-23):** Survey only. Reads end-to-end. Sets up the
next port chip and corrects an attribution error in the
wide-followup doc.

## TL;DR

`FUN_0040fb3a` (8071 B, 1249 lines of Ghidra C) is the per-tick
**integrator for the scene-1 particle table** (table A —
`g_scene1_records_a` / `DAT_069b2fb0`, 4096 slots × 0x25 dw stride).
It is NOT a populator for tables B / C — those have separate writers
yet to be found. The wide-followup doc's claim that this function
"writes every per-record table the wide-frustum render passes read"
is incorrect; it ticks one table only.

The matching **spawn API** is `FUN_00447f4f` (11826 B, also unported)
— a parameterized "allocate one A-slot, write [x,y,z,type,scale],
then per-type-specific init" helper, called both by external scene
code AND by `FUN_0040fb3a` itself for three chained-spawn cases
(types 0x1a → 1, 0x20 → 0x21, 0x34 → 0x35).

Render consumers of table A:
- C8c shop walker, **Pass D** (`src/scene1_shop_walker.c` L240).
  Currently a TODO with the table-walk gated on
  `g_scene1_records_a_count`.
- Wide-followup walker `FUN_004161c7`, **Pass F** (color-cycle quads,
  type 0x92).  Unported.

## Function shape — one giant flat dispatch

```c
void FUN_0040fb3a(void) {
    FUN_00414929();              // per-frame open / setup (separate concern)
    int i = 0;
    do {
        // ~95 self-contained `if (type == X)` blocks against
        // (&DAT_069b2fb0)[i * 0x25].  No switch — just a long
        // if-chain; each block is independent and falls through.
        if (type == 0x43) { /* drift + decay + age, kill at 0x18 */ }
        if (type == 0x60) { /* age only, cap at 400, kill at 0x960 */ }
        if (type == 0x53) { /* drift + decay + age, kill at 0x18 */ }
        ...
        if (type == 0x1a) { /* anchor-snap; kill spawns FUN_00447f4f(0,...,1) */ }
        i++;
    } while (i != 0x1000);
}
```

The dispatch is not optimized — every iteration tests **every type**
in sequence. With 4096 slots and ~95 type tests per slot, the inner
work is ~400k branches per tick.  In retail this is fine (mostly
sentinel slots short-circuit early via `type == -1`); for the port
either replicate verbatim or refactor to one switch over the type
field with each handler split out.

## Distinct type codes — ~95 total

Direct comparisons against the type field (77 codes):

```
0xf 0x10 0x11 0x12 0x13 0x15 0x16 0x18 0x19 0x1a 0x1d 0x1e 0x1f
0x20 0x21 0x22 0x23 0x24 0x29 0x2a 0x2c 0x2d 0x2e 0x32 0x33 0x34
0x35 0x36 0x3c 0x3d 0x3e 0x3f 0x41 0x42 0x43 0x44 0x45 0x4a 0x4b
0x4c 0x4d 0x4e 0x4f 0x50 0x51 0x53 0x54 0x55 0x56 0x57 0x58 0x59
0x5a 0x5d 0x5f 0x60 0x62 0x67 0x68 0x69 0x6c 0x6d 0x6e 0x70 0x71
0x74 0x75 0x78 0x79 0x91 0x92 0x93 0x96 0x97 0x98 99 100
```

Additional types stashed into `iVar2` and tested via multi-type
groups (deduplicated against the above):

```
0x1 0x2 0x3 0x4 0x5 0x6 0x7 0x8 0x9 0xb 0xc 0xe 0x14 0x1b 0x1c
0x25 0x26 0x27 0x28 0x2b 0x37 0x38 0x39 0x3a 0x3b 0x40 0x46 0x47
0x48 0x49 0x52 0x5c 0x5e 0x61 0x65 0x66 0x6f 0x72 0x73 0x76 0x77
0x7a 0x7b 0x7c 0x7d 0x7e 0x7f 0x80 0x81 0x82 0x83 0x84 0x86 0x87
0x88 0x89 0x8a 0x8b 0x8c 0x8d 0x8e 0x8f 0x90
```

Type IDs are particle "species" — chosen by the spawner. Examples:

| Type    | Behavior                                                  | Lifetime  |
|---------|-----------------------------------------------------------|-----------|
| 0x43    | drift + decay + age                                       | 0x18 ticks |
| 0x60    | accumulate age, cap velocity at 400                       | 0x960     |
| 0x92    | sinusoidal X-drift, rotate UVs (used by Pass F)           | 0x100     |
| 0x96/97 | drift + scale + gravity (×0.995 damp)                     | 0x40       |
| 0x69    | drift + heavy damp (×0.98)                                 | 0x80      |
| 0x79    | drift + very-light damp (×0.998)                           | 0x131     |
| 0x4a    | matrix transform on position (D3DX `RotZXY` chain)         | 0x18      |
| 0x34    | matrix transform; at kill, chain-spawns type 0x35          | 0x18      |
| 0x21    | cone-spread velocity sampling; gated by another record    | 0x20      |
| 0x20    | snap to player pos; every 4 ticks chain-spawn type 0x21   | until DAT_056dae8c==0 |
| 0x1a    | anchor-snap to a "0x2e9 table" (NPC?); kill spawns type 1 | 1 tick or anchor-gone |
| 6/7/8/9 | rotate around moving camera anchor (`_DAT_073de39c`)      | until DAT_056dae8c==0 |
| 0x6e    | drift; at tick=100, calls FUN_004385fb + FUN_0044b0f3 (mesh emitter) | 0x74 |

Each handler is structurally one of ~6 shapes (counted from a scan
of the function body):

1. **Decay-drift-kill** (most common, ~40 handlers): `pos += vel;
   vel *= k; age++; if (age == N) kill;` — varies only in damp
   factor `k`, kill threshold `N`, and which axes get an extra add
   (gravity = `vel.y -= g`).

2. **Pure-age** (~10 handlers, e.g. 0x44, 0x50, 0x5f): just
   `age++; if (age == N) kill;`. No motion.

3. **Field-decay** (~15 handlers, e.g. 0x4b/0x4c/0x55): a scalar
   field on the record (`0x069b2f9c` etc.) accumulates from
   `0x069b2f8c` per tick; integrates a non-position scalar like
   alpha or rotation.

4. **Anchor-follow** (~5 handlers, types 6-9, 0x21, 0x20, 0x1a,
   0x23): snap or interpolate pos toward an external anchor (player
   pos `DAT_056da1d8/dc/e0`, camera angle `_DAT_073de39c`, or a
   target record looked up via the 0x2e9 table).

5. **Matrix-transform** (4 handlers, 0x4a, 0x34, 0x35, 0x36): build
   a 4×4 RotZXY matrix on the stack via the
   `thunk_FUN_004a3462 / 3537 / 35d3 / 3670 / 2a03` D3DX helpers,
   transform the per-record `0x069b2fa4` baseline, write back to
   `0x069b2f80/84/88`.

6. **Random sin/cos** (4 handlers, 0x18, 0x92): `local_8 =
   FUN_00503a44(seed)` per tick to inject perturbation.

## Per-record fields touched

Field offsets (in dwords from record base):

| Offset (dw) | Bytes from `DAT_069b2f80` | Meaning                              |
|-------------|---------------------------|--------------------------------------|
| 0           | 0x000 (00 / pfVar7)       | pos.x                                |
| 1           | 0x004 (04 / +0x84)        | pos.y                                |
| 2           | 0x008 (08 / +0x88)        | pos.z                                |
| 3           | 0x00c (+0x8c)             | vel.x                                |
| 4           | 0x010 (+0x90)             | vel.y                                |
| 5           | 0x014 (+0x94)             | vel.z                                |
| 6           | 0x018 (+0x98)             | rot.x (or scalar A — e.g. alpha)     |
| 7           | 0x01c (+0x9c)             | rot.y (or scalar B)                  |
| 8           | 0x020 (+0xa0)             | rot.z (or scalar C)                  |
| 9-11        | 0x024-0x02c (+0xa4..+0xac)| baseline pos offset (A/x, A/y, A/z)  |
| 12          | 0x030 (+0xb0) — TYPE      | record type / sentinel (-1 = empty)  |
| 13          | 0x034 (+0xb4)             | age (lifetime tick counter)          |
| 14          | 0x038 (+0xb8)             | scale (set by spawner from param_6)  |
| 15-16       | 0x03c-0x040 (+0xbc..+0xc0)| `param2_int` (the spawner's param_5 / param_7 stored at 0x069b2fc0/c4) |
| ...         | up to 0x094 (0x25 dw)     | per-type scratch (Ghidra didn't decode all) |

Confirmed from `FUN_00447f4f` lines 33-46 (spawn-init writes) and
`g_scene1_records_a` stride in C8g.1's `scene1_records.h`.

## External dependencies (call graph)

```
FUN_0040fb3a
  → FUN_00414929()            (per-frame open; UNPORTED, separate chip)
  → FUN_00447f4f()             (spawn API; UNPORTED, 11826 B)
      ↳ chained from types 0x1a → spawn type 1
      ↳                  0x20 → spawn type 0x21
      ↳                  0x34 → spawn type 0x35
  → FUN_004385fb()             (pick mesh ID for type 0x6e chained mesh emit)
  → FUN_0044b0f3(&DAT_056da1b8, x, y, z, mesh_id, 1, 0)   (mesh emit)
  → FUN_005031e4(vec3)         (vector length)
  → FUN_00503a44(rad)          (sin)
  → FUN_00503994(rad)          (cos)
  → FUN_00471089()             (rand 0..1)
  → thunk_FUN_004a3462()       (D3DXMatrixIdentity)
  → thunk_FUN_004a3537()       (D3DXMatrixRotationY)
  → thunk_FUN_004a35d3()       (D3DXMatrixRotationX)
  → thunk_FUN_004a3670()       (D3DXMatrixRotationZ)
  → thunk_FUN_004a2a03()       (D3DXMatrixMultiply)

Read-only external tables:
  - DAT_056da1d8/dc/e0/f0/f4/f8       — player + spawn-origin pos
  - DAT_056dae8c                       — scene "still alive" flag
  - DAT_056db05c                       — camera/follow yaw (radian)
  - DAT_056db120/124/128/144 (stride 0xf8)  — NPC pos+yaw table
  - DAT_0076bd54/58/5c/60/64/68/98/b0/c464/c488 (stride 0x2e9) — "people"
                                         table (entities the particle can
                                         track); also written by another
                                         routine.
  - DAT_06932510/14 (stride 0x49) and DAT_069324b0 — TABLE B reads (record-
                                         to-record references; e.g. type 0x21
                                         tests `DAT_069324b0[ref*0x49] == 0`
                                         to decide kill).
  - DAT_0695f1e0 (stride 0xa8)         — entity activation gate for types
                                         0x12/0x13/0x14.
  - _DAT_073de39c, _DAT_073de328/30     — global camera angle/orbit anchors.
```

`FUN_00447f4f` is the single biggest unported dependency — at 11826 B
it's bigger than `FUN_0040fb3a` itself, and dwarfs the wide-followup
walker.

## Call sites (5)

All sim-side, all unported today. Addresses corrected against
`docs/decompiled/by-address/` actual file names (wide-followup doc
had three off-by-tens):

| Caller             | Size    | Context                                          |
|--------------------|---------|--------------------------------------------------|
| `FUN_004427d3`     | 30 B    | 6-call thin wrapper: open + FUN_0040fb3a + close |
| `FUN_004536cb`     | 1745 B  | Sim-side counterpart of `FUN_004547ab` case-1; calls inside an `if (DAT_0438b1c0 == ...)` dispatch (state == INGAME tick) |
| `FUN_00442cef`     | 2490 B  | Pre-render gate; calls `FUN_0040fb3a` then a writes-out RPC at L420-L429 (looks like a save-game serializer of player pos) |
| `FUN_00436f97`     | 4788 B  | Scene-entry reset: 600+ field zeros + `scene1_records_reset`-equivalent + 200-iteration `FUN_00447f4f(0, pos, ..., 0x4f, 1.0, 1)` spawn loop at L691-L699, gated on `*(DAT_068dd2f0 + 0x1b28) != 0` |
| `FUN_0048dbfb`     | 2209 B  | Calls `DAT_0076b960 = 0x1000; FUN_0040fb3a();` at L41-L42 — forces "count = max" for one tick, then runs the integrator over every slot. Unusual — maybe debug / scenechange flush. |

The most direct path is `FUN_004536cb → FUN_0040fb3a` — the INGAME
per-tick caller. `FUN_00436f97` is a scene-entry one-shot. The other
three are scene-transition / save-game related, not per-tick.

## What it means for the render path

### Pass F (wide-followup, type 0x92 color-cycle quads)

To make Pass F actually paint:

1. Port `FUN_0040fb3a` (this chip's subject) — type-0x92 handler at
   L171-189 integrates pos via sinusoidal X drift and bumps the
   rotation triad by 0.0157 (~π/200) per tick.
2. Port the type-0x92 case in `FUN_00447f4f` (L82-115) — spawns one
   color-cycle particle with random initial yaw, randomized
   xy-spread.
3. Find / trigger a spawn site that calls `FUN_00447f4f(0, x, y, z,
   0x92, scale, 1)`. The doc-listed callers don't obviously do this;
   need to grep retail XRefs for sites passing `type=0x92`.
4. Port the wide-followup's Pass F render (`FUN_004161c7` L423-481).

### Pass D (C8c shop walker, table A)

Same pre-requisites 1-3 above as Pass F (Pass D also reads table A,
just with different type filters and uses 3D meshes via
`ID3DXMesh::DrawSubset` instead of billboard quads).  No additional
work for the populator side; only the render side TODO at
`src/scene1_shop_walker.c` L260 needs the per-record draw body.

### Passes B / C (C8c shop walker, table B)

NOT served by `FUN_0040fb3a` — table B (`DAT_069324b0`, stride 0x49
dw) has a separate populator. Verifying who writes table B is a
separate survey. (Hint: `FUN_00455191` at the scene1_emit_record
landing wrote table B fields; the full table B writer chain is
still TODO.)

## Implementation plan — chip ladder

Porting in one shot is ~1500 lines of repetitive C. Break it up:

### C8h.1 — skeleton + 4 anchor types (small)

- Translate the outer loop, type dispatch skeleton, sentinel test.
- Port handler types 6-9 (the camera-orbit attract — depends only
  on `_DAT_073de39c` + `_DAT_073de328/30`, no scratch tables).
- Port type 0x21 (cone-spread, depends on type-B reference).
- Port type 0x20 (player-snap; calls FUN_00447f4f chained spawn).
- Per-frame open `FUN_00414929` — survey separately; provisional
  no-op for first chip.

### C8h.2 — particle physics zoo (medium)

- Port the ~40 "decay-drift-kill" handlers — they're cookie-cutter:
  pos+=vel; vel*=k; age++; if(age==N) kill. Most variation is in
  the damp constant + kill threshold. Consider a small helper
  `tick_decay(rec, damp_xyz, gravity_y, kill_age)` to keep this
  module under 1000 lines.
- Port pure-age (~10), field-decay (~15), and random-sin (4) handlers.

### C8h.3 — matrix + chained-spawn handlers (small but blocking)

- Port the 4 matrix-transform handlers (0x4a, 0x34, 0x35, 0x36).
  Need D3DXMatrix helpers — `d3dx8` exports
  `D3DXMatrixRotationX/Y/Z` and `D3DXMatrixMultiply`. Straight
  wrappers via `mingw-w64`'s `d3dx8.h`.
- Port the 3 chained-spawn cases — these require `FUN_00447f4f`
  to be ported first (or stubbed for chained-spawn calls only).

### C8h.4 — anchor-snap to NPC/people tables (medium)

- Port types 0x1a, 0x12/13/14, 0x78, 0x75/0x93 — these depend on
  the stride-0x2e9 NPC table at `DAT_0076bd54+`. Survey of that
  table is its own chip.

### Separate: spawn API chip (C8i — large)

- Port `FUN_00447f4f` (11826 B). Same shape — single allocate-slot
  scan, then per-type init dispatch.  Equally repetitive but with
  trig calls per spawn.  Estimated 800-1200 lines of new C.

## Re-evaluation of the wide-followup doc's MVP suggestion

The doc proposes:
> Port FUN_00436f97 (710 B) first to populate one record type — gives
> a single billboard / mesh to validate the C8c/C8e draw path against.

This is **not minimal** given what we now know:

- `FUN_00436f97` is a 4788-byte scene-entry reset; the spawn loop is
  one of ~600 things it does. Porting it requires understanding
  ~50 BSS-side scene fields it touches.
- The 200-iteration `FUN_00447f4f(...)` call at L691-L699 needs the
  spawn API (`FUN_00447f4f`, 11826 B) ported too — which the doc
  didn't flag.
- The spawn loop is gated on `*(DAT_068dd2f0 + 0x1b28) != 0` — a
  scene flag that's BSS-zero at INGAME entry today, so even with
  `FUN_00436f97` ported it would spawn zero particles.

A more truly minimal MVP that validates the Pass D / Pass F draw
path:

**Option A — direct slot injection (~50 lines of new C, no porting):**

1. In `scene1_preload_house` (or `render_dispatch`'s INGAME entry),
   after `scene1_records_reset`, manually write **one** particle slot:
   `g_scene1_records_a[0..36]` with `{pos=(camera_x,1,camera_z),
   type=0x92, scale=1.0, age=0, baseline=(0,0,0)}`.
2. Force `g_scene1_records_a_count = 1`.
3. Skip the integrator entirely (record is static).
4. Port the Pass F draw body (`FUN_004161c7` L423-481) — pure
   render-side.

This proves the wide-followup's Pass F can paint, with zero sim-side
porting.  If pixels appear, the table-A render contract is verified.

**Option B — minimal integrator + spawner pair:**

1. Port the type-0x92 cases in both `FUN_0040fb3a` (L171-189) and
   `FUN_00447f4f` (L82-115). About 60 lines of new C.
2. Skip the other 94 type handlers — they short-circuit on type
   mismatch.
3. Add one explicit spawn call from INGAME entry.

Cleaner than Option A (uses real engine APIs), but ports ~10× more
code than the truly minimal direct-injection approach.

**Recommendation:** Do Option A first (pixels in 1 chip).  Use the
output to validate the render port. Then climb the C8h ladder for
the real integrator + spawner only once the render side is known
correct.

## Implementation hazards

- **No type-field validation in the dispatch.** A corrupted type
  byte that happens to match one of the ~95 codes will execute that
  handler against garbage scratch fields. Port verbatim — any
  attempt to "fix" the dispatch with bounds checks will diverge
  from retail.

- **Cross-record references (`(&DAT_069b2fc4)[i * 0x25]` as an
  index into another table).** Several handlers (0x78, 0x75, 0x21,
  0x6d, 0x67) interpret a scratch field as an index into table B or
  the 0x2e9 NPC table. A stale or off-by-one record-index will
  segfault. Mirror retail's lack of bounds-checking; the populator
  is trusted to write valid indices.

- **Heavy `float10` / `double` use.** Ghidra's `float10` is x87 long
  double. The actual code is x87 FPU emit (FLDS/FSIN/FCOS via the
  three thunks `FUN_005031e4 / 503a44 / 503994`). Port the three
  thunks first or wrap them around `sinf` / `cosf` / `sqrtf` and
  accept tiny numeric divergence.

- **D3DX matrix helpers (`thunk_FUN_004a3462 / 3537 / 35d3 / 3670 /
  2a03`) are dispatched as function-pointer thunks.** They resolve
  to `D3DXMatrixIdentity / RotationY / RotationX / RotationZ /
  Multiply` in `d3dx8.dll`. Mingw-w64 ships `d3dx8.h` —
  straightforward calls.

- **The 0x1a handler reads `DAT_0076bd98 + i * 0x2e9` to test
  "anchor still alive".** The 0x2e9 table (stride 745 dw) is the
  "people / shop visitors" table — needs its own chip survey
  before the 0x1a handler can be ported safely.

- **The `count_a / count_b / count_c` globals computed by
  `scene1_records_counter_scan` (landed in C8g.1) are upper bounds,
  not active counts.** Both `FUN_0040fb3a` and the render walkers
  iterate `0..count`, testing sentinel-empty per slot. Don't try to
  maintain a separate "active" count.

## Related files

- `src/scene1_records.{c,h}` — C8g.1 storage + counter scan
  (landed 2026-05-23).
- `src/scene1_shop_walker.c` — C8c port; Pass D body is the TODO at
  L260 that this work unblocks.
- `docs/findings/scene1-wide-followup.md` — sibling survey for the
  wide-followup walker; needs an erratum noting that 0040fb3a is
  table-A-only.
- `docs/findings/scene1-render.md` — C7/C8 chip ladder.
- `docs/decompiled/by-address/40fb3a.c` — 1249-line decomp.
- `docs/decompiled/by-address/447f4f.c` — 1449-line decomp of the
  spawn API (needed for full integrator port — see C8i chip above).
- `docs/decompiled/by-address/436f97.c`, `4427d3.c`, `442cef.c`,
  `4536cb.c`, `48dbfb.c` — the 5 call sites.

## C8h.1 landed (2026-05-23)

`src/scene1_particles_tick.{c,h}` + `src/scene1_spawn.{c,h}` +
18 new unit tests.  All 869 host tests pass; Win32 build links;
boot-idle scenario unchanged.

### Ports in this chip

| Type(s)   | Behavior                                                | Notes                                     |
|-----------|---------------------------------------------------------|-------------------------------------------|
| 6, 7, 8, 9| Camera-orbit attract (4-share one body)                  | See "Pending human checks" §1            |
| 0x20      | Player-snap to spawn-origin; chains 0x21 every 4 ticks   | Chain spawn goes through scene1_spawn()   |
| 0x21      | Cone-spread vel sampling; table-B-referenced kill gate   | -                                         |

### Unwired status

`scene1_particles_tick()` has **no caller**.  The engine's per-tick caller
is `FUN_004536cb` (1745 B, INGAME sim branch — unported).  The
integrator currently runs only from the unit tests; sim integration
lands when `FUN_004536cb`'s scene-1 case ports.

### Pending human checks (for next-session Frida validation)

These are the items where Ghidra's decomp dropped FPU-stack
arguments and the port made best-guess reconstructions.  Each needs a
short Frida read of the retail integrator to confirm the actual arg.

1. **Type 6..9 — line 1120 of FUN_0040fb3a.**
   Decomp shows `fVar9 = (float10)FUN_00503a44();` with no argument.
   Our port uses `sinf((float)age)` based on the most-recent FPU
   load.  The visible effect is "vertical bob" on pos.y.
   - Verify via: spawn a type-6 particle in retail, snapshot pos.y
     over 20 ticks, fit against `sinf(age * k)` for various k.
   - Likely candidates besides age: `age * 0.08`, `local_c - yaw`
     (the orbit angle from L1107).

2. **Type 0x21 — line 487.**
   `fVar9 = (float10)FUN_00503994();` with no arg.  Port uses
   `cosf(angle)` where angle is the just-stored `(age * π/4) / 32`.
   This one is **less ambiguous** — only one float was on FPU TOS.
   Confidence: HIGH.  Frida-verify only if a divergence shows up.

3. **scene1_spawn trailing args.**
   The integrator's chain-spawn calls use `scene1_spawn(0, x, y, z,
   type, 1.0f, 0)`.  The engine's Ghidra decomp shows only 5 args
   (e.g. L471: `FUN_00447f4f(0, pos.x, pos.y, pos.z, 0x21)`).  The
   trailing `scale` + `param7` are dropped by Ghidra.  Defaults of
   `1.0f` and `0` are MVP-safe; the real values land when the C8i
   chip ports the spawn API and we can read the engine's actual
   call-site stack layout.

4. **Type 0x21 — table-B OOB read.**
   Engine reads `g_scene1_records_b[PARAM2 * 0x49]` without bounds
   checking.  Our port treats OOB as "scene_alive" rather than
   crashing.  If a retail test produces an observable crash on OOB
   PARAM2, we'll revert to the unchecked read; for now safe-default.

### Files added/changed
- NEW `src/scene1_particles_tick.{c,h}` — the integrator (4 handlers).
- NEW `src/scene1_spawn.{c,h}` — stub for FUN_00447f4f (record-only).
- NEW `tests/test_scene1_particles_tick.c` (15 tests).
- NEW `tests/test_scene1_spawn.c` (3 tests).
- `tests/Makefile`, `tests/test_main.c` — wire new modules + tests.
