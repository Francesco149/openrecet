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
  `findings/render-diff.md §--explain`. **Remaining Phase 1:** stable **texture identity**
  (content-hash / source-name instead of the raw pointer the opaque-pointer mode
  approximates). **Phase 2 (next):** run the synced sweep from frame 0 of the main menu —
  `export_trace --d3d-trace-verts` + `frida_capture --d3d-trace-verts` over a segtrace, then
  `--explain` frame-by-frame, fixing the first real divergence each time.
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
