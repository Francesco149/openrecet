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

## ✅ PHASE 0 RE COMPLETE (2026-06-07) → `docs/findings/town-map-RE.md`
The transition + the whole mode-8 world-map scene are RE'd from the decompile (cross-checked
vs the recording's anchors/inputs). Headlines that **correct** the recon below:
- **The town map = the WORLD MAP, top-level mode `DAT_0438b1c0 == 8`** (confirmed by the
  preload switch `all.c:51296` + the sim/render dispatchers). Assets = texture slot 10
  (`worldmap_nomal/yugata/night.bmp` + `mappoint.tga`), preload `FUN_004735ad`.
- **`FUN_0045281c`'s 2nd arg is a load-step COUNT, not a map id** — so `0x11/0x1e/0x3c/0x78`
  are durations; `0x11` is NOT "quit-to-title". The destination scene is chosen by the MODE.
- **Exit trigger = the shop door** (user-confirmed): door tooltip → Z-on-door. Handler in
  `house_update` `FUN_0048670f` `all.c:87637` → `DAT_074b2ec4=1` + dissolve-fade
  `FUN_004526f5(0,0x11)` + sets the tutorial flag `DAT_0450f3f9[slot]` → stage-2
  `DAT_0438b1c0=8` at `all.c:86877`. NOT the mode-9 manager `FUN_00453384` (that's other
  transitions). The "PAUSE_OPEN" anchor at the exit = the world map raising the SHARED cursor
  for its destination pointer (red herring confirmed).
- **Tutorial gating** (user-flagged) = per-dest state array `DAT_09643588[]` (0 disabled /
  1 normal / 2 highlighted-pulse) set by `FUN_0049de20` from `DAT_0450f3f9`/`DAT_0450f408`.
  In the recording (first exit) → **dest 3 (Market) highlighted, the rest disabled**.
- Scene fns: init `FUN_0049de20`, sim `FUN_0049e163`, cursor-nav `FUN_0049dfc1` (3×5 grid),
  render `FUN_0049e3a3`. Chip plan **T1–T5** in the findings doc (supersedes P1–P5 below).

The original recon (kept for history; some now corrected):

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
- **AUTO-WINDOW now targets the comparable free-roam segment (2026-06-07, verified via
  the real SPA `CaptureController`).** Earlier the anchored auto-window anchored at the
  LAST anchor — the **town** the shop-exit leads to, which the PORT can't reach → port
  captured 0 ("window never reached"). Now (`139d6bd`+`7803369`) it anchors at the FIRST
  **free-roam entry** (`HOUSE_FREEROAM`, reachable on both) and **caps the span a beat past
  the next scene-change** (`LOADING_START`) — i.e. exactly the walk → the door, the part
  both targets share. (`ops.raw_default_window` base+cap; `ops.first_freeroam_wait` +
  `ensure_window_ops(after_wait=)` place `{caprange}` after the first free-roam `{wait}`,
  MID-trace — that's fine, the parser accepts capranges in any segment; the earlier
  "final-segment only" belief was a red herring, the real bug was `{calltrace}` written as
  a bool instead of `[start,len]`.) Verified end-to-end through `POST /capture`: rerecord →
  **port 270 / retail 240**, caprange `[0,271]` (was port 0 / 1149-vs-383 span-to-end).
- **RE-CAPTURE self-heals a stale window (`8baf1fe`).** A session built before the above
  has a working trace whose window is FLAT or town-anchored; the SPA `/recapture` reuses
  the working trace verbatim, so it kept capturing 0. Now re-capture rebuilds from the
  recording whenever the window isn't in the first free-roam segment
  (`ops.window_at_freeroam`). NEEDS `source_trace` pointing at the recording — `139d6bd`
  stops re-capture clobbering it with the working-trace path; a session clobbered by a
  PRE-fix re-capture must be repaired (set `source_trace` back to the recording) or
  re-captured fresh. Verified: `POST /recapture` on a stale town-windowed session →
  `rebuilding … at the free-roam entry` → **port 270** (was 0).
- **RE-CAPTURE forces a retail re-capture when the window is rebuilt (`626949c`).** When
  the self-heal moves the window, a CACHED retail capture is from the OLD window. A
  port-only re-capture (`{only:port}`) would reuse it → port=new-window (shop walk) vs
  retail=old-window (the town) — the "retail starts at the town map" misalignment (it was
  NOT a 2nd-loading-screen anchor; the agent log shows retail armed at the first
  `LOADING_END`=`HOUSE_FREEROAM`=frame 14306). Now `window_rebuilt` forces `run_retail`
  even under `--only port`. Verified by frame: a port-only-recaptured session had retail
  `frame_00000`=town; a both-target re-capture flipped it to the shop (aligned with port).
- **Verified walk (`town-walk-debug`):** the free-roam walk is **1:1** port↔retail —
  pixel diff 0.06–0.55 meanabs through the walk, then a hard divergence at the door-Z
  (the unported transition). The port reproduces R@HF+51, U@HF+60, R@HF+132 → the door.
- The retail town ground truth is already captured (`town-map-load-fixcheck` retail 130,
  `town-rerecord-fix`/`-160153` retail ~240-249 + `call_trace.jsonl`) — for porting the
  town (Phase 0+), window there once it has its own free-roam anchor.
