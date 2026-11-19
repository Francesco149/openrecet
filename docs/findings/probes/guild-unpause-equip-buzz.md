# Probe: guild-unpause-equip-buzz

- **Run:** `guild-unpause-equip-buzz` · **start:** save:824053720e01 (CRAFTED: all hires level 40) · **task:** `tools/drive/tasks/guild-unpause-equip-buzz.md`
  (checked with a driver copy, steps 4 and 6 fixed — below)
- **Driver:** Claude Code subagent (subscription), claude-opus-5-5
- **Scenario:** `tests/scenarios/guild-unpause-equip-buzz/` (1493 frames)
- **Verdicts (2026-11-19):** check PASS (driver copy); verify --real MATCH (frame 1493, rng 122711bf, Windows D3D8); golden / triage not run

Retail facts observed live through `rd`; claims read from code only are marked (code).

---

**Step frames (`rd check`, fixed copy):** guild f392 · pause over the bag f546 · unpause f607 · floor with control f1198 ·
Equip item list f1296 · buzz f1356. Movie ends on 1F with control (f1493).

**Route.** `.*60 A .*30 A .*120` (house f137) → `rd search --goal "scene == 8" --apply -j 2` (f335) → `wm_cur` 0: `R R` no
move, `U` → 2, `R` → 3, `U` → 6 → `A` → guild. List cursor opens on Louie; `A` hires him and goes STRAIGHT to the bag screen
(bag mode `0x6a49b60` = 1 and item window `0x734b9a0` = 1 from f441; no "Equip items in bag?" at the guild). A help scroll
("Pick the items you wish to bring with you…") blocks the cursor until `A` dismisses it.

**Bag cursor names.** Row `0x5c5aa0` (0 = Depart!, 1..5 slots), scroll `0x6a49b68`, hand x / y `0x438abf4` / `0x438abf8`
(eases: row r → (16, 40r + 124)), `0x438ac00` / `ac04` = the hand's other pair (set equal to the position by
`sub_435693`, the snap), `0x438b150` = 1 after a snap.

**FUN_0045bd36 (guild unpause hook).** Cursor on slot 3 (empty), hand settled at (16, 244).
| frame | scene | row | hand x, y | ac00, ac04 | b150 | 734b998 | 5c6ee0 |
|---|---|---|---|---|---|---|---|
| 545 (before ESC) | 11 | 3 | 16, 244 | 0, 6.667 | 1 | −1 | 0 |
| 546 pause open | 9 | 3 | 16, 244 → 72, 112 (pause hand) | 9.333, −22 | 0 | −1 | 0 |
| 606 (B pressed f607) | 9 | 3 | 72, 112 | 9.333, −22 | 0 | −1 | 0 |
| 607 unpaused | 11 | 3 | **16, 244** | **16, 244** | **1** | −1 | 0 |

`B` closes the guild-side pause in the same tick (scene 9 → 11 at f607, no close animation). The hand is SNAPPED back to the
bag row (both pairs equal, no easing), row / scroll unchanged; compared adventurer `0x5c6ee0` 0 = `dword_7477E70[5c5a9c=1]`.
The pause's own hand was at (72, 112). Strip f604–610 shows the snap. Slot 3 was empty, so the highlighted item `0x734b998` stays −1.
Packed afterwards: slot 3 = Beef Bowl (Food page via `A` on the slot, `L`; Clothes page has only Tough Clothes).

**Dungeon.** Depart → map (cursor on Jade Way) → `L .*12 A .*30 A` → Hall of Trials 1F, fee 3,950 (470000 → 466050).
Arrival f861: `lock` −1 on that tick only (loading 1), then 0 until f1194. "Equip items in bag?" box: `equipq` 1 at f1061,
counts to 118, Yes / No up; `R .*8 A` = No at f1188; control f1194–1198.

**FUN_0046940c (the buzz).** `~` → pause (pm_sel 0); `D D` → pm_sel 2 / pm_type 5; `A` → Equip page (`0x734b9a8` = 6 from
f1299). `D A` picks the weapon slot (Longsword) at f1334 (`0x74b28a4` 1 → 0 = item-list mode). Bag list: "1/20",
"Rearrange", **Beef Bowl greyed (colour 505050)**, cursor on it. `A` at **f1356**:
- `0x734b994` reads 0 before, after and on every tick: (code) `sub_469414` sets it 1 and returns 3, the caller's equip
  check `sub_468DDC` fails → SE 362 + `sub_46940C` zeroes it inside the same tick. The SE flag (`byte_964308C[44]` =
  0x96430b8; 362 is entry 44 of the table at 0x5d1584) is consumed in the tick too: not observable by `rd`.
- What IS visible: `0x5c6ee4` (the "Number possessed" count) 0 → 2 at f1356 (`LABEL_100` recount runs only when the
  return is non-zero), the help box's "Number possessed- 0" → "- 2" (strip f1354–1358). `0x734b998` = 256768 (Beef Bowl).
- Nothing changed: left list still Longsword / Scrap Plate / Wooden Shield / Old Muffler, Attack 56, bag 1/20, `74b28a4`
  stays 0 (list stays open). `B B B` closes the pause (scene 9 → 1 at f1430), HP 197.

**Predicate fixes (driver copy).**
- Step 6 `scene == 9 and s32(0x734b994) == 0` is the resting value (held at f1297, the tick after step 5). Fixed to the A tick:
  `scene == 9 and s32(0x734b9a8) == 6 and s32(0x74b28a4) == 0 and (s32(0x73dddd4) & 0x10) != 0 and s32(0x7337850 + 4*s32(0x734b968)) > 0 and s32(0x734b998) == s32(0x731f598 + 8*(s32(0x731f408 + 4*s32(0x734b968)) + s32(0x7337850 + 4*s32(0x734b968)))) and s32(0x734b994) == 0`
  (a valid item would leave 994 = 1 after the tick (code); the `734b998` term excludes the slot-pick tick f1334, where A is
  down and 994 is 0 but 998 still holds the slot's item 160).
- Step 4 adds `not loading` (fired at the arrival tick f861 otherwise).

**Tool gaps.** SE requests are invisible (queued and consumed in one tick): an `rd` per-tick SE log (`sub_499519` ids)
would make "the buzz" a one-term predicate. Names worth sharing: `bagrow` 0x5c5aa0, `bagscroll` 0x6a49b68,
`bagmode` 0x6a49b60, `handx` / `handy` 0x438abf4 / f8, `iw_active` 0x734b9a0, `iw_mode` 0x734b9a8, `eq_slotmode` 0x74b28a4,
`iw_confirm` 0x734b994, `iw_item` 0x734b998, `possessed` 0x5c6ee4.
