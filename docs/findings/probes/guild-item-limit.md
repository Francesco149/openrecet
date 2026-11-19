# Probe: guild-item-limit

- **Run:** `guild-item-limit` · **start:** save:3f9c0a2684a2 (crafted) · **task:** `tools/drive/tasks/guild-item-limit.md`
- **Driver:** Claude Code subagent (subscription), claude-opus-5-5
- **Scenario:** `tests/scenarios/guild-item-limit/` (597 frames)
- **Verdicts (2026-11-19):** check PASS; verify MATCH (headless + real device, Windows D3D8)
- **State note:** the start save is crafted (`craft_pix_save.py --base 175794a5f61c --add-item 1001x15000`): 15,000
  copies of item 1001 (word `0xfa40`), one per bag slot. No real playthrough reaches this.

## Steps (frames)

| frame | event |
|---|---|
| f137 | house/shop loaded (`.*60 A .*30 A`) |
| ~f198 / f239 | `~` on the morning event; "Do you want to skip this event?" up |
| f250 | A on Yes → `dlg == 0` |
| f385 | shop door → town map (`scene == 8`; route found by search, 130 frames) |
| f399 / f400 | map cursor `wm_cur` 0 → 2 (U) → 3 (R); A |
| **f420** | step 1: guild main list, mode 1, cursor 0 (Buy); options count 6 = [0 Buy, 1 Sell, 3 Fusion, 6 Expansion, 2 Talk, 4 Leave] |
| **f459** | step 2: A on Buy → mode 0, the Buy list ("Swords": Worn Sword, Longsword, Steel Sword, Knight's Blade − 1 Left). **No box here.** |
| **f481** | step 3: A on Worn Sword (112 pix) → `DAT_0438b148` 0 → 1, ramps +1/tick to 8 (f488); mode stays 0 |
| f501 | A pressed (box closing) |
| **f510** | step 4: counter held 8 to f502, then 7 → 0 one per tick; 0 at f510; still mode 0, list open |
| f515 | B → mode 1 (main list) |
| f550 | cursor on Leave (5) |
| **f576** | step 5: A on Leave → town map (`scene == 8`); no "equip items?" prompt; "Well, come back any ti…" farewell line drawn at f550 |

## Retail facts

- **The limit check is at the item select, not the Buy option.** `sub_491B16` (count of bag slots ≠ −1 over
  `bank + 24`, 20,000 dwords) ≥ 15,000 **and** option type `dword_9640624[c04] == 0` (Buy) → `sub_434CEB("Item limit
  reached, cannot buy any more.")` and return (all_ida 105522–105526). This code is in the mode-0 fall-through after the mode
  switch (`v47 = sub_469414(1)`, 105502; v47 1 = A on an item). Case 1 (the main list, 105040–105181) has no limit check:
  A on Buy with 15,032 items opens the list normally (f459). The first version of the task assumed the check was in the
  main list, which was wrong.
- Measured count at f420: `sub_491B16` = **15,032** (15,000 × `0xfa40` + 32 items from the base save); `items` (bank+0x2bb18) also 15,032.
- Box text (`rd text`, f501): `103 212 'Item limit reached, cannot buy any more.'` A yellow scroll is drawn over the list (picture f495). The
  list, the description and "Purchase Price- 112 / Number possessed- 0" stay drawn under it.
- Box counter `DAT_0438b148`: 1..8 over 8 ticks after the press, held at 8. A press then ramps it 8 → 0 in 8 ticks, starting
  one tick after the press. **A (Z) and B (X) close it the same way** (tested both from the `box` checkpoint). B closes
  only the box: mode stays 0 and the list stays open. Gold (9,999,999) and items (15,032) do not change, and no quantity
  overlay (mode 8) opens.
- The 15,000 items changed nothing else we saw. The load frame (f137) and the morning-event skip are the same as the base
  save's run (guild-sell-expansion: f137 / skip prompt ~f199). The guild main list and the Buy list look the same. The item
  names are blue (colour `ff4d4dff`, ABGR), which is normal and has nothing to do with the limit.
- A second A on another item (D, A) shortly after the close did not reopen the box within 12 ticks. Not investigated
  (probably an input lock after the close); that retry is not in the filed movie.

## Tool notes

- The case structure of the guild `switch` in all_ida.c is easy to misread: the mode-0 (item list) handler is the
  code after the switch (the `default: break`), not a `case 0`.
