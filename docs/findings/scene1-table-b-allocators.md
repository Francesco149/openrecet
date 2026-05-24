# Scene-1 table B allocators (FUN_0044376a + FUN_00445a8c) — survey + C8j ladder

**Status (2026-05-24):** survey only.  Documents the two engine
functions that write per-record initial state into the `512 × 0x49 dw`
table at `DAT_069324b0` (the non-particle render-record table
consumed by Pass C of the shop walker + Pass A/B/C/D/E of the wide
followup).

Companion to the C8j.0 survey (`scene1-record-populators.md`) which
located these functions and to the C8i.0 survey
(`scene1-spawn.md`) which is the closest structural analog — table A's
spawn API.  This doc breaks down both allocators body-by-body so a
ladder of C8i-style sub-chips can be planned.

## TL;DR

| Property                | Value                                            |
|-------------------------|--------------------------------------------------|
| Table base              | `DAT_069324b0`                                   |
| Slot stride             | `0x49 dw = 0x124 B = 292 B`                      |
| Slot count              | `0x200 = 512`                                    |
| End sentinel address    | `DAT_069324b0 + 0x200*0x124` (loop ends at `local_1c == 0x200`) |
| Free-slot test          | `slot[0] (type) == 0` — **type IS the sentinel** |
| Live-record count       | `DAT_0076b964` (per C8j.0; not touched by the allocators themselves) |
| Sequence counter global | `DAT_06a46fb8` — monotonically incremented, written into slot+0xCC of every claim |

Both allocators share an identical 36-line **common preamble** that
claims the slot, zeroes a bank of physics/rotation fields, copies a
16-dword owner-matrix into slot+0xC8, sets the scale + life defaults,
and writes the type field LAST so concurrent scanners never see a
half-initialized slot.  The only meaningful preamble diffs are
(a) where the `owner` and `flag` parameters land, (b) which dword
offset on the owner the position triplet is read from, and (c) which
dword offset the owner-matrix copy starts from.  Everything else in
the preamble is byte-for-byte identical.

Per-type init switch differs entirely between the two: each handles a
disjoint type set with only a small overlap (`0x96` confirmed; the
C8j.0 claim of `0x49` overlap is **not** confirmed — see §"Type
overlap analysis").  Bodies that look superficially similar
(e.g. 0x4d/0x4e/0x4f/0x50/0xa5/0xa6 cluster) appear in BOTH allocators
under different owner-pos offsets and slightly different parameters,
so handlers cannot generally be shared across the two.

**Per-frame caller chain (re-iterated from C8j.0):** neither allocator
is called from the wired sim path today.  Their parent fn
`FUN_0043ae20` is the 25.7 KB unported game-logic monster, plus a
handful of call sites in adjacent player+npc fns and a few unported
allocators of their own.

## Common preamble — writer-view column map

Engine: FUN_0044376a L40928-40978 and FUN_00445a8c L42011-42048 (same
shape, field shuffle differences flagged).  Slot base
`piVar = &DAT_069324b0 + i*0x49`; byte-offset base `iVar7 = i*0x124`.

| Byte off | dw | Engine symbol | FUN_0044376a writes | FUN_00445a8c writes | Notes |
|---------:|---:|---|---|---|---|
| 0x00 | 0  | `DAT_069324b0` | `*piVar = param_2 (type)` | `*piVar = param_2 (type)` | type — **also the free-slot sentinel**.  Written near end of preamble in both; only after every other init field. |
| 0x04 | 1  | `DAT_069324b4` | `= 0` | `= param_3 (flag)` | **Diff #1**: 0044376a zeroes; 00445a8c stores the `flag` parameter here. |
| 0x0c | 3  | `DAT_069324bc` | `= param_3 (flag)` | `= 0xffffffff` | **Diff #2**: 0044376a stores `flag` (often `0xffffffff`); 00445a8c hard-codes `0xffffffff`. |
| 0x10 | 4  | `DAT_069324c0` | `= param_1 (owner)` | `= 0` | **Diff #3**: 0044376a's owner ref; 00445a8c zeroes here. |
| 0x14 | 5  | `DAT_069324c4` | `= 0` | `= param_1 (owner)` | **Diff #4**: 00445a8c's owner ref. |
| 0x5c | 23 | `DAT_0693250c` | pos.x source: `param_1+0x20` (or NPC table if `param_3 != -1`) | pos.x source: `param_1+0x3f0` | **Diff #5**: owner-pos offset shifts (entity-shape A vs NPC-shape B). |
| 0x60 | 24 | `DAT_06932510` | pos.y source: `param_1+0x24 - 0.5` | pos.y source: `param_1+0x3f4` | Diff #5 cont. |
| 0x64 | 25 | `DAT_06932514` | pos.z source: `param_1+0x28` | pos.z source: `param_1+0x3f8` | Diff #5 cont. |
| 0x68 | 26 | `DAT_06932518` | `= 0` (vel.x) | `= 0` | |
| 0x6c | 27 | `DAT_0693251c` | `= 0` (vel.y) | `= 0` | |
| 0x70 | 28 | `DAT_06932520` | `= 0` (vel.z) | `= 0` | |
| 0xc4 | 49 | `DAT_069325c4` | `= 0xffffffff` (only 0044376a writes!) | (not written in preamble) | **Diff #6**: 0044376a sets aux sentinel; 00445a8c omits.  Likely benign — both bodies later overwrite if needed. |
| 0xc8 | 50 | `DAT_06932578..b8` | 16-dw copy from `param_1+0xde8` | 16-dw copy from `param_1+0x39c` | **Diff #7**: owner-matrix source offset shifts (still 4×4 matrix copy, but anchored at different field on the two owner shapes). |
| 0xa0 | 40 | `DAT_06932550` | `= 0xffffffff` | `= 0xffffffff` | Aux sentinel. |
| 0xa8 | 42 | `DAT_06932558` | `= 0` (drag/life) | `= 0` | Many type bodies overwrite. |
| 0xb0 | 44 | `DAT_06932560` | (not written) | (not written) | |
| 0xb4 | 45 | `DAT_06932564` | `= 0x3f800000 (1.0f)` | `= 0x3f800000 (1.0f)` | scale.x default. |
| 0xb8 | 46 | `DAT_06932568` | `= *(param_1 + 0xeac)` | `= 0` | **Diff #8**: 0044376a inherits a flag from the owner; 00445a8c zeroes. |
| 0xbc | 47 | `DAT_0693256c` | `= 0x3f800000 (1.0f)` | (not written in preamble) | scale.y default — only 0044376a. |
| 0xc0 | 48 | `DAT_06932570/71` | `= 0/0` (byte writes) | (not written in preamble) | bytewise zero pair — only 0044376a. |
| 0xc0 | 48 | `DAT_06932574` | `= 0` | `= 0` | |
| 0x4c | 19 | `DAT_069325b8` | `= 0x3f800000 (1.0f)` | `= 0x3f800000 (1.0f)` | life multiplier. |
| 0xc0 | 48 | `DAT_069325c0` | `= 0` | `= 0` | |
| 0x9c | 39 | `DAT_0693254c` | `= 0` | `= 0` | per-particle slot index. |
| 0xcc | 51 | `DAT_069325cc` | `= DAT_06a46fb8++` (sequence counter) | `= DAT_06a46fb8++` | Per-slot ID; same global, post-increment. |
| 0x98 | 38 | `DAT_06932548` | `= 0 (age)` | `= 0 (age)` | |
| 0x8c-0x94 | 35-37 | `DAT_0693253c/40/44` | `= 0/0/0` | `= 0/0/0` | Rotation/scratch triple. |

**Per-bodies field-shuffle summary (verified vs C8j.0's claim):**

C8j.0 stated:
- 0044376a: owner at +0x10, flag at +0x14.
- 00445a8c: flag at +0x04, owner at +0x14.

**Refinement:** the actual diff is *more* than a 2-field shuffle.  In
0044376a, `flag` lands at **+0x0c** (not +0x14 as C8j.0 claimed), and
the slot's +0x14 dword is **explicitly zeroed**.  In 00445a8c, `flag`
lands at **+0x04**, and the slot's +0x0c dword is hard-coded to
`0xffffffff`.  Net: BOTH allocators eventually have `owner` somewhere
and `flag` somewhere, but the consumer-side render+integrator must
read from *both* +0x10 and +0x14 (owner candidates) and BOTH +0x04
and +0x0c (flag/state candidates) to know which allocator produced
the record.  Worth checking what FUN_0043ae20 actually reads.

Other "subtle" diffs found by direct comparison:

| Diff | 0044376a behavior | 00445a8c behavior |
|------|------------------|-------------------|
| pos.y bias | `pos.y = owner.y - 0.5` (built-in 0.5 floor offset) | `pos.y = owner.y` (no bias) |
| owner-matrix src | `owner + 0xde8` | `owner + 0x39c` |
| aux at +0xc4 | sets `0xffffffff` | omitted |
| aux at +0xb8 | inherits `*(owner + 0xeac)` | hard-zeroed |
| aux at +0xbc, +0xc0 (byte-pair) | sets defaults | omitted |
| pos-fallback branch | `param_3 != -1` selects an alt "people table" pos at `param_1 + param_3*0x44 + 0x9e0` | no fallback path |

The pos-fallback in 0044376a is a 2-way switch: when the **caller**
already has a people-table entry pinned (passes `param_3 = NPC index`
instead of `-1`), the allocator pulls position from
`owner + 0x44*npc_idx + 0x9e0`.  Stride 0x44 ≠ NPC stride 0x2e9 from
the table at `DAT_0076bd54`, so this isn't the same people table —
likely a per-owner inline NPC slot array on the parent entity.  Of the
~30 callable sites, *all* observed call sites pass `0xffffffff`
explicitly for `param_3`, so the alt-path is dead in shipping config.

## Per-type switch catalog — FUN_0044376a

44 distinct type IDs branch in the switch.  Loop counts in the
"Particles" column: `1` = single-spawn, `N` = `local_8 == N-1`
comparison drives termination, `param_7` = caller-supplied count via
param chain.  (FUN_0044376a's caller can't supply count — its 3 params
are owner/type/flag.  So "param_7" here means a hard-coded comparison
not yet matched to a constant — flagged as TBD.)

| Type | Particles | Anchor (decomp line) | One-line description |
|-----:|----------:|----------------------|----------------------|
| 0x23 | 3 | L40979 | pos += (sin(-yaw)*15, +30, cos(-yaw)*15) OR people-table-deref; per-particle pos offset; +0xa8 = 0; calls `thunk_FUN_004a35d3` (matrix init) with random rot.z |
| 0x9b | 1 | L41029 | NPC-table-deref bend angle `*(owner+0x948) * 2π/8`; life mult = 1.3 |
| 0x9d | 1 (early return) | L41034 | Same bend angle as 0x9b; sets pos triplet + alt-pos triplet + radial vel via sin/cos; sets +0xb8 = 10.0f; **returns immediately** with explicit `*piVar = 0x9d` write |
| 0x24 | 1 | L41052 | Pure preamble (jumps to LAB_004457e7) |
| 0x3e | 1 | L41053-66 | Sets rot=`*(owner+0xea4)` only; group with 0x5f |
| 0x5f | 1 | L41053-66 | Same body as 0x3e |
| 0x60 | 1 | L41066 | Same body as 0x3e/0x5f (anchor reservation, jump-share) |
| 0x82 | 1 | L41054-65 | Same as 3e/5f/60 but also sets scale.x = 2.0f |
| 0x29 | 1, 2, or 3 | L41067 | pos += sin/cos(-yaw)*15 OR people-table-deref + ground-clamp; per-particle pos jitter at local_8∈{1,2}; +0xa8 = 0 |
| 0x66 | 1 | L41107 (default tail) | NOT a no-op — falls through to default branch that does nothing special.  Anchor type for sentinel claim only. |
| 0x58 | 1 | L41108 | vel = sin/cos(rot_param)*3, pos -= sin/cos(rot_param)*0.5 + +1y, NPC-bend, +0xa8 = 20.0f |
| 100 (0x64) | 5 | L41126 | local_c = rot_param shifted by ±π/10 or ±π/5 per local_8; pos += sin/cos*0.5; vel = sin/cos*0.4; sets type=100 again, +0xc2 = 2, life mult = 0.5, +0xc0 = 1, +0x9c = local_8 |
| 0x74 | 1 | L41158 | NPC-bend angle, pos via sin/cos*1.2 + +1.3y, then 3-way switch on `*(owner+0x948)`: 0 → pos.x -=0.41, 4 → pos.x +=0.41, else pos.z -=0.1; shared with 0x79 below |
| 0x79 | 1 | L41158 | Same body as 0x74 — LAB-share |
| 0x68 | 1 | L41187 | RNG-driven amp + angle; pos = sin/cos*amp + +20y; iterates people-table (`DAT_0076c478` array, stride 0x2e9) looking for a matched slot via FUN_005031e4 distance check; vel = (alt_pos - pos)/10; life mult = 0.6 |
| 0x65 | 8 | L41241 | NPC-bend + RNG offset → per-particle angle; vel = sin/cos*(u*0.08+0.04); vel.y = 0.7; +0xa8 = 0.5f, +0xc0 = 1, +0x9c = local_8 |
| 0x69 | 8 | L41241 | Same body as 0x65 — LAB-share |
| 0x83 | (TBD) | L41264 (negative-branch) | Distinct body not yet read; falls into the big 4d..a6 cluster only via NOT-equal predicate.  Needs deeper re-read. |
| 0x4d | 1 | L41265 | "Big cluster A": NPC-bend with per-particle angle shifts (5 shift constants); pos via sin/cos*0.3; +0xb4 scale per-type; life mult per-type; vel via sin/cos*local_10; iVar10 multi-shape final loop |
| 0x4e | 1 | L41265 | Cluster A — same body, scale/life variant |
| 0x4f | 3 | L41265 | Cluster A — iVar10 = 3 |
| 0x50 | 5 | L41265 | Cluster A — iVar10 = 5 |
| 0xa5 | 6 | L41265 | Cluster A — iVar10 = 6 |
| 0xa6 | 8 | L41265 | Cluster A — iVar10 = 8 |
| 0x52 | 1 | L41265 | Cluster A variant — local_10 = 0.3 |
| 0x53 | 1 | L41265 | Cluster A variant — pos.y from owner+0x24 (no +0.7 lift); +0xc0 = 3 (different aux byte) |
| 99 (0x63) | 1 | L41265 | Cluster A variant — local_10 = 0.3, life mult = 0.75 |
| 0x51 | 1 | L41265 | Cluster A variant — +0xb4 scale = 1.5 |
| 0x6a | 8 | L41399 | NPC-bend; vel via sin/cos*0.1; +0x8c, +0x94 = random angles; +0x8c = local_8*2π/10; life mult = 0.3 |
| 0x61 | 1 | L41422 | NPC-bend (same angle math); vel via sin/cos*0.1; +0xb4 = 0.5, life mult = 0.3, +0xc0 = 0 — looks like static-pose variant of 0x6a |
| 0x8a | 1 | L41442 (negative) | Tail body shared with 0x8b — sets `+0xb4 = 0x3e4ccccd` |
| 0x8b | 1 | L41443 | Sets `+0xb4 = 0x3dcccccd` then jumps to shared 0x8a body |
| 0x62 | 1 | L41447 | NPC-bend + RNG angle shift; pos via sin/cos*1.3 + +1.2y; vel via sin/cos*0.5; +0xa8 = 0.5, life mult ≈ 0.45 |
| 0x73 | 4 | L41469-86 (mega-cluster) | xz wobble via large angle table + LAB-loop; covered with 0x76/0x77/0x7a/0x7b/0x7c/0x78/0x7e |
| 0x7e | 8 | L41469-86 | Mega-cluster — wider amp, different life mult |
| 0x76 | 8 | L41469-86 | Mega-cluster — same angle table, sets +0x9c = 1 if local_8>0 |
| 0x77 | 8 | L41469-86 | Mega-cluster — local_10 = 0.3, life mult = 0.8, +0x8c = local_c |
| 0x7a | 8 | L41469-86 | Mega-cluster — wider amp, life mult = 0.125 |
| 0x7b | 8 | L41469-86 | Mega-cluster — local_10 = 0.24, +0x6c = 0.1 |
| 0x7c | 5 | L41469-86 | Mega-cluster — per-particle bidirectional fan, pos -= 2*vel |
| 0x78 | 8 | L41469-86 | Mega-cluster — narrowest amp |
| 0x30 | 1 | L41472 | Reverse-yaw cone: pos via sin/cos(0.314 - yaw)*1.5; if NPC index != -1 pull toward people-table[idx] via FUN_005031e4 distance norm + FUN_00503dd0; else vel via sin/cos*0.7 |
| 8 | 1 | L41509 | Manual zero vel triple; pos += vel*10; +0xa8 = 20.0f; +0xc0 = 1; +0x94 = (local_8+1)*2π |
| 0x5b | 1 | L41526 | Sets `+0xb4 = 0x3f333333 (0.7)` then jumps to shared LAB_00444be6 |
| 0x5c | 1 | L41529 | LAB_00444beb — sets `+0xb4 = 1.0` then shared body |
| 0x5e | 1 | L41533 | LAB_00444beb — same as 0x5c |
| 0x85 | 1 | L41536 | LAB_00444be6 — `+0xb4 = 0` (special), then shared body |
| 0x86 | 1 | L41539 | LAB_00444be6 — `+0xb4 = 0.4`, then shared body |
| 0x87 | 1 | L41542 | LAB_00444be6 — `+0xb4 = 1.0`, then shared body |
| 0x71 | (TBD) | L41564 | Empty branch (if-test only) — falls through to 0x72/0x75 sibling code unless 0x7d redirect |
| 0x72 | 1 | L41626 | Sets `+0xb4 = 0.3`, then default tail body (sin/cos vel + pos += sin/cos*0.5 — different formulation) |
| 0x75 | 1 | L41629 | Suppresses default vel write, otherwise default tail |
| 0x7d | 1 | L41565,68 | LAB_00444adc — sets `+0xb4 = 1.5`, then tail |
| 0x6d | 1 | L41569 | NPC-bend; vel via sin/cos*3; pos -= sin/cos*0.5 + 1y; +0x94 = random*2π; +0xa8 = 20.0f, +0xc0 = 1; +0x9c = local_8-1; group with 0x6e/0x6f/0x70 |
| 0x6e | 1 | L41569 | Same body as 0x6d |
| 0x6f | 1 | L41569 | Same body as 0x6d |
| 0x70 | 1 | L41569 | Same body as 0x6d |
| 2 | 1 | L41594 | "Sin/cos drift cluster" (shared with 0x54, 3, 4, 0x22, 0x67): NPC-bend; vel via sin/cos*3; pos -= sin/cos*0.5 + +1y; +0xb4 set per-type variant; iVar10 = 1; goto LAB_004449b0 (random rot.z tail) |
| 0x54 | 1 | L41594 | Sin/cos drift cluster |
| 3 | 1 | L41594 | Sin/cos drift cluster (special: if `param_3 != -1` sets `+0xb4 = 0.5`) |
| 4 | 1 | L41594 | Sin/cos drift cluster |
| 0x22 | 1 | L41594 | Sin/cos drift cluster — `+0xb4 = 2.0` |
| 0x67 | 1 | L41594 | Sin/cos drift cluster — `+0xb4 = 1.2` |

**Note on type sets:** the C8j.0 survey listed types
`0x22, 0x3e, 0x49, 0x51, 0x52, 0x53, 0x58, 0x5b, 0x5f, 0x60-0x62,
0x66, 0x67, 0x69, 0x6a, 0x71, 0x83, 0x85-0x87, 0x9b` from call-site
greps.  The function body actually handles a much wider set — many
types (like 0x73/0x7c/0x6d) are reachable only through the engine's
NPC update fns and don't appear directly in the grep.  Real set is
~60 types.

## Per-type switch catalog — FUN_00445a8c

Similar breadth.  Sample of types in switch order (line numbers from
the decomp).

| Type | Particles | Anchor (decomp line) | One-line description |
|-----:|----------:|----------------------|----------------------|
| 0x56 | 1 | L42049 | NPC-bend; pos via sin/cos*1.5 + +1.8y; vel via sin/cos*0.3; +0x8c, +0x94 = random*2π; calls `thunk_FUN_004a35d3` (mat rot.y) + `thunk_FUN_004a3537` (mat rot.x) + `thunk_FUN_004a2a03` (mat compose); +0xa8 = 0.5, +0xc0 = 1 |
| 0x53 | 1 | L42088 | NPC-bend; pos via sin/cos*0.3 + +0.08y (LOW lift); vel via sin/cos*0.5; +0xa8 = 0.5 |
| 0x4d | 1 | L42112 | "Big cluster B": NPC-bend with per-particle angle shifts (4 shifts); pos via sin/cos*0.8 + +1.4y; vel via sin/cos*0.5; +0xb4 per-type; group with 0x4e/0x4f/0x50/0xa5/0xa6 |
| 0x4e | 1 | L42112 | Cluster B variant |
| 0x4f | 3 | L42112 | Cluster B — uVar5 = 3 |
| 0x50 | 5 | L42112 | Cluster B — uVar5 = 5 |
| 0xa5 | 6 | L42112 | Cluster B — uVar5 = 6 |
| 0xa6 | 8 | L42112 | Cluster B — uVar5 = 8 |
| 0xa0 | 8 | L42162-end | "Mega cluster B" (shared body L42935+): NPC-bend; pos via sin/cos*1.2 + +1.3y; alt-pos via sin/cos*0.8 + +1.3y; 3-way NPC-state switch; per-particle angle wobble; +0x9c, life mult per-type |
| 0xa1 | 1 | L42162-end | Mega cluster B — `+0xb4 = 1.0`, +0x8c = local_1c (no wobble) |
| 0xa2 | 1 | L42162-end | Mega cluster B — `+0xb4 = 0.5`, +0xa8 = 0.4 wide-arc |
| 0xa3 | 8 | L42162-end | Mega cluster B — `+0xb4 = 0.5`, wider angle increment |
| 0xa4 | 8 | L42162-end | Mega cluster B — `+0xb4 = 1.0`, target-aim toward player (uses DAT_056da1d8/e0 to compute aim angle via FUN_00503dd0); cone-skip filter (rejects ±144° forward arcs) |
| 0x73 | 4 | L42162-end | Mega cluster B — `+0xb4 = 0.25`, life mult var |
| 0x7a | 8 | L42162-end | Mega cluster B — `+0xb4 = 0.25`, narrowest amp |
| 0x7c | 5 | L42162-end | Mega cluster B — per-particle bidirectional fan, pos -= 2*vel (same shape as 0044376a's 0x7c — see overlap §) |
| 0x7e | 1 | L42162-end | Mega cluster B — `+0xb4 = 0.4`, wider amp |
| 0x68 | 1 | L42164 | Two-stage RNG amp + random ANGLE (no NPC-bend!); pos via sin/cos*amp + +20y; alt-pos via sin/cos*amp + player_y; **uses DAT_056da1d8/dc/e0 (player pos)** as alt target; vel = (alt - pos)/10; life mult = 0.6 |
| 0x51 | 1 | L42199 | NPC-bend with cluster B angle shifts (4 shifts); pos via sin/cos*0.3 + +0.7y; vel via sin/cos*0.5; jumps to LAB_00445c9a (sets +0x8c = local_1c, +0xa8 = 0.5) |
| 0x84 | 1 | L42232 | Player-aim variant: pos = owner.pos + +3y (no jitter); aim toward player via `DAT_056da1d8/e0` with FUN_005031e4 distance norm; vel via FUN_00503dd0 angle + random; +0xa8 = 0.5 |
| 0x96 | 1 | L42232 | Same body as 0x84 with extra: pos += sin/cos*1; life mult = 0.2; angle has wider RNG bend, vel amp uses different RNG seed |
| 0x33 | 1 | L42297 | Pulls pos from owner+0x6fc..0x704 (DIFFERENT field — not 0x3f0); vel via sin/cos(owner+0x420)*0.8; +0x6c = pos.y * -0.01 (negative-Y drag); +0xa8 = 0.7; +0x94 = random*2π |
| 0x2f | 6 | L42316 | Per-particle angle = local_8*2π/3 + π/2; +0x94 = that angle; +0x8c = `owner+0x420` (alt rot); +0xc4 = 0.5 (RNG cone offset); pos via sin/cos(0x420)*0.5 + +1.5y; vel via sin/cos*2; +0x9c = local_8%3; +0xa4 = (local_8<3) ? 0 : 1 |
| 0x2e | 1 | L42356 | RNG-randomized angle pair; pos via sin/cos*0.5 + +4y; vel via sin/cos*0.4; +0x6c = 0.14f, +0x6c = -(prev) (sign flip!); +0x60 -= 4.0 (subtractive lift!) |
| 0x36 | 8 | L42356 | LAB-share with 0x2e: pulls pos from `owner + 0x1c2*4 + local_8*0xc`, `owner+0x70c+0xc*local_8`, `owner+0x710+0xc*local_8` (per-particle indexed offset!); vel = (pos - owner.pos)*0.02 (10× damped); life-cap = 8 |
| 0x27 | 1 | L42422 | pos.y += 2.0 (early lift); pos via sin/cos(owner+0x420)*2.5 + +8y; vel via sin/cos*0.5; +0x6c = -0.05; goto LAB_004462ed |
| 0x2b | 1 | L42448 | RNG amp (0.1..0.225); if owner+0x424 == 0x45 amp *= 1.5; RNG angle; vel via sin/cos*amp; +0x6c = (u*0.8+0.2) positive lift; +0x94 = random*2π; +0xb4 = 0.2 |
| 0x26 | 1 | L42473 | pos.y += 2.0; RNG amp; pos via sin/cos(owner+0x420)*2.5 + +4.8y; vel via sin/cos*amp; +0x6c = -u*amp (downward drift, then) |
| 0x2a | 1 | L42473 | Same body as 0x26 but pos.y += 3.5 instead of 4.8 and vel.y = (u*0.8-0.4)*u*0.5 (different) |
| 0x31 | 1 | L42516 | RNG amp; vel via sin/cos(owner+0x420)*amp; +0x6c = (u-0.5)*0.1 (centered drift); +0xb4 = 2.0, +0xa8 = 0 |
| 0x32 | 1 | L42516 | Same body as 0x31 but amp *= 0.5; +0x6c uses different RNG formula |
| 0x25 | 1 | L42547 | pos.y += 2.0; RNG amp (0.1..0.17); pos via sin/cos(owner+0x420)*2.5 + +8.8y; vel via sin/cos*amp; +0x6c = amp - u*0.3 (positive lift); goto LAB_0044701d (life-rot.z tail) |
| 0x3b | 1 | L42571 | vel via sin/cos(owner+0x420)*0.6; goto LAB_004462ed |
| 0x3c | 1 | L42582 | Pure preamble + goto LAB_004462f0 (sets +0xa8 = 0).  Minimal-body anchor type. |
| 0x98 | 5 | L42583 | NPC-bend with 4 angle shifts (±0.38, ±0.56 — wider than 0x4d cluster); vel via sin/cos*0.3; pos at owner+0x3f0..0x3f8 + +0.25y; +0x8c = arctan-derived angle; +0xa8 = 20.0f; +0x9c = local_8 |
| 0x5a | 1 | L42620 | Zero vel; pos at owner.pos + +0.25y; +0x8c = NPC-bend; +0xa8 = 20.0f |
| 0x6b | 1 | L42637 | NPC-bend; RNG amp (u+1)*4; pos via sin/cos*amp + +0.2y; +0x8c = atan2-based, +0xa8 = 0 |
| 0x6c | 1 | L42658 | NPC-bend; vel via sin/cos*0.2; pos = vel*3 + owner.pos + +1.5y; +0xa8 = 1.0 |
| 0x1f | 1 | L42682 | NPC-bend; 5-way switch on owner+0x424 → amp ∈ {0.1, 0.12, 0.14, 0.15, 0.16, 0.2}; vel via sin/cos*amp; 2-way switch on owner+0x424 (=0x24 or =0x23) → pos uses sin/cos*1.5; else pos = vel*3; +0xa8 = 20.0f |
| 0x3a | 1 | L42735 | Centered random pos ±5 around player pos (DAT_056da1d8/dc/e0); +0x8c = π/2 (constant); +0x94 = random*2π; calls `thunk_FUN_004a35d3` (mat rot); goto LAB_004462f0 |
| 0x28 | 1 | L42754 | vel via sin/cos(owner+0x420)*0.3; +0x6c = 0.13; pos = vel*2 + owner.pos + +0.8y; +0xa8 = 20.0f; +0xb4 = 0.5 |
| 0x38 | 1 | L42784 | vel via sin/cos(owner+0x420)*0.5; +0xb4 = 3.8 (high scale); pos from owner+0x6fc..0x704 (alt field, same as 0x33); +0xa8 = 3.0 |
| 0x21 | 1 | L42800 | Random-bias angle from owner+0x420; RNG amp; vel via sin/cos*amp; +0x6c = 0.02; pos at owner.pos + +1.8y; +0x94 = random*2π; goto LAB_004466a8 (+0xa8 = 20) |
| 0x12 | 1 | L42822 | Sets `+0xb4 = 3.0` then goto LAB_00446b7a |
| 0xe | 1 | L42823 | LAB_00447584 — preamble + age-bump only |
| 0x97 | 1 | L42823 | LAB_00447584 — same as 0xe |
| 0x46 | 1 | L42823 | LAB_00447584 — same as 0xe |
| 0xf | 1 | L42824 | Sets `+0xb4 = 0.2` then goto LAB_00446b7a |
| 0x24 | 1 | L42828 | LAB_00447584 |
| 10 (0xa) | 1 | L42828 | LAB_00447584 |
| 0xb | 1 | L42828 | LAB_00447584 |
| 0x14 | 1 | L42828 | LAB_00447584 |
| 0x13 | 1 | L42828 | LAB_00447584 |
| 0x99 | 1 | L42828 | LAB_00447584 |
| 0xd | 1 | L42831 (negative) | Falls through to mega-cluster B body (player-pos jitter) |
| 0x11 | 1 | L42831 | Same as 0xd |
| 0x15 | 1 | L42831 | Same as 0xd |
| 0xc | 1 | L42831 | Same as 0xd |
| 0x10 | 1 | L42831 | Same as 0xd |
| 0x16 | 3 | L42833 (special) | Two-stage: increments local_8 (multi-spawn driver); sets +0x8c = NPC-bend, +0x9c = (local_8-1) per-particle index; goto LAB_00447cbe |
| 0x17 | 3 | L42833 | Same body as 0x16 |
| 0x9c | 1 | L42834 (negative) | NPC-bend; `+0x8c = bend angle`; `+0xb4 = 0.45`, then goto LAB_0044703f (rot.z + life tail) |
| 0x1e | 1 | L42835 | "owner-pos w/ early return" group: pos at owner.pos + +1y; alt-pos at owner.pos + +0.9y; vel via sin/cos(owner+0x420)*2; **returns explicitly** with type-claim; if type==0x9e also sets +0xb4 = 0.6 and +0xa8 = 10.0 |
| 0x88 | 1 | L42835 | Same body as 0x1e |
| 0x89 | 1 | L42835 | Same body as 0x1e |
| 0x9a | 1 | L42835 | Same body as 0x1e |
| 0x9e | 1 | L42835 | Same body as 0x1e + extra scale/life |
| 0x34 | 8 | L42858 | 8-element static index table `local_4c[0..7] = {-3,-1,-4,2,1,3,-2,4}`; picks index via `(DAT_0438b8cc + local_8) % 8`; amp = idx*5 ± 3; uses `-owner+0x420` (negated angle!); writes alt-target slot at +0x84..0x8c (DAT_06932530..38); pos at owner.pos + +11y; vel via sin/cos(owner+0x420)*2; **uses player_pos (DAT_056da1d8/e0)** as alt-target anchor; +0x98 = local_8*-4; +0xa0 = local_8; sets `*piVar = 0x34` explicitly |

## Type overlap analysis

C8j.0 claimed 0x49 and 0x96 overlap.  Body inspection shows:

| Type | In 0044376a? | In 00445a8c? | Body identical? |
|-----:|:------------:|:------------:|:----------------|
| 0x49 | NO (not in switch body) | NO (not in switch body — call-site greps showed neither hits the per-type init; both must default-fall through to preamble-only) | Cannot identify — likely the call-site greps were noisy.  Recheck with `objdump` once a sub-chip ports more bodies. |
| 0x96 | NO | YES (L42232 — player-aim variant) | n/a — only in 00445a8c |
| 0x53 | YES (cluster A, no Y-lift variant) | YES (L42088, distinct shape: low Y-lift 0.08) | DIFFERENT |
| 0x51 | YES (cluster A) | YES (L42199, cluster B body) | DIFFERENT shape |
| 0x4d-0x50, 0xa5-0xa6 | YES (cluster A) | YES (cluster B) | DIFFERENT — both clusters use the same `*0.8 + +1.4y` formula but different RNG seq sites (cluster A in 0044376a is multi-line per-type customization; cluster B in 00445a8c is more compact) |
| 0x73, 0x7a, 0x7c, 0x7e | YES (mega-cluster A in 0044376a, L41469+) | YES (mega-cluster B in 00445a8c, L42162-end) | SIMILAR shape but with allocator-specific tweaks (pos source offset already differs).  Possible candidate for shared helper IF the writer-view offsets resolve. |
| 0x68 | YES (with people-table iteration) | YES (with player-pos target) | DIFFERENT — 0044376a iterates `DAT_0076c478` to find a target; 00445a8c uses player_pos directly.  Cannot share. |

**Net unique types across both:** ~70+ distinct IDs (more than C8j.0's
"~47" estimate, which was call-site-grep-only).  No clean handler
sharing across the two allocators — the pos-source diff plus owner-
shape-derived field offsets (owner+0x420, owner+0x424, owner+0x18,
owner+0x6fc, etc., in 00445a8c vs owner+0x948, owner+0xea0,
owner+0xea4 in 0044376a) means each per-type init body is allocator-
specific even when the visible shape (sin/cos vel + Y-lift) looks
familiar.

**Plan implication:** each allocator gets its own ladder.  Bodies
shouldn't be merged across allocators — but within each allocator,
LAB-shares (next section) can collapse several types per chip.

## Shared-body patterns (LAB-jump shares)

These are jump targets multiple type cases dispatch to.  Mirrors C8i's
`init_type_shared_unit_half(bias)` / `LAB_0044a43d` patterns.

### FUN_0044376a

| LAB | Reached by types | What the body does |
|-----|------------------|--------------------|
| LAB_00443a5d | 0x23 | Sets `+0xa8 = 0` then goto LAB_00444230 |
| LAB_00444230 | 0x23 (via 43a5d) + 0x8a/0x8b tail | Sets `+0xa8 = uVar1` then goto LAB_004457e7 |
| LAB_004457e7 | many tail-share | `bVar14 = local_8 == 0` → either return (1-particle done) or loop iteration |
| LAB_004449b0 | 0x30 (NPC branch) + sin/cos drift cluster (2/0x54/3/4/0x22/0x67) | Random rot.z (+0x94 = u*2π) then goto LAB_004449c1 |
| LAB_004449c1 | LAB_004449b0 + 0x58 | Sets `+0xa8 = 20.0f` then goto LAB_00443dbe |
| LAB_00443dbe | many | Sets `iVar10 = 1`, `+0xc0 = 1`, then goto LAB_004455ed |
| LAB_004455ed | many | `local_8++`, `bVar14 = local_8 == iVar10` (loop-count termination) |
| LAB_004457ee | many | `local_8 = uVar7 + 1` then check `bVar14` |
| LAB_00443a24 | 0x23 sub-particles | Sets `+0x64 = fVar2` (pos.z lift assignment) |
| LAB_00443c85 | 0x29 sub-particles | Same pattern — sub-particle pos.z lift |
| LAB_004449b0 ↔ many | drift cluster | Random rot.z + LAB_004449c1 (huge share) |
| LAB_00444be6 | 0x5b/0x5c/0x5e/0x85/0x86/0x87 | Per-type `+0xb4` scale write + shared sin/cos vel body |
| LAB_00444beb | 0x5c (entry) + types in LAB_00444be6 | `+0xb4 = 1.0` then shared body |
| LAB_004451c8 | 0x76, 0x78, 0x7a (mega-cluster) | `bVar14 = local_8 == 7` (8-spawn termination) |
| LAB_004451f0 | 0x8a (tail) + 0x8b | Standard sin/cos vel + pos -= sin/cos*0.5 + +1y body |
| LAB_00444adc | 0x71 redirect + 0x7d | Sets `+0xb4 = 1.5` then default tail |
| LAB_00444de8 | 0x7x cluster sub-particles | Sets alt-pos +0x84 = fVar2 (sub-position write) |
| LAB_00444f72 | 0x78/0x7a sub-particle | Alt path that skips per-particle vel.y RNG |

**Sin/cos drift cluster (LAB-share via fall-through):** types
`2, 0x54, 3, 4, 0x22, 0x67` share one ~30-line body (L41594-21).  Six
types collapse into one helper.  Probably the densest share in
0044376a.

**Big cluster A (4d..a6 + 99/0x51/0x52/0x53):** 11 types share one
body (L41265-1396) with per-type `+0xb4`/life/local_10 customization.
This is the analog of C8i.4's mega-group (34 types in one body) but
smaller.

**Mega-cluster A (73/76/77/78/7a/7b/7c/7e):** 8 types share one body
(L41650+) with the "angle table" pattern (modulo-32 angle increment).

### FUN_00445a8c

| LAB | Reached by types | What the body does |
|-----|------------------|--------------------|
| LAB_00445c9a | 0x56 (tail) + 0x51 | Sets `+0x8c = fVar1`, then `+0xa8 = 0.5`, then LAB_004469d2 |
| LAB_004469d2 | 0x1f + LAB_00445c9a | Sets `+0xa8`, `+0xc0 = 1` then LAB_00447cb8 |
| LAB_00447cb8 | many | `local_8++`, `bVar11 = local_8 == uVar5` (loop-count termination) |
| LAB_00447cbe | many | Check `bVar11`, either return or loop next slot |
| LAB_00447572 | 0x53, 0x96 (player-aim) | Sets `+0x8c = fVar1` then LAB_0044757e |
| LAB_0044757e | many | Sets `+0xa8 = uVar2` then LAB_00447584 |
| LAB_00447584 | many tail-share | `local_8++` then `bVar11 = local_8 == 1` (single-particle termination) |
| LAB_004462ed | 0x27, 0x3b, 0x38 tail | Sets `+0x70 = fVar1` (vel.z final assign) |
| LAB_004462f0 | many | Sets `uVar2 = 0` then LAB_0044757e |
| LAB_004466a8 | 0x21 + 0x5a tail | Sets `uVar2 = 20.0f` then LAB_0044757e |
| LAB_0044703f | 0x2b + 0x31/0x32 + 0x9c | Sets `+0xb4 = uVar2` (life mult) |
| LAB_0044701d | 0x25 + 0x31/0x32 | Sets `+0x70 = fVar1` then random rot.z + drag setup |
| LAB_00447325 | 0x36 + 0x34 sub-particles | `bVar11 = local_8 == 8` (8-spawn termination) |
| LAB_00447672 | mega-cluster B sub-particles | Sets alt-pos `+0x84 = fVar1` |
| LAB_00447802 | mega-cluster B sub-particles | Alt path that skips vel.y RNG |
| LAB_00447b67 | mega-cluster B (0xa0/0xa3/0x7a) | `bVar11 = local_8 == 8` |
| LAB_00446f4d | 0x28 + 0x38 | Calls FUN_00503dd0 for atan2 + sets `+0x8c` to that, then LAB_00446f7d |
| LAB_00446f7d | 0x28 (tail) + 0x9c | Sets `+0x8c = fVar1` |
| LAB_00446b7a | 0x12, 0xf | Sets `+0xb4 = uVar2` (life mult shortcut) |

**Big cluster B (4d..a6):** 6 types share one body (L42112-61) —
smaller than cluster A's 11.

**Mega-cluster B (0xa0..0xa4, 0x73, 0x7a, 0x7c, 0x7e):** 8 types share
the giant L42162-end body (~975 lines).

**Owner-pos early-return group (0x1e, 0x88, 0x89, 0x9a, 0x9e):**
5 types use one body that **explicitly returns** after type-claim
(unique to 00445a8c — 0044376a doesn't have an explicit-return group).

## Cross-references (engine globals + functions)

### Engine functions called from per-type bodies

| Engine fn | Already in C? | What it does | Sites in 0044376a | Sites in 00445a8c |
|-----------|:-:|---|--:|--:|
| `FUN_00503994` (cos) | yes — `cosf` | math cos | many | many |
| `FUN_00503a44` (sin) | yes — `sinf` | math sin | many | many |
| `FUN_00471089` (rng_unit) | yes — `scene1_rng_unit` | RNG [0,1) | many | many |
| `FUN_005031e4` (sqrt) | yes — `sqrtf` via `scene1_sqrt` | sqrt | 2 (0x68, 0x30) | 2 (0x84/0x96 family) |
| `FUN_00503dd0` (atan2) | needs port helper (used in C8i.3a for 0x73/0x77) | atan2 from xy | 1 (0x30) | 4 (0x84/0x96, 0xa4, 0x28, 0x38) |
| `FUN_00432e50` (ground clamp) | partial stand-in present | ground-y raycast | 1 (0x29 NPC branch) | 0 |
| `thunk_FUN_004a35d3` | not yet ported | mat rot.y compose | 1 (0x23) | 2 (0x56, 0x3a) |
| `thunk_FUN_004a3537` | not yet ported | mat rot.x compose | 0 | 1 (0x56) |
| `thunk_FUN_004a2a03` | partial (used in 0044376a only) | mat 4×4 multiply | 0 | 1 (0x56) |
| `thunk_FUN_005041f6` | not yet ported | RNG integer | 0 | 1 (0x2e/0x36 sign coin) |

### Engine global reads (stand-ins needed)

| Engine global | Stand-in name (proposed) | Used in 0044376a | Used in 00445a8c |
|---------------|-------------------------|:-:|:-:|
| `_DAT_073de39c` | `g_scene1_camera_yaw` (already exists) | yes (0x23, 0x29, 0x30 reverse-yaw) | no |
| `DAT_056da1d8/dc/e0` | `g_scene1_player_pos[3]` (already exists) | no | yes (0x68, 0x84/0x96, 0x3a, 0x34, mega-cluster B aim) |
| `DAT_0076bd54..6c` | already mapped — NPC people-table stride 0x2e9 (`g_scene1_people` per C8h.2) | yes (0x23 fallback, 0x29 NPC branch, 0x30 NPC target) | no |
| `DAT_0076c478` | **NEW** — iterated by 0x68 looking for a matched alt-target slot at stride 0x2e9; same stride as people-table, likely an alias or sister table | yes (0x68) | no |
| `DAT_0438b8cc` | `g_sim_frame_counter` (already exists, per `sim.h`) | no | yes (0x34 — used as `(frame + local_8) % 8` index) |
| `DAT_06a46fb8` | new — slot-ID counter for both allocators (post-incremented sequence ID) | yes (preamble) | yes (preamble) |
| owner-shape A offsets (param_1 + 0x20/0x24/0x28 for pos, +0xde8 for matrix, +0xea0/0xea4/0xeac for misc, +0x948 for NPC bend, +0xe3c for frame offset) | NEW — entity struct shape A | yes (every type body) | no |
| owner-shape B offsets (param_1 + 0x3f0/0x3f4/0x3f8 for pos, +0x39c for matrix, +0x18 for NPC bend, +0x420/0x424 for misc, +0x6fc/0x700/0x704 for alt-pos, +0x1c2*4+i*0xc for per-particle indexed pos) | NEW — NPC entry struct shape B | no | yes (every type body) |

The two owner shapes are large — A is at least 0xeac bytes, B is at
least 0x704 bytes plus indexed slots from 0x1c2*4 + per-particle*0xc.
Both will need partial layout maps in `scene1_records.h` (or sibling)
before any per-type init can be ported.  The C8h.2 people-table port
has a partial map at owner+0x18 (NPC bend) and owner+0x3f0..0x3f8
(pos); extending to +0x420, +0x424, +0x6fc..0x704, +0x1c2*4 is
incremental.

## Argless trig sites (Ghidra dropped FPU args — needs raw-asm verify)

Mirrors C8i.0 pending-human-check #7 pattern.  Sites where Ghidra
shows `FUN_00503994()` or `FUN_00503a44()` with no argument; the
engine x87 asm stashes the angle in `[ebp-0x1c]` and reloads.  Read
via `objdump -d --start-address=0x44376a` / `--start-address=0x445a8c`
on the unpacked exe to confirm pairing.

| Engine line | Function | Argless call | Paired sin/cos site | Expected angle source |
|---:|:-:|:-:|---|---|
| L41073 | 0044376a | `FUN_00503994()` | L41070 sin(-yaw) | `-_DAT_073de39c` (same as sin) |
| L41092 | 0044376a | `FUN_00503a44()` | L41089 cos(-yaw) | `-_DAT_073de39c` |
| L41118 | 0044376a | `FUN_00503994()` | L41112 cos(owner+0xea4) | `*(owner+0xea4)` |
| L41131 | 0044376a | `FUN_00503994()` | L41128 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41148 | 0044376a | `FUN_00503994()` | L41145 sin(local_c) | `local_c` (per-particle angle) |
| L41183 | 0044376a | `FUN_00503994()` | L41180 sin(owner+0x948 angle) | NPC-bend angle |
| L41218 | 0044376a | `FUN_00471089()` (RNG, OK) | n/a | n/a |
| L41224 | 0044376a | `FUN_00503994()` | L41221 sin(rng*2π) | rng-derived angle |
| L41253 | 0044376a | `FUN_00503994()` | L41250 sin(fVar2) | local angle fVar2 |
| L41296 | 0044376a | `FUN_00503994()` (multiple, lines 41307, 41379) | paired with explicit sin earlier | dVar4 from owner-derived angle |
| L41485 | 0044376a | `FUN_00503994()` | L41482 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41554 | 0044376a | `FUN_00503994()` | L41548 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41581 | 0044376a | `FUN_00503994()` | L41574 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41609 | 0044376a | `FUN_00503994()` | L41602 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41633 | 0044376a | `FUN_00503994()` | L41630 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41640 | 0044376a | `FUN_00503994()` | L41636 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41660 | 0044376a | `FUN_00503994()` | L41655 sin(dVar4) | dVar4 (NPC-bend) |
| L41752 | 0044376a | `FUN_00503994()` | L41750 sin(slot+0x8c) | `*(slot+0x8c)` (per-particle angle) |
| L41820 | 0044376a | `FUN_00503994()` | L41817 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41826 | 0044376a | `FUN_00503994()` | L41822 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41846 | 0044376a | `FUN_00503994()` | L41843 sin(owner+0xea4) | `*(owner+0xea4)` |
| L41852 | 0044376a | `FUN_00503994()` | L41848 sin(owner+0xea4) | `*(owner+0xea4)` |
| L42103 | 00445a8c | `FUN_00503994()` | L42099 sin(NPC-bend) | NPC-bend |
| L42141 | 00445a8c | `FUN_00503994()` | L42138 sin(local_10) | local_10 (per-particle angle) |
| L42186 | 00445a8c | `FUN_00503994()` | L42182 sin(rng-angle) | rng angle |
| L42226 | 00445a8c | `FUN_00503994()` | L42223 sin(local_24) | local_24 |
| L42291 | 00445a8c | `FUN_00503994()` | L42287 sin(local_24) | local_24 |
| L42308 | 00445a8c | `FUN_00503994()` | L42304 sin(owner+0x420) | `*(owner+0x420)` |
| L42333, 42340 | 00445a8c | `FUN_00503994()` and `FUN_00503994()` | paired w/ 42329 sin | owner+0x420 |
| L42397 | 00445a8c | `FUN_00503994()` | L42393 sin(local_24) | local_24 |
| L42437 | 00445a8c | `FUN_00503994()` | L42433 sin(owner+0x420) | owner+0x420 |
| L42462 | 00445a8c | `FUN_00503994()` | L42457 sin(local_24) | local_24 |
| L42498 | 00445a8c | `FUN_00503994()` | L42495 sin(local_24) | local_24 |
| L42536, 42566 | 00445a8c | `FUN_00503994()` ×2 | paired with sin(local_24) | local_24 |
| L42577 | 00445a8c | `FUN_00503994()` | L42573 sin(owner+0x420) | owner+0x420 |
| L42760 | 00445a8c | `FUN_00503994()` | L42756 sin(owner+0x420) | owner+0x420 |
| L42790 | 00445a8c | `FUN_00503994()` | L42786 sin(owner+0x420) | owner+0x420 |
| L42812 | 00445a8c | `FUN_00503994()` | L42808 sin(local_18) | local_18 (per-particle bend) |
| L42849 | 00445a8c | `FUN_00503994()` | L42845 sin(owner+0x420) | owner+0x420 |
| L42893 | 00445a8c | `FUN_00503994()` | L42888 sin(owner+0x420) | owner+0x420 |
| L43065 | 00445a8c | `FUN_00503994()` | L43062 sin(slot+0x8c) | slot+0x8c per-particle |

Almost all argless cosines are the **paired** post-sin call (same
pattern as C8i.0 pending-check #7 — `[ebp-0x1c]` stack stash that
Ghidra erases).  Safe to assume; spot-check 2-3 sites per chip by raw
asm before landing.

## Chip ladder proposal (C8j.5 onward)

Each chip should be ~250-500 lines of new C and ~1-2 sessions.  This
ladder mirrors C8i.0-5c.

| Chip | Scope | New types covered | Est lines | Notes |
|------|-------|-------------------|----------:|-------|
| C8j.5 | **skeleton + common preamble + 3 anchor types (both allocators)** — LANDED 2026-05-24 | 0044376a: 0x24, 0x60, 0x82 (preamble-only / minimal) + 00445a8c: 0xe, 0x97, 0x46 (LAB_00447584 tail-share); + scene1_record_b_spawn_entity() + scene1_record_b_spawn_npc() public API | ~400 | Wires both allocators behind feature flags.  Trace ring buffer (mirroring scene1_spawn.c) for instrumentation.  Note: 0x82 substituted for surveyed 0x66 (which executes the drift cluster body, not anchor-only). |
| C8j.6 | **sin/cos drift cluster + cluster A (0044376a only)** — LANDED 2026-05-24 | 0044376a: 2, 3, 4, 0x22, 0x54, 0x67 (drift) + 0x4d-0x50, 0xa5-0xa6, 99, 0x51, 0x52, 0x53 (cluster A) | ~430 | init_entity_drift_cluster + init_entity_cluster_a (per-type SCALE_X/LIFE_MULT/local_10 vel-mag).  Multi-particle outer loop refactor (body returns cap; outer loop scans for next free slot per particle).  Argless cos pairs at L41296/L41307/L41379 (cluster A — `[ebp-0x2c]` ⇒ cos(local_c)) + L41602/L41609 (drift — `[esi+0xea4]` ⇒ cos(ang)) verified via raw-asm spot-check at 0x444034/0x444131 + 0x443cbd/0x443d0c.  17 types covered.  21 host tests (1295→1316). |
| C8j.7 | **mega-cluster A (0044376a) + cluster B (00445a8c)** — LANDED 2026-05-24 | 0044376a: 0x73/0x76/0x77/0x78/0x7a/0x7b/0x7c/0x7e (mega A) + 00445a8c: 0x4d/0x4e/0x4f/0x50/0xa5/0xa6 (cluster B) | ~440 | 14 types covered.  Mega-cluster A's "angle table mod 32" pattern (uVar9 = (owner+0xe3c + local_8) % 32, fVar2 = (uVar9 & 7) - 4, fVar3 = uVar9 / 8 → ROT_X wobble) ported via direct owner-field read — no global stand-in needed.  3-way owner+0x948 dispatch (mode 0 → POS_X -= 0.41, mode 4 → POS_X += 0.41, else → POS_Z -= 0.1; same shift on ALT_POS).  0x7a swaps local_c to owner+0xea4 AFTER pos writes.  0x7c per-particle bidirectional fan + rebound (POS -= 2*VEL).  0x76 part>0 → PART_IDX = 1.  Survey correction: 0x77/0x7b/0x7e are 1-PARTICLE (not 8 as survey claimed), engine's bVar14 = (local_8 == 0) at L41809-41811 confirms.  NPC outer loop refactored to multi-particle (mirrors C8j.6 entity refactor) — body returns cap, outer commits up to cap slots.  Cluster B simpler than cluster A (owner shape B at +0x18 bend / +0x3f0 pos / no per-type SCALE_X / hardcoded LIFE_MULT=0.4).  Argless cos at L41752 verified via raw-asm at 0x444769 (`fld QWORD [ebp-0x2c]`).  19 new host tests (1316→1335). |
| C8j.8 | **NPC-table-deref + camera-yaw + matrix-init (0044376a)** — LANDED 2026-05-24 | 0044376a: 0x23 (matrix), 0x29 (people-table + ground hook), 0x30 (reverse-yaw + people target), 0x9b, 0x9d (NPC-bend), 0x3e, 0x5f (group tail w/ 0x60) | ~440 | 7 new types covered (0x82/0x60 were in C8j.5).  Reuses existing g_scene1_people (C8h.4c) + g_scene1_camera_yaw + math3d's mat4_rotation_x (for engine thunk_FUN_004a35d3).  New ground-query hook `scene1_record_b_spawn_set_ground_query` for 0x29's people-table branch (PHC #15; default stub returns no-hit).  Engine's per-particle dispatch in 0x23/0x29 (local_8 ∈ {1, 2}) ported verbatim but unreachable in normal flow (cap=1 means outer loop only passes part_idx=0).  0x9d's "explicit return" matches our cap=1 outer loop semantically.  0x30 skips engine's dropped-return atan2 call (FUN_00503dd0) — no observable slot side-effect.  14 new host tests (1335 → 1349). |
| C8j.9 | **remaining single-spawn 0044376a types** | 0x58, 0x65, 0x69, 0x68, 100, 0x74, 0x79, 0x6a, 0x61, 0x6d, 0x6e, 0x6f, 0x70, 0x62, 0x8a, 0x8b, 0x71, 0x72, 0x75, 0x7d, 0x5b, 0x5c, 0x5e, 0x85, 0x86, 0x87, 8 | ~450 | 27 types.  Big chip but most are minor variants of bodies already ported (sin/cos vel + Y-lift + per-type scale/life write).  0x68's people-table iteration may require sub-chip carve-out. |
| C8j.10 | **00445a8c skeleton + drift + cluster B + mega-cluster B** | 00445a8c: 0x56, 0x53, 0x51, 0x84, 0x96 (player-aim), 0xa0-0xa4, 0x73, 0x7a, 0x7c, 0x7e (mega B), 0x68 (alt player-aim) | ~450 | Mirrors C8j.6+7 for the NPC allocator.  Player-pos integration (already wired) carries over. |
| C8j.11 | **remaining 00445a8c types** | 0x33, 0x2f, 0x2e, 0x36, 0x27, 0x2b, 0x26, 0x2a, 0x25, 0x31, 0x32, 0x3b, 0x3c, 0x98, 0x5a, 0x6b, 0x6c, 0x1f, 0x3a, 0x28, 0x38, 0x21, 0xd, 0xf, 0x10-0x17, 0x9c, 0x1e/0x88/0x89/0x9a/0x9e (return group), 0x34 (8-element table) | ~500 | 30+ types.  0x34's static index table + 0x36's per-particle pos array need careful porting (alt-source from owner+0x6fc/0x700/0x704 — already mapped) |
| C8j.fin (optional) | **trivial tails (00445a8c LAB_00447584 catch-all)** | 0x24, 0xa, 0xb, 0x14, 0x13, 0x99 | ~50 | One-line table extension.  Could fold into 5/10. |

Total ~3000 lines of new C across 7-8 chips.  Comparable to C8i's ~2700
lines across 8 chips.

## Open questions

1. **C8j.0's 0x49-overlap claim:** the body inspection found **no**
   handler in either allocator for type 0x49.  Either both default to
   preamble-only (likely — and that's why it appeared in both call-site
   greps), or the call sites we found pass 0x49 but the per-type
   switch silently falls through.  Worth a 1-line Frida trace of
   `scene1_record_b_spawn_entity(_, 0x49, _)` to confirm behavior at
   land time of C8j.10/11.

2. **owner+0x424 dispatch in 00445a8c 0x1f:** five magic values
   (`7, 8, 9, 0x23, 0x24`) drive per-NPC-type amp selection.  What
   field is +0x424?  Probably the NPC's "kind" enum; spot-check
   against scene1_people_entry_t (already partially mapped in C8h.2).

3. **owner+0xe3c in 0044376a mega-cluster A:** read as a per-frame
   offset for the angle-table modulo.  Likely the entity's "sub-frame
   counter" or similar; needs stand-in.

4. **owner+0x6fc, owner+0x700, owner+0x704 in 00445a8c (0x33, 0x36, 0x38):**
   alt-pos source (different from the +0x3f0..0x3f8 default).  This
   looks like a "spawn-target world position" — perhaps the NPC's
   action target?

5. **owner+0x1c2*4 + local_8*0xc base in 00445a8c 0x36:**
   per-particle indexed pos array on the NPC.  Likely a fixed-size
   array of waypoints/effect-anchors per NPC entry; 8 entries per the
   loop count.  Stride 0xc (3 floats) is a position triple.

6. **DAT_0076c478 in 0044376a 0x68:** iterated from +0 to
   `&DAT_007c9678` with stride 0x2e9 (4× sizeof people entry +
   small).  This is a sister table to the NPC people-table, OR an
   alias for the same table read at a different field offset.  C8h.2's
   people-table port has DAT_0076bd54 + stride 0x2e9; +0x724 difference
   from c478 vs bd54 means it could be the same table read from a
   different base field.

7. **Sequence counter (DAT_06a46fb8) wraparound:** if the engine runs
   long enough for the int to wrap, do consumers depend on monotonic
   ordering or just uniqueness?  Probably uniqueness only — but worth
   a Frida read at high run times.

8. **Why does FUN_0044376a write +0xa0 = 0xffffffff but FUN_00445a8c
   skips it?**  The aux sentinel at +0xc4 (`DAT_069325c4`) is set by
   0044376a in preamble but not by 00445a8c.  If FUN_0043ae20 reads
   +0xc4 as a flag, the field will be uninitialized (or zero from
   slot reuse) on 00445a8c-allocated slots.  Could be a real bug —
   verify via Frida-trace what value +0xc4 actually has post-spawn for
   a known 00445a8c-allocated type like 0x4d.

9. **The "explicit-return" group in 00445a8c (0x1e/0x88/0x89/0x9a/0x9e):**
   returns from the allocator immediately after type-claim — does NOT
   loop to check more slots.  This means these types ALWAYS spawn
   exactly 1 particle, and the caller never gets multiple slots from
   one call regardless of how many free slots exist.  Different
   semantics from the per-particle loop tail.  Need to test the
   integrator side to confirm consumer doesn't expect multiple slots.

10. **Counts on cluster A (0044376a 0x4d-0xa6):** the iVar10 final
    assignment (`iVar10 = 1; if (param_2 == 0x4f) iVar10 = 3; ...`)
    sets the loop termination cap.  But the assignment is at L41380
    after a long body — make sure the C port doesn't skip it by jumping
    to LAB_004455ed with stale iVar10.
