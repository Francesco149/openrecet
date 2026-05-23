# Scene-1 "people" table (stride 0x2e9) — survey

**Status (2026-05-23):** Survey only.  Written to unblock C8h.4 (the
`scene1_particles_tick` handlers that anchor onto in-shop customers/NPCs).

## TL;DR

`DAT_0076bd54` is the base of a flat **128-entry × 2980 B** ("people")
table.  Each entry models one in-shop NPC (customer, dungeon visitor,
event actor — the integrator doesn't distinguish).  The particle
integrator reads only the header (~+0..+0x68) — pos vectors, the
alive flag, an action ID, and two cooldown counters.  The remaining
~2900 B/entry is sprite/AI/dialog scratch that the integrator never
touches.

Total footprint: 128 × 0x2e9 dw = 128 × 2980 B = **95 360 B** of BSS.

The table is **not paged, not per-floor, not pointer-indexed**.  It's
a single contiguous global array.  Empty slots are detected by
`alive_flag == 0` at byte offset +0x44.

## Header layout (the only bytes the integrator cares about)

| Byte offset | Name (assigned) | Type   | Meaning                                                       |
|-------------|-----------------|--------|---------------------------------------------------------------|
| +0x00       | `pos.x`         | float  | Primary world-space position                                  |
| +0x04       | `pos.y`         | float  |                                                               |
| +0x08       | `pos.z`         | float  |                                                               |
| +0x0c       | `target.x`      | float  | Second position vector — integrated `target += vel` per frame in FUN_00430c6d L175-183.  Looks like an accumulated displacement / interpolation target, NOT a velocity. |
| +0x10       | `target.y`      | float  |                                                               |
| +0x14       | `target.z`      | float  |                                                               |
| +0x44       | `alive`         | int    | 0 = empty slot.  1 / 2 are distinct alive-states (probably "spawned" / "leaving").  Integrator's kill gate only tests `!= 0`. |
| +0x5c       | `action_id`     | int    | State-machine ID — set by spawners, consumed by `FUN_0044376a`'s particle dispatch.  Integrator reads but does not act on it (stored into a local that's only used by chained-spawn parameters). |
| +0x910      | `state_counter` | int    | Small-int state (0 / 1 / 2) set by FUN_00430c6d based on distance thresholds (L114-124).  Integrator's anchor gate: `state_counter < 1` ⇒ "free to anchor". |
| +0x934      | `cooldown`      | int    | Per-frame decrement (FUN_00430c6d L105-106).  Integrator's anchor fallback: `cooldown > 0` ⇒ "stay anchored even past state_counter". |

Field nicknames are author-assigned for the port; the engine has no
symbol names.  The remaining ~2900 B (offsets +0x68..+0xba8, with
the two integrator-only ints at +0x910 and +0x934) are sprite /
animation / AI / dialog state — out of scope for C8h.4.

## Why the integrator's kill gate is two-part

Engine line 1228 (the 0x1a handler):

```c
if (((&DAT_0076bd98)[i * 0x2e9] != 0)                   // alive
    && ((int)(&DAT_0076c464)[i * 0x2e9] < 1)) {        // state_counter < 1
    ...                                                  // snap particle pos to NPC pos
    if ((0 < action_id_via_DAT_0076bdb0)
        || (0 < (int)(&DAT_0076c488)[i * 0x2e9])) {     // cooldown > 0
        anchor_alive = true;
    }
}
```

Reading: "if the NPC exists AND we've already finished interacting
with them, snap the particle to them.  Then, if EITHER the NPC's
current action is non-trivial OR there's queued cooldown work, keep
the particle anchored."  Anything else kills the particle and
chain-spawns type 1 at the last snap position.

## Allocator / spawner

Multiple call sites scan the table for `alive == 0` and fill the
slot in-place.  No symmetric "destroy" — entries are reset to
`alive = 0` from various event paths.  The hottest spawner is
`FUN_0042b6b7` (large scan + init).

`FUN_00430c6d` is the **per-frame people-table tick** (called from
the main game loop's sim branch — its own caller chain hasn't been
fully mapped yet).  It runs the for-loop `0..0x80`, decays
`cooldown` (+0x934), updates `state_counter` (+0x910) from a
distance metric, and integrates `target += vel` (+0xc..+0x14).

`FUN_0042ea35` is a second per-frame walker that touches the table
header and gates additional behavior on `alive != 0 && something < 1`
similar to the integrator's kill gate.

## What ENTRY CAP = 128 implies for the port

The 0x80 cap is confirmed in two places:
- `FUN_0042ea35` line 376: `if (local_c == 0x80) return;`
- `FUN_00430c6d` line 90+: explicit `0..0x80` iteration

So the port can statically allocate a 128 × 2980 B BSS array.  At
~93 KB it's not a heap concern.  For C8h.4 we only need the header
(+0x00..+0x68 plus the two integer fields at +0x910 / +0x934) —
nothing forces us to allocate the full 2980 B/entry until the people
tick / render port lands.

Two options:

(a) **Allocate the full 128 × 2980 B BSS now.**  Matches engine
    layout exactly.  Lets future ports drop in without re-allocating.
    93 KB BSS cost.

(b) **Allocate only the integrator-touched header fields** (a small
    struct: pos1[3], pos2[3], alive, action_id, state_counter,
    cooldown).  ~52 B/entry × 128 = ~6.6 KB.  Has to be re-layouted
    when later ports need more fields.

**Recommendation: (a).**  Layout-equivalent storage prevents subtle
divergence later, and the 93 KB cost is negligible.  Wire as
`uint8_t g_people[128 * 2980]` with named accessor helpers for the
header fields the integrator reads.

## Other consumers (out of scope for C8h.4 but worth listing)

| Function     | Role                                                          |
|--------------|---------------------------------------------------------------|
| FUN_00430c6d | Per-frame people tick (state_counter update, target integrate, cooldown decrement) |
| FUN_0042ea35 | Second per-frame walker (alive-gated dispatch)                |
| FUN_0042b6b7 | Spawner (slot scan + init)                                    |
| FUN_0044376a | Action-driven particle dispatch (consumes action_id at +0x5c) |
| FUN_004176ff | chr walker (renders the table — out of scope today, vertex shaders) |
| FUN_00456f56 | One of the two unported mesh walkers (Iw on the ladder)       |
| FUN_0042439e, 0x424b42, 0x42aef7, 0x42f7ad, 0x42f746, 0x44d47d, 0x44f13d, etc. | event/state/dialog updates touching the alive flag or counters |

27 functions total touch the table base; the port doesn't need any
of them — only the BSS storage so the integrator's reads have
sensible (zeroed) defaults.

## Implications for C8h.4 port

The integrator handlers we'll port in C8h.4c (`0x1a`, `0x78`,
`0x75`/`0x93`) all read this table.  Their behavior on an empty
table (all-zero BSS):

| Type     | Behavior with empty people table                              |
|----------|---------------------------------------------------------------|
| 0x1a     | `alive == 0` → kill gate falls through → particle dies after 1 tick, chain-spawns type 1 at (0,0,0).  Harmless. |
| 0x78     | Reads `(&DAT_0076bd60)[index * 0x2e9]` (= target.x at +0xc).  Index comes from per-record scratch (slot +15).  Empty table ⇒ reads zeros ⇒ particle snaps to (0,0,0) + own baseline offset.  Visible but stationary. |
| 0x75/0x93 | Same as 0x78 — reads target.x at +0xc.  Empty table ⇒ snaps toward (0,0,0). |
| 0x12-0x14 | Reads from people table too (line 786+) — checks DAT_0695f1e0 activation gate first.  Likely no-op when both tables empty. |

So **C8h.4c can land safely against an empty people table** — the
handlers won't crash; they'll just produce no-op particle motion
when the populator hasn't run.  When the people table eventually
gets populated (by the FUN_0042b6b7 spawner chain — far future
work), the same integrator code will produce visible behavior.

## Stride math (for grep / lookup)

| Symbol       | Byte offset from base | Note                          |
|--------------|-----------------------|-------------------------------|
| DAT_0076bd54 | +0x00 (base)          | pos.x of entry 0              |
| DAT_0076bd58 | +0x04                 | pos.y                         |
| DAT_0076bd5c | +0x08                 | pos.z                         |
| DAT_0076bd60 | +0x0c                 | target.x                      |
| DAT_0076bd64 | +0x10                 | target.y                      |
| DAT_0076bd68 | +0x14                 | target.z                      |
| DAT_0076bd98 | +0x44                 | alive                         |
| DAT_0076bdb0 | +0x5c                 | action_id                     |
| DAT_0076c374 | +0x820                | (referenced by 430c6d L269 — entity ref?) |
| DAT_0076c464 | +0x910                | state_counter                 |
| DAT_0076c488 | +0x934                | cooldown                      |

To translate a `&DAT_XXXXXXXX[i * 0x2e9]` access: byte offset =
0xXXXXXXXX - 0x0076bd54, then byte address = base + (i * 2980) +
that offset.  Dword index = byte offset / 4.

## Related files

- `docs/decompiled/by-address/430c6d.c` — per-frame people tick (the
  authoritative writer of pos/target/state_counter/cooldown).
- `docs/decompiled/by-address/42ea35.c` — second per-frame walker;
  confirms 0x80 cap.
- `docs/decompiled/by-address/42b6b7.c` — spawner (slot scan).
- `docs/decompiled/by-address/40fb3a.c` — the integrator (this
  table's consumer in C8h.4c).
- `docs/findings/scene1-particles-tick.md` — chip ladder; this
  survey unblocks the C8h.4c sub-chip's anchor-snap types.
