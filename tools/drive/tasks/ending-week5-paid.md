---
name: ending-week5-paid
start: save:67e22cc643bf
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - two days on: day >= 31
  - four days on: day >= 33
  - the week-5 payment day is over: day >= 36
  - something after the last payment has control or has ended: day >= 36 and frame > 30000
forbid:
  - game over: scene == 2
---
Probe: **the last debt payment (week 5, 500,000 pix) paid, and what the game does after it** — the payment night,
the ending scenes, credits, whatever follows (a title return? "endless" play? a new save prompt?). No recording
and no agent trace reaches any of it; the port has never been compared there.

**Crafted start save:** the day-29 morning save of `calendar-week4` (day dword 29, HUD Day 30, merchant level 6,
the morning after the 200,000 payment) with gold crafted 198,930 → 900,000 (`craft_pix_save.py --gold`) so the
payment can be met without running shop sessions. Load: `.*60 A .*30 A`, wait `scene == 1 and dlg == 0`.

Read `docs/findings/probes/calendar-week4.md` first (the routine that worked: burn a period = shop door → town map
→ Market → Leave → home, ~240 ticks; map states `wms`; script ids `sid`; the payment night; how each morning is
saved) and `payday-week2-paid.md` (the collection form). Shared names: `day`, `stime`, `wms`, `sid`, `gold`.

Do: walk days 29 → 35 the cheap way (no shop sessions). Each morning: `wms` — visit every destination marked 2,
follow the event (`rd tap R A --n 300 --gap 6 --text --until "dlg == 0"` gives a transcript: keep the first lines
and who speaks; ESC-skip only what is very long once you have its opening); read the Adventurers' Guild list for
names beyond Louie and Charme; pause → Save, `rd endsave`, `rd ck R save dayNN`. On the payment night (day dword
35, HUD 36) the form takes 500,000. **Then do NOT skip anything**: tap through the ending with `--text`
(transcript), take strips of each new screen, note every scene id (`scene`), script id, and any prompt (a save
prompt, "continue playing?", mode choices) with its options — choose the option that keeps playing, and say what
the others were. If the game returns to the title, note the title menu's entries (new ones?), then load the newest
save and report what day / state it is. End with control somewhere, or at the title if that is where it leaves you.

If the budget runs low: stop at a morning with a save and report the sha and the day — the next run continues.

Report, beyond the card's list: per day the events met (script ids, first lines, who), the payment's frames and
amounts, a transcript of the ending (trimmed to what fits), every scene id / state the ending passes through, the
prompts and their options, what the title offers afterwards, each save's sha with its day.
