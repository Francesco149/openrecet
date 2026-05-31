# Plan: faithful full port of the HOUSE game-state controller (FUN_0048670f → FUN_0048b850 → FUN_00483170)

> **Status (2026-06-01):** ACTIVE. Foundation + Chips 1–2 landed
> (`4090a36` §75 + chip plan, `202883c` Chip 1, Chip 2 this commit). This is the
> durable repo copy of the approved chip plan — the summary lives in
> `un-mvp-structural-parity.md` Step 3.1/3.2, the structural map in
> `engine-quirks.md` §75 (+ §76 for the Chip 2 render-bank finding), the
> per-step source map below.

## Context

The HOUSE player controller is the active front (STATUS top-chip) and the last big
`PORT-DEBT(simplified, …)` on the in-game spine. `scene1_player_ctrl_tick`
(`src/scene1_player_ctrl.c`) is a **hand-rolled** free-roam controller: read d-pad →
accumulate walk impulse → clamp → integrate-with-collision → damp, inline. §69 made
it **behaviourally** bit-exact vs retail (`house-walk-*` / `house-table-corner`
benches), but it does **not** mirror retail's structure, so the next free-roam
divergence has to be chased on a skeleton that doesn't match the engine.

The real engine code is a **3-function decomposition**, all keyed on the in-game
state id `DAT_0438cc08` ("cc08"):

- **`FUN_0048670f`** (1637 lines, `docs/decompiled/all.c:86539-88178`) — the whole
  INGAME interaction controller: shop-counter haggling, customer approach, menus,
  cutscene timing, **and** free-roam walking (the `cc08==1` arm; reads d-pad, runs
  proximity/approach detection, calls `FUN_0048b850` to move). Runs first in the
  default sim arm (`FUN_00442cef`).
- **`FUN_0048b850`** ("Cpop", 5 KB, `all.c:89757+`) — the movement/effects
  sub-controller the `cc08==1` arm calls: velocity clamp, facing octant, the
  integrate-and-collide call, damp, particle/after-image effects, and the actor
  render-slot population. Leaves already extracted (`scene1_player_ctrl.c:24-327`).
- **`FUN_00483170`** — physics integrate + collide. **Already ported** as
  `collision_resolve_player` (`src/collision_resolve.c`, §66/§69).

**User directives:** (1) work toward the **full** game-state controller, not just
cc08==1; (2) **sequence chips so we don't accumulate unnecessary MVP debt**;
(3) companion facing has slight per-frame diffs the user will **human-verify via a
feed diff** when goldens shift — don't chase to bit-exactness or treat a golden
mismatch there as a regression.

## Debt discipline (the core principle)

- **Structural stub** = real engine signature + `CALL_TRACE_ENTER_STUB(va)` + empty/
  minimal faithful body. Ledger **"stubbed"**. **NOT debt.** Far-future cc08 states
  (combat/dungeon/dialogue/menus) land as these.
- **Simplified body** = a hand-rolled approximation that fakes behaviour
  (`PORT-DEBT(simplified, …)`). **This is the debt.** The current tick is one.

The migration replaces the single `simplified` debt with a faithful skeleton whose
off-path states are honest `stubbed` entries. **Zero** new `simplified` bodies. Each
chip is independently **bit-exact** on the benches and retires concrete debt (or stays
neutral) — never introduces a new fake.

## State inventory (FUN_0048670f cc08 dispatch — full version in §75)

| cc08 | hex | role | near-path |
|----:|----:|------|-----------|
| 0 | 0x00 | free-roam entry / idle-anim init | yes (entry) |
| 1 | 0x01 | **free-roam walk** (d-pad → FUN_0048b850) | **yes (core)** |
| 2 | 0x02 | in-scene NPC/prop crowd | later |
| 3 | 0x03 | camera/viewpoint preview on entry | entry-adjacent |
| 4 | 0x04 | scripted NPC approach lock | stub |
| 10 | 0x0a | customer approach setup → 0x17/4 | stub |
| 15 | 0x0f | shop-front cursor / counter proximity | stub |
| 16 | 0x10 | object-interaction router | stub |
| 17 | 0x11 | fade-in input guard → 0xf | stub |
| 18 | 0x12 | menu / camera-pan cursor | stub |
| 23 | 0x17 | customer dialogue cutscene → 3 | stub |
| 30 | 0x1e | NPC dialogue choice select | stub |
| 50 | 0x32 | shop counter menu | stub |

Per-frame machinery that runs **regardless of state** (port, don't stub): actor-record
spawn refresh (`DAT_005ce3c4` loop), scene-transition flag early-returns
(`DAT_0450f470/485/488/495` fades), camera-shake ramp counters (`DAT_0438b74c/750`),
event-timing counters (`DAT_0438b924/b4e0`), common tail `LAB_004893ff`
(`FUN_00486435` room clamp → `FUN_00485861` → return).

## Free-roam per-frame pipeline — where each step lives (§61/§69 reconciled, §75)

| # | step | const | function / site |
|--:|------|-------|-----------------|
| 1 | walk **impulse** `daabc/daac4 += sin/cos(db05c)·0.1` + anim id `daae8` | accel **0.1** | **FUN_0048670f cc08==1** controllable code — `*(player+0x904)`, invisible to a `DAT_056daabc=` grep (§61) |
| 2 | speed-cap + clamp `|v| ≤ 0.175` | cap **0.175** | FUN_0048b850 @ L90010 (cap tree → 0.175 in HOUSE) |
| 3 | facing octant `dab00` + sticky snap | π/8,2π,8 | FUN_0048b850 @ 0x48bfd2 + `dae3c` snap |
| 4 | integrate + collide | — | FUN_00483170 (called from b850 @ L90122) = `collision_resolve_player` |
| 5 | room-bounds clamp `pz≤9.5; px≥−1.5 when pz>7` | — | FUN_00486435 in the FUN_0048670f **tail** = `player_ctrl_house_room_clamp` |
| 6 | damp `*= 0.82` | **0.82** | FUN_0048b850 damp tree @ L90161-90198 (→ 0.82 grounded) |

§69's "impulse at FUN_0048b850 L319-326" was **imprecise**: those lines are the
`da1bc`-gated stun/hop path (accel 0.3), skipped in free-roam (`da1bc==0`). The real
0.1 walk impulse is step 1 in the controllable code → ports in the FUN_0048670f chip.
Steps 5/6 are order-independent (pos vs vel).

## Chip sequence (bottom-up; each bit-exact, retires real debt, adds zero simplified debt)

- **Chip 1 — `FUN_0048b850` free-roam body. ✅ DONE (`202883c`).** Extracted
  clamp/octant(→FACING)/collide/damp into `player_ctrl_b850_move()`; tick calls it +
  keeps impulse (step 1) + room-clamp (670f tail). Bit-exact (damp↔room-clamp reorder
  is independent). `CALL_TRACE_ENTER_STUB(0x48b850)` (body incomplete — render-slot is
  Chip 2). 3048/0, both exes clean.

- **Chip 2 — `FUN_0048b850` render-slot populator → retire synthetic-data debt. ✅ DONE.**
  The premise ("`chr-walker` is the player draw, the inject stands in for it")
  was wrong (see §76): `FUN_00456f56` draws ADDITIVE after-image banks, the solid
  player draws via `FUN_004552d0` (`scene1_shop_walker`), and the inject was DEAD
  code (never called since Cchr.2h retired `--force-chr-walker`). So Chip 2:
  - `scene1_player_ctrl.c` now OWNS the two render banks (`DAT_056dab6c` trail /
    `DAT_056dacc0` burst) + the two 40-slot history rings + the burst/decay
    counters, and wires the b850 tail (`player_ctrl_b850_render_tail`:
    history-shift → burst → decay-edge → trail-advance) as their live writer.
  - `scene1_chr_walker.c` reads the banks via `player_ctrl_render_bank_slot()` /
    `player_ctrl_burst_count()`; the synthetic inject + `set_inject` are deleted.
  - **NET-ZERO visible** (NOT the anticipated golden shift): the banks are dormant
    in free-roam (no dash spawn / zero burst counter), so the walker draws no
    after-images. House-walk-tables byte-identical to the pre-chip build;
    house-table-corner 9/9; +2 host tests (3050 pass).
  - Retired: `PORT-DEBT(synthetic-data, FUN_0048b850)`; debt 6→5 (synthetic-data
    now 0).
  - **Deferred to a later b850 sub-chip:** the dash/`FUN_0044376a` spawn that
    lights the banks, and opening `chr-walker` Pass 2 (needs the real
    `DAT_0438b4b4` entry-fade gate sourced — see §76).

- **Chip 3 — `FUN_0048670f` prologue + per-frame bookkeeping + tail (skeleton shell).**
  Faithful outer structure: actor-record refresh, the `DAT_0450f470/485/488/495`
  transition-flag handlers (port reachable, structural-stub off-near-path fades),
  `b74c/750/924/b4e0` counters, common tail `LAB_004893ff` (`FUN_00486435` clamp +
  `FUN_00485861`). cc08 dispatch shell still routes cc08==1 to the free-roam body.
  No behaviour change; benches bit-exact. Adds only `stubbed` entries.

- **Chip 4 — `FUN_0048670f` cc08 dispatch + cc08==1 arm faithfully → retire simplified
  debt.** Port the cc08==1 free-roam arm (d-pad masks `DAT_073dddd4/dddd6`, proximity →
  cc08==4, door/exit, `cc04==0` walk; the **step-1 impulse lives here**). Off-path
  states (4,10,0xf,0x10,0x11,0x12,0x17,0x1e,0x32,2,3) → `CALL_TRACE_ENTER_STUB`
  structural stubs. **Delete** the hand-rolled tick body.
  - Retires: `PORT-DEBT(simplified, FUN_0048670f)`; debt 5→4. Add
    `CALL_TRACE_ENTER(0x48670f)` + Frida hook-and-diff at HOUSE_FREEROAM → "verified".

- **Chip 5+ — flesh out gameplay cc08 states** (shop counter, dialogue, menus) as each
  becomes reachable. Each replaces a stub, adds no debt. End state: full FUN_0048670f
  verified; port-debt.json shrinks toward empty.

## Critical files

- `src/scene1_player_ctrl.c` / `.h` — `player_ctrl_b850_move` (Chip 1, done) + leaves
  (24-327); the tick to migrate into faithful `FUN_0048670f` (Chips 3-4).
- `src/scene1_chr_walker.c` (78-121) — the synthetic render-slot inject to retire (Chip 2).
- `src/collision_resolve.c` — `collision_resolve_player` (FUN_00483170); reuse.
- `src/call_trace.h` — `CALL_TRACE_ENTER` / `_STUB`.
- `tests/test_scene1_player_ctrl.c`, `tests/test_main.c` — host tests (extend per chip).
- Decomp: `docs/decompiled/all.c:86539-88178` (FUN_0048670f), `:89757+` (FUN_0048b850).
  Verify step sizes / masks against `objdump` of `vendor/unpacked/…exe` (§53/§61/§69 pattern).

## Verification (per chip)

- `nix develop --command make -C tests run` (host suite) + `make -C src` (both PE exes
  warning-free).
- `tools/scenario-test.py house-walk-tables --target both` + `house-table-corner` +
  `tools/wall_collide_diff.py` — keep the bit-identical frames (shift-+1 RMS Δpx=0) +
  the W3 cardinal walks.
- Frida hook-and-diff (E.1/E.2) of the ported FUN at HOUSE_FREEROAM → args/retval/
  state-writes vs retail → add `CALL_TRACE_ENTER` → ledger "verified".
- After Chip 2/4 (visible output): `tools/regen-comparisons.py`, push index.html to the
  feed. **Companion-facing golden diffs → USER feed-diff verification.**
- `python3 tools/gen_port_debt.py` + `gen_port_ledger.py` (pre-commit hook regens).
