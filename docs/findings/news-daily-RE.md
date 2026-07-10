# Daily-news subsystem RE — FUN_00436623 (generator) + FUN_004363c6 (picker) + FUN_004361b2 (classifier)

2026-07-10.  Closes FRONT target #2 "news-list population".  Objdump-verified
(0x436623–0x436f96, scratchpad news_gen.asm sweep); decomp faithful, precision
notes below.  Port: `src/news_daily.{c,h}`.

## Who writes DAT_0450ad68 (the 20-entry daily news list)

**`FUN_00436623` = the daily news generator.**  3 call sites, ALL gated `day
(fb84/SHOP_DAY) > 8` ⇒ news starts day 9 (post-tutorial week); existing day-1/2
traces draw ZERO rng from any of this (gates short-circuit before the rng call):

1. **all.c:60358 (FUN_00462403 customer-leave restore, fb88<4 arm, f404==0):**
   after the Recette hop-down reposition — `rng%3==0` → generator + `b92c=1`
   (ticker timer).  Mid-day news break after a served customer.
2. **all.c:86738 (FUN_0048670f master tick, clock-advance):** `cc08==1 && cc04==0
   && DAT_045105a0[slot]==1` → `f405a0=0`, shoptime++, `rng%5==0 && day>8` →
   generator + `b92c=1`; then FUN_0045e028; shoptime>3 ⇒ day-end path.  The
   surrounding timed-clock mechanic is UNPORTED → PORT-DEBT(news-clock-advance).
3. **all.c:86711 (master tick, morning beat):** `day>8 && b928==1 && b924==0` →
   generator once at beat start (short-circuit `(b924!=0 || (news(), b928==1))`);
   at `b924==0xbf` → `b92c=1`.  Same day>8 block: `b92c>0` → b92c++ each frame,
   at `b92c==0x1e && headline_count>0` → FUN_00499519 SE request (news jingle).
   Also at beat start (no day>8 gate... NB it IS inside the `day>8` outer `if`
   86697): day==9 && DAT_045114fc[slot]!=2 → f488=1; FUN_00490e56 (daily order
   regen — ported, npc_schedule.c).

**`FUN_00436180` = news-list reset**: 20 entries → id=-1, trend=0, dur=0
(target untouched).  NB id=-1 ≠ "empty" for the generator (empty test is id==0)
⇒ a reset list never regrows until entries are zeroed elsewhere (fresh saves are
zero-filled; expiry writes id=0).

## List entry layout (bank, per-slot stride 0x2dfc8)

`0x450ad60 + i*0xc`, 20 entries: `+0 target_id(i32)` (item id, -1 none, 500 ⇒
debug "normalized" row), `+4 news_id(i32)` (0 free, -1 reset, 1-based news.txt
row, 500 debug), `+8 trend(char)` (= def.rate low byte; 0→'d' generic marker),
`+9 duration(char)`.  Dword indices: target 0x9d72, id 0x9d73, trend/dur bytes
in dword 0x9d74.

## Generator flow (FUN_00436623), rng draws marked ★

1. headline_count (0x2a6c0) = 0.
2. **New-news pick** — first slot with id==0 (none ⇒ skip to 3):
   - id = FUN_004363c6()+1 ★(≤1 draw, see picker).  Retry loop (abort >100
     tries ⇒ id=0, no news):
   - **dedup vs each active entry** (0<id<500): same id ⇒ conflict; cand.rate==0
     && existing.rate==0 ⇒ conflict (one generic at a time); both rates ≠0 ⇒
     conflict iff EXISTING.attr_mask==0 ? categories equal : attr_masks equal.
   - **eligibility**: item_id>0 OK; else attr_mask==-1 (特殊) ⇒ reroll ★; else
     category==-1 && attr_mask==0 ⇒ reroll ★ (failed-lookup row); else OK.
   - **accept**: trend=rate byte (0→'d'); sprintf("%s", body) into headline;
     duration = dur_base_byte (+ ★rng%dur_range iff dur_range>0) + 1, min 2;
     target=-1; iff price_lo>=0: 2-pass g_item scan (valid==1, price>0, price in
     [price_lo,price_hi], attr_mask==-1 ⇒ never / ==0 ⇒ category== / else &≠0),
     ★1 draw iff ≥1 match ⇒ target = picked row's item_id; then iff trend!=0 &&
     target>10 ⇒ slot=FUN_004681f6(target); '<'-splice body into headline
     (see below); headline_count++ iff id!=0.
3. **Debug draws**: "IT %4d " × 20 (FUN_00451874 col 0, rows 10+ — dormant).
4. **Boom scan** (news-pairs 0x2dde4, 20×{i16 item_id,i16 ttl}; writer =
   FUN_00460083 count[8]>8 block, ported): find max multiplicity (first-max
   index).  Iff ANY pair active: ★rng%100 vs p {mult 4:10,5:25,6:50,7:80,≥8:100}
   (draw happens even at p=0 — load-bearing).
5. **TTL pass**: active pairs ttl--; ttl==0 ⇒ clear pair.
6. **Boom news** iff step-4 hit && SHOP_RANK(fb98) ≥ 9 && a free slot: id=0x24;
   ★rng%100 vs p2 {4:10,5:30,6:50,7:70,≥8:90} ⇒ id=0x25; trend=def.rate byte;
   ★duration: id==0x25 ? (rng&3)+2 : rng%3+4; target=hot item id (raw, stored
   pre-slot-conv); trend!=0&&target>10 ⇒ slot conv for name; clear ALL pairs
   with that id; sprintf("%s", def.body)+'<'-splice; headline_count++
   (unconditional).
7. **Expiry pass**: entries id>0 && dur>0: dur--; at 0 ⇒ headline:
   - id==500: "The price of %s has normalized." (slot=conv(target)).
   - trend==0: target!=-1 ⇒ "The %s boom has ended." with plural name at
     **RAW target used as SLOT (no id→slot conv — engine quirk #132)**;
     target==-1 ⇒ attr_mask>0 ? FUN_0049e6b3(mask) : def.name — same fmt.
   - trend=='d': target!=-1 ⇒ conv(target) slot plural; -1 ⇒ attr/def.name;
     "The %s boom has ended.".
   - else: target>0 ⇒ conv slot, "The price of %s has normalized."; target<=0 ⇒
     attr/def.name, same fmt.
   - count++, id=0.  NO rng.
8. **Day-range news** iff day ≥ 10: count news-def rows with category==-100 &&
   period_start<=day && (day<=period_end || period_end==999); ★rng%count iff
   count>0 ⇒ pick, sprintf("%s", body), count++.
9. **Offsets**: per headline i: offs[i] (0x2bac4) = cum; cum += strlen+4; total
   → 0x2bb14.  Debug draws "T %d " × 20 (col 0x14).
10. RNG = FUN_00471084 thunk → FUN_005041f6 (`rng_next15`).

**'<'-splice semantics** (objdump 436932-4369bc): dst[0]=body[0]; loop while
dst[a]!=0: if dst[a]=='<' ⇒ body ptr += 2 (the marker is '<' + 2 more bytes,
i.e. '<' + one SJIS char) and the picked item's PLURAL name (+0x8a) is copied
over dst[a..], a+=len; else a++; then dst[a]=*(++body).  Re-checks the
just-written char (a name containing '<' would re-splice).

## Picker (FUN_004363c6)

day(fb84)==9 ⇒ return 0 (⇒ news id 1 scripted on the first post-tutorial day),
NO draw.  Else: count rows with category != -100 && period_start<=day &&
(day<=period_end || period_end==999); 0 eligible ⇒ -1 (no draw); else ★1 draw,
return the rng%count-th eligible ROW INDEX (0-based; +1 = news id).
NB parse default period_end=100 ⇒ rows without 時期 go stale past day 100
unless the file sets 999.

**id==0 edge** (picker returned -1): the generator dereferences news-def "row
0" = 0x56e0d44..0x56e0dff = BSS BELOW records[0] (0x56e0e00).  Zero bytes ⇒
rate 0 ('d'), empty body, dur_range 0, price_lo 0 ⇒ scan window [0,0] matches
nothing ⇒ ZERO draws, id stays 0, not counted.  Port mirrors with a zeroed
sentinel row.

## Classifier (FUN_004361b2) — already documented §22 haggle RE; port notes

Tutorial-sell gate (b1c0==1 && dungeon==0 && cc08==4 && f404) ⇒ 0.  Sums trend
chars of active entries matching the item (direct target id, def.item_id,
category-name prefix-match FUN_0049ef78 when attr_mask<1, else attr&mask);
'd' skipped; any char ≤-2 ⇒ return -2; else clamp [-1,1].  id==500 entry pops a
debug MessageBoxA("init s3") — dead in shipped data, port no-ops it.  NO rng.

## Fixed en route

`news_record_t` +0x94/+0x98 were misnamed price_lo/price_hi (actually the
LIFETIME base/rng-range) and +0xac/+0xb0 misnamed days_lo/days_hi (actually the
target-item PRICE window) — renamed dur_base/dur_range/price_lo/price_hi in
tables_news.{h,c} + tests.  Semantics proven by the generator asm (duration
counter byte read +0x94, rng%(+0x98); +0xac/+0xb0 compared vs item price).

## Consumers / follow-ups

- Roster scan news block (57479-57490) — ported (cs_roster_scan).
- Classifier FUN_004361b2 — ported this arc; retires the PORT-DEBT(cs-price-
  trend) STUB at cs_offer_up (haggle tilt draw when trend≠0 now live),
  customer_service_render.c, scene1_merchant_hud.c name colour.
- Headline RENDER (the town/shop newspaper ticker reading 0x2a6c4/0x2bac4,
  scroll driven by b92c) — NOT yet RE'd/ported: PORT-DEBT(news-ticker-render).
- Clock-advance call site (86733 block) — mechanic unported:
  PORT-DEBT(news-clock-advance).
- News-def day-range rows (category -100) double as the pool for both the
  picker exclusion and the day-range extra headline — vendor news.txt contents
  worth dumping once for the ledger (runtime data, not in repo).

## §VERIFIED 1:1 — live golden gate 2026-07-10

**news_daily_update is BIT-EXACT vs retail FUN_00436623: 18/18 samples (3 arena
variants × 6 seeds) match on EVERY field — list entries, headline BYTES, scroll
offsets, pairs, rng-draw count, and `final_seed` (the whole RNG stream).**

Protocol (the roster golden pattern): live day-2 save (slot 001), arena
snapshot/diff-poke restore per sample; variants patch the template — `natural`
(as captured ⇒ new-news pick), `expiry` (day=10 + two active entries dur 1,
trend 5 + 'd' ⇒ dedup + both expiry branches + day-range), `boom` (day=10,
rank=9, 8 pairs of item 12 ⇒ threshold/variant/duration rolls + pair clears).
Harness: `src/news_golden_replay.{c,h}` (`OPENRECET_NEWS_GOLDEN` env; NB env
crosses WSL→exe only via `WSLENV=OPENRECET_NEWS_GOLDEN:…` + `wslpath -w`
paths); capture: `tools/news_gen_capture.py`.

**★ Methodology unlock: `seed_after_call`.** Round 1 matched all VISIBLE
outputs on 12/12 samples but final_seed/draws on only 5/12 — retail appeared
to draw MORE.  Root cause: the capture read the final seed via a client-side
RPC racing the RESUMED live sim (bg-NPC draws landed in the window; deltas
varied 2..37).  Fixed in the agent's engine-thread call queue: snapshot
DAT_006023a0 immediately BEFORE **and AFTER** the `fn.apply` (probeRunQueued-
Calls), returned as `seed_at_call`/`seed_after_call` — the callee's exact
consumption window, race-free.  With the atomic window: 18/18 including
final_seed.  Any future callq golden should use BOTH fields, never a separate
seed read.
