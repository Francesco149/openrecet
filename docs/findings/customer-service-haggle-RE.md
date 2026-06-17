# Customer-Service / Selling / Price-Haggle subsystem — RE map

Structural map of the in-shop **customer-service / selling / price-haggle**
gameplay (the first-customer selling tutorial and the general sell loop).
Grounded in `docs/decompiled/all.c`, verified vs `vendor/unpacked` objdump +
the PE `.rdata` float table. **As of 2026-06-17 the ENTIRE subsystem is a port
GAP** (only a comment-ref to `FUN_0045edaa` in `src/npc_schedule.h`; the
`docs/port-ledger.md` ✓ for it is stale — no `.c` implementation exists).

Scenario: `tests/scenarios/house-customer-tutorial` (user recording
rec-20260617-051426: NEW_GAME → prologue → walk to sell counter → tooltip → Z →
customer-service tutorial alternating dialogue with 5 haggle rounds → closing
cutscene → first real customer).

## State globals (the `DAT_0730bXXX` block)

| Global | Role |
|---|---|
| `DAT_0438cc08` | HOUSE interaction mode: 1=free-roam, **4=customer-service**, 0xf=counter-display menu, 0x32=display-exchange |
| `DAT_0730b534` | **per-customer interaction sub-state** (the SELL machine var, §3) |
| `DAT_0730b544` | sub-frame timer (`==1` = first frame of a state) |
| `DAT_0730b5a8` | **transaction-type selector** → which per-kind machine runs |
| `DAT_0730b56c` | active customer **kyaku-record index** (record = `&DAT_06a5ea90 + idx*0x2c670`) |
| `DAT_0730b570` | the **item slot** being transacted |
| `DAT_0730b5d8` | resolved want-list index (`FUN_0045e80f` return) |
| `DAT_0730b584` | **haggle round counter** (0 = first offer, ++ per round) |
| `DAT_0730b574` | customer's current **offered price** |
| `DAT_005c6bb8` | **player's asking price** (the input/accumulated number) |
| `DAT_005c6bc0` | **base/reference price** (sum of item DB base prices); `DAT_005c6bbc` = running sum |
| `DAT_0730b57c/b580/b588` | working haggle bounds (ceiling / floor / patience-% ) |
| `DAT_0730b590` | **patience** (0..0xf; 0xf → forced decision) |
| `DAT_0730b58c` | per-round input delay gate (0..5) |
| `DAT_073dddd4` | per-frame **input mask**: 0x10=Z/A, 0x20=X/B, 0xc=up/down, 0x40=details, 0x70=any |
| `DAT_044e37a4[slot*0xb7f2]` | **player gold** (working save-bank); a sale ADDS to it |
| `DAT_044f7030[…]` | display grid (`SAVE_BANK_FIELD_DISPLAY_GRID`, dword 0x4e26) = items for sale |
| `DAT_0450f404[slot]` | sell-active flag (set on Z at counter) |
| `DAT_0450f406[slot]` | **forced/scripted tutorial sale** flag (set by iv1_7) |
| `DAT_073dddb8/bc/c0` | `buysell.txt` debug override (`g_buysell`, parsed in `src/tables_buysell.c`) |

**Kyaku haggle fields** (record-relative; folded DATs verified by disasm immediates):
`+0x51a8` like_count (`DAT_06a63c38`), `+0x51ac` like_attr_mask (`DAT_06a63c3c`),
`+0x51b4` activity_time (`DAT_06a63c44`), `+0x51bc` gullibility 騙 (`DAT_06a63c4c`),
`+0x51c0` rise1 上昇1 (`DAT_06a63c50`), `+0x51c4` rise2 上昇2 (`DAT_06a63c54`),
`+0x51c8` initial 初回 (`DAT_06a63c58`), `+0x51cc` random ランダム (`DAT_06a63c5c`),
`+0x51d0/d4` budget low/high (`DAT_06a63c60/64`), `+0x5158` like_kinds (`DAT_06a63be8`).
**`+0x51b8` suspicion (疑) is parsed but NEVER read** — dead/vestigial in this build.

## 1. Entry / trigger — `FUN_0048670f` (house_update), all.c:87491–87698

The `cc08==1` free-roam arm (same walk arm as the display-stand menu, reached after the `bVar17` door guard).
- **`bVar3`** (87548–87553) = customer-at-counter + player in sell zone + facing it:
  player pos `DAT_056da1d8`(X)/`DAT_056da1e0`(Z) in the counter rect (tier<3: X∈[−5,0],Z>6.9;
  else X∈[5.7,10.7],Z>15, by shop tier `DAT_04510578`) AND facing octant
  `_DAT_056db05c ∈ (−1.885, −1.257)` AND **`DAT_0450f3fd[slot]==1`** (customer queued).
- **`DAT_0438be7c`** = near-counter affordance cooldown (=0x14 at 87577; dec at 87557). Its `!=0`
  branch (87651) opens the counter-DISPLAY menu (`cc08=0xf`, `FUN_004850fe`) — a *different* feature.
- **Z → enter sell** (87680–87695): `bVar3 && DAT_0450f3ff[slot]!=0` and Z(`&0x10`) →
  `DAT_0450f404[slot]=1`, `FUN_00461bf6()`, **`DAT_0438cc08=4`**, **`FUN_0045edaa()`**.
  7 other `cc08=4` auto-arrival sites (86895/87046/87094/87150/87460/87487/87693).
- **Per-frame while `cc08==4`** (87366): `FUN_0047019f(); FUN_00462403()` (master tick, 87432).

No separate GO!-style FUN for the sell counter per the agent — but the USER reports a visible
counter tooltip ("similar to GO!"); confirm in-capture whether it's the FUN_0040a765 emote bubble
(db004 cell) repurposed at the counter, or the customer-arrival "!" in FUN_00462403.

## 2. Session init / customer select — `FUN_0045edaa` (0x45edaa, 4455 B), all.c:57338

1. Zeroes `DAT_0730bXXX`, `DAT_0730b534=0`.
2. Counts displayed items (57432), `customers = (rng&1)+1+displayed/4`, cap 5 (57455).
3. Roster build (57555–58211): all 50 kyaku, gated by activity-time vs `DAT_0450fb88`, attr mask,
   story flags `DAT_0450f4xx[slot*0x2dfc8]`; → eligible list `DAT_06a5d558[]`, sort `FUN_0045e505`,
   fill queue `DAT_0730aca0[entry*6]` = {kyaku_idx, item_slot, kind, …}.
4. Item-they-want: `FUN_0045e80f(kyaku_idx, item_slot)` (all.c:57074) — match like_attr_mask/like_kinds
   vs displayed items, `rng % match_count` (57139); −1 → skip.
5. **Forced paths:** `DAT_073dddb8!=0` (buysell `ok:`) → customer `DAT_073dddbc` kind `DAT_073dddc0`;
   **`DAT_0450f406!=0` (TUTORIAL)** → fixed customer `0xd` (**kyaku 13 = "Woman"**) (58218–58231).
6. Active record ptr recomputed per frame = `&DAT_06a5ea90 + DAT_0730b56c*0x2c670`.

## 3. Sell/haggle state machine

**Master tick `FUN_00462403`** (0x462403, 5618 B, all.c:60136): arrival/leave anim
(`DAT_0730b5a0` enter, `DAT_0730b520` leave → `FUN_004526f5` tile-dissolve), bubble pos
`DAT_0438cc38/3c/40`, patience tick, switch on `DAT_0730b534` (owns states 1 greeting,
0x1e sold-pause, 10/0xb/0xc/0xd closing, 0x14/0x15 queue-advance, 0 idle).
Transaction dispatch by **`DAT_0730b5a8`** (60563):

| b5a8 | machine | FUN |
|---|---|---|
| 4 | **SELL (haggle)** | **`FUN_00463cfb`** (3371B) all.c:61269 |
| 1 | BUY-from-customer | `FUN_00464af0` 61885 |
| 0 | kind0 | `FUN_00465372` 62239 |
| 2 | kind2 | `FUN_004658ab` 62461 |
| 3 | chat | `FUN_004639f5` 61128 |
| 5 | kind5 | `FUN_00464a26` 61840 |

### SELL graph — `FUN_00463cfb`, var `DAT_0730b534`

| b534 | meaning | actions / transitions |
|---|---|---|
| 2 | greeting | first frame `FUN_00460a1a(rec,0x10,1)`; Z → init item-pick `DAT_0730b274`, `DAT_005c6bcc=1`, →3 |
| 3 | **item-select menu** (reuses display-menu `FUN_00469414`/`469a9f`/`468338`) | nav/confirm/cancel/details; confirm computes price →4 |
| 4 | price setup | sum base prices → `DAT_005c6bbc`→`bc0`, seed `bb8`; single-match →5 else `FUN_00460eba()` →0x10 |
| 5 | first offer | Z: `FUN_00460161()` (haggle-up) + `FUN_0045ff11()` (digits) →6 |
| 6 | customer reaction | `FUN_00460a1a(rec,9,0)`; Z →0xf |
| **0xf** | **HAGGLE DECISION** | `FUN_004622d9()` input poll (1=commit/2=cancel/0=cont); `FUN_0045ecc0()` budget; `FUN_00460672()` accept/counter/reject → accept 7 / pushback 8/0x28 / reject 9 |
| 7 | ACCEPT→sale | `DAT_044e37a4 += DAT_005c6bb8` (gold), SE 0x14d, `FUN_00460d52(0)/4606fc(10)/460b93()` →10 |
| 8 / 0x28 | "too much" pushback | `FUN_00460f16()` line; `FUN_00460161()` →6, or accept →0xb |
| 9 | final reject | →0xb (leave) |
| 0x10/0x11 | customer COUNTER-OFFER | `FUN_00460a1a(rec,8/2,1)`; 0x10→`FUN_00460161`, 0x11→`FUN_004603cf` (down) →0x12 |
| 0x12 | leave after counter | →0xb |
| 0xb/10/0xc/0xd | closing & queue advance | in `FUN_00462403` → next customer / cc08→0/1 |

`FUN_004622d9` (all.c:60044) player input poll: patience++ ; Z→1(commit if patience spent),
X→2(cancel), up/down→toggle `DAT_0730b540` + cursor; patience≥0xf forces 1.

## 4. Haggle math (EXACT — FP consts from `.rdata`; Ghidra dropped x87 → read from disasm)

Floats: `0.5 0x51935c, 2.0 0x519314, 0.1 0x5193a0, 0.35 0x519bc4, 0.45 0x519b58, 100.0 0x519368,
1.0 0x519364, 1.5 0x5198e0, 0.2 0x5198d8, 65.0 0x519cd8, 0.65 0x519df0, 5.0 0x51953c`;
`FUN_00460672`: `1.005 0x519e08, 0.995 0x519e00, 1.05 0x5198ac, 0.95 0x519df8`.

**`FUN_00460161`** (offer UP, all.c:58422). `P = DAT_0730b57c` (init = base `bc0`).
`t = FUN_004361b2(DAT_0730b5a4)` = item price-trend level (**unported PORT-DEBT daily-market
classifier — default t=0 makes the tilt a no-op**). `trendf = (float)t`. Trend-tilt:
- t≥1: `P = ftol(P·(0.5·trendf + 2.0))`
- t≤−2: `P = ftol(P·(0.1·trendf + 0.35))`
- t==−1: `P = ftol(P·(0.1·trendf + 0.45))`  (t==0 unchanged)

Round 0 (`b584==0`): `b580(floor) = ftol(P·(trendf·0.1+1.0))`;
`init=(float)record[+0x51c8]`; if `record[+0x51cc](random)>0`: `init += (rng%(2·random+1)) − random`;
**`b574(offer) = ftol(P·init/100.0)`**; `b588 = ftol(P·(trendf·0.1+1.0))`.

Round ≥1 (`b584!=0`): `rate = (round==2 ? rise1(+0x51c0) : rise2(+0x51c4))`; `b574 += ftol(P·rate/100)`.
Gullibility (騙): `g=record[+0x51bc]`; if `g>2`: `h=g/2; g_eff=(rng%h)+h` else `g_eff=g`;
`step = ftol(((bb8 − b574)·g_eff)/100)`; clamp `step ≤ bc0·0.5`; if `step>0`: `b574 += step`.
If tutorial (`DAT_0450f406`): `b574 = ftol(P·1.5)`. `b584++`.

**`FUN_004603cf`** (counter DOWN, all.c:58487): mirror; round-0 uses 65.0/0.65; rounds SUBTRACT
rise1/rise2 + gullibility; if `DAT_0730b56c==0x12` ask `bb8` & base pre-scaled ×5.0.

**Accept/reject `FUN_00460672`** (all.c:58549): `m=DAT_0730b588`; `lo1=ftol(m·1.005)`,
`hi1=ftol(m·0.995)`, `lo2=ftol(m·1.05)`, `hi2=ftol(m·0.95)`; if `m<110` then `hi1:=lo1`.
With `ask=bb8`: `ask∈[lo1,hi1]`→**1 ACCEPT**; else `ask∈[lo2,hi2]`→**2 COUNTER**; else→**0 REJECT**.

**Budget `FUN_0045ecc0(idx,slot)`** (all.c:57284, pure int): `v = clamp(market_price[slot]/10, max 10)`;
`ceiling = budget_low + (budget_high − budget_low)·v/10`. In 0xf, ask rejected (→0x28) if
`ask > ceiling·N·1.2` (61737).

## 5. Tutorial scripting (iv cutscenes bracket the live haggle)

The haggle is the LIVE machine (not scripted); the per-customer lines come from `FUN_00460a1a(rec,
mode, flag)` (all.c:58772) — random line from the customer's own dialogue buffer (kyaku `file:` →
`record+0x6e70`, counts at `+0x6df8`); mode = category (greeting 0x10/0xf, reaction 9, accept 8,
reject 2/0xe/0x13). With buysell-debug it pulls fixed `msg##`/`rmsg##` from `DAT_073b1a18/68`.

The bracketing CUTSCENES run through the already-ported ivent interpreter (`scene1_intro_dialogue`
/ `FUN_0046c320`), scheduled by `FUN_0044bd0d` (all.c:45406). Gate pattern:
`if (flag[slot] && !done[slot] && DAT_0438b1c8==0){ DAT_005c7a2c=scene; DAT_005c7a30=sub;
DAT_0438b1c8=2; FUN_00452d07(arg); done=1; }`. Selling-tutorial chain (save-arena `0x2bc..`):

| iv | scene,sub | gate→done | content |
|---|---|---|---|
| iv1_4 | 1,4 | `f3f4 && !f3f6` (sets f3f2) | "crash course… put some items on display." |
| iv1_5 | 1,5 | `!f3fc && f3fb==1` (row-0 cell filled) | "Those counters by the window…" |
| iv1_6 | 1,6 | `!f3fe && f3fd==1` (all cells filled) → f3ff | "Alright. That should do for displaying our wares." |
| **iv1_7** | 1,7 | `!f401 && f400==1` → **sets f406** + f407 | "open the store proper… handle them as we practiced." |
| **iv1_8** | 1,8 | `!f403 && f402==1` → f409,f407 | "Congratulations. You did well… I did it!" |

Loop: iv1_4/5/6 (display) → **iv1_7 sets f406** → next counter interaction = **forced tutorial
haggle (kyaku 13)** → completing sets f402 → iv1_8. iv1_5/iv1_6 are ALREADY ported+1:1
(item-display-2 arc). iv1_7/iv1_8 + the f400/f401/f402/f403/f406 flags + `FUN_0044bd0d` branches
are unimplemented. `FUN_0044ba2c` = the `start_single` shim these reuse (from merchant-guild-RE).

## 6. Render

- **`FUN_0046602e`** (0x46602e, 2668B, all.c:62872) — called from `FUN_00409925` (the merchant-HUD
  fn = port's `scene1_merchant_hud_render`, call at 6423). Draws the customer **portrait + shop-mode panel**.
- **`FUN_00466b7b`** (0x466b7b, 5305B, all.c:63270) — overlay tail gated `b1c0==1` (7044). Draws the
  **price/offer number, name plate, item card, offer buttons & cursor**.
- Helpers `FUN_00465db4` (text/number at pos), `FUN_00466a9a` (offer-button row); price→string via
  `FUN_005038ff` (ported, `scene_floor.c`) `"%dpix"`.

**Textures** (loaded by `FUN_0047193c`, handles already in `src/scene_buy.h`, dormant):
`DAT_073a9580` = **shopmode.tga** (0x400×0x200) panel; `DAT_073cc8d0` = **chrname.tga**
(`bmp/ivent/chrname.tga`) name plate; item icons `g_scene_buy_sprites[page][slot]` (`DAT_073aa7e8`);
item-select sub-menu reuses `item_win.tga`/`data_win.tga` via `FUN_0046b00a` (ported,
`scene1_display_menu.c`). Bubble anchor `DAT_0438cc38/3c/40`.

## 7. Port status & plan

Entire subsystem ✗ (only `npc_schedule.h:64` comment-ref; ledger ✓ for FUN_0045edaa is stale).
Reusable: `tables_kyaku` (haggle fields loaded; suspicion proven unused), `tables_buysell`
(g_buysell override the forced path reads), `scene_buy.{c,h}` (shopmode/chrname/icon loaders, dormant),
`scene1_display_menu.c` (item-select state-3 verbatim reuse), `scene1_intro_dialogue`+`FUN_0046c320`
(iv1_7/iv1_8 cutscenes), `scene1_merchant_hud_render` (host for the FUN_0046602e render call).

**Highest-value first chip:** the pure haggle math trio (`FUN_00460161`/`004603cf`/`00460672` +
`FUN_0045ecc0`) — host-testable, constants recovered above; the only external dep `FUN_004361b2`
(trend level) is known PORT-DEBT → default t=0 (tilt no-op).

**Caveats:** the haggle FP math was reconstructed from disasm (Ghidra dropped x87). Immediates +
sequence decoded, but the multi-term grouping (esp. the `P·(c1·trend+c2)` tilt) should be
pure-function-diff'd vs retail (Frida) on a few `(base,init,random,gullibility,rise1/2)` tuples at
port time. State-table arrows read directly from the decompile (solid). b5a8 kinds 0/2/5
(non-sell/buy machines) not deep-read (out of scope for the sell tutorial).
