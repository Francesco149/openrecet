---
name: endless-first-days
start: save:c062daff8848
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - the next morning: day >= 37 and scene == 1 and cc08 == 1 and dlg == 0
  - two days on: day >= 38
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Endless Mode's first days** — load the Endless save (LOAD GAME → slot 001), and walk two days: what the
mode changes against the story (no debt: what does the pause calendar show? the Daily Profits screen? new town
events, new adventurers at the guild now that the story is over, new Merchants' Guild / Market stock?).

**Start save:** the Endless save of `ending-week5-paid` (day dword 36, 400,000 pix, level 6; crafted-gold
lineage). Its report (`docs/findings/probes/ending-week5-paid.md`) ends with the first control in Endless Mode
(the title card 12/1 plays at load) — read it and `calendar-week4.md` (the day routine, `wms`, `sid`, the cheap
period burn, the morning save) first.

Do, each day: `wms` every period — visit every destination marked 2 and follow its event with a transcript
(`rd tap R A --n 300 --gap 6 --text --until "dlg == 0"`; keep the first lines and who speaks); read the
Adventurers' Guild list (names, levels, fees) and the Merchants' Guild main list; one look at the Market's and
the guild's Buy categories (new ones against `market-buy-display.md`?). The pause menu's calendar once (a
picture). The night and the Daily Profits screen (figures). Each morning: pause → Save to a new slot,
`rd endsave`, `rd ck R save dayNN`.

Report, beyond the card's list: per day and period the map states and events (script ids, first lines, who),
the guild lists, what the calendar and the profits screen show without a debt, each save's sha with its day;
anything the story mode did not have.
