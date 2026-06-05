<!--
  The ONE hand-edited status block.  tools/gen_port_ledger.py injects everything
  below the marker line verbatim into docs/STATUS.md's "Current front" section, so
  STATUS can never drift from reality.  Update THIS when the active front moves;
  keep it short (a 60-second read).  Everything else in STATUS is derived from code.
-->
<!-- FRONT:BEGIN -->
- **Phase:** Foundation for frame-by-frame 1:1 parity (plan: `plans/` — render-parity
  diff engine + knowledge reorg + durable proof ledger), then resume the 1:1 sweep from
  frame 0 of the main menu.
- **Active work:** Phase 1 — the render-parity diff engine. **Vertex capture LANDED**
  (2026-06-05): both sides (`src/d3d_trace.c` + Frida agent) capture per-draw vertex bytes
  under `--d3d-trace-verts`; `tools/render_diff.py --explain` FVF-decodes aligned draws and
  names the first divergent **(vertex, field)** (e.g. `vertex 2 POSITION.z: retail -7.2 port
  -6.5`). Validated: synthetic field/count/structural/color paths + port↔retail decode the
  same screen corner on the title. Schema: `findings/d3d-trace.md`; usage:
  `findings/render-diff.md §--explain`. **Stable texture identity LANDED (2026-06-05):**
  `SetTexture` now carries a load-stable **`tex_name`** (source asset path) on BOTH sides —
  port via a `texture*→name` registry (`src/d3d_tex_names.c`, populated at `sprite_load_impl`,
  host-tested), retail via Frida hooks on the loaders `FUN_0047193c` (UI) / `FUN_00471b24`
  (mesh). `render_diff._event_key` keys texture identity on the NAME when present (order- and
  pointer-independent), falling back to the opaque pointer otherwise. Validated on `boot-idle`:
  the four title textures align by name across disjoint pointer values; retail's extra
  `nowloading.tga` bind surfaces by name instead of hiding in pointer noise. **Phase 1
  complete.** (Also fixed a `scenario-test` footgun: `wslpath_w` left a relative `--run-dir-root`
  output path relative → traces silently landed in the exe's cwd; now resolved to absolute.)
- **Execution + dataflow trace LANDED (2026-06-05) — the PRIMARY divergence drill-in.**
  d3d `--explain` names the wrong *draw*; this names the *logic cascade* that produced the
  wrong state. The port call-tracer carries declared payloads (`CALL_TRACE_BEGIN/FIELD/END`,
  per-frame `seq`); the Frida agent reads the same-named fields from retail per
  `tools/flow/retail_fields.json`; `tools/flow_diff.py` aligns the per-frame call CHAIN by
  `seq` and names the first call whose inputs matched but output/state diverged
  ([chain]/[data]). Plan + workflow: `plans/execution-flow-trace.md`. Coverage grows with
  the sweep — each touched function declares its fields on both sides.
- **Unified harness LANDED (2026-06-05):** `tools/scenario-test.py <scn> --target both
  --call-trace --d3d-trace --d3d-trace-verts` = ONE command for a synced port↔retail capture
  (save-virtualized, aligned, **forced 17ms/frame 1:1 timestep** both sides). Flow-trace
  frame attribution is clean (scheduler→sim→render in seq order). See CLAUDE.md "Run/build".
- **Phase 2 — IN PROGRESS. `boot-idle` title frame is now STRUCTURALLY 1:1 (2026-06-05):**
  `flow_diff --mapped-only` reports **✓ chain + data aligned (40 vs 40 calls)** on frames 30
  AND 60 — the port's instrumented title call chain + the data through it match retail.
  Three landings got there: (1) **render leg** — `render_quad_add`/`flush` carry per-quad
  dst/src/tex/diffuse + vcount both sides; the title's quads are bit-1:1 in geometry/UV/
  texture; the "menu too bright" bug fixed (denormal `0x95`→`0x5f`, engine-quirks §97).
  (2) **SIM leg** — `scene_title_sim` (0x49a59e) declares its 10 persisted menu-state fields
  (frame_counter/cursor_pos/cursor_anim/select_phase/pulse_phase/menu_folding_out/submenu_*/
  settings_dirty/fade_counter) on both sides via the new **`CALL_TRACE_BEGIN_STUB`** (field-
  bearing + `"stub":true`); verified bit-1:1 (frame_counter==pulse_phase==frame index,
  menu_folding_out=1, else 0). (3) **render BATCHING fixed** — the port flushed per-quad;
  retail batches same-texture groups (FUN_0049c644: menu items → one vcount=24 flush under
  ADDSIGNED, decoration tiles → one vcount=18 under MODULATE; standalone bg images flush
  per-quad in retail too). Split `title_quad`→`title_quad_add`(no flush)+`title_quad`; port
  flush vcounts are now `[6,6,6,6,24,18]`, bit-identical to retail (pixel-benign — engine-
  quirks §98). Tooling notes: `input_poll` (0x47b73c) marked **`chain_benign`** (the TAS
  harness substitutes synthetic input, so the port never runs the engine's DirectInput poll);
  Frida arg indexing is **0-based**. **Pixel parity:** `boot-idle` frames 0/30/60 are **0-px
  port-vs-LIVE-retail** (bit-identical); `golden-retail` was re-blessed 2026-06-05. (The old
  "benign 896px FPS overlay" was a STALE golden — captured on a day retail's runtime `dispfps`
  gate read 0 so the bottom-right FPS box baked in; live retail draws no FPS today, the
  function just early-outs. The 2026-05-27 benign-divergence-registry note no longer
  reproduces.) **Remaining title gaps:** only un-probed retail-internal funcs (CRT/MCI/audio
  — coverage gaps, not divergences).
- **Title scenarios WITH input verified 1:1 (2026-06-05):** drove the three menu-nav
  scenarios `--target both --call-trace`, diffed every frame with `flow_diff --mapped-only`.
  • **`title-down-press`** — DOWN steps cursor_pos 0→1 bit-identically on both sides;
  frames 30/35/50 ✓ chain+data aligned. • **`title-z-press`** — select countdown +
  dispatch + fade-out all ✓ aligned AFTER fixing a real bug: the selected-item brightness
  pulse used scale **127** (a guess); the .rdata constant at `0x519468` is **−128.0**
  (commit a4da502, engine-quirks §99). It diverged by exactly 1 LSB (port 0xf9 / retail 0xfa
  on the NEW GAME glyph) on the *one* countdown frame where `sin·scale` crossed an integer
  boundary. Root-caused by Frida-reading retail's render-time pulse counters (identical to
  the port → ruled out a phase offset) then objdump'ing the constant. Post-dispatch frames
  (73/90/92+) diverge into the **unported new-game INGAME scene** (port shows a placeholder —
  expected coverage gap, per scenario.yaml). • **`title-options`** — DOWN×2 nav + the whole
  settings panel (bg, all six rows, slider values) are 1:1 — **including the hand cursor,
  now PORTED (2026-06-05).** `FUN_00435747`'s body landed in `title_save_dialog_cursor_render`
  (40×40 hand from nowloading.tga, src (192,0)-(232,40), dst `(168−|sin(b154·0.1)|·8,
  row·40+148)`), driven by the shared cursor state (`title_save_dialog_cursor_snap/slide/
  set_visible` = `FUN_00435693/710/61a/612`): the OPTIONS dispatch snaps it to row 0, settings
  UP/DOWN slide it (6-frame ease), exit hides it. `flow_diff` frames 39/60 = **✓ 174 vs 174
  aligned**; the cursor quad is **bit-1:1 incl. the bob position** (port==retail (161.946,148)
  / (166.543,148)) — the existing `anim_tick` (`FUN_004356cd`) already increments `b154` every
  title frame so the bob phase lines up for free. boot-idle/down/z unaffected (cursor gated
  off; visible defaults 0).
- **Skip-prompt cursor unified + phase-pinned to bit-1:1 (2026-06-05, user-confirmed).** The
  Yes/No choice box now drives the ONE shared cursor (`title_save_dialog_cursor_snap/slide` +
  `title_save_dialog_cursor_render` = `FUN_00434def`/`ed2`/`435747`) instead of an inline copy
  (commit f9708c7). Confirmed against the decompile: retail's `FUN_0043537e` draws banner+text,
  a *separate* `FUN_00435747` draws the cursor off the shared `abf4/abf8`+`b150` globals.
  Three things got it to 1:1: (1) **drive via the real ESC dispatch** — the trace's `{esc:2}`
  op replays `esc_pressed()` on BOTH sides (port `segtrace_esc_cb`; retail `synthesizeEscRetail`);
  the `OPENRECET_FORCE_SKIP_AT` env hack is **gutted** (TAS is the universal trigger — add
  anchors, not hacks). (2) **tick the bob every INGAME frame** — `b154` is advanced in sim.c
  mirroring retail's `FUN_00406584` (was frozen through the prologue). (3) **{phasepin} now
  zeroes `b154`** (cb4002c) — `DAT_0438b154` free-runs from boot with no reset (engine-quirks
  §100), so its absolute value is load-dependent (§85); pinning it at `TEXT_ANIM_END` makes the
  bob bit-1:1. Two port runs with different absolute frames are now bit-identical. **Residual
  box delta:** ±1 LSB on the gold banner (port gold ~1 darker — 2D-UI colour/texture precision,
  the same `ledger #52` box-edge-halo class; geometry/UVs proven bit-exact vs `FUN_00404efc`)
  + the prologue background's own §85 phase. NOT the cursor/box logic. Top/ceiling bit-identical.
- **Method note (worth reusing):** the `flow_diff` render-leg `col` field caught a 1-LSB
  greyscale divergence that no side-by-side would; the sim stub logs counters at sim-onEnter
  (pre-increment) while the render consumes them +1 (post-increment) — when reconstructing a
  render formula from logged sim state, account for that. Re-bless after a confirmed-correct
  render change: `golden` (port) + `golden-retail` both went stale (port brightness, retail's
  long-gone FPS overlay) and were re-blessed for down/z scenarios.
- **Sparkle is deferred ON PURPOSE — do not re-attack early.** The shop-display "目玉商品"
  sparkle (template 0x3b) has verified bit-1:1 data (texture/UV/world-matrix) but isn't yet
  visibly 1:1. It is finished **last**, only once the entire command stream UP TO its frame
  is structurally 1:1 with retail. Reason: against a path that still diverges upstream, the
  sparkle's delta is tangled with compounding drift; once everything before it matches, the
  sparkle is the only thing different and its divergence reads directly. WIP is in the
  working tree (emitter `scene1_player_ctrl.c`, render `scene1_render.c`); template loader
  is committed.
- **Authoritative parity facts:** see `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an assumed
  regression.
<!-- FRONT:END -->
