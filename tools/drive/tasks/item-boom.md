---
name: item-boom
start: save:9f429be80a97
steps:
  - loaded with control: scene == 1 and dlg == 0
  - four or more sale-log pairs carry the same item (the boom candidate): s16(bank + 0x2dde4) != 0
  - a player-driven boom news filed: s32(bank + 0x9d74*4 - 4) == 36 or s32(bank + 0x9d74*4 - 4) == 37 or s32(bank + 0x9d74*4 + 8) == 36 or s32(bank + 0x9d74*4 + 8) == 37
forbid:
  - game over: scene == 2
  - back at the title: scene == 0 and frame > 400
---
Probe: **the player-driven boom news** — `FUN_00436623`'s steps 4 / 6 (port `news_daily_update`, `src/news_daily.c`; read
it: "boom roll", "player-driven boom news (rank gate ≥ 9)"). The coverage index (`where.py FUN_00436623 --unhit`:
0x436a6d..0x436bxx) shows no recording reaches them. Mechanism (port, from the disasm): from day dword 9 each sale
(`cs_item_log_append`, outside a news-event session) appends (item id, TTL 3) to the 20 int16 pairs at
`bank + 0x2dde4`; each news update (the daily one; from day 9 also the mid-day break, 1 in 3 after a served customer)
finds the item carried by the most pairs (mult), rolls `rand % 100 < p` with p = 10 / 25 / 50 / 80 / 100 for mult 4 / 5 /
6 / 7 / ≥ 8, decrements the TTLs; on a hit with merchant level dword > 8 it files news id 36 (`NEWS_BOOM_ID`; or 37 the ALT id,
p2 10..90) for that item, clears its pairs and adds a headline; days later its expiry line ("The … boom has ended.").

**Crafted start** `9f429be80a97` = `1a5a45e631b4` (played day-14 save + merchant level dword 9 → HUD 10) `--add-item
4001x12` (12 Apples). The crafted level plays a level-up event at load (`~`, wait ~40 ticks, `A` on Yes — DRIVER.md).

Do: display Apples (as many as the stands take), open the store and sell Apples — the same item — as often as possible
(food requests take them; buyers at the display), aiming for ≥ 8 Apple sales within 3 news updates (so mult ≥ 8 → p 100).
Watch the pairs (`rd ram R read <bank+0x2dde4> 80`) and the news list (bank dword SAVE_BANK_FIELD_NEWS_LIST in
`src/save_bank.h`; Tear's News page / the ticker shows headlines) at each day change. Record: the pairs before each news
update, whether the boom fired (the headline text, the news id, its trend char / duration), the effect on Apple prices /
demand the next day (customers' targets for Apples), and the "boom has ended" line if you get that far. Save in game at
the end, `rd endsave R`.

(Step 3's predicate reads news slots 0 and 1 only — entry e's id is `s32(bank + (0x9d74 + 3·e)·4 − 4)`; extend it to the
slot the boom lands in. Step 2 is a weak check: the real condition is ≥ 4 pairs with the same id.)
