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
- **First consumer (deferred until the engine lands):** finish the shop-display "目玉商品"
  sparkle (template 0x3b). Data is verified bit-1:1 vs retail (texture/UV/world-matrix);
  it draws via the records-A pass but isn't yet visibly 1:1 — the engine will name why.
- **Authoritative parity facts:** see `findings/confirmed-parity-ledger.md`. A tooling
  "divergence" on a human-confirmed-1:1 item is a lead to investigate, NOT an assumed
  regression.
<!-- FRONT:END -->
