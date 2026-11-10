---
name: calendar-week4
start: save:59e71a0bcb65
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - the next day: day >= 23
  - three days on: day >= 25
  - five days on: day >= 27
  - the week-4 payment day is over: day >= 29
  - control on the day after the payment: day >= 29 and scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe + service: **walk the calendar through week 4** — from the morning after the week-3 payment (day dword 22,
HUD Day 23) to the morning after the week-4 payment (200,000 pix; the save has 398,930), following every scripted
scene and town event, and leaving a save at each morning.

**Start save:** sha 59e71a0bcb65, made by the `calendar-week3` probe — read `docs/findings/probes/calendar-week3.md`
first: it has the method that worked (how a period is burnt, the map states per time of day, the Pub at night as
the cheapest visit, how the day closes, how to save each morning) and the town events of week 3, so you can tell
what is NEW in week 4. Also `payday-week2-paid.md` for the payment night.

Besides that probe's routine, once per morning: open the Adventurers' Guild when it is enabled and read the
"Adventurer Selection" list (`rd text`) — report any name beyond Louie and Charme and from which day; and look at
the Merchants' Guild main list for new options.

The cheap way to pass a period is: shop door → town map → Market → Leave → home. Do that, do NOT run shop sessions
(they cost thousands of ticks). But each morning, before burning the day: look at the town map's destination states
and visit every destination marked 2 (an event waits) — follow the event to its end (tap A; read its first lines
with `rd text`; ESC skips only if it is very long and you have its first lines). Tap through morning / night
scenes. On the payment night the collection form takes 200,000.

Every morning with control (and once after the payment): pause menu → Save (any slot, confirm), then `rd endsave` —
report the sha with its day. Keep a checkpoint per day (`rd ck R save dayNN`). If the budget runs low, stop at a
morning with a save and report where you are.

Report, beyond the card's list: per day (HUD number and day dword) the morning scenes and town events met (script
ids `s32(0x5c7a2c)` / `s32(0x5c7a30)` while `dlg != 0`, first lines, who appears), new map states, the Daily
Profits figures, the payment's frames and amounts, each morning's save sha; anything that looks new (a new
adventurer at the guild, a new shop customer, a new destination).
