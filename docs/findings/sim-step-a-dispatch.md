# `sim_step_a` / FUN_004536cb — full survey

**Survey date:** 2026-05-23.  **Engine fn:** `FUN_004536cb` @ `0x4536cb`,
1745 B.  **Existing port:** `src/sim.{c,h}` (`sim_step_a`) — covers ~5
of the function's ~25 distinct logical blocks (button ring, font-age
hook, worker-busy gate, fade-tick tail, state-0 dispatch).

This doc surveys what's left and lays out a chip ladder.  Trigger for
the survey: closing C8i means the next milestone in the scene-1 ladder
(see `openrecet_scene1_render_ladder.md`) is **wiring
`scene1_particles_tick` into the unported INGAME sim caller** —
i.e., here.

## Why it's the next chip

Per `docs/findings/scene1-particles-tick.md` §"Call sites", the
integrator (`FUN_0040fb3a`, ported as `scene1_particles_tick`) has
5 call sites, 4 of which live inside `FUN_004536cb`'s state machine:

| Reachable from               | Path                                         |
|------------------------------|----------------------------------------------|
| State 1 (HOUSE / INGAME)     | `FUN_004427d3()` (30-B wrapper) → `FUN_0040fb3a` |
| State 1 (HOUSE / INGAME)     | `FUN_00442cef()` (2490 B pre-render gate) → `FUN_0040fb3a` |
| States 2, 3, 6, 7, 8, 0xb, 0xd-0x10 | direct `FUN_00406584(); FUN_0040fb3a();` at `LAB_00453bed` (state 4/5/0xc skip the particle tick) |
| Scene-entry one-shot         | `FUN_00436f97` (separate, 200-iter spawn loop) |

So `FUN_004536cb` is the per-tick driver.  Without it, the integrator
never ticks and Pass D + Pass F never receive evolving records.

## The function as 25 blocks

Walk `docs/decompiled/by-address/4536cb.c` top-to-bottom:

| # | Lines      | What it does                                                 | Port status        |
|---|------------|--------------------------------------------------------------|--------------------|
| 1 | L19-23     | If state != 0, bump `DAT_044e37a0[scene_slot]` visit counter | unported (no consumer) |
| 2 | L24        | `FUN_0047c29d()` — font-LRU age tick                          | ported (`font_age_tick`) |
| 3 | L25-28     | `DAT_06a49954 != 0` → `sim_loading_pump` + early return       | ported (worker-busy branch) |
| 4 | L29        | `DAT_06a49958 = 0` — one-shot escape clear                    | unported |
| 5 | L30-41     | One-shot zero of 8-entry block at `DAT_073dddda` (`DAT_06a499cc` latch) | unported |
| 6 | L42-70     | Button-state ring (2 players × 16-bit cur/prev/pressed/held, 16 repeat counters) | ported (`sim_button_ring_update`) |
| 7 | L71-91     | If `DAT_06a4993c == 1`: cursor mod-16 / mod-64 from press bits + xor a flag in `DAT_0450f3e0` | unported |
| 8 | L92-97     | If `DAT_06a4993c == 2`: `FUN_004518a3()` + clear flags        | unported (612 B callee) |
| 9 | L98-107    | `FUN_0040cea6()` (pause? menu open?) — if true and `_DAT_073dddd4 & 0x10` set, do `FUN_0040ce0d()` + return; else just return | unported (226 B) |
|10 | L108-110   | If `_DAT_073dddd4 & 0x100` and not loading and state!=0: `FUN_00453384(0)` (ESC handler, 821 B) | unported |
|11 | L111-126   | `DAT_06a499c8` counter (2-tick latch) → reset effect counters + `FUN_00435c98()` + `FUN_004526f5(0, 0x11)` fade-out kick | unported (309 B + 276 B) |
|12 | L127-131   | If `DAT_06a49998 == 3`: force state = 9, call `FUN_0047f2f6()` + `FUN_00452cde()` worker spawn | unported (372 B + worker_load wired) |
|13 | L132       | `FUN_00405552()` — 498 B common-tick helper (?)               | unported |
|14 | L133       | `FUN_004693e3()` — 41 B common-tick helper (?)                | unported |
|15 | L134-147   | `DAT_06a499c4` "splash retry" path: poll `FUN_004528b3()`; on success reset state, call `FUN_0049a3a3()` (splash, 154 B), `FUN_0045281c(0,0x11)`, `FUN_00474d92()` (232 B), `FUN_00452cde()` | unported |
|16 | L148-166   | `DAT_06a49964` "fade-in apply" path: same poll; on success commit scene + `FUN_00452eed()` alt worker spawn | unported (worker_load wired) |
|17 | L167-198   | `DAT_06a49998 != 0`: state-9 nested cleanup + dispatch by `DAT_06a4997c` selector (`FUN_0047fa76` / `FUN_0048dbfb` / `FUN_0048f931`) | unported |
|18 | L201-202   | State < 9 fast path                                          | partial             |
|19 | L203-206   | State 0 → `FUN_0049a59e()` then `LAB_00453cfb`               | unported (3719 B — splash/title, almost certainly current `scene_title_sim_default`) |
|20 | L217-240   | State 1 (INGAME): nested by `DAT_0438b1d0` (sub-state)         | unported |
|     |            | - sub `!= 0`: `FUN_004427d3()` + `FUN_00406584()` + post-load gate | |
|     |            | - sub == 0 && `DAT_0438b1d8 == 0` && `DAT_0438b1c8 == 0`: `FUN_00442cef()` + `FUN_00406584()` | |
|     |            | - else: same `FUN_004427d3()` + `FUN_00406584()`              | |
|21 | L201-215, L242-246 | Per-state pre-dispatch.  Within `< 9` arm: state 0 → `FUN_0049a59e` and goto LAB_00453cfb; states 2/3 → goto LAB_00453bed (with particle tick); states 4 → bare goto LAB_00453cfb (NO particle tick); state 5 → `FUN_0046c039` then LAB_00453cfb (NO particle tick); states 6/7/8 → goto LAB_00453bed.  Within `>= 9` arm (block 17 already returned for 9): state 10 → `FUN_0047e711` and goto LAB_00453cfb (NO particle tick); state 12 → bare LAB_00453cfb (NO particle tick); states 0xb / 0xd / 0xe / 0xf / 0x10 → goto LAB_00453bed. | unported |
|22 | L247-291   | `LAB_00453bed`: `FUN_00406584(); FUN_0040fb3a();` then per-state callee. **This is the path that drives the particle tick for states 2, 3, 6, 7, 8, 0xb, 0xd-0x10.** Per-state callees: 2→`0049d8a4`, 3→`0041ee24`, 6→`00490e24`, 7→`0049db8a`, 8→`0049e163`, 0xb→`0045c051`, 0xd→`0045e3dc`, 0xe→`0045e053`, 0xf→`0045e1a5`, 0x10→`0045e2dd` | unported (`scene1_particles_tick` exists but no caller) |
|23 | L292-303   | `DAT_0438b1c8 == 1` epilogue: poll `FUN_0046c320()` (1353 B), on success drop `DAT_0438b1c8`, call `FUN_00473c0c()` + per-mode `FUN_0045281c` / `FUN_004526f5(0,3)` + `FUN_0044baad()` (109 B) | unported |
|24 | L304-317   | `LAB_00453cfb`: smoothed approach of `_DAT_0438b7d4` toward `(&DAT_0450fb88)[scene_slot * 0xb7f2]` (clamped to 3.5) | unported (purely cosmetic per-scene timer) |
|25 | L318-320   | `FUN_004526ab()` + `DAT_0438b8cc++`                          | partial (tail `g_sim_frame_count++` present; `FUN_004526ab` 41 B stubbed) |

## State machine reference (`DAT_0438b1c0` — `g_scene_state`)

From the dispatch in blocks 18-22 and from cross-referencing
`PROGRESS.md` / `winmain-and-bootstrap.md`:

| state | meaning (best guess)                  | calls particles_tick? |
|-------|---------------------------------------|------------------------|
| 0     | Title                                 | no                     |
| 1     | INGAME / HOUSE (shop interior)        | yes (via FUN_004427d3 or FUN_00442cef) |
| 2     | Cutscene (`FUN_0049d8a4`)             | yes                    |
| 3     | Conversation / dialog (`FUN_0041ee24`)| yes                    |
| 4     | (fallthrough only)                    | yes                    |
| 5     | Worldmap (`FUN_0046c039`)              | no (own path) — does NOT hit LAB_00453bed |
| 6     | (`FUN_00490e24`, 17 B trivial)         | yes                    |
| 7     | Dungeon idle (`FUN_0049db8a`)          | yes                    |
| 8     | Dungeon combat (`FUN_0049e163`)        | yes                    |
| 9     | Scene transition target (state==9 forced after DAT_06a49998 latches; dispatches via DAT_06a4997c) | no |
| 10    | Title menu? (`FUN_0047e711`)           | no                     |
| 11    | (`FUN_0045c051`, 3021 B — biggest scene callee) | yes |
| 12    | (gap)                                  | no                     |
| 13-16 | Ending screens (`FUN_0045e3dc` ... `FUN_0045e2dd`) | yes |

The "HOUSE-visible" milestone is **state 1**.  Per-tick the engine
runs `FUN_004427d3()` 6-call wrapper which calls `FUN_0040fb3a` plus 5
other handlers, then runs the post-game-logic gate via
`FUN_00406584`.

## `FUN_004427d3` (the 30-B INGAME-tick wrapper)

```c
void FUN_004427d3(void) {
    FUN_0048407f();   //  795 B — unknown
    FUN_00430c00();   //  109 B — unknown
    FUN_0043ae20();   // 25750 B (!) — almost certainly the player+NPC+world tick
    FUN_0043a5d9();   // 1429 B — secondary INGAME logic
    FUN_0040fb3a();   // ← scene1_particles_tick (already ported)
    FUN_004426a7();   //  300 B — likely camera or post-game-logic
}
```

`FUN_0043ae20` at 25750 B is by far the largest single function in
the engine outside of `FUN_004547ab` (render dispatch) — porting that
is months of work and is **not** required for visible particle
effects.  To get particle pixels on screen we only need to call
`scene1_particles_tick` from sim; the other 5 callees can stay
stubbed (nothing in the data side reads what they'd produce, because
the only consumers — Pass D meshes, Pass F billboards — are static
geometry sourced from `g_scene1_records_a` which `scene1_spawn` /
`scene1_particles_tick` populate themselves).

## Chip ladder

The function is too big for a single chip; we already have ~150 lines
of `sim_step_a` covering the always-on prelude + state-0 dispatch.
The natural progression keeps adding state arms until the HOUSE
(state-1) particle path is live, then expands outward.

### Cs1 — particles-tick hookup (smallest viable)

Goal: get `scene1_particles_tick()` ticking when scene state is INGAME,
without porting the surrounding 25 KB of game logic.

- Add `SCENE_STATE_INGAME = 1` to `src/scene.h` (already named?).
- In `sim_step_a` scene-dispatch: add `case 1: scene1_ingame_tick();`
  where `scene1_ingame_tick()` is a new module-local function that
  ports the **minimum** of `FUN_004427d3`:
  - Call `scene1_particles_tick()` (the one piece we have).
  - Document the 5 unported siblings as stubs in a comment.
- Add a per-tick host test scenario that exercises one type-0x92
  particle: spawn at boot via `scene1_records_inject_test_type92` (we
  already have the helper for `--show-pass-f-test`), step sim 100
  times, assert age > 0 and pos.x has drifted (`scene1_particles_tick`
  type-0x92 handler integrates pos via sinusoidal X drift).
- No render-side change yet — Pass F MVP still needs the manual
  `--show-pass-f-test` flag.

Risks: state 1 has 13 other prerequisite checks in the engine path
(`DAT_0438b1d0` sub-state, `DAT_0438b1d8` overlay gate, `DAT_0438b1c8`
worker-load gate).  Cs1 ignores them and just calls
`scene1_particles_tick` unconditionally on state==1.  Safe because
all gates are BSS-zero on cold start and no producer writes them yet.

### Cs2 — `LAB_00453bed` mass dispatch (cleanup chip)

Wire states **2, 3, 6, 7, 8, 0xb, 0xd-0x10** into a shared helper that
calls `scene1_particles_tick()`.  States 4, 5, 0xa, 0xc explicitly do
NOT hit the particle tick in the engine (see block 21).  Per-state
callees (`FUN_0049d8a4`, etc.) stay stubbed — they're scene-specific
update routines unused in HOUSE.  Quiet chip; mostly hooking up
`switch` cases.

### Cs3 — `FUN_00406584` per-tick common helper

1017 B.  Called from both the state-1 path and the `LAB_00453bed`
path right before the particle tick.  Likely a per-tick state
counter (game-time / day-cycle / weather animation drivers).  Port
when something starts reading the timers it updates.

### Cs4 — scene-transition machinery (blocks 11-17)

The five gates the engine uses to schedule scene-changes:
`DAT_06a499c8` / `DAT_06a49998` / `DAT_06a499c4` / `DAT_06a49964` /
`DAT_06a4997c`.  These drive **all** scene-change effects (fade
+ worker_load spawn + new scene init).  Today no producer writes any
of them, but they'll be needed once any state-1 → state-N transition
ports (e.g., HOUSE → worldmap via leaving the shop).

### Cs5 — input-mode arms (blocks 7-10)

Cursor mod-16/mod-64 navigator (`DAT_06a4993c == 1`) + ESC handler
(`FUN_00453384`) + pause check (`FUN_0040cea6`).  Inert in HOUSE
until the inventory/menu UI ports.

### Cs6 — visit counter + per-scene float timer (blocks 1, 24)

Cosmetic / debug.  Visit counter at `DAT_044e37a0` is per-scene-slot
(stride `0x2dfc8`, indexed by `DAT_0438b1e0`); the float timer
`_DAT_0438b7d4` ramps toward `(&DAT_0450fb88)[scene_slot * 0xb7f2]`
(some per-scene "ambient intensity"?  clamped to 3.5).  Both can be
ported when a consumer surfaces.

## Where each port lives

- All Cs* chips land in `src/sim.c` (with the partial port today).  No
  new module is needed.  New file only if `sim.c` exceeds ~600 lines.
- New scene-specific tick helpers (e.g., `scene1_ingame_tick`) belong
  in `src/scene_sc1.{c,h}` (already exists for the worker-body
  wiring) or a new `src/scene1_sim.c` — TBD when Cs1 lands.
- Host tests in `tests/test_sim.c` (already exists for the button
  ring) and a new `tests/test_scene1_ingame_tick.c` for the
  scene1-dispatch case.

## Globals legend (BSS-zero defaults)

Single-source-of-truth for the engine globals referenced in this
function.  All BSS-zero unless noted.  None have a port-side
equivalent today.

| Name              | Stride / size | Read by               | Written by            |
|-------------------|---------------|-----------------------|-----------------------|
| `DAT_0438b1c0`    | int32         | scene-state dispatch (= `g_scene_state` in port) | `FUN_00474681`, `FUN_00474d92`, post-init |
| `DAT_0438b1d0`    | int32         | state-1 sub-state     | `FUN_004358c9`, init  |
| `DAT_0438b1d8`    | int32         | state-1 gate          | (cutscene UI?)        |
| `DAT_0438b1c8`    | int32         | post-load worker gate | `FUN_0046bf38` post-spawn |
| `DAT_0438b1e0`    | int32         | per-scene-slot index 0..1 | scene-slot toggle |
| `DAT_06a49954`    | int32         | "loading sub-mode" escape | scene transition init |
| `DAT_06a499c4`    | int32         | splash-retry path     | `FUN_0049a3a3`?       |
| `DAT_06a49964`    | int32         | fade-in apply path    | block-11 latch        |
| `DAT_06a49998`    | int32 (0..3)  | scene-change pending  | block-11 latch        |
| `DAT_06a4997c`    | int32 (0/1/2) | state-9 worker selector | scene-target setter |
| `DAT_06a49980`    | int32 (1..4)  | post-fade mode        | scene-target setter   |
| `DAT_06a49984`    | int32 (0/1)   | post-fade kick gate   | scene-target setter   |
| `DAT_06a499c8`    | int32         | 2-tick latch          | block-11 setter       |
| `DAT_06a4993c`    | int32 (0/1/2) | cursor-mode           | menu open/close       |
| `DAT_06a49944`    | int32 (0..15) | cursor x (mod 16)     | block 7               |
| `DAT_06a49948`    | int32 (0..63) | cursor y (mod 64)     | block 7               |
| `_DAT_073dddd0`   | uint16        | input pressed mirror  | input poll            |
| `_DAT_073dddd4`   | uint16        | input held-with-repeat mirror | input poll    |
| `_DAT_073dddd6`   | uint16        | input held-once mirror | input poll           |
| `DAT_073dddda`    | 8 × int32     | menu cursor scratch (block 5) | block 5 latch |
| `DAT_06a499cc`    | int32 (0/1)   | one-shot latch for block 5 | block 5         |
| `_DAT_0438b7d4`   | float         | per-scene ambient timer | block 24            |
| `DAT_0438b8cc`    | uint32        | "frame counter" tail  | block 25 (= `g_sim_frame_count`) |
| `DAT_044e37a0`    | array, stride 0x2dfc8 × 2 | visit counter per scene slot | block 1 |
| `DAT_0450fb88`    | float array, stride 0xb7f2 × 2 | per-scene ambient target | static data |

## Pending human checks introduced by this survey

None — this is read-only.

## How to apply

Start with **Cs1**: it's tiny (one switch arm + one tick call + one
host test) and unblocks the next visible particle effect.  Cs2 is
mechanical follow-up.  Cs3-Cs6 land as each prerequisite ports
(`FUN_00436f97` spawn loop, scene-transition writers, menu UI, etc.).
