---
name: payday-week2
start: save:4c1259b71fd7
steps:
  - in the shop on the start day: scene == 1 and day == 14 and dlg == 0
  - the day has rolled over: day == 15
  - the week-2 payment was taken: day == 15 and gold < 480000
  - free roam on the new day: day == 15 and scene == 1 and cc08 == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the week-2 debt payment, paid** — the day close, the payment screen, the verdict scripts and the next
morning's scenes. FRONT lists the day-15 scene's camera eases and the payment screen's 3D shop as unverified ("no
recording reaches it"); the only recordings of this night are short of money (game over).

**Crafted start save** (`tools/craft_pix_save.py --base <dungeon3-run2-save's end save> --gold 500000`): the played
day-14 save (HUD "Day 15", merchant level 6 on the HUD) with 500,000 pix instead of 9,086, so the payment succeeds.
Everything else is as played.

Read first: `docs/findings/day-close-RE.md` (how a day ends: the shop-time slots, the night exchange, the "Daily
Profits" summary), `docs/PROGRESS.md` (search "payment verdict" / "debt-week1-paid" for how week 1's payment
played out), FRONT's "Still PORT-DEBT around the day" paragraph.

The load starts with a morning event (ESC = Event Skip → Yes, or tap A through it). Then make the day pass — the
cheapest legal way you can find (open the store and let the time slots run out, or the town map: each outing costs
slots) — through the night, the summary, the payment, into the next morning with control back. Tap through
dialogue with `rd tap`; use `rd text` to read what the game says about the debt.

Report, beyond the card's list: the sequence of scenes / modes the night goes through with frames (`scene`,
`cc08`, `dlg`, plus whatever variables you find for the summary and payment screens), the amount paid (gold
before / after), every scripted scene that plays on the new morning (titles / first lines from `rd text`), and
strips of the payment screen and of the first new scene. Do an in-game Save at the end (pause menu → Save, any
slot) and run `rd endsave` so later tasks can start in week 3 — report the sha.
