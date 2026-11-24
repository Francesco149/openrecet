---
name: shop-story-nagi
start: save:a23d9e447eb4
steps:
  - loaded with control: scene == 1 and dlg == 0
  - a shop session open (the counter menu's Open Store): cc08 == 4
  - Nagi's first shop scene (script 8/5, the master tick's story probe): sid == 8005
  - the scene over, the shop running again: cc08 == 4 and dlg == 0 and sid == 8005
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the adventurers' shop story scenes** — `FUN_00462403`'s `b524 == 50` story probe (port
`cs_story_probe`, `src/customer_service.c`; IDA all_ida 65599..65928). On a shop session's FIRST customer
(`s32(0x730b528) == 0`) the probe plays at most one adventurer scene, chosen by work-bank story bytes, the
closeness counters and whether that adventurer's customer id is in the day's eligible list (`DAT_06a5d450`,
20 dwords, -2 / -1 terminated: `rd ram R dwords 0x6a5d450 20`). The basic-block coverage (`where.py FUN_00462403
--unhit`, `docs/findings/retail-coverage.md`) shows **no recording plays any of them except Charme's** — they are
how Nagi, Tielle, Elan, Griff and Caillou come to the shop and join; ported, unverified.

Start save `a23d9e447eb4` = played lineage (`end:dungeon3-deep`, day 22 night, merchant level dword 6). Its bank
already has Nagi's trigger: byte `bank + 179395` (0x2bcc3) == 1 and `bank + 179396` == 0 → when customer id **7**
is in the eligible list at a session's first customer, the probe sets +179396 = 1 and plays **script 8/5**
(`sid == 8005`). (`bank` in `rd eval` = the active working slot's base, e.g. `u8(bank + 179395)`.)

Do: from the night, sleep to the next morning, and open the store each period (morning / afternoon / evening; the
counter menu's "Open Store"). After each roster scan (the frame `cc08` becomes 4) read the eligible list. If 7 is
there, the scene should start within the first customer's idle; let it play (`rd tap R "A" --text` for a transcript),
then serve customers normally a while. If 7 is not in the list, close the shop / let the period pass and try the
next period — use checkpoints (`rd ck`) before each Open Store so a period with no 7 costs nothing to retry
differently (the roster scan draws RNG, so a different number of frames before Open Store gives a different list).

Record for each scene that fires (any `sid` change during a session's first customer, not only 8/5): frame, `sid`,
the transcript, which story bytes changed (`rd ram R mark` before Open Store → `rd ram R diff --lo <bank+0x2bc00>
--hi <bank+0x2bd40>` after). Also record the closeness word of customer 7 (int16 at `bank + 0x2d210 + 4·5`; the
second scene 8/6 = Nagi joins needs it > 2 and +179396 == 1) and whether Nagi buys / sells when she visits.
**Stretch:** keep serving Nagi (sell her what she wants) on later days until closeness > 2, then the next first-
customer with 7 eligible should play **8/6** (`sid == 8006`, sets +179397, +179281 and the roster byte +0x2bc51).
If you get 8/6 too, say so — that is a second probe in the same movie.

If after ~10 periods 7 never appears in the eligible list, stop and report the lists you saw (ids + the period +
the day) — the premise (Nagi eligible on this save) is then wrong and needs the roster's gates read.
