---
name: item-boom
start: save:11cb7b194beb
steps:
  - loaded with control: scene == 1 and dlg == 0
  - eight or more sale-log pairs carry the Apple (4001; mult >= 8 = p 100): sum((1 if s16(bank + 187876) == 4001 else 0), (1 if s16(bank + 187880) == 4001 else 0), (1 if s16(bank + 187884) == 4001 else 0), (1 if s16(bank + 187888) == 4001 else 0), (1 if s16(bank + 187892) == 4001 else 0), (1 if s16(bank + 187896) == 4001 else 0), (1 if s16(bank + 187900) == 4001 else 0), (1 if s16(bank + 187904) == 4001 else 0), (1 if s16(bank + 187908) == 4001 else 0), (1 if s16(bank + 187912) == 4001 else 0), (1 if s16(bank + 187916) == 4001 else 0), (1 if s16(bank + 187920) == 4001 else 0), (1 if s16(bank + 187924) == 4001 else 0), (1 if s16(bank + 187928) == 4001 else 0), (1 if s16(bank + 187932) == 4001 else 0), (1 if s16(bank + 187936) == 4001 else 0), (1 if s16(bank + 187940) == 4001 else 0), (1 if s16(bank + 187944) == 4001 else 0), (1 if s16(bank + 187948) == 4001 else 0), (1 if s16(bank + 187952) == 4001 else 0)) >= 8
  - a player-driven boom news filed: s32(bank + 161228) in (36, 37) or s32(bank + 161240) in (36, 37) or s32(bank + 161252) in (36, 37) or s32(bank + 161264) in (36, 37) or s32(bank + 161276) in (36, 37) or s32(bank + 161288) in (36, 37) or s32(bank + 161300) in (36, 37) or s32(bank + 161312) in (36, 37)
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

**Crafted start** `11cb7b194beb` = `9f429be80a97 --gold 100000` (the week-2 payment at the first day change; 9,086 pix → game over) = `1a5a45e631b4` (played day-14 save + merchant level dword 9 → HUD 10) `--add-item
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
