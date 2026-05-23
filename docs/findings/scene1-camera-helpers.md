# Scene-1 camera helpers — `FUN_00441c3e` + `FUN_004424e7` (Cc.0 survey)

> **Cc.1 erratum (2026-05-23, post-port):** The "Outputs FUN_00441c3e writes" section below has the eye/look-at labels swapped.  `FUN_0040120c` calls `D3DXMatrixLookAtRH(out, &DAT_073de31c, &DAT_073de328, &up)`; the standard signature is `LookAtRH(out, eye, target, up)`.  So `_DAT_073de31c..324` (the orbital triplet computed via `radius_xz * sin/cos(yaw)`) is the **EYE**, and `_DAT_073de328..330` (the input-bias triplet, no trig math) is the **TARGET / lookat**.  The existing `g_scene1_camera_anchor[2]` extern in `scene1_particles_tick.h` correctly aliases (target.x, target.z) — particles orbit the target (= player anchor), not the eye.  The Cc.1 implementation in `src/scene1_camera.c` uses the corrected names (g_scene1_camera_eye / g_scene1_camera_lookat).  HOUSE camera with char_mode=2 + yaw=0 lands at **eye=(-1, -6.2, -9), lookat=(-1, 3.0, -5)** (NOT the "(0, 0.3, 0) looking at (0, -9, -4)" claim in the original survey — that conflated the labels and missed the `bias_x = -1` clamp + the `bias_z = -5` uVar2>=2 lift).


**Engine fns:**
- `FUN_00441c3e` @ `0x441c3e`, **2217 B** — camera-pose update (eye + look-at).
- `FUN_004424e7` @ `0x4424e7`, **429 B**  — scene-angle update (yaw + pitch + 8-azimuth orientation matrix).
- `FUN_0040120c` @ `0x40120c`,  **116 B** — view-matrix builder (LookAtRH × Z-roll); called from the tail of `FUN_00441c3e`, drops the matrix at `DAT_073de29c`.

**Survey date:** 2026-05-23.  **Chip:** Cc.0.

These three functions are the **only blocker between "Cf.2 spawns 200 type-0x92 records at the player pose" and "first scene-1 pixels appear in HOUSE"**.  Today's smoke (post-Cf.2) confirmed end-to-end plumbing: CLI override → `scene1_postload_ambient_spawn` → 200 active records → `counter_scan` → Pass F gate.  Zero visible pixels because `g_scene1_view` stays identity (the stubs at `src/scene1_render.c:77-94`).  HOUSE default player pose `(-40, 2, -60)` is outside the 45° FOV at z=-60 (X half-width ≈ 33), so every particle renders off-screen left.

## Call graph

```
FUN_004547ab (render dispatch, INGAME branch)
  └─ FUN_0045bbf9 (scene1_render_camera_setup, ports as C7f)
       ├─ if (counter_998==0) FUN_00441c3e      [Cc.1 ← THIS SURVEY]
       │     └─ FUN_0040120c                    [Cc.1 ← inline port, 116 B]
       ├─ FUN_004424e7                          [Cc.1 ← THIS SURVEY]
       ├─ SetTransform(D3DTS_VIEW, &DAT_073de29c)
       ├─ ... build PROJECTION ...
       └─ FUN_00459dfd (scene1_render_meshes, ports as C8a)
```

In our port, `scene1_render_camera_setup` already hands `g_scene1_view` to D3D every INGAME frame (Cr.1, 2026-05-23).  The shim that needs to fill `g_scene1_view` is `scene1_camera_pose_TODO()` + `scene1_camera_angle_TODO()` — both are no-op stubs today.

The view matrix flow in the engine is:

1. `FUN_00441c3e` computes new eye + look-at, writes the 6 floats at `_DAT_073de31c..330`.
2. `FUN_00441c3e` calls `FUN_0040120c` at its tail.
3. `FUN_0040120c` reads `_DAT_073de31c..330`, builds the LookAtRH matrix, multiplies by a Z-roll from `_DAT_006051c4 / 2.0` (a `.rdata` roll-angle constant), writes the final 4×4 to `_DAT_073de29c`.
4. Next frame, `FUN_0045bbf9` calls `SetTransform(VIEW, &_DAT_073de29c)`.

`FUN_004424e7` is independent of step 1-4: it reads `_DAT_073de31c..330` (set by step 1) and derives yaw/pitch (`_DAT_0438cd78`, `_DAT_0438cdb8`) + a separate 4×4 orientation matrix (`_DAT_0438cdf8`) used by **other** consumers (HUD layers, fx_tail, 2D overlays).  Engine calls it unconditionally after the pose helper.

## `FUN_00441c3e` — block breakdown

| Block | Lines (decomp) | Purpose | Cc.1 scope |
|-------|----------------|---------|------------|
| **A. stage-mode read** | L39842-39843 | `uVar2 = *(uint *)(&DAT_045105a4 + DAT_0438b1e0 * 0x2dfc8)` — per-something (per-character record?) at stride 188 KB.  Gate flag for camera mode in subsequent blocks.  In HOUSE, `DAT_0438b1e0=0` ⇒ reads `_DAT_045105a4` directly. | yes — read the value, defer "what's at +0" to a small lookup |
| **B. stage-class offsets** | L39844-39865 | Four-way switch on `DAT_0438b4e8` (stage-class flag, BSS-zero ⇒ value 0 for HOUSE).  Writes `_DAT_0438b77c/74/78` (camera offset triplet) from `.rdata` constants `_DAT_005c4fd0/d4/d8` minus stage-class-specific biases. | yes — class-0 path only (`DAT_0438b4e8==0`, sub-case `uVar2<2`) ⇒ all three offsets = 0.0 |
| **C. floor height bias** | L39866-39879 | `_DAT_06a46f9c = lerp(_DAT_06a46f9c, max(palette+0x1b1c, DAT_056da1dc-4) , 0.1) + 0.3`.  First-frame branch via `DAT_0438cc68`. | yes — depends only on palette field (HOUSE: 0) + a stage-counter (BSS-zero); collapses to `0.3 + lerped 0.0 ≈ 0.3` |
| **D. stage-class-1 fast path** | L39880-39888 | `DAT_0438b4e8 == 1` (post-load animated transition).  Reads `_DAT_0438cc38..58` directly into locals + pins `_DAT_06a46f9c = 3.0`. | **defer to Cc.2** — only fires on stage transitions, not steady HOUSE |
| **E. default-path eye/look-at** | L39890-39957 | The main body.  Reads input-derived camera offsets `DAT_056da1d8/dc/e0` (BSS-zero in HOUSE — no input bind), applies blend with `_DAT_0438b7a0/a8` if `DAT_0438b7ac` non-zero (BSS-zero), clamps to per-class limits via 4-way switch on `(&DAT_068dd3fc)[stage*0x6cf]` (per-stage view-mode; **need to read the byte for HOUSE — likely 0**). | yes — HOUSE simplifies to `local_10 = local_8 = 0` with all clamps no-ops |
| **F. cinematic counter** | L39960-39969 | `DAT_0438be94` ramps 0..240 to a 0..5.0 sin-curve boost.  BSS-zero ⇒ `local_c = 0` always. | **defer to Cc.4** — only fires on scripted camera moves |
| **G. final eye/look-at compose** | L39970-39981 | Uses camera-yaw `_DAT_073de39c` (already exposed as `g_scene1_camera_yaw`) to orbit the eye around the look-at.  `local_104 = (offset + 0 + 0) * sin(yaw) + 0`, `local_fc = 0 - (...) * cos(yaw)`.  In HOUSE with offsets=0 this also collapses; **what makes the camera non-trivial is `_DAT_0438b774` (set in B) + `_DAT_0695ef70` (stage-record-relative-distance, separate writer)** — at boot one or both must be non-zero or the camera sits inside the player.  Likely `_DAT_0438b774` defaults non-zero (engine `.data` initialiser, not BSS); resolve at port time via `objdump -s`. | yes — verbatim compose; the offset constants resolve via `.data` initialisers, not stand-ins |
| **H. smoothing lerp** | L39982-39998 | If `DAT_0438cc68 == 0` (BSS-zero ⇒ true for steady-state): lerp the stored eye + look-at (`_DAT_0438cc38..58`) toward the computed values at rate 0.2.  Else: direct copy + clear the flag. | yes — both paths needed (the first-frame branch sets the initial state) |
| **I. canonical state writeback** | L39999-40007 | `_DAT_073de31c..330 = _DAT_0438cc38..58 + shake.y` (shake added on first writeback, before the recompute at LAB_004424b9 — see block L). | yes — verbatim |
| **J. debug HUD overlays** | L40008-40021 | Four `FUN_00451874` calls (HEIT / DIST / ??? / MIPMAP).  Pure visual debug. | **defer to Cc.3** — no functional effect on rendering |
| **K. view matrix build** | L40022-40047 | Two `thunk_FUN_004a3462` calls (D3DXMatrixRotation builders) + `thunk_FUN_004a2a03` (matmul) → local matrices `local_238` / `local_278` / `local_1f8`.  Then `FUN_004a3b52(&DAT_0438ce38, &local_38, &local_20, &local_2c)` builds a separate matrix at `DAT_0438ce38` (likely shadow-projection matrix; consumer not yet found).  Final `atan2(local_1c8, local_1c0)` → `_DAT_0438ce78` (a derived angle stored for 2D layers). | **scoped subset to Cc.1** — the `_DAT_0438ce38` matrix has no current consumer (port = no-op); the `_DAT_0438ce78` atan2 result is read by C8c shop walker (already ported), so write it for completeness |
| **L. shake counter** | L40048-40069 | `_DAT_0438cc14` decrements; on each non-zero tick randomizes `_DAT_0438cc18/1c/20` from `_DAT_0438cc24/28/2c` amplitudes.  All BSS-zero today ⇒ counter never armed. | **defer to Cc.2** — gates on `DAT_0438cc14 > 0`, which has no writer in our port |
| **M. tail apply + view build** | L40070-40073 | Add shake.y to eye.y + lookat.y, then call `FUN_0040120c` (the view-matrix builder). | yes — verbatim |

### `FUN_0040120c` (the view-matrix builder, 116 B)

```c
local_up = (0, 1, 0);
FUN_004a3b52(&DAT_073de29c, &eye, &lookat, &local_up);  // D3DXMatrixLookAtRH
FUN_004a3670(local_50, _DAT_006051c4 / 2.0);            // D3DXMatrixRotationZ from .rdata constant
FUN_004a2a03(local_90, &DAT_073de29c, local_50);        // D3DXMatrixMultiply
// memcpy local_90 → DAT_073de29c (16 dwords)
```

So the engine view is `LookAtRH × RotZ(roll/2)`.  The roll constant `_DAT_006051c4` is in `.data` (initialised at static-init time, not BSS-zero); need an `objdump -s` read to confirm the literal.  If it's 0, the multiply is a no-op and `mat4_lookat_rh(g_scene1_view, ...)` suffices.  If non-zero, we need a Z-rotation followed by a matmul (both already in `math3d.h` as `mat4_rotation_z` + `mat4_mul`).

**Port plan:** inline FUN_0040120c into `scene1_camera_pose` since it's the only caller and 116 B.

### Inputs FUN_00441c3e reads (HOUSE-default state)

| Symbol | Type | HOUSE value | Source / Notes |
|--------|------|-------------|-----------------|
| `DAT_0438b4dc`           | int | 0 | Current stage index (we have `g_stage_index`-equivalent in `stage_state.c`) |
| `(&DAT_068dd3fc)[stage*0x6cf]` | int | **TBD via objdump -s** | Per-stage "view mode" (0..4 expected); `0x6cf` = 1743 B stride per stage record |
| `DAT_0438b1e0`           | int | 0 (BSS-zero) | Per-character index for the `0x2dfc8` stride |
| `(&DAT_045105a4)[char*0x2dfc8]` | uint | **TBD via objdump -s** | Per-character record field (the `uVar2` gate; values 0/1/>1) |
| `DAT_0438b4e8`           | int | 0 (BSS-zero) | Stage-class flag (0/1/2/3); 1 = post-load anim, 2/3 = unknown |
| `_DAT_005c4fd0/d4/d8`    | float×3 | **TBD via objdump -s** | `.rdata` camera-offset constants (likely small numbers like 0/0/0 or 0/18/12 depending on context) |
| `DAT_056da1d8/dc/e0`     | float×3 | 0 (BSS-zero) | Input-derived camera offsets; written by an input/sim function not yet ported |
| `DAT_0438b7ac`           | int | 0 (BSS-zero) | Cinematic blend flag |
| `_DAT_0438b7a0/a8`       | float×2 | 0 (BSS-zero) | Cinematic blend reference |
| `DAT_0438be94`           | int | 0 (BSS-zero) | Cinematic counter (0..240); BSS-zero ⇒ `local_c = 0` always |
| `_DAT_069b2f78`          | float | 0 (BSS-zero) | Stage-record-distance scratch #1 |
| `_DAT_0695ef70`          | float | **TBD via objdump -s** | Stage-record-distance scratch #2 (might be `.data`-init, not BSS) |
| `_DAT_044e2c70`          | float | 0 (BSS-zero) | Y-bias scratch |
| `_DAT_073de39c`          | float | 0 (BSS-zero) | Camera yaw — exposed as `g_scene1_camera_yaw` |
| `DAT_0438cc68`           | int | 1 at boot? | "First-frame" flag — needs initial write somewhere |
| `DAT_068dd2ec`           | int | 0? | Some palette/stage flag |
| `DAT_073dfcf0`           | DWORD | 0 | MIPMAP LOD bias (debug HUD only) |
| `_DAT_0438cc14`          | int | 0 (BSS-zero) | Shake counter; gates the whole shake block |

### Outputs FUN_00441c3e writes

- `_DAT_0438b77c/74/78` — camera offset triplet (per-class).
- `_DAT_06a46f9c` — smoothed floor-height bias.
- `_DAT_0438cc38/3c/40` — smoothed look-at (x/y/z).
- `_DAT_0438cc50/54/58` — smoothed eye (x/y/z).
- `_DAT_0438cc68` — clears on first-frame branch.
- `_DAT_073de31c/320/324` — canonical look-at (x/y/z, with shake.y added).
- `_DAT_073de328/32c/330` — canonical eye (x/y/z, with shake.y added).
- `_DAT_0438cc18/1c/20` — shake offsets (x/z/y); BSS-zero gated.
- `_DAT_0438ce38` (4×4) — shadow/light projection (no current consumer in our port).
- `_DAT_0438ce78` — derived 2D-layer angle.
- `_DAT_073de29c` (4×4) — final view matrix (built by `FUN_0040120c`).

## `FUN_004424e7` — block breakdown

| Block | Lines | Purpose | Cc.1 scope |
|-------|-------|---------|------------|
| **A. delta + singular guard** | L40093-40097 | `dx = eye.x - lookat.x; dz = eye.z - lookat.z; if both zero, dz = 0.01` | yes |
| **B. yaw + horizontal dist** | L40098-40099 | `yaw = atan2(dx, dz)`, `dist = sqrt(dx²+dz²)` | yes |
| **C. zero-dist guard** | L40100-40102 | If `dist == 0`, call `FUN_00404bb8` (engine error) | yes — port the guard; replace error call with stub log |
| **D. pitch compute + write** | L40103-40107 | `pitch = atan2(dist, eye.y - lookat.y); _DAT_0438cd78 = π/2 - pitch; _DAT_0438cdb8 = yaw + π` (via `thunk_FUN_004a3537/35d3` setters) | yes |
| **E. orientation matrix** | L40108 | `thunk_FUN_004a2a03(&DAT_0438cdf8, &pitch, &yaw)` — builds 4×4 orientation matrix at `DAT_0438cdf8` from the two angles | yes — **consumed by `scene1_wide_followup` Pass D / Pass F** (already-ported C8f.1; per-record matrix chain `T × S × DAT_0438cdf8 × RotY(...)`).  Required for wide-followup to render meaningful billboards. |
| **F. 8-azimuth loop** | L40109-40120 | Loop 0..7 calling `sin(i*π/4)`, `cos(i*π/4)`, `sin(yaw+π)`, `cos(yaw+π)`.  **All return values dropped by Ghidra** — the loop only side-effect is incrementing `_DAT_0438bfa8` to 8.  This looks like Ghidra dropped the actual writes; raw asm read recommended. | yes — port the counter increment (`_DAT_0438bfa8 = 8`); raw-asm the inner side-effects |
| **G. smoothed sample count** | L40121 | `_DAT_0438bfac = lerp(_DAT_0438bfac, 8, 0.1)` | yes (one line) |

**Why bundle Cc.1 with FUN_004424e7:** The angle helper is called unconditionally right after `FUN_00441c3e` in `scene1_render_camera_setup` (engine `FUN_0045bbf9` L11).  Skipping it leaves `DAT_0438cd78/cdb8/cdf8` BSS-zero, which downstream code reads — `scene1_shop_walker` already references the yaw, and `scene1_render_fx_tail` (already ported as C7g) may read pitch.  Landing both together avoids partial state.

**Pending human checks added by Cc.1:**
- **#10** — raw-asm read of FUN_004424e7's 8-azimuth loop (block F) to identify what the dropped sin/cos return values feed into.  Defer until a consumer of `_DAT_0438bfa8` appears.

## Cc chip ladder

| Chip | Scope | Size | Why |
|------|-------|------|-----|
| **Cc.0** | Survey doc (this file) | — | Block-by-block decomp of both functions + dependency map. ✅ landed 2026-05-23 |
| **Cc.1** | Default-path camera pose + angle helper + view-matrix builder (HOUSE-faithful, no shake / no debug / no cinematic / no class-1 transition) | ~250 LoC across `scene1_render.c` + new `scene1_camera.{c,h}` | The chip that unblocks visible scene-1 pixels. Includes FUN_0040120c (116 B) inlined.  Resolves the `.rdata` / `.data` constants via objdump at port time.  Wires `scene1_camera_pose_TODO` / `scene1_camera_angle_TODO` to real bodies. |
| **Cc.2** | Stage-class-1 transition + camera shake | ~100 LoC | Class-1 path (block D) + shake block (L).  Dormant on BSS-zero writers today.  Follow-up when stage transitions or scripted shakes need to ramp. |
| **Cc.3** | Debug HUD overlays | ~50 LoC | The four FUN_00451874 calls (HEIT/DIST/???/MIPMAP).  Pure debug; can stay deferred indefinitely. |
| **Cc.4** | Cinematic counter + sub-frame phase counter consumers | ~30 LoC | Block F + the dropped `_DAT_0438bfa8` consumer search.  Lowest priority — no caller writes `DAT_0438be94` in our port today. |

After **Cc.1** lands:
1. The default INGAME render frame produces a non-identity view matrix.
2. The Cf.2 smoke (`--force-ambient-spawn --ambient-spawn-type 0x92` + title-z-press) should immediately surface visible color-cycle billboards at the player pose.
3. The postload-path MVP visual smoke becomes possible — analogous to C8g.2 MVP but through the production spawn path, not the manual injection.
4. The 14 walker stubs in `scene1_shop_walker.c` + `scene1_wide_followup.c` start drawing *real* geometry the moment their bodies port (no longer blocked on identity view).

## Open questions for Cc.1 port (some resolved 2026-05-23 during Cc.0)

### Resolved

1. **`_DAT_005c4fd0/d4/d8`** ✅ — three `.rdata` floats: `-1.8f / 14.0f / 21.0f`.  Resolved via Python PE parser; matches the L39851 expression literally (`-1.8 - _DAT_005c4fd0 = 0`, `18.0 - 14.0 = 4.0`, `11.8 - 21.0 = -9.2`).

2. **`_DAT_006051c4`** ✅ — **BSS-zero**.  The unpacked exe's .data section has `vsize=0x9126838` (152 MB virtual) vs `rsize=0xdbe00` (initialised); everything past VMA `0x603e00` is loader-zeroed BSS.  `0x6051c4 > 0x603e00`, so `_DAT_006051c4 = 0` at load time.  ⇒ Z-roll angle `0 / 2.0 = 0` ⇒ `RotZ(0) = identity` ⇒ **the matmul in `FUN_0040120c` is a no-op**.  Cc.1 can skip the matmul entirely and write `mat4_lookat_rh(view, eye, lookat, up)` directly.

3. **`(&DAT_068dd3fc)[0 * 0x6cf]`** ✅ — per-stage view-mode for HOUSE.  Address `0x68dd3fc > 0x603e00`, so it's **BSS-zero** = `0`.  Value 0 IS in [0..4] (the gate at L39840-41), so block A's `uVar2` *does* get read from the per-character table.

### Unresolved (port-time work)

4. **`(&DAT_045105a4)[0 * 0x2dfc8]`** — `uVar2` for character index 0.  Address `0x045105a4` is **outside any mapped section** of the unpacked PE (text ends 0x51458e, this address is **below** that).  This must be a runtime-allocated block at a fixed address (engine does `VirtualAlloc(0x045105a4, ...)`-style mapping on character/scene init).  Empirically: the camera path we need (HOUSE shop view with non-degenerate eye/look-at) requires `uVar2 >= 2` so that block B picks up the (0, 4, -9.2) offsets.  **Port-time recommendation:** expose `g_scene1_camera_char_mode` as a stand-in global, default to `2` (= "shop view" — the only HOUSE-relevant value), document the unresolved per-character allocation in pending-human-check #11.  When the per-character init code ports, replace the stand-in.

5. **`_DAT_0438cc68`** — first-frame flag.  Address `0x0438cc68 < 0x528000` so **also outside the static image**.  Same caveat as #4 — written by per-stage / per-scene init code at runtime.  **Port-time recommendation:** initialise to `1` via `scene1_camera_init()` (new init hook called from `main.c` after `scene_post_fade_init` wires the `worker_load_set_cb`); the flag auto-clears on first pose-helper run (block H L39991).

6. **Existing consumer check (done):**
   - `_DAT_0438ce38` 4×4: no consumer in our port ⇒ drop the build in Cc.1.
   - `_DAT_0438cdf8` 4×4: consumed by `scene1_wide_followup` Pass D + Pass F ⇒ **must** be written by Cc.1's `FUN_004424e7` port.

7. **`FUN_004a3b52` arg shape** — at its `FUN_0040120c` call site the args are `(&DAT_073de29c, &eye, &lookat, &up)` — matches `D3DXMatrixLookAtRH(out, eye, at, up)`.  At the `FUN_00441c3e` block K call site (`&DAT_0438ce38, ...`) we're dropping the call anyway, so the alternate signature (`D3DXVec3TransformCoord`) doesn't need disambiguation for Cc.1.

### Why HOUSE camera = (0, 4, -9.2) offset from player

With #1-3 resolved and #4 stand-in'd at 2:

- Block A: `uVar2 = 2` (stand-in for the per-character "shop view" mode).
- Block B (`DAT_0438b4e8==0, uVar2>=2`): `_DAT_0438b77c=0, _DAT_0438b774=4.0, _DAT_0438b778=-9.2`.
- Block C: `_DAT_06a46f9c → 0.3` (floor height bias).
- Block G with BSS-zero camera-yaw (0):
  - `local_104 = (4.0 + 0 + 0) * sin(0) + 0 = 0` (look-at.x)
  - `local_100 = -9.2 + 0 + 0 + local_f4 = -9.2 + eye.y` (look-at.y; reads stale value first frame, lerps in over ~5 frames)
  - `local_fc = 0 - (4.0 + 0 + 0) * cos(0) = -4.0` (look-at.z)
- Block I: writes canonical `eye = (0, 0.3, 0) + smoothed`, `lookat = (0, eye.y - 9.2, -4.0) + smoothed`.

So **the engine's HOUSE camera sits at the world origin looking ~9° downward toward `(0, ~-9, -4)`**.  The player position (-40, 0, -60) is well off to the left and far behind — which means **even with Cc.1 landed, the Cf.1 ambient spawn at the player pose won't be on-screen**.

This is a critical finding.  The ambient-spawn smoke needs additional plumbing to see pixels:

- **Option A:** port the `DAT_073de31c` writer that re-anchors the camera to the player pose (probably in the unported `FUN_004012cc` family or a per-stage init).
- **Option B:** add an `--ambient-spawn-pose <x>,<y>,<z>` CLI override that places the spawn at the camera's view origin (e.g. (0, 0, -10)) for smoke purposes.  Small CLI extension to Cf.2's family.
- **Option C:** accept that Cc.1's smoke is "camera is correct + Pass F state pipeline is correct, but particles spawn at the engine-canonical player pose which is off-frame" and validate Pass F via `--show-pass-f-test` (already proven) until the player-pose anchor connects to the camera.

**Port-time recommendation:** land Cc.1 + add `--ambient-spawn-pose` (~10 LoC CLI extension).  Together they give the first scene-1 visible pixels through a fully-engine-correct pipeline (just with a debug-only override on spawn pose).

### Open question: is `DAT_073de31c` actually decoupled from the player pose?

The decomp suggests so — `_DAT_073de31c..330` is *output* of `FUN_00441c3e`, not input.  The function builds eye/lookat from class-offset constants + input-derived deltas (BSS-zero) + camera-yaw — **none of which reference the player pose directly**.

If that's correct, the HOUSE camera is **fixed at world origin** regardless of where the player is — which would mean Recettear's HOUSE / shop scene literally renders from a fixed POV and the player walks around within it (consistent with the actual game: the shop interior is one static room).  The "off-screen" particles smoke result is then **expected** — ambient particles only show up in the HOUSE shop view if their spawn pose intersects the camera's frustum, and the engine's HOUSE-entry initial spawn at the player's spawn point is intentionally low-density local effect that may or may not be visible depending on where the player happens to be relative to the camera.

This would also explain why the engine's HOUSE camera helpers are so static — the dungeon scenes use per-tile / per-room cameras (the L39842 `&DAT_045105a4 + DAT_0438b1e0 * 0x2dfc8` stride = per-area record), but HOUSE is a single fixed view.

## How to validate Cc.1 lands cleanly

1. Build + run host tests (small new tests are useful: `mat4_lookat_rh` integration through `scene1_camera_pose_compute_house`, sin/cos-driven `_DAT_0438cdf8` matrix shape, etc. — algebraic invariants the host *can* check.  Expect ~10-15 new tests).
2. Re-bless title-z-press if frames 90+ now show pixels (they should — once `--ambient-spawn-pose 0,0,-10` is added per option B above, Pass F billboards become visible; without that, they spawn at HOUSE player pose which is off-frame from the world-origin HOUSE camera).
3. Re-run the Cf.2 smoke with the new pose override: `--force-ambient-spawn --ambient-spawn-type 0x92 --ambient-spawn-pose 0,0,-10 --turbo --silent-audio`.  Should surface 200 drifting color-cycle billboards in the camera frustum.
4. Capture a new scenario `tests/scenarios/house-ambient-spawn` if the visual is clean.  Note: this captures with the debug pose override; not byte-equivalent to retail until per-stage camera anchoring ports.
5. Boot-idle scenario should remain bit-exact (no camera_setup runs in TITLE state).
6. title-z-press: even without `--force-ambient-spawn`, frames 90+ will now have a non-identity view matrix.  No visible difference vs current golden (all walker bodies still stubs), but the SetTransform state is different.  Should remain bit-exact because nothing draws under the new view.  Confirm with a hash diff against current goldens.

## What's still blocked after Cc.1

The 14 walker stubs in `scene1_shop_walker.c` + `scene1_wide_followup.c` stay dormant on BSS-zero table B/C counts.  The mesh-side scene draws (walls/floor/jutan/table) don't paint until at least one walker body ports.  But the **camera-correct empty scene** is a meaningful intermediate state — confirms the view + projection pipeline, lets Pass F paint, and unblocks any future walker chip from being "is this dormant because the data's missing OR because the camera's broken?".
