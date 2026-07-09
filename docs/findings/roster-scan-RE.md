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

## Live harness notes (reusable)
- RNG seed VA = **DAT_006023a0** (MSVC LCG `s=s*0x343fd+0x269ec3; return s>>16 &0x7fff`). Poke to
  pin the seed across A/B trials.
- `call_function` REQUIRES `argt` (e.g. `argt:["int"]`) — omitting it → "not a function". Runs on
  the engine thread at the pre-sim input-poll (safe for sim-touching fns).
- Reached day-1 shop via R1 (NEW GAME + ~70 `tap a`). Kyaku tables + save flags resident there.
