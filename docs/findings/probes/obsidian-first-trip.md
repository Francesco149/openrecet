# Probe: obsidian-first-trip

- **Run:** `obsft` · **start:** `save:bbdbe357822b` (crafted: `824053720e01` with dword 0xaf38 = 0x00000101, i.e.
  +0x2bce2 = +0x2bce3 = 0, the natural first-trip state) · **task:** `tools/drive/tasks/obsidian-first-trip.md`
- **Driver:** subscription subagent (2026-11-19)
- **Scenario:** `tests/scenarios/obsidian-first-trip/` (3435 frames; no save at the end)
- **Verdicts (2026-11-19):** check PASS; verify MATCH; retail golden (Windows, real device); port triage identical
  ticks 1..3735 (every v2 block every tick) after the +0x2bce2 beat port (below). Control: the port before it splits
  at f638 (`party` block: the cutscene's party placement).

Retail facts (observed live):
- Guild Depart to Obsidian Tower row 0 sets `bank+0x2bce2` = 1 at f637; the cutscene stage's **stage index
  `s32(0x438b4dc)` = 19** (stage id 20 via FUN_0047fa4f; 1F's index is 12). So FUN_0042df55's past-the-table read
  (stage index >= 20 → the enemylist DAT_0053f8e8) is not reached by the type-20 stage (`bosses/ROSTER.md` row).
- Event **7/6** opens 201 ticks after Depart (event tick 200; f838): "So this is the entrance." / Tear: "Yes, the door is
  sealed." / "I see no obvious way to open it." / "Yeah, I can't think of anyth-" / "Wha?!" / Recette: "Whoa, wait, what?!".
- A dark round chamber, sealed door at the back, the party on a glowing pad, fixed camera. After the dialogue a white wash
  (the fade at tick 230), a rune and a glyph above the party (the curtains part, ticks 260..400; shake + SE 369 at 260),
  pad lock 4 past tick 400, fade to black; at tick 460 (f1681) +0x2bce2 = 0, +0x2bce3 = 1, and 1F loads (f1697).
- 1F then opens the "Equip items in bag?" prompt (the prompt waits for +0x2bce2 = 0).
- Obsidian 1F enemies: 44 ×3, 0 ×4, 45 ×3, 53 ×4, 1 ×2, 17, 2, 7. The task's `ex`/`ez` point at a non-exit object
  on that floor; the exit cell worked.

Port defects it found: FUN_0044e08f's +0x2bce2 arm (the party diagonal, DAT_0438c148, the three curtain movers) and
FUN_004427f1's dungeon-3 +0x2bce2 beat (camera, 7/6, fade, shake, curtains, the floor reload at 460) were PORT-DEBT
stubs → `d3_first_trip_setup` / `d3_first_trip_event` (`src/dungeon_flow.c`).

Tool gaps: the guild's dungeon-select cursor is unnamed (`u8(0x73ddddc)`, 3 = Obsidian Tower); `rd text` has no
speaker names (sprites).
