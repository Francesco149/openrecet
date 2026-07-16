# HOUSE free-roam render/depth parity — resolved redirect

> **Status:** DONE-AS-SCOPED; obsolete open theory archived  \
> **Resolved:** 2026-06-04; dust phase confirmed 1:1 on 2026-06-07  \
> **Historical plan:** `archive/freeroam-render-depth-parity.md`

The plan's final open theory—that a 3D mesh must occlude foot dust—was disproved.
The actual defect was the projection used for the records-A effect pass:

- retail `FUN_004176ff`: `z_far = 500.0`;
- old port effect pass: `z_far = 2000.0`;
- corrected port: records-A effects use 500.0, restoring the body/dust depth
  relationship.

After the separate foot-dust RNG-consumption fix, the user confirmed the dust 1:1.
Ground truth: `../findings/scene1-walk-dust.md` and
`../findings/scene1-tear-visual-diffs.md`.

Do not reopen the archived “mesh occluder” Phase 4 without new contradictory evidence.
Remaining free-roam render gaps belong in `freeroam-structural-parity.md`, the current
front, or explicit `PORT-DEBT`.
