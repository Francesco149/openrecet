---
name: survival-hell-start
start: save:c062daff8848
steps:
  - at the title: scene == 0 and frame > 60
  - in the game with control: scene == 1 and cc08 == 1 and dlg == 0 and frame > 400
  - the first payment day is over or the mode ended the run: (day >= 2 and scene == 1 and cc08 == 1 and dlg == 0) or scene == 0 and frame > 3000
forbid:
  - a crash to nothing: frame > 200000
---
Probe: **Survival Hell's start** and its first payment — title → SURVIVAL MODE → "Survival Hell". The companion of
`survival-start` (Normal Survival): read `docs/findings/probes/survival-start.md` first and report only what
DIFFERS — the prompts, the bank diff across the start (is bank dword 0xb759 the mode: 1 New Game +, 2 Normal
Survival, 3 cleared / Endless — what does Hell write?), the opening script, the start state (`gold`, `day`,
`mlevel`, `items`), and above all the PAYMENT SCHEDULE: the pause menu's calendar (amount, due day — a picture),
then pass days the cheap way (shop door → town map → Market → Leave → home per period; the night; the morning)
until the first payment night: what is asked, what happens when it cannot be paid (the start has 1,000 pix): a
game over? a score screen (the title's SURVIVAL SCORE page showed "Survival Hell Record Day --")? Follow it to
the end without skipping (`rd tap R A --n 300 --gap 6 --text`), and say where it leaves you (title? which entry?).
Then open the title's SURVIVAL SCORE page (`t_cur` 5) and read the records again.

Shared names: `t_cur`, `t_item`, `t_sub`, `day`, `stime`, `wms`, `sid`, `gold`; the mode list's cursor is s32
0x9643558 (0 = Hell, 1 = Normal). The picker is New Game +'s ("Please choose a file for New Game+ data.").

Report, beyond the card's list: every difference from Normal Survival, the payment amounts / days seen, the
failed-payment sequence (frames, scene ids, script ids, text), the records page after; strips of the calendar
and of the end sequence. If the budget runs low before the payment night: stop with a save and say the day.
