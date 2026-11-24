# Probe: arma-finisher

- **Run:** `afn-1` → scenario `arma-finisher` · **start:** `lapis-41f-50f@27600` (50F arrival; crafted lineage) · **task:** `tools/drive/tasks/arma-finisher.md`
- **Driver:** Claude Code subagent (subscription), live `rd` (`win-wifi`)
- **Scenario:** `tests/scenarios/arma-finisher/` (31,407 frames; ends in the post-wipe item picker)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f31407, rng e77e3dbc); golden + triage: **identical**; gate row. No port change.

First recording of boss Arma's finisher (state 5) and its records 0x9c (156) / 0x9e (158) (`FUN_0043ae20` 0x43b7a3..0x43ba32,
0x43b364; `FUN_00445a8c` 0x446c05 / 0x446c58 / 0x447d67). Card: `docs/findings/bosses/arma.md`.

| frame | event |
|---|---|
| 30429 / 30436 | lob state; record 161 spawned, flew straight along −z (x fixed, vx 0, her yaw −2.66 — not along her facing octant as the card's §5 says) |
| 30488 | at age 53 the lob stopped at the south wall and became a blast (life 0.5 → 4.0): hit Louie 4.9 away, **104** damage, knock-down 70, a 5-tick freeze |
| 30493 / 30494 | state 0 (counter 69, `+0xa68` 241) → **state 5**; side 1 (pz −50.3 < −40) |
| 30533 / 30544 | sub 1 (yaw π) / **sub 12**: records 156 (a descending orb at Arma, settles at y 3.24 by age ~29) and **158** (a beam: life 2.5, "vz −1" a direction) |
| 30623 | 158 fires at age 80: hit Louie 9.2 away, 0.2 off its line — **980** → HP 0, the party wipes |

- The lob arms the finisher by design: its blast lands at state tick 59 and with the 5-tick freeze the knock-down counter
  is still > 60 when state 0 resumes — it needs the lob to reach a wall (an on-axis lob in open room landed nothing).
- After one lob hit the finisher is a guaranteed wipe at this level; Arma's AI froze at sub 210 in state 5.
- Tool gap fixed: `rd scan` / `eval --at-frame` crashed on a NaN f32 column (`num()`), now prints nan.
