# HOUSE player controller — movement-first wiring plan

> Approved 2026-05-30. Progress: **W1 landed** (`fcb2a46`); **W2 blocked on a
> target-verification question** (see "W2 open question" below). Companion
> memory: `project_next_char_controller`. Narrative: `docs/PROGRESS.md`.

## Context

Cpop.1–8 extracted every pure-leaf of `FUN_0048b850` (the HOUSE player
controller) — facing snap, camera shake/zoom, motion-history rings, dash-trail +
after-image fills, HP/SP gauge tween. That vein is mined out, and the leaves are
**dormant**: nothing calls them, so the HOUSE player is a static idle pose seeded
once at scene entry (`scene1_postload_pose_house_standing` →
`player_ctrl_pose_house_standing`, read by `sw_pass_light` in
`scene1_shop_walker.c`).

**Architecture reframing (the key finding):**

- `FUN_0048b850` (5 KB, leaves done) is the camera/effects **sub-controller** —
  shake, zoom, after-images, gauges. Wiring its body produces correct internal
  *state* but **no visible change**.
- `FUN_0048670f` (11.5 KB, unported) is the real driver: it reads the input
  masks (`DAT_073dddd4/dddd6`, already populated by the ported `input_poll`),
  moves the player (`DAT_056da1d8`/`dc`/`e0`), sets the animation record
  (`DAT_056daae8`), and *calls* `FUN_0048b850` + `FUN_004897c6` as sub-steps.
  The engine runs it **first** in the default sim arm (`FUN_00442cef`
  L40595-40598, before the records-B tick).

**Decision:** go **movement-first** — target `FUN_0048670f` so the player
visibly walks + animates (matches the recorded "live anim+motion" goal);
`FUN_0048b850`'s effects wire in afterward.

## Substrate (how the port represents engine globals)

Engine `DAT_` globals are **normal C variables** (module static / `extern` via
headers), `DAT_` address in a comment — *not* fixed-address mapping. The
actor-state globals already exist as real C arrays in `src/scene1_player_ctrl.c`,
fronted by **const read accessors** the draw side already calls:

- `s_actor_char[3]` (`DAT_056da1cc`), `s_actor_scale_xz/y[3]`
  (`DAT_056dae18/24`), `s_actor_record[3][11]` (`DAT_056daae8`).
- Player position `g_scene1_player_pos[3]` (`DAT_056da1d8/dc/e0`), `extern` in
  `scene1_particles_tick.h`.

Frame hook (**done, W1**): `scene1_ingame_default_arm_tick()` (`scene1_sim.c`)
now calls `scene1_player_ctrl_tick()` before `scene1_records_b_tick`. The draw
side (`sw_pass_light`, `scene1_shop_walker.c:528`) reads the actor accessors live
each frame → once the tick writes those globals, the player updates with zero
draw-side changes.

Callee status for the movement slice: collision/probe `FUN_00432e50` and
`FUN_004147d5` are **already ported**; `sinf/cosf/sqrt/rng/ftol` map to existing
helpers. The heavy unported callees (`FUN_0048a833` 3 KB, `FUN_00483170` 3.3 KB,
`FUN_0048cdcc` 2 KB, `FUN_004897c6` 870 B) belong to the cutscene/event and
effects branches — stubbable for the first milestone.

## Sub-chips (one commit each)

- **W1 — tick hook + sim wire. ✅ DONE (`fcb2a46`).** `scene1_player_ctrl_tick()`
  (FUN_0048670f entry, `CALL_TRACE_ENTER_STUB(0x48670f)` body) wired into the
  default arm before records-B. Baseline test
  `test_player_ctrl_tick_is_pose_preserving_stub`.

- **W2 — free-roam input decode → movement.** Decode the movement bits of
  `DAT_073dddd4/dddd6`, integrate `g_scene1_player_pos` (engine step sizes +
  ported collision `FUN_00432e50`), write facing octant `DAT_056dab00` + sticky.
  Input→octant as a host-tested leaf; mutation in the tick. Stub non-playable
  `cc08` branches + `FUN_0048b850`/`0048a833`/`004897c6` sub-calls. Payoff: the
  player slides under input. **⚠ blocked — see open question.**

- **W3 — animation record walk-cycle + facing.** Set actor anim-id
  (`DAT_056daae8[0]`) from movement state and advance the walk-frame timer/frame
  fields. Resolve whether the frame advance is the chr-sprite frame tick
  (`FUN_00482a71`, unported) or inline in `FUN_0048670f`. Payoff: the walking
  player animates — the visible milestone.

- **W4 — wire `FUN_0048b850`'s body (Cpop effects).** Stand up persistent
  controller state; call the done Cpop leaves against it; stub the ~13 unported
  callees (3 large as no-ops). Live camera shake/zoom, after-image trail, gauge
  tween. State-diffable vs retail via the trace harness.

## ⚠ W2 open question — verify the target before porting

Reading `FUN_0048670f`'s `DAT_0438cc08` state machine (decomp
`docs/decompiled/all.c:86539-88178`):

- `cc08==0xf` is **shop-counter menu/cursor navigation** (haggling:
  `DAT_0438cc0c = (cursor ± 1) % count` from `dddd6 & 4/8`), with facing 6/2
  (left/right) from `da1d8 <= da1f0`.
- The player-**position** writes (`DAT_056da1d8 -= 0.125`, `da1e0 ± 0.05`) seen
  earlier are in the **scripted `cc08==4`** branch, not input-driven free-roam.

So `FUN_0048670f` may be largely the **selling-interaction** controller, with
top-down free-roam *walking* input in a different `cc08` sub-state or another
function. **Before W2 ports anything, confirm where free-roam walk-by-input
lives.** Fastest disambiguator: a Frida call-trace while *actually walking*
Recette around the shop on retail (the `--auto-z-spam` drive won't walk — needs
real directional input or a recorded movement trace). Alternatively a full
`cc08`-state decode. Do not port a "movement" chip against an unverified target
(dropped-FPU / int↔float mislabel risk — engine-quirks §53/§56 pattern).

## Verification

- **Per chip:** `nix develop --command make -C tests run` (host suite, 3010) +
  `nix develop --command make -C src` (both exes warning-free).
- **Milestone (after W3):** drive the port (`tools/run-openrecet.sh` HOUSE) and
  retail (`--capture-at-anchor HOUSE_FREEROAM+k` + a movement trace); compare
  with `tools/pixel_diff.py`; pop in `eog`. Regen `tools/regen-comparisons.py`
  once movement is live (it changes visible output, unlike the dormant leaves).
- **State parity (W4):** `tools/render-diff` / call-trace diff of the controller
  globals vs retail Frida ground truth.

## Critical files

- `src/scene1_player_ctrl.c` / `.h` — state, `scene1_player_ctrl_tick`,
  input→octant + walk-frame leaves.
- `src/scene1_sim.c:68` (`scene1_ingame_default_arm_tick`) — the tick hook.
- `src/scene1_shop_walker.c:528` (`sw_pass_light`) — draw side; **read-only**.
- `tests/test_scene1_player_ctrl.c` + `tests/test_main.c` — host tests.
- Decomp: `FUN_0048670f` `docs/decompiled/all.c:86539-88178`; verify step
  sizes / button masks against `objdump` of `vendor/unpacked/…exe`.
