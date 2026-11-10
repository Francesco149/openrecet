---
name: calendar-week3
start: save:fe0c2efd7af8
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - the next day: day >= 16
  - two days on: day >= 17
  - four days on: day >= 19
  - the week-3 payment day is over: day >= 22
  - control on the day after the payment: day >= 22 and scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe + service: **walk the calendar through week 3** — from day 16 (HUD) afternoon to the morning after the
week-3 debt payment (80,000 pix; the save has ~470,000), following every scripted scene the days bring, and
leaving a save at each morning for later probes. Nothing past week 2 has been recorded; each new morning may bring
story events, new customers, new town events — those are the findings.

**Start save:** sha fe0c2efd7af8, made by the `shop-session-week3` probe: day dword 15 (HUD Day 16), `stime` 2 (two
periods used), lineage = the played day-14 save with crafted gold. `docs/findings/probes/` has what earlier probes
learned: `payday-week2-paid.md` (how a day closes: night exchange → Daily Profits → the payment's collection form →
verdict script → morning scenes), `pub-evening-week3.md` and `town-rounds-week3.md` (the town map; a visit costs
nothing, GOING HOME after a visit costs one period; `stime` 4 ends the day; event-marked destinations show state 2
in `0x9643588[dest]`), `shop-session-week3.md` (what a shop session costs in ticks).

The cheap way to pass a period is: shop door → town map → Market → Leave → home. Do that, do NOT run shop sessions
(they cost thousands of ticks). But each morning, before burning the day: look at the town map's destination states
and visit every destination marked 2 (an event waits) — follow the event to its end (tap A; read its first lines
with `rd text`; ESC skips only if it is very long and you have its first lines). Tap through morning / night
scenes. On the payment night the collection form takes 80,000.

Every morning with control (and once after the payment): pause menu → Save (any slot, confirm), then `rd endsave` —
report the sha with its day. Keep a checkpoint per day (`rd ck R save dayNN`). If the budget runs low, stop at a
morning with a save and report where you are.

Report, beyond the card's list: per day (HUD number and day dword) the morning scenes and town events met (script
ids `s32(0x5c7a2c)` / `s32(0x5c7a30)` while `dlg != 0`, first lines, who appears), new map states, the Daily
Profits figures, the payment's frames and amounts, each morning's save sha; anything that looks new (a new
adventurer at the guild, a new shop customer, a new destination).
