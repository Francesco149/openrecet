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
- **Active work:** Phase 1 — the render-parity diff engine. Extend the d3d-trace (Frida
  agent + `src/d3d_trace.c`) to capture per-draw **vertex buffers** (FVF-decoded) and
  **texture identity**, then `tools/render_diff.py --explain` names the first divergent
  draw/field. This is the tool that makes render parity mechanical.
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
