# tools/archive — retired one-off tools (kept per "archive, don't delete")

Moved here 2026-06-09 during the audit cleanup pass
(`docs/audits/2026-06-09-methodology-audit.md`). Each had **zero code
references** (grep-verified across tools/src/tests/CI); historical mentions in
`docs/findings/` / `PROGRESS.md` refer to the chips they served, which are
landed. They still run if pointed at the right inputs — nothing was rewritten.

| tool | was | superseded by |
|---|---|---|
| `trace_space.py` | stretch segtrace idle gaps (self-described "TEMPORARY robustness hack") | real menu anchors in recorded traces |
| `chr_leaf_to_inject.py` | chr-leaf arg-injection helper for an early diff chip | `tools/diff_test.py` targets |
| `dump_camera_groundtruth.py` | one-off retail camera dump (camera chip, landed) | flow-trace fields (`flow-trace-cheatsheet.md`) |
| `dump_collision_objects.py` | one-off retail collision-object dump (W4 RE) | `src/collision_*` + host tests |
| `dump_demvp_groundtruth.py` | one-off retail DEMVP matrix dump | `--d3d-trace-verts` + `render_diff --explain` |
| `dump_phase1_groundtruth.py` | walker phase-1 ground truth (Cpop arc, landed) | flow-trace fields |
| `dump_phase2_groundtruth.py` | walker phase-2 ground truth (Cpop arc, landed) | flow-trace fields |
| `dump_wingglow_groundtruth.py` | Tear wing-glow ground truth (landed, confirmed 1:1) | flow-trace fields |
| `freeroam_reach.py` | static call-graph reachability for the freeroam survey | the survey doc's work list (`docs/plans/freeroam-structural-parity.md`) |

> **Note:** `montage_frames.py` was briefly archived here then RESTORED to `tools/`
> (2026-06-09) — `frida_capture.py` imports it by module name, which the filename-based
> reference scan missed. Lesson: verify by the bare module name (`from X import`), not
> just `X.py`, before archiving an importable tool.

Per-frame state questions these once answered now go through the flow-trace
(`CALL_TRACE_*` + `tools/flow_diff.py`) — annotate the function on both sides
instead of writing a new dumper.
