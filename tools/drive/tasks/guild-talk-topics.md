---
name: guild-talk-topics
start: save:64d43650865f
steps:
  - loaded with control: scene == 1 and cc08 == 1 and dlg == 0
  - at the Merchants' Guild: scene == 6
  - back in the shop at the end: scene == 1 and cc08 == 1 and dlg == 0 and frame > 2500
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe + transcript: **the Merchants' Guild's "Talk" topics** — six topics, all still tagged "New" (What is the
guild?, What can I do here?, About merchant levels, About the town, About unknown items, About fusion): the game's
own description of its systems, never read in any recording. Then the same at the Market if its menu has a Talk.

**Start save:** the week-3 morning save (sha 64d43650865f). Shop door → town map → Merchants' Guild (`wm_cur` 3,
`scene == 6`); main list Buy, Sell, Fusion, Talk, Leave (`gmode`, `gcur`; `docs/findings/merchant-guild-RE.md`,
`docs/findings/probes/guild-sell-expansion.md` for the menu's state).

Do: open Talk; for each topic: `A`, then `rd tap R A --n 200 --gap 8 --text --until "dlg == 0"` (or whatever
state says the topic ended — name it) and keep the FULL transcript; note the "New" tag going away and where that
is stored (`rd ram mark/diff` around one topic, bank range `--lo 0x44e3798 --hi 0x4511760`). After all six: Leave,
Market (`wm_cur` 1): if it has Talk, the same. Home; in-game Save, `rd endsave`.

Report, beyond the card's list: the full transcript per topic (this is the deliverable), the topic list's state
(mode, cursor, the "New" flags' addresses), frames per topic; one strip of the topic list.
