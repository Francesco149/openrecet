# Probe: shop-story-caillou

- **Run:** `ssc-1` → scenario `shop-story-caillou` · **start:** save:3228c6dabd3d (`shop-story-griff`'s end; played lineage) · **task:** `tools/drive/tasks/shop-story-caillou.md`
- **Driver:** Claude Code subagent (subscription), live `rd` on win (`win-wifi`)
- **Scenario:** `tests/scenarios/shop-story-caillou/` (28,918 frames); end save `7c616abd381c` (day dword 29, slot 023)
- **Verdicts (2026-11-24):** check PASS; verify --real MATCH (f28918, rng d8cd9d6c). Port triage: **split f19151** (the quest
  sale: retail's `b534 == 30` arm was a PORT-DEBT stub) → ported → **identical** ticks 1..29218, save IDENTICAL; gate row.

Caillou's reagent quest and his joining (`docs/findings/shop-story-scenes.md`, the 5/x rows), all on the played lineage.

## Reagents (one Jade Way trip 1F..10F, fee 2,550)

- 3106 Slime Liver: the 5F crown slime (f7590; `0x2bd39` → 1), picked up f7955. 3101 Charred Lizard: the 10F rat king
  (type 24; f16620, `0x2bd3b` → 1) — the drop lies under the body until its 500-tick collapse ends (f17314). A guaranteed
  slot-0 drop on the second kill of types 23 / 24 in dungeon 1 (first-kill bytes `0x2bd38` / `0x2bd3a` already 1).
  3110 Bat Wing was in storage. (Other 3101 sources: Yellow Roper type 20, Jade 6–14F kobolds ~1 %.)

## Scenes

| frames | sid | bytes | excerpt |
|---|---|---|---|
| 17934–19119 (day 28 period 2, first customer) | 5004 | +179403 0 → 1 at the start; viewed `0x2d4d0` | "Did you finally manage to get my order in?" / "Da-ta-daaa!" |
| 24556–27046 (day 29 morning, first customer) | **5005 — Caillou joins** | +179404, `0x2bc4e` 0 → 1; viewed `0x2d4d1` | "A guild card..." |

## The quest sale (b534 30, IDA 66103..66129)

Ask `s32(0x5c6bb8)` = 3800 (the three prices ×1). b534 = 30 from the scene start, through the dialogue (dlg 0 f19119);
`b530` counts to 32; on that tick (f19151) the `<I>` macro = the fixed "Charred Lizard, Bat Wings<BR>Slime Liver"
(`FUN_004607f3`'s b534 30 branch), `<Y>` = "3800pix", line 8 ("Yay! I sold … for 3800pix!"), gold += 3800, SE 333; A →
b534 0, b524 0 (f19520). The scene counts as customer 1. The quest removed all three items from storage.

## Port fixes

- The `b534 == 0x1e` arm (was `PORT-DEBT(cs-sold-pause)`, a bare return: no gold, no line, the visit never ended).
- `FUN_004607f3` whole: the b534 30 string, the b5a8 4 oder plural, and the plus format `"%s+%d"` (0x5c6d74; the port
  printed "%s %d").

## Facts

- Closeness slot 2 (`bank + 0x2d218`) 0 → 5 in one deal (he sold an Unthankful Statue, base 140, bought at his target 95).
- Caillou's record (`0x6a5ea90 + 4·0x2c670`): attr (−6, −6); active all day; budget 3,000–1,000,000.
- The week-4 payment (200,000) at f23681.
- Dungeon bag `0x438b5ec` (20 slots, value >> 6); floor drops = table C (`0x6956cb0`, 37-dword stride, type = item + 7).
- Tool gaps: a price search with 2-tick `U .` presses never hit the target (the digit editor needs ~3 ticks release);
  the Bash tool backgrounds commands after ~180 s.
