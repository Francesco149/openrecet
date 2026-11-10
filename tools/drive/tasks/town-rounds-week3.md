---
name: town-rounds-week3
start: save:64d43650865f
steps:
  - on the town map: scene == 8
  - inside a town location with its event or menu: scene not in (0, 1, 8, 9)
  - back on the town map afterwards: scene == 8
  - home again with control: scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **every town destination in week 3** — the town map and each place it offers (Town Square, Market, Pub,
Chapel, the guilds …): which are enabled, which play an event on entry (and which script), what each costs in
time. The port has the map and the two guilds verified; town events only as far as recordings went (week 2).

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f; day dword 15, 470,000 pix). Load:
`.*60 A .*30 A`, then check `dlg`.

Read first: `docs/findings/town-map-RE.md` (the map: `wm_cur` = `s32(0x9643684)` is the cursor / destination;
leaving the shop: within 1.8 of the door at (3.0, −3.4) and facing +x), `docs/findings/merchant-guild-RE.md` (the
first-visit / event trigger: `DAT_0438b1c8 = 2`, "iv/iv%d_%d.ivt" from `DAT_005c7a2c / 30`), `src/scene_town_event.h`.

Visit each destination the map lets you enter, in map order. Where an event plays, follow it to its end (tap A;
read the first lines with `rd text`); where a menu opens, look at its options and leave. Note the time slot
(`stime`, find it: the shop-time counter the earlier report mentions; "If you return now no time will pass") after
each visit; stop visiting when the day would end, go home.

Report, beyond the card's list: per destination its map index, `scene` value inside, whether an event played and
its script id (`s32(0x5c7a2c)` / `s32(0x5c7a30)` while `dlg != 0`), first lines, time cost; the destinations that
were disabled and what the map says. A strip of each new location.
