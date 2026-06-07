# Plan — Port the shop-exit → TOWN MAP transition + the town-map scene

**Goal (user, 2026-06-07):** get the port to reproduce the **`town-map-load`
reference trace** end-to-end: Continue-load → shop free-roam → **walk/menu out of
the shop** → the engine fades + loads → the **TOWN MAP** (the overworld where you
pick destinations). The port currently stops dead at `HOUSE_FREEROAM`; the whole
shop-exit transition + the town-map scene are **unported**. Reference recording:
`runs/recordings/town-map-load.{raw.jsonl,save.bin}` (a Continue trace).

Full port, not MVP — port the whole transition + scene path, verify each chip 1:1 vs
retail via `trace_studio capture … --target both` + `flow_diff`/pixel-diff against the
captured retail ground truth.

## What unblocked this (2026-06-07)
The retail leg of a recorded trace used to die before running with
`BadGzipFile("Not a gzipped file (b'\xdaD')")` — `resolve_save` unconditionally
`gzip.open`'d the `{savefile}` ref, but a fresh recording's ref points at the **raw**
`.save.bin` (the retail drive resolves against the original recording). **Fixed in
`787cc51`** (`tools/trace_save.py`: sniff the gzip magic; pass a raw save straight
through). The `\xdaD` was just the `0x341944da` save-arena header magic. With that
fixed, a both-target capture now yields the retail ground truth:
- **`runs/trace-studio/town-map-load-fixcheck/`** — retail captured **92 town-map
  frames** (`retail/frames/`, `retail.mp4`) + a **24 209-line `retail/call_trace.jsonl`**.
- ⚠ The capture window starts at the **second `LOADING_END`** (post-load), so the
  call-trace covers the **town map only — NOT the transition frames** (the shop-exit
  fade/load). Phase 0 must widen the window to capture the transition.

## Recon findings (2026-06-07) — CONFIRMED vs HYPOTHESIS vs UNKNOWN

### The transition is an INGAME mode change via the universal fade manager
- **Top-level scene var = `DAT_0438b1c0`** (CONFIRMED). Values seen in the decompile:
  `0`=title (confirmed via the pause-menu RE), `1`=shop/ingame (dominant), and
  `6 / 8 / 9 / 0xb` (others). `8` is common (14 assignments).
- **`FUN_00453384` @ `0x453384` (821 B) = the scene-transition / fade manager**
  (CONFIRMED). Wrappers: `FUN_004536a8`→`FUN_00453384(0)`, `FUN_004536b9`→
  `FUN_00453384(1)`. It drives mode **9** (the transient fade/load mode): on entry it
  saves the prior mode in `DAT_06a499a8 = DAT_0438b1c0` (`all.c:50297`), bumps the
  fade counter `DAT_06a49998`; when `DAT_0438b1c0==9 && 0xb < DAT_06a49998` it
  **restores** `DAT_0438b1c0 = DAT_06a499a8` and re-inits the target scene
  (`FUN_004682d0`, `FUN_00435612`, `FUN_004844ef`, and one of
  `FUN_00473c03/00473668/00473672` keyed by the transition kind `DAT_06a4997c`
  0/1/2). The decompiled body is `all.c:50173-50316`.
- **Scene/map loads route through `FUN_0045281c(scene, map_param)`** (CONFIRMED as the
  loader; the quit-to-title path uses `FUN_0045281c(0,0x11)`). Observed map_params:
  `0x11` (very common), `0x78` (paired `FUN_0045281c(1,0x78)` + `(0,0x78)` at
  `all.c:81085/81095`), `0x1e` (`all.c:60393`). Which one is the town map = UNKNOWN.
- **Anchors at the shop-exit** (from the recording, retail ground-truth): on ONE frame
  both `LOADING_START` (`loading_active` 0→1) **and** `PAUSE_OPEN` (`DAT_0438b150` —
  the shared menu/cursor flag — 0→1) fire, then `LOADING_END` ~10 frames later. So the
  exit kicks a brief load **and** raises the cursor/menu flag on the same frame. The
  `PAUSE_OPEN` name is incidental (`b150` is the *shared* cursor used by pause/save/
  display menus alike — see the shop-display arc), NOT proof the pause menu is involved.

### Open questions = Phase-0 RE targets
- **HYPOTHESIS: the town map is the SAME ingame mode (`DAT_0438b1c0==1`) with a
  DIFFERENT map loaded**, not a distinct top-level scene. Evidence: the `scene_state`
  stayed `ANCHOR_SCENE_INGAME` (==1) across the exit — no `NEW_GAME`/title anchor
  re-fired (the anchor world snapshots INGAME from the top-level mode). The shop is
  `shop_1st.x`; the town map is a different map mesh. CONFIRM in Phase 0 (it could
  instead be mode `6`/`8`).
- **UNKNOWN: the exact exit TRIGGER.** Walk-to-door? A "Go Out" option on the shop's
  system/pause menu (`FUN_0047fa76` dispatcher — options 1/5/6 = `FUN_0047ff40 /
  FUN_004802cf / FUN_0049f365`, one may be "leave")? The `b150` rise + load on the
  same frame is the signal to chase. NOT captured by the current call-trace (window is
  post-load).
- **UNKNOWN: the town-map scene-id (`DAT_0438b1c0` value) + the `FUN_0045281c`
  map_param.**
- **UNKNOWN: the town-map scene's own preload / per-frame sim / render** functions
  (its meshes/sprites, the destination cursor, the day/gold HUD). The retail
  call-trace's town-map window shows only SHARED instrumented helpers firing once/frame
  (`0x404efc` quad-draw, trig `0x503xxx`, font, `0x4356cd` cursor anim, `0x406584`
  bob); the scene's unported logic isn't instrumented → needs fresh RE.

## Phase 0 — RE the transition + the town-map scene (findings doc first)
1. **Re-capture with a window over the TRANSITION.** Edit the trace's `{caprange}` (or
   add a capture window) to start *before* the shop-exit `LOADING_START` (anchor the
   window to the first `HOUSE_FREEROAM` + an offset, span through `LOADING_END`+N), so
   `--call-trace --d3d-trace-verts` captures the fade/load + the town-map arrival on
   BOTH sides. (Retail-only is fine for ground truth since the port can't follow yet —
   use the recording → `frida_capture`/`trace_studio` retail leg.)
2. **Frida-trace retail through the exit** (broad hook, not just the instrumented
   subset): confirm the trigger (door vs menu option), the `FUN_00453384` call + its
   `param_1`, the `FUN_0045281c(scene, map_param)` for the town map, and the resulting
   `DAT_0438b1c0` value. Pin `DAT_06a4997c` (transition kind).
3. **Decompile the town-map scene.** From the map_param + the restored mode, find the
   town-map preload + the per-frame tick + render (analogous to `scene1_preload` /
   `FUN_0048670f` / the scene1 render for HOUSE). Identify map meshes/sprites, the
   destination/cursor UI, the HUD.
4. **Write `docs/findings/town-map-RE.md`** (the transition mechanism + the scene
   structure + the chip plan). Update `docs/findings/INDEX.md`.

## Phases 1–N — port chips (each its own commit, verified vs retail where visible)
Provisional; refine after Phase 0. Likely:
- **P1 — exit trigger + `FUN_00453384` mode-9 fade.** Port the shop-exit trigger and
  the transition manager enough to *leave* HOUSE (mode 1 → mode 9 fade). Verify the
  port emits `LOADING_START`/`PAUSE_OPEN` at the exit like retail (anchors match).
- **P2 — `FUN_0045281c` town-map load.** Port the scene/map load for the town-map
  map_param: load the town-map meshes/assets, restore the target mode. Verify the port
  reaches the second `LOADING_END` + the town-map mode (no longer 0 frames).
- **P3 — town-map RENDER.** Port the town-map scene render; pixel-verify vs the 92
  retail frames (`town-map-load-fixcheck/retail/frames`) via `pixel_diff` / the studio
  diff ribbon. Likely reuses the ported quad/font/`FUN_00404efc` primitives.
- **P4 — town-map SIM / navigation.** Cursor + destination selection + the day/gold
  HUD; the per-frame tick. Verify state 1:1 via `flow_diff` (annotate the town-map
  funcs on both sides — that IS the comparison tool now).
- **P5 — full-trace replay.** Drive `town-map-load` `--target both`; confirm the port
  follows shop → fade → town map and the windows pair (port == retail kept-count),
  PHASE-CLEAN under `{phasepin}`+`{rngseed}`.

## Verification assets
- `runs/recordings/town-map-load.{raw.jsonl,save.bin}` — the reference recording
  (Continue → shop → exit → town map; 987 lines, anchors at the tail).
- `runs/trace-studio/town-map-load-fixcheck/` — the first post-fix both-capture: retail
  92 town-map frames + `retail.mp4` + `retail/call_trace.jsonl` (town map only). Port
  leg captured 0 (the gap this plan closes). Serve: `trace_studio.py serve --session
  town-map-load-fixcheck`.
- A Phase-0 transition-window re-capture (TODO, step 1 above) for the fade/load frames.

## Notes / gotchas
- The port load-pump is non-deterministic (plan `trace-studio-v2.md` Phase-1 finding) —
  the same recording gave port=835/retail=0 originally and port=0/retail=92 on the
  re-run. Pair captures may need a retry; pin with `{phasepin}`+canonical `{rngseed}`.
- A NEW segtrace op (if any needed) takes THREE parsers: engine `input_segtrace.c`,
  agent `segtraceBuildSegments`, retail harness `frida_capture.py` (~L983 `else`).
- Adding a town-map mode to the anchor set may want a new anchor (e.g. `TOWN_FREEROAM`)
  in `src/anchor_trace.c` — that's a clean window sync point for the studio.
- **INPUT REPLAY MUST BE ANCHORED (2026-06-07, verified).** A recorded Continue trace
  distilled FLAT replays inputs at boot-relative recording frames (walk at R@249/U@258/
  …Z@409=door), but the port's free-roam starts at a *different, non-deterministic* frame
  than the recording (port HF≈388-422 vs recording HF=198 — longer load), so the whole
  walk fires during the port's load and is LOST; the port's first free-roam input is the
  post-door town arrow → "holds up at ord 224", reproduces nothing. The **anchored**
  distil rebases inputs to anchor-relative (R@HF+51, U@HF+60, R@HF+132, Z@HF+211=door), so
  the walk replays correctly relative to free-roam regardless of load timing. **Tooling
  fixes:** `f3a70b3` (recordings auto-anchor), `b71cadd` (re-capture self-heals a stale
  FLAT working trace by rebuilding anchored from the manifest's `source_trace` — the SPA
  recapture passes the working trace, so the recording's anchors were invisible before).
  **A pre-fix session's working trace is FLAT** until a re-capture rebuilds it.
- **Verified walk:** `runs/trace-studio/town-walk-debug` (anchored, **HOUSE_FREEROAM**-
  windowed `{caprange [0,230]}`) captures the free-roam walk on BOTH sides (port 229 /
  retail 226 frames) — the port walks R→U→R to the door, diverging only at the door-Z
  (unported). Built by truncating the distilled trace at the shop-exit so the HF segment is
  LAST (the engine parser only accepts `{caprange}` in the final segment, and `{calltrace}`
  must be `[start,len]`, NOT a bool — `/tmp/build_walk_debug.py`). The auto-window anchors
  to the LAST anchor (town, port-unreachable → port=0); for a port↔retail WALK comparison,
  window on `HOUSE_FREEROAM` (the first anchor both sides reach).
