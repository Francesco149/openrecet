---
name: pub-evening-week3
start: save:64d43650865f
steps:
  - the Pub can be entered (afternoon or later): scene == 8 and stime >= 2 and s32(0x9643588 + 4*4) != 0
  - inside the Pub: scene not in (0, 1, 2, 6, 8, 9, 11, 13, 14)
  - home again with control: scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the Pub** — town-map destination 4, disabled until `stime >= 2` (the `town-rounds-week3` probe found it
dimmed all morning). No recording has been inside.

**Start save:** the week-3 save of `payday-week2` (sha 64d43650865f; day dword 15, `stime` 0, 470,000 pix).
`docs/findings/probes/town-rounds-week3.md` has the map: destinations 0 Recettear, 1 Market, 2 Town Square, 3
Merchants' Guild, 4 Pub, 5 Chapel, 6 Adventurers' Guild; enable states at `0x9643588[dest]` (0 disabled, 1
enabled, 2 highlighted = an event waits); `wm_cur`; the door walk; today's Town Square event is long (ESC skips).

Make two periods pass the cheapest way you can find (the map's box says what each action costs: "This action will
not take any time" / "Going to a dungeon will take 2 periods of time"; opening the store costs one; find what else
does), then enter the Pub. Follow whatever plays to its end, look at any menu, leave, go home (if the day ends on
the way home, stop at the first frame with control or at the night exchange — say which).

Report, beyond the card's list: what costs time and how much, the Pub's `scene` value, its event script ids
(`s32(0x5c7a2c)` / `s32(0x5c7a30)`) and first lines (`rd text`), any menu, the enable states afternoon / evening;
a strip of the Pub.
