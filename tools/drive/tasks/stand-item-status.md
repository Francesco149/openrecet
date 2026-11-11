---
name: stand-item-status
start: save:64d43650865f
steps:
  - in the house with control: scene == 1 and dlg == 0 and cc08 == 1
  - the faced stand's item status is open: cc08 == 50
  - closed again, free roam: cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the item status of the stand Recette faces** — `PORT-DEBT(stub, FUN_0048670f cc08 50)` in the port
(`src/scene1_player_ctrl.c:980`): in free roam in the shop, facing a display table / shelf cell that holds an item,
the button whose pressed bit is 0x40 opens the item's status (`FUN_0047f1ce`, `cc08 = 50`, `FUN_00468286(item)`,
`FUN_004681db(2)`, sound 710). The port does nothing there.

The start save is day 15 morning (made by probe payday-week2-paid; `.*60 A .*30 A` loads it; if an event plays, `~`
then `A` on Yes). Find which rd button has bit 0x40 (try `X`, `Y`, `C`, `1`..`5` while facing a stocked stand; `A`
on a stand opens the display menu instead — that is not this). The conditions in the port's comment: not at the
door, not at the counter, the faced furniture cell is a plain table (not a vender), the display grid cell under the
cursor (`cbfc`, `cc00` = `s32(0x438cbfc)`, `s32(0x438cc00)`) holds an item.

In the mode: what the window shows (`rd text`), what each button does (arrows, A, B, the same button again), how it
closes, whether other stands can be browsed from inside. Do it on at least two different stands / items, including
one press on an EMPTY cell and one while facing a wall (nothing should open — say what happened).

Report, beyond the card's list: the button, the frames, cc08 / related mode variables per phase (mark / diff), the
texts, a strip with the window open.
