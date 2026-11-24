---
name: shop-story-arma
start: save:64d43650865f
steps:
  - loaded with control: scene == 1 and dlg == 0
  - a shop session open: cc08 == 4
  - Arma's shop scene (script 9/2, the master tick's story probe): sid == 9002
  - the scene over, the shop running again: cc08 == 4 and dlg == 0 and sid == 9002
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **Arma's first shop scene, 9/2** — an arm of `FUN_00462403`'s story probe (`docs/findings/shop-story-scenes.md`,
read it first: mechanism, eligibility, the closeness slot map). No recording plays it; the port has it ported
(`cs_story_probe`, `src/customer_service.c`), unverified.

Start save `64d43650865f` = the played day-14 save `17264a73…` + crafted gold, at the day-15 morning. Its bank has
`u8(bank + 179438) == 1` (the wake-up scene 9/1 already played) and `u8(bank + 179439) == 0` → customer **9** (Arma) is
**forced** into the eligible list when the store opens (band ≥ 0, her time-of-day bit); at the session's first customer
the probe sets +179439 = 1 and plays **9/2** (`sid == 9002`). Higher arms win the same session if their customer is
eligible (order: Charme 3, Louie 2, Nagi 7, Griff 8, then Arma 9) — on this save they should not be.

Do: open the store (counter menu, "Open Store"; DRIVER.md has the walk to the counter for the day-14 saves). Read the
eligible list after the scan (`rd ram R dwords 0x6a5d450 20`) and `u8(bank + 179439)`. If 9 is in it, let the scene
play (`rd tap R "A" --text` for the transcript), then serve customers a while. If 9 is not there (wrong time of day,
or a news-event session `s32(0x730b5e8) != 0`), close / let the period pass, try the next period; checkpoint before each
Open Store.

Record: the frame, the transcript, story bytes changed (`rd ram R mark` before Open Store, `rd ram R diff --lo
<bank+0x2bc00> --hi <bank+0x2bd40>` after), whether Arma buys / sells anything, her closeness (slot 7: int16 at
`bank + 0x2d210 + 28`). Then save in game (pause → Save) and `rd endsave R`, so the scene's after-state is a start
point. Stop there.
