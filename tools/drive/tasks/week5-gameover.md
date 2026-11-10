---
name: week5-gameover
start: save:89190665bb8f
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - the payment day has come: day >= 35
  - the failed payment ended the loop: scene in (0, 2) and frame > 20000 or day < 29 and frame > 20000
forbid:
  - a runaway: frame > 150000
---
Probe: **the last payment missed** — from the day-29 morning save (198,930 pix against 500,000 due on day dword
35) walk to the payment night WITHOUT earning the money, and follow what the game does: the collection form, the
verdict, the game over / loop restart (the week-1 version is `docs/findings/probes/gameover-loop-dungeon0.md`:
"payment short → loop restart with level + items kept" — is week 5's the same script? what day does the new loop
start on? what is kept?).

Read `docs/findings/probes/ending-week5-paid.md` first: the same days walked with the money (the cheap period
burn, the events of days 29–35 — skip them here with ESC + Yes once you recognise their first lines, or just do
not visit them), and `calendar-week4.md` for the routine. Shared names: `day`, `stime`, `wms`, `sid`, `gold`.

Do: burn days 29 → 35 the cheapest way (no events, no saves needed except one on the payment-day morning:
`rd endsave`, report the sha). On the payment night do NOT skip: `rd tap R A --n 300 --gap 6 --text` through the
form and everything after; note every scene id, script id (`sid`) and prompt with its options (choose the one
that continues). Stop when the game gives control back (a new loop's first morning) or sits at the title; then
report `day`, `gold`, `mlevel`, `items`, and `rd ram mark/diff` is not needed.

Report, beyond the card's list: the failed-payment sequence (frames, scenes, scripts, a trimmed transcript), what
the new loop keeps and resets against `gameover-loop-dungeon0`, the payment-morning save's sha; strips of the
form and of the game-over screens.
