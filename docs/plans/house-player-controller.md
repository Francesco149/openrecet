# HOUSE player controller — movement-first wiring plan

> Approved 2026-05-30. Progress: **W1 landed** (`fcb2a46`). **W2's
> verification question is RESOLVED (2026-05-30) by empirical TAS ground truth
> — and it overturns BOTH the W1 reframing and my own first (wrong) static
> conclusion.** Free-roam HOUSE walking **does** exist; it is gated behind the
> two new-game intro dialogue events, and it lives in **`FUN_0048b850` (Cpop)**
> — the movement controller (velocity from facing angle, walk anim, facing) —
> integrated by **`FUN_00483170`** (physics) and driven by `FUN_0048670f`'s
> controllable `cc08==1` state. This **re-validates** the original
> `project_next_char_controller` direction (continue Cpop). Confirmed via the
> anchor-segmented TAS trace + per-frame watch + differential call-trace
> (engine-quirks §60). See the "⚠ W2 — RESOLVED" section below.
> Narrative: `docs/PROGRESS.md`.

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

- **W2 — reach the controllable state deterministically. ✅ DONE via TAS
  (2026-05-30).** The blocker was never "where is movement" but "how to *reach*
  the controllable state past the two unported intro events." Solved by the
  anchor-segmented TAS trace (`traces/house_zspam.jsonl`): `wait HOUSE_FREEROAM`
  → Z-spam event1 → `wait HOUSE_FREEROAM` → Z-spam event2 → controllable. Holding
  UP then walks (pz `9.35→8.941`, walk anim). Empirically confirmed the movement
  controller (below).

- **W3 — free-roam *movement* ported + validated. ✅ DONE (2026-05-31).**
  Ground-truth-first (per the chosen approach): captured `runs/w3-walk-watch`
  (retail HOUSE walk-left, 15-global `--watch`) and decoded the full physics —
  accel 0.1, speed cap 0.175, damp 0.82, facing-octant ftol formula
  (cam-yaw −π), room-bounds clamp `FUN_00486435` (px≥−1.5/pz≤9.5). Implemented
  in `scene1_player_ctrl_tick` (no longer a stub): impulse→clamp→integrate→damp
  in engine order, so the end-of-frame state matches the retail per-frame watch
  to 1e-4. Pure leaves `player_ctrl_dpad_angle` / `_facing_octant` /
  `_house_room_clamp` + 6 new host tests (incl. the LEFT trajectory replay).
  Writeup: engine-quirks §61. **KEY correction to §60/W2:** the walk velocity is
  written through the player-struct pointer (`player+0x904`), not as the named
  `DAT_056daabc` — that's why it isn't in `FUN_0048b850`'s body (whose only
  sin/cos accumulate is the `da1bc`-gated stun/hop path, speed 0.3, not 0.1).

  Deferred: **W3b** — walk-cycle *frame* timing (the `chr_anim_tick` dt + the
  full `daae8` 11-dword record) needs a record-watch capture to validate
  frame-for-frame; today the cycle runs on the already-tested mechanism with
  dt=1.0. **W4** — furniture/mesh collision (`FUN_00483170`/`FUN_004830f1` +
  `FUN_00432e50`), the companion actor, and the real `cc08` controllable gate.

- **W4 — wire it into the live `cc08==1` controllable arm of `FUN_0048670f`**
  and diff the controller globals (position, velocity, facing, anim) vs retail
  Frida ground truth at matched anchors. Visible payoff: Recette walks under
  the arrow keys, matching retail.

## ⚠ W2 — RESOLVED (2026-05-30): free-roam IS real; it's `FUN_0048b850`, gated behind the intro

My first static pass concluded the HOUSE shop had *no* d-pad free-roam, from
decoding `FUN_0048670f`'s `cc08` branches (all of which route the d-pad to menus
/ scripted approach). **That was wrong** — and the failure mode is the lesson:
the controllable state runs only *after* two new-game intro dialogue events
(2D fixed-picture, then 3D-house-background), so the movement path is invisible
to a static read of the idle/scripted branches.

Empirical ground truth (anchor-segmented TAS drive + watch + differential
call-trace; engine-quirks §60): driving past both events and holding UP walks
the player (`pz 9.35→8.941`, anim→walk, facing→0) in state `cc08==1`. The
per-frame free-roam call set is `FUN_0048670f → FUN_0048b850 + FUN_00483170 +
FUN_0048a833 + FUN_00432e50 + …`; the **movement controller is `FUN_0048b850`
(Cpop)** (velocity from facing angle, walk anim, facing) and **`FUN_00483170`**
integrates it with collision. This **re-validates the original
`project_next_char_controller` Cpop direction** and corrects W1's "FUN_0048b850
is just effects" reframing — it is the movement controller.

Verification risk note: watch for dropped-FPU / int↔float mislabels in
`FUN_0048b850`/`FUN_00483170` (engine-quirks §53/§56 pattern); confirm the
sin/cos angle args + step sizes against `objdump`; validate every leaf with the
differential call-trace + position watch rather than trusting the decomp.

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
