---
name: shop-story-griff
start: save:d7c10c7e06df
steps:
  - loaded with control: scene == 1 and dlg == 0
  - Griff's wake-up scene 7/1 (flag 0x95 = bank + 0x2bcdd): u8(bank + 0x2bcdd) == 1
  - the plaza night events 7-2 / 7-3 seen (flag 0x97): u8(bank + 0x2bcdf) == 1
  - Griff in the shop, scene 7/4 (story probe): sid == 7004
  - Louie's scene 7/5 at a later session (Obsidian Tower opens, bank + 0x2bce1): sid == 7005 and u8(bank + 0x2bce1) == 1
  - the scene over, the shop running: cc08 == 4 and dlg == 0 and sid == 7005
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Griff's arc → Obsidian Tower opens** (`docs/findings/shop-story-scenes.md` — read it first). Start: the end save
of `shop-story-tielle` (`d7c10c7e06df`: day dword 24 evening, Tielle joined; played lineage). From the code read:

1. **7/1** — a wake-up scene (`sub_44BD0D` IDA 49477): Tielle met (`bank + 179390` ≠ 0), time of day 0 (morning), day ≠ 8
   → plays at the next morning in the house, sets flag 0x95 = `bank + 0x2bcdd`.
2. **7-2, 7-3** — town events from `data/event.txt` (`FUN_0045de68`): in the **plaza (Town Square) at night**, 7-2 needs
   0x95 and sets 0x96 (`0x2bcde`), 7-3 needs 0x96 and sets 0x97 (`0x2bcdf`). Likely one per night: go to the Town Square
   on the town map at night (stime 3) on two nights (`wms` shows a destination with an event waiting as 2).
3. **7/4** — Griff (customer 8) is then forced into the eligible list in the evening / night; at a session's first
   customer the story probe plays 7/4 (`sid == 7004`) and sets `0x2bce0`.
4. **7/5** — Louie (customer 2) is then forced; at a later session's first customer the probe plays 7/5 and sets
   `0x2bce1` (Obsidian Tower selectable at the guild).

Open the store in the evening / at night for 7/4 (checkpoint before Open Store; re-time it if 8 is not in the eligible
list `rd ram R dwords 0x6a5d450 20`), then any period for 7/5. Record each scene's frame, `sid`, a transcript excerpt
(`rd tap R "A" --text`), bytes changed (`rd ram R mark` / `diff --lo <bank+0x2bc00> --hi <bank+0x2bd40>`); for the
town events also what the town map showed. If 7/1 does not come the next morning, report bytes `0x2bcdd..0x2bce1` and
`bank + 179390` and stop. Save in game at the end, `rd endsave R`.
