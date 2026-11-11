---
name: day8-visitor
start: save:92f349ae39d4
steps:
  - in the house with control on day 7: scene == 1 and dlg == 0 and s32(0x450fb84) == 7
  - the day rolled to 8: s32(0x450fb84) == 8
  - the day-8 visitor scene is armed (work byte +0x2bcd8): u8(0x450f470) == 1
  - the scene is over and its seen flag is set: u8(0x450f470) == 0 and u8(0x450f480) == 1 and s32(0x450fb84) == 8 and dlg == 0 and scene == 1
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the day-8 morning visitor scene** (`FUN_004706ea` start, `FUN_00470a46` / `FUN_00470970` / `FUN_004708f7`;
port `src/story_visitor.c`, "ported from the disasm, no recording reaches it").

The arm (IDA `all_ida.c` ~line 94808, port `src/scene1_day_close.c:107`): at the morning house entry, if the day
dword (bank dword 0xb0fb, `s32(0x450fb84)`) == 8, shop-time (0xb0fc, `stime`) == 0, bank dword 0x2dd64/4 != 2 and
work byte +0x2bce8 (`u8(0x450f480)`) == 0 → `FUN_004706ea()`, work byte +0x2bcd8 (`u8(0x450f470)`) = 1.

**The start save is CRAFTED** from the day-15-morning save `64d43650865f` (470,000 pix, level dword 5): day dword set
to 7 and the scene's seen byte +0x2bce8 cleared (`craft_pix_save.py --set 0xb0fb=7 --set 0xaf3a=0x100`). The week
flags of the bank still say week 3, so the night of day 7 may or may not ask for a payment: report what happens.
Validate the addresses above first (`rd eval` on the loaded save: day must read 7; if the bank base differs, find
it — `rd ram R list` has `day`, `stime`, `bank`).

Do: load (`.*60 A .*30 A`), pass day 7 the cheapest way (how the calendar probes passed a day:
`docs/findings/probes/calendar-week3.md` — leave by the shop door, visit places on the town map until night, home,
the night exchange, the day-end summary), reach the morning of day 8, and let the visitor scene play out fully
WITHOUT Event Skip if it allows normal advancing (`rd tap R A --text` for the transcript); then walk a little with
control, open the pause menu and Save.

Report, beyond the card's list: the scene's frames (arm, each phase), the transcript, the state the scene drives
(the 0x26-dword visitor record `g_story_visitor` — find its retail address from `FUN_004706ea`; the two visitor
sprites chars 47 / 48; camera), who the visitor is, a strip of the scene, and whether the payment night happened.
