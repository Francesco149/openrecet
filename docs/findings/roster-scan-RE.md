# Roster-scan RE — FUN_0045edaa (customer eligibility / WHO walks in + WHAT they want)

`customer_service_session_init` = **FUN_0045edaa** @ 0x45edaa (4455B, all.c 57338-58261).
Runs once per shop-open (cc08==4 entry). Decides the customer QUEUE (who + which item)
+ builds the chibi-NPC roster. Port state: **only the TUTORIAL (f406) forced-kyaku-13 branch
+ the SELL-ACTIVE (f404) 3-deep placeholder branch are ported**; the general
not-tutorial/not-sell scan (all.c 57474-58212) is `PORT-DEBT(cs-roster-scan)` — THE blocker
for an autonomous day. This doc maps it for the 1:1 port.

## Branch structure (top level)
Prologue (PORTED): zero ~40 session VAs; count displayed items `local_8` over the 15×20 grid
`DAT_044f7030`; +tier bonus (tier1/2/3 → +3/+4/+5); ONE rng15 draw → `customer_count =
(rng&1)+1+displayed/4`, cap 5 (consumed on EVERY path, load-bearing; result unused on
tut/sell). Then:
- `f404!=0` (sell-active): b51c=1, queue[0..2]={kyaku 1,kind 0}, count=3. **PORTED.**
- `f404==0 & f406!=0` (tutorial): queue[0]={kyaku 13,item 0,kind 0}, eligible={13,-2}. **PORTED.**
- `f404==0 & f406==0` & `DAT_073dddb8==0`: **THE ROSTER SCAN** (below). UNPORTED.
- `f404==0 & f406==0` & `DAT_073dddb8!=0`: buysell-debug forced kyaku (`DAT_073dddbc`). UNPORTED (debug).

## The scan (LAB_0045f1e8 …) — data flow
1. **News/featured-item block** (only if `DAT_04511578[slot]==0`): scan 20-entry news list
   `&DAT_0450ad68+slot*0x2dfc8` (stride 0xc) for an active featured item; pull its def from
   `DAT_056e0de0` (stride 0xbc) → `DAT_0730b5f0/f4/ec` (target id / attr mask). Score `local_c`
   over the display grid (front-row bonus). ONE rng15: `if (rng&7)+8 < local_c` → set
   `DAT_0730b5e8=1` (news EVENT active) + latch `DAT_04511578[slot]=1`.
2. **Per-kyaku candidate build** (loop 50 kyaku, `DAT_06a5d558/55c/560/564` stride-4 scratch,
   100 slots): eligibility gated by per-slot STORY FLAGS `DAT_0450f4xx[slot*0x2dfc8]` (dozens of
   byte flags, hand-coded per kyaku id — the unlock schedule) + category. Weight =
   **FUN_0045e55c** (see below) + item-count tiers. NO rng in the loop body.
3. **rng jitter #1:** fill `local_1b8[100]` with `rng%3 + (45-5i)` → **100 rng15 draws.** Then
   `FUN_0045e505(local_1b8, cand_count)` shuffle (= cand_count draws), add jitter to scores.
4. **rng rejection-sample:** loop up to 100× `rng%100` until it lands on a positive-tier
   candidate → boost that score +100. **VARIABLE 1..100 draws** (data-dependent). Load-bearing.
5. **Tier select** (`DAT_0730b5e8` branch): normal → filter by score threshold; news-event →
   filter by featured category + ONE rng (`(rng&1)+8` = pick count).
6. `FUN_0045e505(eligible, n)` shuffle (n draws); refill+shuffle a 2nd index array (n draws);
   ONE rng (`rng&1 [+ (n-4)/2]`) = extra-customer count.
7. **News-featured injection** (DAT_0730acb0 block): per injected customer several rng draws
   (`rng&1`/`rng%3+2`/`rng&3`/`rng&1+2`). Variable.
8. **Queue fill** (3 passes into `DAT_0730aca0` stride-6 entries `{kyaku, item_slot, kind, …}`):
   assign item via **FUN_0045e80f** (0 or 1 rng each). `DAT_0730ac98` = final count. Perm
   `DAT_0730b1a8` = identity then `FUN_0045e505` shuffle (count draws).
9. `FUN_0046f8ba(DAT_06a5d450)` — build the chibi-NPC roster from the eligible list.

**⇒ RNG consumption is deeply DATA-DEPENDENT** (shuffle lengths + the rejection-sample count +
per-injection draws all depend on display grid / story flags / affinity / news). Cannot be
validated by one matched value — needs a **golden reference**: retail in a fully-known input
state → capture rng-draw count + queue/eligible. (cf. `feedback_rng_full_consumer_survey`.)

## Helpers (decompiled + verified)
| fn | role | rng |
|---|---|---|
| **FUN_0045e55c**(rec_ptr) | customer WEIGHT: scan display grid; per item matching wanted attr-mask(+0x51ac) +2, wanted category(+0x51a8 count /+0x5158 list) +4, **front-row ×3**; + tier bonus 3/6/10 | none (pure) |
| **FUN_0045e80f**(cat,kyaku) | pick an item of a category for a kyaku; quality cap = `DAT_045109a8[kyaku]/10` clamp shop-rank | 0/1 |
| **FUN_0045ecc0**(kyaku_rec,kyaku) | customer BUDGET = `min + (max-min)·clamp(DAT_045109a8[kyaku]/10,0..10)/10` | none |
| **FUN_0045e505**(arr,n) | Fisher-Yates shuffle | **n draws** |
| FUN_0040a68f(x,y) | distance² from `DAT_0438b4b8/bc` → ftol (activity/proximity band) | none |
| FUN_0045e6e0() | event-state 0..4 (specific item-ids 0xc1d/c26/c22 on display) | none |
| FUN_004681f6(id) | item id → catalog row (linear `DAT_095d3804` stride 0xb3); port = `tables_item_find_slot_by_id` | none |
| FUN_0045ed12() | front-2-rows has an item in id-range (quest gate) | none |
| FUN_0047f1ce / FUN_004681d3 | rebuild party-model list / display-menu reset | none |

## ★ Answer to the standing user question (closeness/decoration) — verified in CODE
§22 (haggle-RE) correctly found **NO closeness/atmosphere in the haggle DECISION** (お得意様度
parsed-then-discarded) and flagged the roster scan as the one place it could live. **It lives
there:**
- **DECORATION → WHO spawns:** real, via `FUN_0045e55c` — displayed items matching a customer's
  wanted attr/category raise that customer's weight (+2/+4), **front row ×3**, + shop-tier bonus.
  Not an abstract "atmosphere score" — it's item-preference matching against what's on display.
  Also `displayed/4` feeds `customer_count`. **LIVE-CONFIRMED:** poked shop tier 0→3, called
  `FUN_0045e55c(kyaku13)` on the engine thread → return 0→**10** (the +10 tier-3 term).
- **CLOSENESS → budget + item quality:** `DAT_045109a8[slot][kyaku]` (per-customer SHORT,
  clamped ≥0) is a genuine per-customer counter. `FUN_0045ecc0` scales BUDGET by
  `clamp(counter/10,0..10)`; `FUN_0045e80f` caps ITEM QUALITY by `counter/10`; the scan uses it
  for eligibility jitter. So higher closeness → richer customers who buy pricier items.
  **This is a NEW finding vs §22** (which only ruled it out of the haggle *decision*).
- **Open lead:** the serve-time INCREMENTER of `DAT_045109a8` isn't in its symbol xrefs
  (FUN_0045e939 = buysell-debug display; FUN_0045ecc0/edaa read; scan clamps/shifts) — the
  write is an indexed store in the sale-commit path Ghidra didn't attribute. Find it to port the
  accumulation.

## Key data tables / addresses (slot 0; per-slot stride 0x2dfc8 bytes = 0xb7f2 dwords)
- display grid `DAT_044f7030` (15×20 dwords, cell = id<<6 or -1; **IS the render-backing array** —
  poking a cell floats an item at its world coord even with no display furniture placed).
- shop tier `DAT_04510578` (int, /slot 0xb7f2); shop rank `DAT_0450fb98`; day `DAT_0450fb84`.
- kyaku record base **0x06a5ea90** + kyaku*0x2c670 (want-fields at +0x51a8 count/+0x51ac
  attr-mask/+0x5158 list; budget min/max at +0x51c8/cc via DAT_06a63c60/64). 50 kyaku.
- affinity/closeness `DAT_045109a8` short, `(slot*0xb7f2 + kyaku)*4` byte offset.
- news list `DAT_0450ad68` (20×0xc); news-def `DAT_056e0de0` (0xbc). item catalog `DAT_095d3804`
  (stride 0xb3 dwords; id/attr `+0/+? `, category `DAT_095d3808`, attr-mask `DAT_095d37f8` — NB
  these live far above the static image = a separately-allocated buffer, so a Frida raw read of
  the VA does NOT hit the in-process catalog; call FUN_004681f6 in-process instead).
- outputs: queue `DAT_0730aca0` (stride 6: kyaku/item_slot/kind), count `DAT_0730ac98`, eligible
  `DAT_06a5d450` (−2 terminated), perm `DAT_0730b1a8`.

## Golden reference (for the 1:1 port verify) — `tools/roster_scan_capture.py`
Captures retail's scan output for a seed sweep via the live harness:
snapshot the per-slot working arena → for each seed {restore arena, pin
`DAT_006023a0`, `call_function(FUN_0045edaa)`, read count/eligible/queue +
recover rng-draw count by stepping the LCG from the pinned→final seed}. The scan
is nearly idempotent: only **arena+8** (`DAT_044e37a0`, a per-slot increment
counter, all.c:50359) mutates, so a diff+single-poke restore is race-safe (a BULK
188KB arena write from the RPC thread races the live sim → CRASH; restore only the
changed dwords). Needs the daemon `readmem`/`writemem` cmds (added 2026-07-10).

Sample (fresh day-1, arena `sha16=78deef6f`, `docs/findings/data/roster-golden-day1.json`):
| seed | count | rng_draws | eligible | queue (kyaku,item_slot,kind) |
|---|---|---|---|---|
| 1 | 1 | 176 | [17] | (17,20,0) |
| 2 | 0 | 144 | [17,15,17] | — |
| 3 | 1 | 134 | [15] | (15,16,0) |
| 7 | 2 | 159 | [15,15] | (15,16,0)(15,17,0) |
| 42 | 1 | 167 | [15,11] | (15,17,0) |
| 19937 | 1 | 142 | [17,15] | (17,18,0) |

⇒ different seeds → different customers (kyaku 11/15/17) + items + **rng-draw
counts 134-176** (the sensitive 1:1 gate — a data-dependent-rng port MUST consume
the same count). **NB the fixture is valid ONLY for its exact arena snapshot** —
a fresh day-1 arena is NOT byte-deterministic run-to-run (the prologue varies
RNG/flags), so seed=1 gave count=1 here vs count=2 on another nav. The port's
primary gate stays a deterministic `--target both` trace where the scan runs
naturally; this JSON is a cross-check + regression reference. To make it a runnable
host test the port also needs the read-only deps (kyaku-def records `DAT_06a5ea90`
50×0x2c670, item catalog `DAT_095d3804`) loaded — a data-loading prerequisite for
the port, separate from the algorithm.

## ★ OBJDUMP CORRECTIONS (2026-07-10) — Ghidra dropped real logic (gotcha #1)
Verified against `objdump` of `vendor/unpacked/recettear.unpacked.exe`. The Ghidra
decompile is WRONG on two helpers; the binary values below are authoritative.
- **FUN_0045e55c tier scaling** — decompile shows a bogus argless `local_8 = __ftol()`;
  the asm (0x45e643-53) is a per-tier f32 MULTIPLY: `w = ftol((float)w * DAT_005c6bd0[tier])`
  BEFORE the flat +3/+6/+10. `DAT_005c6bd0` (f32 @0x5c6bd0) = **{1.0, 6/7=0.857142866,
  2/3=0.666666687, 3/7=0.428571433}** (idx 0 unused, guarded tier>0). ⇒ `weight = ftol(base·
  mult[tier]) + {0,3,6,10}[tier]`. Front-row ×3 applies to row0 cols {1,2,3,4,11,12,13}
  (DAT_005c6be0).
- **FUN_0040a68f is a BAND CLASSIFIER, not a distance** — the decompile folded the band map
  into the callers. Real: `dist=(float)sqrt(dx²+dy²)` (dx=b4b8-px, dy=b4bc-py; skips sqrt if
  d2≤0 ⇒ dist=0), then f32 thresholds (rodata) → return: `dist<1.153846→4, <3.461999→3,
  <9.230769→2, <12.692307→1, ≤15.0→0, >15.0→-1`.
- **FUN_0048439a (centroid, PORT-DEBT A3, was UNPORTED)** — produces DAT_0438b4b8/bc that
  a68f reads. `X = Σ deco_table[sel].x + Σ item-attr nudges` (0x200→+, 0x400→- on X;
  0x4000→+, 0x8000→- on Y; step 3 if item_id∈3000..3099 else 1), clamp ±0xd. 4 deco coord
  tables (rodata, {y@+0,x@+4} pairs): floor@0x5ccf4c(15)←sel DAT_04510580(dw 0xb37a),
  wall@0x5ccfc4(15)←0451057c(0xb379), carpet@0x5cd03c(8)←04510588(0xb37c),
  table@0x5cd07c(8)←04510584(0xb37b). Contents baked into `src/customer_roster.c`.
- **news-def DAT_056e0de0** = news record base(0x56e0d44)+0x9c ⇒ scan's de0+{0,4,8,0xc} map
  1:1 to the port's `news_record_t {attr_mask@0x9c, category@0xa0, item_id@0xa4,
  target_group@0xa8}` (g_news, already loaded). NO new news loader needed.
- **item/request pool DAT_06a5dbd8** (stride 0x13 dw, count DAT_06a5d448) — built at
  table-load (all.c ~75269-75348, inside FUN_00475270) from a token list; entry
  {attr@[0], id/cat@[1], quality-tier-idx@[2], …, category@[0x11]}. **Port lacks this
  table entirely** — a prerequisite for FUN_0045e80f + the scan's news/queue-fill blocks.
  Quality thresholds DAT_005c6c00 = {0,3,10,17,22}. range cols DAT_005c6c14={1,2,3,4,11,12,13}.
- FUN_005038ff/FUN_00451874 = debug-text sprintf+tile-draw ("%2d "); NO rng/state ⇒ stubbable.

## Port status (customer_roster.c / .h — 2026-07-10)
PORTED + host-tested (tests/test_customer_roster.c) + objdump-exact: **e55c weight, a68f band,
e505 shuffle, 0048439a centroid** + save_bank consts (closeness 0xb484, news-list 0x9d74,
news-latch 0xb778, sched 0xa97e, deco 0xb379-c). PENDING (need the item-pool loader / scan
context): **e80f item-pick, e6e0 event-state, ed12 range-gate, the 740-line scan body, +the
port-side golden-replay verify harness.** e6e0/ed12 decoded (ed12 = row-0 ONLY, index-mismatch
quirk: guard cell=grid[list[k]] but item=grid[k] — replicate faithfully).

## Live harness notes (reusable)
- RNG seed VA = **DAT_006023a0** (MSVC LCG `s=s*0x343fd+0x269ec3; return s>>16 &0x7fff`). Poke to
  pin the seed across A/B trials.
- `call_function` REQUIRES `argt` (e.g. `argt:["int"]`) — omitting it → "not a function". Runs on
  the engine thread at the pre-sim input-poll (safe for sim-touching fns).
- Reached day-1 shop via R1 (NEW GAME + ~70 `tap a`). Kyaku tables + save flags resident there.
