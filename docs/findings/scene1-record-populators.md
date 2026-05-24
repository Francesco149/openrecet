# Scene-1 render record populators — table B + table C survey

**Status (2026-05-24):** survey only.  No port yet.  Documents the
engine functions that populate the two non-particle render-record
tables consumed by `scene1_shop_walker` (Pass C) and
`scene1_wide_followup` (Pass A/B/C/D/E).

## TL;DR

| Table | Base addr | Layout | Sentinel | Count global |
|-------|-----------|--------|----------|--------------|
| A     | DAT_069b2f80 | 4096 × 0x25 dw | type==-1 (0xffffffff) | DAT_0064e818 |
| B     | DAT_069324b0 |  512 × 0x49 dw | **type==0** (NOT -1) | DAT_0076b964 |
| C     | DAT_06956cd8 |  200 × 0x25 dw | type==-1 (0xffffffff) | DAT_0076b968 |

Table A's populator (`FUN_00447f4f`) and integrator (`FUN_0040fb3a`)
are already ported (C8h/C8i ladders).  Tables B and C have **distinct
allocators + their own integrators** that have not been ported.

**Critical sim-arm finding:** the existing Cs1 port of `scene1_sim`
covers only the rare INGAME sub-arm (`FUN_004427d3`, 30 B wrapper).
The **default INGAME arm** is `FUN_00442cef` (2490 B), and *that's*
where the table B/C ticks live — neither table will see real records
until FUN_00442cef ports.

## Table B (DAT_069324b0) — 512 × 0x49 dw

### Allocators

Both allocators use the same "scan for type==0 sentinel, claim slot,
set fields, write type last" pattern (the type field IS the sentinel —
zero means free).

**FUN_0044376a @ 0x44376a (8538 B)** — `(owner, type, flag)`.
Spawns *from a small entity* (uses `owner+0x20..0x28` as pos triplet).
Allocator shape (decomp L40928-40945):
```c
do {
  if (*piVar13 == 0) {                                   /* free slot */
    (&DAT_069324c0)[i * 0x49] = owner;                   /* +0x10 owner */
    (&DAT_06932548)[i * 0x49] = 0;                       /* +0x98 age */
    *piVar13           = type;                           /*  +0x00 type (claim) */
    (&DAT_0693250c)[i * 0x49] = *(undefined4 *)(owner + 0x20);  /* +0x5c pos.x */
    ...
  }
  piVar13 += 0x49;
} while (...);
```
Followed by a switch-on-type that sets per-type velocity, animation
seed, etc. (~8.3 KB of per-type init code, similar in shape to
FUN_00447f4f for table A).

**Distinct types observed across ~30 call sites:**
`0x22, 0x3e, 0x49, 0x51, 0x52, 0x53, 0x58, 0x5b, 0x5f, 0x60-0x62,
0x66, 0x67, 0x69, 0x6a, 0x71, 0x83, 0x85-0x87, 0x9b`.

**FUN_00445a8c @ 0x445a8c (8952 B)** — `(owner_npc, type, flag)`.
Spawns *from a people-table NPC entry* (uses `owner+0x3f0..0x3f8` as
pos triplet — confirmed identical offset to `scene1_people_entry_t`).
Allocator shape is parallel to FUN_0044376a:
```c
do {
  if (*piVar8 == 0) {
    (&DAT_069324b4)[i * 0x49] = flag;                    /* +0x04 flag */
    *piVar8           = type;                            /*  +0x00 type (claim) */
    (&DAT_069324c0)[i * 0x49] = 0;                       /* +0x10 unused */
    (&DAT_069324c4)[i * 0x49] = owner_npc;               /* +0x14 owner */
    (&DAT_0693250c)[i * 0x49] = *(undefined4 *)(owner_npc + 0x3f0);  /* pos.x */
    ...
  }
} while (...);
```
Slight field-shuffle: FUN_0044376a stores owner at +0x10 with flag at
+0x14; FUN_00445a8c stores flag at +0x04 with owner at +0x14.  Both
allocator-then-init halves end up at the same "ready to integrate"
state by the time the type-claim happens — the field-placement diff is
because FUN_0044376a's parent is a generic entity (struct shape A) and
FUN_00445a8c's parent is a people-table NPC (struct shape B).

**Distinct types observed across ~30 call sites:**
`0x10-0x15, 0x1a, 0x1e, 0x1f, 0x21, 0x25, 0x26, 0x46, 0x49, 0x56,
0x6b, 0x6c, 0x88, 0x89, 0x96, 0x98-0x9a, 0x9e`.

**Type-namespace overlap:** types 0x49 and 0x96 appear in BOTH
allocator's call sites.  Other types appear in only one.  Net unique
type set across both allocators: ~47 distinct render-record types.

### Tick / integrator

**FUN_0043ae20 @ 0x43ae20 (25750 B)** — the giant player+NPC+world
game-logic monster.  Currently stubbed in `src/scene1_sim.c` as one of
FUN_004427d3's 6 unported siblings.  Its body iterates table B
(decomp L36420+) with per-record state-machine ticks similar in spirit
to FUN_0040fb3a but ~3.2× larger and tightly coupled to player + NPC
state.  This is the Mt. Everest of the sim port; it touches gameplay
logic far beyond what's needed for HOUSE-only rendering.

### Per-tick caller chain

`scene_state_dispatch (FUN_004536cb)` →  at `DAT_0438b1c0 == 1` (INGAME),
the dispatch picks ONE of three sub-arms based on transient flags:

| Predicate | Sub-arm | Contains table B tick? |
|-----------|---------|------------------------|
| `DAT_0438b1d0 != 0` (transition flag set) | FUN_004427d3 (30 B wrapper) | yes — via FUN_0043ae20 |
| `DAT_0438b1d8 != 0` | (skipped — no sim call) | n/a |
| `DAT_0438b1c8 == 0` (default running) | **FUN_00442cef (2490 B)** | **yes — via FUN_0043ae20** |
| else (paused) | FUN_004427d3 | yes |

`scene1_sim.c::scene1_ingame_tick` (Cs1) ports the FUN_004427d3 path,
which is the transition + paused arm.  The *default running* arm
(FUN_00442cef) is unported — that's where the bulk of frame-to-frame
gameplay logic fires, including table C's tick (see below).

## Table C (DAT_06956cd8) — 200 × 0x25 dw

### Allocators

Slot layout subtlety: the "type" field at DAT_06956cd8 is **not** at
slot-offset 0.  The slot actually starts 10 dwords (40 B) earlier at
DAT_06956cb0, with type at slot+10dw.  Both allocators below walk
slots from DAT_06956cb0 and test `puVar[10] == -1` for the free
sentinel.

**FUN_0044aef0 @ 0x44aef0 (96 B)** — `(p1, p2, p3, p4, type)`.  Minimal
13-write slot setup; appears to be the "lightweight" allocator for
quick item drops where the producer already has the type pinned.
```c
puVar1 = &DAT_06956cb0;
do {
  if (puVar1[10] == -1) {
    *puVar1     = p2;        /* +0x00 (10 dw before type) — entity ref? */
    puVar1[1]   = p3;        /* +0x04 */
    puVar1[2]   = p4;        /* +0x08 */
    puVar1[5..3] = 0;        /* +0x14, +0x10, +0x0c (vel xyz?) */
    puVar1[0xb] = 0;
    puVar1[10]  = type;      /* +0x28 — DAT_06956cd8 (claim) */
    puVar1[0xc] = 0x3f800000; /* 1.0f */
    puVar1[0x10] = 2;        /* state = 2 */
    puVar1[0x11] = 0;
    puVar1[0x24] = 0;
    puVar1[0xf]  = 0;
    return;
  }
  puVar1 += 0x25;
} while (puVar1 != &DAT_0695e050);
```
Called from 2 sites only: L34981, L35013 (inside the same enclosing
fn — not yet identified).

**FUN_0044af50 @ 0x44af50 (419 B)** — 11-param allocator.  The
heavy-duty version: writes ~30+ slot fields, handles type-overriding
via RNG cycle for types in the 0xc80..0xce3 range (sets type ∈
{0,1,2,3,4} based on `rng % 100` percentile thresholds — this is the
**4-color world-item drop ramp**, L44833-44849).  Sig:
```c
FUN_0044af50(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11)
//                  ^ type (or via p11 override)
```
Called from 2 direct sites (L47549, L47837) + via wrappers below.

**Wrappers around FUN_0044af50:**
- **FUN_0044b0f3 @ 0x44b0f3 (60 B)** — 9-arg → FUN_0044af50 with
  `p10=0, p11=0xffffffff`.  This is the item-drop wrapper, called
  ~9 times (L10035, L28026, L28077, L28086, L28105, L28117, L28131,
  L28210, etc.).  Already referenced as a stub from C8h.4b's
  `scene1_mesh_emit` for type 0x6e particle chains.
- **FUN_0044b12f @ 0x44b12f (61 B)** — 10-arg → FUN_0044af50 with
  `p10=0, p11=p10`.  Called 1× at L89826.

### Tick / integrator

**FUN_0044284b @ 0x44284b (1083 B)** — table C per-tick integrator
(decomp L40229).  Two-phase:

1. **Overflow eviction** (L40247-40287): scans for type∈{0,1,2,3}
   slots, if count > 6, finds oldest among them (by `slot[1]`= age?)
   and writes `0xffffffff` to its type field to kill it.  This is the
   "no more than 6 world-item drops" cap.

2. **Per-slot integrate** (L40288-40391): iterates all live slots.
   For each:
   - If `slot[0x19] == 1` (state phase): just decrement counter.
   - Else if `slot[5] == 2` (held/pickup state): age-bump, optional
     bob (`slot[-10] += 0.05` over age 0x14..0x4f), and at age==0x78
     fire `FUN_00484dd1(slot.type, slot.x, slot.y)` to commit
     pickup-into-inventory, kill slot.
   - Else if `slot[5] == 0` (world-drop physics): drift + damping ×
     0.97, micro-bounce check via FUN_00433674 ground raycast, then
     velocity → target-attraction toward DAT_056da1d8..1e0 (player
     pos) for nearby (`slot < 0x4b0`) items, plus `FUN_00432e50` ground
     clamp.  Kills at age==0xf0 (240 frames) or pos.y < -1.0.

The integrator is type-agnostic for the bulk of the body — types
0/1/2/3 are the "4-color world item" group with identical physics, and
type 0x78 has a special pickup branch.  No FUN_0040fb3a-style per-type
mega-switch.

### Per-tick caller chain

`FUN_00442cef` (the default-running INGAME arm — see table B above) at
L40611:
```c
FUN_0044284b();   /* table C tick */
```
Same caller also fires `FUN_0043ae20` at L40603 (table B tick), so
porting FUN_00442cef wires both ticks in one chip.

## Implications for the C8j ladder

**Net unported surface to get tables B+C populated in HOUSE:**

- **FUN_00442cef** (2490 B) — the default INGAME sim arm wrapper.
  Inner body has lots of game-logic conditionals (player carry-state,
  pickup proximity, NPC interaction gating) that we may stub or port
  thin depending on what HOUSE actually needs.
- **FUN_0043ae20** (25750 B) — table B per-record tick + writers.
  This is **the** Mt. Everest.  Full port not justified for HOUSE-only
  rendering; we'll need a chip strategy that stubs out player+NPC
  branches and only ports the per-record drift code.
- **FUN_0044284b** (1083 B) — table C tick.  Small, mostly geometry +
  physics; portable as a single chip (analogous to C8h.1 size).
- **FUN_0044376a** (8538 B) — table B allocator (small-entity owner).
  Similar shape + size to FUN_00447f4f.  Could ladder into ~4-5
  sub-chips by type group, mirroring C8i.0-5c.
- **FUN_00445a8c** (8952 B) — table B allocator (people-table owner).
  Similar shape + size.  Could be a parallel ladder, or merged with
  FUN_0044376a since they share the per-type init switch body.
- **FUN_0044aef0** (96 B) + **FUN_0044af50** (419 B) + wrappers —
  table C allocators.  Small; portable as a single chip.

**Realistic chip ordering (proposed C8j ladder):**

1. **C8j.0** — this survey doc (current chip).
2. **C8j.1** — port FUN_0044284b (table C tick).  Smallest, no
   dependencies.  Adds `scene1_records_c_tick()`.  Wires into a new
   `scene1_sim_arm_b` stub.
3. **C8j.2** — port FUN_0044aef0 + FUN_0044af50 + 2 wrappers (table C
   allocators).  Adds `scene1_record_c_spawn_small/heavy/wrapped()`.
4. **C8j.3** — port FUN_00442cef as a thin wrapper that calls only
   the table A/B/C ticks (FUN_0040fb3a, FUN_0043ae20-stub, FUN_0044284b)
   and stubs out the gameplay-logic branches.  Wires into `scene1_sim`
   as the default-running arm.
5. **C8j.4..n** — port FUN_0044376a + FUN_00445a8c per-type init
   sub-chips (C8i-style ladder, ~5 chips by type cluster).
6. **C8j.fin** — survey FUN_0043ae20 (25750 B) for a HOUSE-minimal
   port chip ladder.  Without this, no in-port code will *write* table
   B records — but the allocator + tick + render path will all be wired
   so a `--force-record-b-spawn` style CLI flag could drive smoke
   visibility.

**Alternative minimal path (smoke-flag-driven, no FUN_0043ae20):**
1. C8j.1 + C8j.2 + C8j.3 as above.
2. Add `--force-record-b-spawn <type>` and `--force-record-c-spawn
   <type>` flags that exercise the allocators directly.
3. Visible HOUSE Pass A/B/C/D/E pixels become driveable via CLI for
   smoke / regression, deferring the FUN_0043ae20 monster.

This mirrors the C8h+C8i+Cf strategy: get the integrator + spawn API
+ render walker structurally in place behind feature flags, then port
the production data source later.

## Open questions / unknowns

1. **Wrapper sentinel `0xffffffff` for table C**: FUN_0044b0f3 passes
   `0xffffffff` as the type override.  FUN_0044af50 L44833-44849 reads
   the override; if `< 0` it falls into the 4-color RNG ramp.  So the
   wrapper effectively says "spawn a random-colored world item drop".
   Confirm via Frida that the RNG branch fires for typical wrapper
   calls.

2. **DAT_0438b1c8 semantics**: the flag that gates which INGAME
   sub-arm runs.  Need to identify writers — likely set by menu-open /
   conversation-open / cinematic-open events.  Until identified, the
   port of FUN_00442cef will use a stand-in pinned to 0 (always run
   default arm).

3. **DAT_0438b1d0 semantics**: similar — the transition flag that
   routes to FUN_004427d3.  Need writers to confirm Cs1's port
   correctness for non-transition frames.

4. **Net HOUSE need from FUN_0043ae20**: which subset of its body
   actually writes table B records that HOUSE walkers consume?  A
   targeted decomp scan (similar to the FUN_0040fb3a survey that
   yielded scene1-particles-tick.md) is the prerequisite for C8j.fin.

5. **Field maps for table B and C slots**: scene1_records.h has
   partial maps for table A.  After C8j.2/j.3 lands, extend with full
   stride-0x49 (table B) and stride-0x25 (table C) field tables
   anchored to the allocator writes documented above.
