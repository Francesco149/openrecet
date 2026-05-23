# Scene-1 particle spawn API (FUN_00447f4f) — survey + C8i ladder

**Status (2026-05-23):** Survey only.  Plans the C8i chip ladder.  This
function is the matching writer for table A whose ticker (FUN_0040fb3a)
already landed in C8h.

## TL;DR

`FUN_00447f4f` @ 0x447f4f (11826 B, 1449 lines of Ghidra C) is the
**per-type spawn API for the scene-1 particle table** — same table A
that the integrator ticks (`g_scene1_records_a` / `DAT_069b2f80`,
4096 slots × 0x25 dw stride).  It writes the *initial* slot state for
~95 type IDs; the integrator then ages/drifts/kills each slot per its
type-specific tick body.

Currently stubbed by `src/scene1_spawn.c` — the integrator's three
chained-spawn cases (types 0x1a→1, 0x20→0x21, 0x34→0x35) route through
`scene1_spawn()` which only records the call into a trace ring buffer.
C8i replaces that stub with the real per-type init.

Once C8i lands, the spawn API will populate slots that the C8h
integrator already knows how to tick, and the Pass D / Pass F render
walkers (already structurally ported in C8c/C8g.2) will receive real
records — i.e. **C8i unblocks the first visible particle effects in
HOUSE.**

## Function shape — outer slot scan + per-type init dispatch

```c
void FUN_00447f4f(int hint, float x, float y, float z, int type,
                  float scale, int param_7) {
    uint local_8 = 0;   /* count of particles spawned so far */
    int local_10 = 0;   /* current slot index 0..4095 */
    do {
        int iVar7 = local_10 * 0x94;            /* byte offset */
        float *pfVar8 = &DAT_069b2f80 + local_10 * 0x25;  /* slot base */
        if ((&DAT_069b2fb0)[local_10 * 0x25] != -1)
            goto LAB_0044acdf;                  /* slot busy → next */

        /* --- common preamble: write 11 fields --- */
        slot.aux_18 = hint;          /* DAT_069b2fc8 */
        slot.pos    = (x,y,z);       /* DAT_069b2f80..88 */
        slot.vel    = (0,0,0);       /* DAT_069b2f8c..94 */
        slot.rot    = (0,0,0);       /* DAT_069b2f98..a0 */
        slot.age    = 0;             /* DAT_069b2fb4 */
        slot.param2 = 0;             /* DAT_069b2fc4 */
        slot.type   = type;          /* DAT_069b2fb0 */
        slot.scale  = scale;         /* DAT_069b2fb8 */

        /* --- per-type init dispatch (long if-chain) --- */
        if (type == 1 || type == 0x5e || type == 2 || type == 3 ||
            type == 0x52 || type == 0x65) { /* group-A radial burst */ }
        else if (type == 0x92) { /* color-cycle billboard burst */ }
        else if (type == 0x79) { /* swarm-128 */ }
        else if (type == 0x5d) { /* swarm-45 */ }
        ...
        else goto LAB_0044acdf;     /* unknown type → just leave preamble */

        /* --- return logic varies per type --- */
        local_8++;
        if (local_8 == THRESHOLD_FOR_THIS_TYPE) return;

LAB_0044acdf:
        local_10++;
        if (local_10 == 0x1000) return;
    } while (true);
}
```

So `local_10` walks **slots** (sentinel-empty first-fit); `local_8`
counts **particles spawned**.  Each call to the spawn API may produce
1..N particles, where N depends on `type` (see the count table below).

## Per-type loop count (how many particles per call)

| Count | Types                                                |
|-------|------------------------------------------------------|
| 1     | most — single-spawn types (default)                  |
| 8     | 1, 2, 3, 0x52, 0x5e, 0x65                            |
| 12    | the line-1240 mega-group (~60 types — see below)     |
| 14    | 0x29 (`local_8 == 0xd`)                              |
| 45    | 0x5d (`local_8 == 0x2c`)                             |
| 128   | 0x79 (`local_8 == 0x7f`)                             |
| param_7 | 0x12, 0x54 (caller specifies count via param_7)    |
| spawn count varies | 0x67, 0xe, 0x2b, 0x1b, 0x3b, 0x76        |

After spawning N particles, the function returns even if more empty
slots exist beyond `local_10`.

## Special control-flow cases

- **type 0x60** — no per-type body at all; common preamble only,
  returns after 1 (LAB_0044a997).  Used as a "reserve a slot for me"
  primitive.
- **type 0x20** — common preamble + age=0, returns after 1
  (LAB_0044a994 → 0a997).  Integrator's type-0x20 every-4-tick chain
  fires `spawn(0,x,y,z,0x21)`.
- **type 0x21** — full init body + sets param2 = param_7; returns
  after 1 (LAB_00448f57 → LAB_0044a985).
- **type 0x66** — vel = (0,0,-1.0) hard-coded, random age = u%100+20.
  Cleanest "anchor" type — no PRNG-driven xy/z; useful as C8i.1
  validation target.
- **types 6, 7, 8, 9** — sets param2 = DAT_056dae84 only (no vel/age
  writes), returns immediately.  Integrator's type-6..9 handler is the
  camera-orbit attractor — these spawn slots are pre-anchored by the
  caller.

## Line-1240 mega-group (60+ types share one init body)

The dispatch at L1240 (after the parenthesis explosion) covers:
`0x25, 0x26, 0x27, 0x28, 0x37, 0x38, 0x39, 0x3a, 0x46, 0x47, 0x48,
0x49, 0x7a..0x84, 0x86..0x90` — all share one randomized init body
(small ground-skew velocity, ground pos with small offset, color =
u%10, age=0, return after 12 particles).  This is the "scatter
generic small particle" body.

This is the line-1240 case that lets C8i.4 land ~60 types in one
helper + one big if-condition.

## C8i chip ladder

| Chip | Engine scope | Est new C | Notes |
|------|--------------|-----------|-------|
| C8i.0 | this survey | — | persisted here |
| C8i.1 | outer loop + common preamble + 3 anchor types (0x60 no-op, 0x20 single-field, 0x66 zero-vel-down + age) | ~250 | also wires `scene1_spawn()` from stub to real call; trace ring buffer kept as opt-in instrumentation |
| C8i.2 | 8-spawn group (1/2/3/0x52/0x5e/0x65), 0x92 burst, 0x79 swarm-128, 0x5d swarm-45 | ~300 | shared "radial-burst" helper falls out of group-A; 0x92 mirrors integrator's matrix-init pattern |
| C8i.3 | single-spawn drift zoo (~41 cookie-cutter types) — split into 4 sub-chips after survey was reread | ~600 | helpers from C8h's `drift_decay_*` shape carry over |
| C8i.3a | world-anchored radial variants (lines 143-264): 0x69, 0x68, 0x73, 0x77, 99, 0x78 | ~150 landed | 0x69 shares 0x79's LAB_004481fa AGE-stagger but with wider mag = 2*(u+0.2). 0x68 swaps sin→vx cos→vy. 0x73/0x77 use param_7/65536.0 as fixed-point Q16 angle. 99/0x78 anchor pos back -40× and write BASE = vel*-40 (recovery target). |
| C8i.3b | mixed-shape multi-particle radials (lines 289-527): 0x53, 0x4a, 0x43, 0x97, 0x96, 0x40, 0x36, 0x74, 0x4e | ~250 landed | 0x97/0x96 are 64-spawn spherical with secondary u*π/2 elevation; 0x96 adds camera-angle bend via stand-in global `g_scene1_spawn_camera_counter_948` (pending-human-check #8 — engine reads `*(int*)(slot_hint+0x948)` directly). 0x4a writes matrix rot seeds + PARAM1=param_7 + AGE=local_8*-4 (8-spawn via LAB_0044acd2). 0x36/0x74 are param_7-count (dispatcher gained `spawn_count_is_param7` fork). |
| C8i.3c | local_8-azimuth + chain pair (lines 528-735): 0x34, 0x35, 0x2c, 0x29, 0x32, 0x4c, 0x55, 0x4b, 0x33, 0x4d, 0x51, 0x57, 0x3e | ~400 landed | 0x34 has a dead-coded vel batch overwritten by final vel.y/z = u*2π (engine reuses VEL slots as integrator scratch). 0x35 is the kill-chain target; preamble vel=0 means pos collapses to (x,y,z). 0x29 doesn't write pos/age/PARAM1 at all — preamble values stand. The string family (0x32 / 0x4c / 0x55 / 0x4b / 0x33 / 0x4d / 0x51 / 0x57 / 0x3e) all encode `rot.x = local_8 * π + offset` (offset π/2 for 0x32, π/4 for the rest). 0x33/0x4d/0x51 then overwrite rot.x with random. 28 host tests added (1009 → 1037 total). |
| C8i.3d | orbit/fountain/world-jitter exotics (lines 736-975): 0x3d, 0x6d, 0x45, 0x6c, 0x6e, 0x1f, 100, 0x23, 0x22, 0x3c, 0x5a, 0x2d, 0x1d | ~200 | 0x6d/0x45 read DAT_056db05c (camera yaw) for orbital fountain bias. 0x1f/100 read DAT_056dab58 (scene-counter wave). 0x6e is the waypoint-homing single particle (computes vel = (target - pos)/100). 0x22/0x3c/0x5a/0x2d are 20-particle world-jitter via LAB_00449894 with the iVar7 = -4-local_8 or (-8-local_8)*2 AGE biasing. |
| C8i.4 | line-1240 mega-group (~60 types in one body) | ~150 | single `init_scatter_small()` + giant `is_scatter_type()` predicate |
| C8i.5 | param_7-count + table-dep + activation-gate + chained-spawn tails (0x12/0x54/0x6e/0x75/0x93/0x98 + 6-9 + 0x11 + 0x59/0x67/0xe/0x2b/0x1b/0x3b/0x76 + 0x21 + 0x32 + 0x41/0x61/0x62/0x72 + 0x4f/0x58/0x3f/0x56/0x10/0x91/0xf/0x71/0x50/0x15/0x16/0x18/0x44/0x42/0x94/0x2e/0x1e/0x2a/0x13/0x14/0x24/0x5f/4/0x70/0x1c/0x19/0x1a) | ~300 | resolves pending-human-check items #3 and #6 (5-arg Ghidra sig confirmed once the caller-stack layout for chained spawn is observed in raw asm) |

After C8i.5 lands, all ~95 per-type spawn handlers are covered and
`scene1_spawn()` is the real engine entry point.  Next functional
milestones (per `openrecet_scene1_render_ladder` memory):

1. Wire `scene1_particles_tick` into the unported `FUN_004536cb`
   INGAME sim caller.
2. Port `FUN_00414929` per-frame open (~1465 B, ticks two non-particle
   entity tables at DAT_00730c30 / DAT_0064e8a0).
3. Port `FUN_00436f97`'s 200-iter spawn loop (or smaller MVP analog)
   that actually *calls* `scene1_spawn()` from sim code.
4. Route `scene1_render_meshes` into `render_dispatch`.

At that point Pass D + Pass F start receiving real records, and the
HOUSE scene should produce visible particle effects.

## Table A column writes (writer view)

`scene1_records.h` documents the universal field offsets from the
*consumer* (counter scan + render walker) view.  The spawn API writes
all of them and uses the following dwords for type-specific scratch:

| Off | Slot field | Spawn-writer use                                |
|-----|-----------|--------------------------------------------------|
| 0   | pos.x     | param_2 (caller-supplied world x)                |
| 1   | pos.y     | param_3                                          |
| 2   | pos.z     | param_4                                          |
| 3   | vel.x     | random or trig-derived                           |
| 4   | vel.y     | random or trig-derived                           |
| 5   | vel.z     | random or trig-derived                           |
| 6   | rot.x     | 0 (default); some types overwrite                |
| 7   | rot.y     | 0 (default); some types overwrite                |
| 8   | rot.z     | 0 (default); some types overwrite                |
| 9   | base.x    | only types 99/0x78 (anchor-to-emitter)           |
| 10  | base.y    | only types 99/0x78                               |
| 11  | base.z    | only types 99/0x78                               |
| 12  | type      | param_5 (always set)                             |
| 13  | age       | 0 default; some types pre-roll with `u%24` etc.  |
| 14  | scale     | param_6 (always set)                             |
| 15  | aux_15    | param_7 (types 0x41/0x61/0x62/0x72)              |
| 16  | param1    | param_7 (types 0x4a/0x12/0x78); random life cap (many types) |
| 17  | param2    | per-type scratch (random color index / counter)  |
| 18  | aux_18    | param_1 (slot_hint, every type)                  |

## Pending-human-check tie-ins

When C8i.1 lands, the chained-spawn call sites in
`scene1_particles_tick.c` (types 0x1a / 0x20 / 0x34) flip from calling
the stub `scene1_spawn(0, x, y, z, type, 1.0f, 0)` to calling the real
one with the same signature.  The 5-arg Ghidra sig (`FUN_00447f4f(0,
x, y, z, type)`) is resolved by reading the caller-side raw asm for
the missing `scale` / `param_7` push order — done once at C8i.1 land
time, removes pending items #3 and #6.

## Cross-references

- Consumer of this writer's output: `src/scene1_particles_tick.c`
  (C8h.1-4d, all per-type tick handlers).
- Render path that walks the populated table:
  `src/scene1_render_meshes.c` + Pass D (in `src/scene1_shop_walker.c`)
  + Pass F (in `src/scene1_pass_f.c`).
- Sentinel reset of the same table: `src/scene1_records.c`
  (`scene1_records_reset`).
- Sibling stub for non-particle mesh emit: `scene1_mesh_emit` in
  `src/scene1_spawn.c` (FUN_0044b0f3); not in scope for C8i.
