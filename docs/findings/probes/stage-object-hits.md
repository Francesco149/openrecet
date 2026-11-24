# Probe: stage-object-hits

- **Run:** `soh-a` → scenario `stage-object-hits` · **start:** save:824053720e01 (crafted hire lineage; Louie) · **task:** `tools/drive/tasks/stage-object-hits.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/stage-object-hits/` (3,147 frames; Jade Way 1F)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH; golden + triage: **identical**; gate row. No port change.

First recording of the record state machine's stage-object arm (`FUN_0043865e` 0x43a17a / 0x43a1d7) and particles 0x15 / 0x16.

- Kinds 0..3 (from collision triangle types 8..11) are the **trees, ferns and grass on the raised ledges** around Jade Way's
  sunken arenas (1F: 20 × kind 0, 9 × 1, 15 × 2, 9 × 3; all at y 4, the floor at ~0).
- f2388: Louie's skill (`B`, record 34) hits a kind-0 tree → 6 particles 0x16; its dword 9 (the hit shake) 5 → 0.
  f3127: one record 34 hits a kind-2 grass (6 × 0x15) and a kind-0 tree (6 × 0x16) in the same tick (both list the record
  in their hit list — the per-record scan runs twice that tick).
- The sword can never hit them: its record's radius 2 at y ≈ 1.04 fails the height test (object y − record y < radius,
  2.96 > 2). Record 34: radius 3.5, y up to ~2.05, live at actt 5..9.
- Hit radius = the kind table 0x5c4c90 [3] × the object's scale (0.833): kind 0 → 2.5, kinds 2 / 3 → 0.83; heights [4] = 10.
- New rd views from this probe: `rd ram R objects`, `rd ram R particles`.
