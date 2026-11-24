---
name: survival-shop-days
start: save:099af5b31445
steps:
  - loaded with control: scene == 1 and dlg == 0
  - a Survival shop session open: cc08 == 4 and s32(bank + 46937 * 4) == 2
  - an adventurer customer in service in Survival (ids 2..9, 18..20): cc08 == 4 and b534 >= 2 and cust >= 2 and cust <= 9
  - three Survival sessions done (day dword ≥ 3): day >= 3 and scene == 1 and dlg == 0
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **shop days in Normal Survival** — the roster scan's Survival-mode unlock gates (`FUN_0045edaa`, port
`cs_roster_scan` `mode == 2` branch, `src/customer_service.c` ~457: customers 2..9, 19, 20 by bank bytes `0x2bd28..0x2bd30`,
18 by `0x2bd31` when ranked); the coverage index (`where.py FUN_0045edaa --unhit`: 0x45f30a..0x45f3f6) shows no recording
opens the store in Survival. In Survival the story probe is skipped (bank dword 46937 == 2) and the day dword starts at 0.

Start `099af5b31445` = `survival-start`'s end (played from the Endless save; Normal Survival, day dword 1 morning, slot
007). Read `docs/findings/probes/survival-start.md` first. Its bank has `0x2bd28` = `0x2bd29` = 1 and `0x2bd2f..0x2bd31` = 1
(others 0) — so Louie (2), Charme (3), Arma (9), Alouette / Prime (19 / 20) and Euria (18, if the rank gate passes)
should be candidates, Caillou..Griff (4..8) not.

Do: open the store in each period for three days (`rd ram R dwords 0x6a5d450 20` = the eligible list after each Open
Store; `qn` + records `0x730aca0` = the visit queue), serve customers normally (sell near their target `tgt`; request /
order customers that trap an A-mash: B, U, A on "All set!"). Record per session: the eligible list, the queue, each
customer (id, kind, item, price, outcome), whether any of 4..8 ever appears (they should not), any event / scene that
plays (`sid`), and what Survival shows that the story does not (score, a daily target, a game-over condition — read
the screen). Save in game at the end (a new slot), `rd endsave R`.
