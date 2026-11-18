# Probe: elan-flurry-kill

- **Run:** `elan-flurry` · **start:** `hire-elan:near7` (crafted `824053720e01` lineage) · **task:** `tools/drive/tasks/elan-flurry-kill.md`
- **Driver:** the orchestrator session, by hand + 4 searches (2026-11-18)
- **Scenario:** `tests/scenarios/elan-flurry-kill/` (3685 frames)
- **Verdicts (2026-11-18):** check PASS; verify --real MATCH; retail golden (Windows, real device); port triage identical ticks 1..3985 (port with the FUN_00430c6d tick calls, below)

Retail facts (observed live):
- Elan's skill 1 (key `2`, 20 SP) is refused while he is in the hurt state (`p_act` 1, ~31 ticks after a slime hit).
- The flurry's punches (record 0x86) leave a hit enemy alive at HP 0 with `+0xba0` = 30, re-armed on every punch; the
  final blast (record 0x87) zeroes `+0xba0` and kills through FUN_0042e791 → FUN_0042e1a6. Two slimes within ~2 units
  were both held at HP 0 (f3525) and both killed by the one blast (f3639): the blast reaches every enemy the punches reach.
- **Not reached:** FUN_00430c6d's own kill (`+0xba0` runs out at HP 0, no further hit → FUN_0042e1a6). A 300 s search
  (850k ticks, two slimes, both SP charges) for an HP-0 enemy with `+0xba0 == 1` found none. Enemy poison (`+0xb10`,
  the tick's FUN_0042e791(a, ftol(clamp(maxHP·0.1, 1, 20)), 4) every 60 ticks) has no retail writer but the reset to 0:
  dead in retail.
