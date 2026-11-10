---
name: survival-start
start: save:c062daff8848
steps:
  - at the title: scene == 0 and frame > 60
  - in the game with control: scene == 1 and cc08 == 1 and dlg == 0
  - a day later: scene == 1 and cc08 == 1 and dlg == 0 and day >= 2
forbid:
  - game over: scene == 2
---
Probe: **Survival Mode's start** — title → SURVIVAL MODE → "Normal Survival": what it asks, the opening, the state
at first control (gold, day, level, items, the debt / payment schedule shown on the pause menu's calendar), and one
day passed the cheap way to see the next morning. A game mode no recording or probe has entered.

**Start save:** the Endless save of `ending-week5-paid` (slot 001; crafted-gold lineage). Read
`docs/findings/probes/newgame-plus.md` first: the title's entries (`t_cur` 2 / `t_item` 6 = SURVIVAL MODE; `A`
opens a list "Survival Hell" / "Normal Survival"), how New Game + started, what a bank diff across the start
showed. `docs/findings/probes/calendar-week4.md` has the cheap day routine (shop door → town map → Market → Leave
→ home burns a period; the night; the morning).

Do: (1) title → SURVIVAL MODE → Normal Survival: every prompt's text (a save picker? a confirm?), `rd ram
mark/diff` on the bank across the start (`--lo 0x44e3798 --hi 0x4511760`) if a save is loaded as its base.
(2) The opening: `rd tap R A --n 400 --gap 6 --text --until "dlg == 0 and cc08 == 1"` (transcript of the first
lines; ESC + Yes if it is long and offers Event Skip). (3) With control: `gold`, `day`, `mlevel`, `items`, `wms`
on the town map, the pause menu's calendar (debt amount and due day — a picture), what differs from the story
mode's day 1. (4) Pass one day the cheap way; report the night / morning scenes and the state the next morning.
(5) In-game Save to a new slot, `rd endsave` (report the sha).

Report, beyond the card's list: prompts and their options, the start state, the payment schedule as shown, any
bank dwords that mark the mode (compare with New Game +'s 0xb0f9 / 0xb0fa), frames of each scene; strips of the
mode list and of the first morning.
