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

## 3.5 ⚠ CORRECTION (2026-06-17, EMPIRICAL): the tutorial sell uses **kind-2 `FUN_004658ab`**, NOT kind-4 `FUN_00463cfb`

The §3 table maps `b5a8==4 → SELL FUN_00463cfb`, and §3 explicitly never deep-read kinds
0/2/5. **The captured retail state proves the customer-service tutorial runs under `b5a8==2`
(→ `FUN_004658ab`)** the whole time — `FUN_00463cfb` (kind 4) is never reached in the
tutorial (it is the *player-initiated* sell with the item-select sub-menu, a later/different
flow). The haggle MATH is shared (both call `FUN_00460161`/`00460672`), so §4 is unaffected;
the STATE-MACHINE wrapper to port for the tutorial is **`FUN_004658ab`** (all.c:62461), which
is SIMPLER than `FUN_00463cfb`:

- **No item-select / price-setup states** (2/3/4 of the kind-4 machine). The customer already
  wants a specific displayed item; the player only names a price. States: 2 greeting → 6
  reaction → 0xf decision → 7 accept / 8 pushback / 9 reject (+ the leave/closing 0xb/10 in
  the master tick).
- **Decision (state 0xf, all.c:62600):** `FUN_004622d9()` poll (1 commit / 2 cancel→6 / 0
  cont). On commit: if **`b574(offer) < b8(ask)`** → (`ask < b580(floor)` OR tutorial `f406` →
  state 8 pushback) else state 9 reject (−stock); else (offer ≥ ask) → if `base·0.8 < ask` OR
  not-`f404` → `FUN_00460672()` (its 1/2/0 result only tunes the kyaku like-count +5/+2/+1, the
  accept is already decided by offer≥ask) + `FUN_00460e50()`→b53c → **state 7 ACCEPT**; else
  state 8. **`FUN_004658ab` does NOT call `FUN_0045ecc0` (budget ceiling) — that gate is kind-4
  only.**
- **Math wiring:** state 2→6 `FUN_00460161()`(offer-up) + `FUN_0045ff11()`(digit count); state
  6 holds `FUN_0045ff31()`(digit edit, U/D/L/R on the price) while Z is up, Z → state 0xf; state
  8→6 `FUN_00460161()` again (offer rises each round).
- **Details overlay** (prologue, `FUN_004681e6()!=0`): Button-3 opens the item-detail card
  (`FUN_004681db`/`FUN_00468286`, SE 0x2c6) — same as the buy flow's pressed&0x40 overlay.

**Empirical timeline** (cache `house-customer-tutorial-34f44b18/retail`, **offset 0:2700, 2700
frames BIT-EXACT**; probe = the extended `retail_fields.json` 0x48670f, +b534/b5a8/b56c/b574/
b584/b590/ask/base/b520/b5a0/b524/b544/b1e0):

| offset | event |
|---|---|
| 0 | cc08 **already 4** (entered during the load — master tick gated on `DAT_0438b1cc==2`), b534=0, **b5a8=2**, **b56c=1**, ask=base=1000, b524 already counting (61) |
| 90 | b534 0→**1** (greeting); ask=base=3000 |
| 969 | b5a0 ramps 1→60 (customer **arrival** slide); ask=base=1200 |
| 2345 | ask→1300 (player named a price) |
| 2440 | **b574=1536, b584=1** — the FIRST customer offer (round 1), the BARGAIN!! panel (raw 2920) |

**`b56c==1` but the on-screen customer is kyaku 13 ("Woman", silver hair — visually confirmed).**
So `b56c` is NOT the kyaku id directly — it indexes the queue/active-record slot; the entry's
`queue[0].kyaku=0xd` is reached through that indirection (the arrival/queue-advance logic at
all.c~59100-59720 sets `b5a8=2` + `b534` — the kind/greeting trigger, still to deep-read for
the master-tick chip). The haggle math binds the customer tuning fields from **kyaku 13's
record** regardless of the index.

## 3.6 entry → idle → greeting → machine flow (RESOLVED 2026-06-17) + function inventory

The per-frame master tick `FUN_00462403` (60136) drives, by `DAT_0730b534`:
- **b534==0 IDLE** (all.c:60670-61027): `b524`++ each frame. `FUN_00461068`(667B,@b524==0x14)
  customer-walk setup; the **story-event probe** (b524==0x32, the `FUN_0044ba2c(kind)` story
  triggers — inert for kyaku 13/tutorial); `FUN_00461303`(1167B) / `FUN_00461792`(1124B) =
  the **transaction-kind selector** (sets `b5a8` — **2 for the tutorial**); queue-advance at
  b524==0x3c. **The greeting trigger (all.c:61002-61021): `b524 > 0x77 (119)` AND `b52c >= 0x20
  (32)` → `b534 = 1`**, and the BASE PRICE is computed there: `bbc = item_base (095d37d4[id*0xb3]
  via FUN_004681f6)`, `bc0 (base) = bbc * bc4 (count)`, `bb8 (ask) = ftol(...)`, `b584 = 0`.
  (Matches the capture: b534 0→1 at off 90 ⇔ b524≈152.)
- **b534==1 GREETING** (master tick, all.c:60398-60425): first frame `FUN_00460a1a(rec,0,0)`
  (greeting line); Z (`b55c` set & 0x10) → `b534 = 2`.
- **b534>=2 → FUN_004658ab** (the b5a8==2 machine, §3.5): greeting 2 → offer 6 → decision 0xf
  → accept 7 / reject 9 / pushback 8 → closing 0xb/10 (back in the master tick).

**Chip-2 function inventory (the master-tick + machine port):** master `FUN_00462403`(5618B,
the arrival/leave anim + bubble pos `DAT_0438cc38/3c/40` + patience + the b534 switch + b5a8
dispatch), kind selector `FUN_00461303`(1167B)/`FUN_00461792`(1124B), walk setup
`FUN_00461068`(667B), the **machine `FUN_004658ab`**(62461), input poll `FUN_004622d9`(60044),
digit count/edit `FUN_0045ff11`/`FUN_0045ff31`, line picker `FUN_00460a1a` + `FUN_00460f16`,
accept side-effects `FUN_00460d52`/`FUN_004606fc`/`FUN_00460b93`/`FUN_00460b3a`/`FUN_00460083`/
`FUN_0046002a`. Wire the §4 math at FUN_004658ab states 2→6/8→6 (`FUN_00460161`) + 0xf
(`FUN_00460672`). **Verify** state evolution vs the BIT-EXACT capture cache
`runs/studio-v3-cache/house-customer-tutorial-34f44b18/retail` via `flow_diff` on the extended
0x48670f probe (the port must emit b534/b5a8/b574/ask/base/b584/b590 when cc08==4).

## 3.7 ⚠⚠ CORRECTION (2026-06-17 PM, EMPIRICAL — supersedes §3.5/§3.6's machine choice): the tutorial sell is the **SCRIPTED machine `FUN_00461c00` (`b51c==1` path)**, NOT `FUN_004658ab`

§3.5/§3.6 concluded "tutorial = kind-2 `FUN_004658ab`" from `b5a8==2` alone — that inference is
**wrong**: it missed the `DAT_0730b51c` gate. The BIT-EXACT capture cache
`house-customer-tutorial-34f44b18/retail` (the SAME one §3.5 read) proves the haggle is driven by
the **scripted-sell machine `FUN_00461c00`** the whole window. Decisive evidence (extracted from
the `0x48670f` probe's `call_trace.jsonl`, all 2490 once-per-frame rows):
- **`b534` only ever takes values {0,1}** — the kind-machine states (2/6/0xf/7/8/9) are NEVER
  entered. `b544` (per-state timer) climbs to 2350+ **without ever resetting** while `b534`≡1.
- Yet **`base`, `b5a0` (arrival ramp 0→60), and `b574` all change *during* `b534==1`** (base
  1000→3000@off90→1200@off969; offer `b574=1536`/`b584=1`@off2440). The ONLY `b534==1` driver
  that mutates those is **`FUN_00461c00`** (dispatched from the master tick's `b534==1` arm when
  `b51c!=0`, all.c:60406; it sets base/ask via its opcode-2, `b5a0=1`, and `b574` via
  `FUN_00460161` at opcode-4 / all.c:59981). The plain greeting (`b51c==0`) recomputes none of these.
- **`b56c==1`** (not 13): set by `FUN_00461303`'s **`f404` sell-active branch** (all.c:59312-59317
  → `b56c=queue[0].kyaku`, `b5a8=2`, `b5a4=0xc0`), and that queue head is `1` only via
  `FUN_0045edaa`'s **sell-active else-branch** (all.c:58234 → `b51c=1`, `queue[*]={kyaku:1}`,
  `count=3`). The f406 *tutorial* branch would give `b56c=13`; the roster scan would give a real
  kyaku id. So **`f404` is set, `f406` is NOT**.
- **Math check (clincher):** offer `1536 = base 1200 × 1.28` ⇒ `FUN_00460161` round-0 with
  `init_eff≈128` and **NO `f406` override** (the override would force `work·1.5 = 1800`). Confirms
  `f406` clear ⇒ sell-active / `b51c` path.
- **Corroborated by the port itself:** `src/tables_tuto.h` already names **`FUN_00461c00` (line
  59759) as the consumer** of the `DAT_005d1fc8` script table, parsing `data/tuto1..3.txt` with
  exactly the opcode vocab (`CHR0/CHR1` dialogue, `PRID`/`PRIA` price cues, `GOTO`, `BUN0`, …) that
  `FUN_00461c00` dispatches. The scripted tutorial-sell data layer is **already built + host-tested**.

**Consequence for the port:** this capture window (off 0:2700) is the **scripted tutorial sell**
(greeting → arrival → first scripted offer; only reaches round 1). `FUN_004658ab` (the live kind-2
customer machine) is **not exercised here** — it would be the "first real customer" *after* the
tutorial+closing (beyond this window; needs a re-windowed capture). The corrected Chip-2 inventory:
- **Entry:** `FUN_0045edaa` **sell-active else-branch** (`b51c=1`, `queue[0..2]={kyaku:1,kind:0}`,
  `count=3`) — currently PORT-DEBT in `customer_service.c` (Chip 1 ported only the f406 branch).
- **Kind selector** `FUN_00461303` **f404 case** (the 6-line head: `b56c/b570` from queue, `b5a4=0xc0`,
  `b5a8=2`, return 1).
- **Master tick** `FUN_00462403` (arrival ramp `b5a0`, bubble pos, the `b534==1 && b51c` →
  `FUN_00461c00` dispatch, the b524 idle/greeting trigger).
- **The scripted machine `FUN_00461c00`** (1753B, all.c:59745) — the real core: PC `_DAT_0730b604`
  walks `g_tuto[b5b0*200 + pc]`; opcodes = dialogue (`FUN_0046098f`), price-set, `PRID`/`PRIA`
  (`FUN_0045ff11`/`31` digit + `FUN_00460161` offer), conditional `GOTO` (`FUN_004623bc`) on ask/base
  ratio thresholds. Wire `customer_haggle` (`FUN_00460161`) + `tables_tuto` (`g_tuto`).
- Helpers `FUN_004623bc` (GOTO target lookup), `FUN_0046098f` (dialogue-line setup), `FUN_0045ff11/31`,
  `FUN_004622d9` (input poll).
**Verify** the b534/b5a8/b56c/base/ask/b574/b584/b5a0 trajectory above vs the `34f44b18` cache via
`flow_diff`. `b5b0` (`DAT_005c6bb0`, the script-file index) is set by `FUN_00461bf6` at the cc08=4
entry sites (house_update 87048/87149/87463/87692) — determine its tutorial value when porting.

## 4. Haggle math — ✅ PORTED `src/customer_haggle.{c,h}` (DISASM-EXACT, +9 host tests)

**The Ghidra decompile is WRONG here — it dropped the x87 stack AND mis-rendered the
rng-driven values as deterministic.** Transcribed from the unpacked disasm; every const
decoded from `.rdata`; the LCG-draw ORDER is replicated (load-bearing for RNG parity).
`u` below = `rng_next_unit()` = `FUN_00471089` = `(rng_next15()&0x7fff)/32768.0` (0..1);
`rng` = `rng_next15()` = `FUN_005041f6`. ftol = x87 truncate-toward-zero.

Float consts (all verified): `0.5=0x51935c 2.0=0x519314 0.1=0x5193a0 0.35=0x519bc4
0.45=0x519b58 1.0=0x519364 100.0=0x519368 0.0=0x519320 1.5=0x5198e0 0.2=0x5198d8 65.0=0x519cd8
0.65=0x519df0 0.03=0x519900 0.05=0x5198f8 5.0=0x51953c 32768.0=0x519ef8`;
`FUN_00460672`: `1.005f=0x519e08 0.995d=0x519e00 1.05f=0x5198ac 0.95d=0x519df8` (×1.005/×1.05 are
FLOAT/DWORD, ×0.995/×0.95 are DOUBLE/QWORD).

**`FUN_00460161` offer UP** — `P=b57c` (seeded = base `bc0`). `t=FUN_004361b2(b5a4)` price-trend
(**PORT-DEBT — default 0 ⇒ no tilt, no rng draw**). The tilt is **RNG-DRIVEN** (the RE first-pass
missed this), the trend only picks the branch:
- t≥1:  `b57c = ftol((u·0.5 + 2.0)·b57c)`  ← draws 1 u
- t≤−2: `b57c = ftol((u·0.1 + 0.35)·b57c)` ← draws 1 u
- t==−1:`b57c = ftol((u·0.1 + 0.45)·b57c)` ← draws 1 u   (t==0: unchanged, NO draw)

Round 0 (`b584==0`), draws in ORDER:
- `b580 (floor) = ftol((u + 2.0)·b57c)`                       ← **rng**, NOT 1.0+0.1·trend
- `init_eff = initial(+0x51c8)`; if `random(+0x51cc)>0`: `init_eff += (rng%(2·random+1)) − random`
- `b574 (offer) = ftol(b57c · init_eff / 100.0)`
- `b588 (accept_ref) = ftol((u·0.1 + 1.0)·b57c)`             ← **rng**, NOT deterministic
- `b584++`

Round ≥1 (`b584!=0`): `rate=(round==2 ? rise1(+0x51c0) : rise2(+0x51c4))`; `b574 += ftol(rate·b57c/100)`;
gullibility `g=+0x51bc`: if `g>2` `h=g/2; g_eff=(rng%h)+h` (1 rng) else `g_eff=g`;
`step = ((bb8 − b574)·g_eff)/100`; clamp `step ≤ base·0.5`; if `step>0`: `b574 += ftol(step)`;
if tutorial (`f406[slot]`): `b574 = ftol(b57c·1.5)` (LAST, overwrites); `b584++`.

**`FUN_004603cf` offer DOWN** — same tilt. Round 0: if `b56c==0x12` `b57c=ftol(b57c·5.0)`,
`bb8=ftol(bb8·5.0)`; `b580 = ftol((u·0.1 + 0.2)·b57c)`; same `init_eff`;
`b574 = ftol(b57c·(65.0 − (init_eff − 100.0))/100)` = `·(165 − init_eff)/100` (seeds LOW);
`b588 = ftol((u·0.1 + 0.65)·b57c)`. Round ≥1: `b574 −= ftol((u·0.03 + 0.05)·base)` (an EXTRA random
decrement, **draws 1 u** — distinct from UP); `b574 −= ftol(rate·b57c/100)`; gullibility step, and
`if step<0: b574 += ftol(step)` (NO clamp, no tutorial override).

**Accept/reject `FUN_00460672`** — `m=b588`: `iVar1=ftol(m·1.005f)`, `iVar2=ftol(m·0.995d)`,
`iVar3=ftol(m·1.05f)`, `iVar4=ftol(m·0.95d)`; if `m<110` then `iVar2:=iVar1`. With `ask=bb8`:
`iVar2 ≤ ask ≤ iVar1`→**1 ACCEPT**; else `iVar4 ≤ ask ≤ iVar3`→**2 COUNTER**; else→**0 REJECT**.
(1.005f/1.05f round DOWN ⇒ the upper edges are ftol(m·1.005)−1 e.g. 1607/1679 for m=1600.)

**Budget `FUN_0045ecc0(idx,slot)`** (pure int): `v = clamp(market_price/10, max 10)`;
`ceiling = budget_low + (budget_high − budget_low)·v/10`. market_price = the int16 at save-bank
dword **0xb484 + slot** (`DAT_045109a8`). In state 0xf, ask rejected (→0x28) if `ask > ceiling·N·1.2`.

**NOT yet wired** into the (unported) cc08==4 state machine — exact-edge FP is x87(port)≡x87(retail)
by construction; a Frida pure-function-diff is the recommended belt-and-suspenders follow-up.

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

**Landed 2026-06-17:** the harness ({wait,timeout} cross-target load-burst bridge, `47cdd8c` —
the port now drives the retail recording + v3-joins, occurrence-aware) + the **haggle math**
(`src/customer_haggle.{c,h}`, `d0ac215` — budget/accept-reject/offer up+down, disasm-exact, +9 host
tests, NOT yet wired). **Remaining (the next-session queue, all ✗):**
1. **Entry** — cc08 1→4. The tutorial haggle AUTO-STARTS (shows ~440f before the recorded Z, so it's
   an auto-arrival site, the `f406` forced sale — NOT the Z-press path). Port the `f406` branch of
   `FUN_0045edaa` (forces kyaku 13) + the cc08=4 set in `FUN_0048670f` + the session-state scaffolding.
2. **Master tick** `FUN_00462403` (arrival anim, bubble pos, patience, the b534 switch) + **sell
   machine** `FUN_00463cfb` (greeting→item-select→price-setup→offer→decision→accept/leave), wiring the
   §4 math at states 5/6/0xf.
3. **Render** `FUN_0046602e` (shopmode.tga panel + portrait) + `FUN_00466b7b` (BARGAIN!! banner, base
   price, name-a-price, offer buttons, cursor) — verify via v3 content-match vs the cached retail
   haggle frame (the join is PARTIAL 440/1200 by the load-seam; content-match specific game states).
4. **Tutorial dialogue** (Tear's `FUN_00460a1a` lines overlaid on the haggle).

Original census (pre-port):
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

## 8. cc08==4 WIRED (2026-06-18) + the trace-replay BLOCKER (post-load walk-input eaten)

**Landed (`7e163bb`):** the cc08==4 subsystem is now wired into the engine loop —
`scene1_player_ctrl.c`: the bVar3 **Z-at-counter entry** (counter rect X(-5,0)/Z>6.9
tier<3 + facing octant + Z → bank `f404`=1, `FUN_00461bf6(2)`, cc08=4, `session_init`;
PORT-DEBT(cs-entry-flags) on the f3fd/f3ff customer-queued gate set by the unported
customer-spawn / iv1_7 machinery), the **cc08==4 per-frame arm** (d3e load-gate release +
`customer_service_master_tick`; PORT-DEBT(cs-arrival-anim) on the f405 pose + px ramp), the
`notify_loaded`→`b1cc=1` fix (the engine d3e worker BODY value the master tick AND render
both read), and the broadened `0x48670f` call-trace (cc08 + b534/b5a8/b56c/base/ask/b574/
b584/b590/b520/b5a0/b524/b544). 3330 host tests pass.

**THE SCENARIO IS A *LOAD* TRACE (user-confirmed 2026-06-18):** Continue → loads `cad868`
(slot 0, the **pre-tutorial-entry** shop state — items placed but the runtime f404/f3fd/f406
flags are 0; later slots 1-5 in the same .sav have them, = post-tutorial saves) → drop at the
shop **back-center** (px −0.30, pz 9.35, facing +π/2) → walk LEFT + turn to the sell counter
→ Z initiates the scripted haggle tutorial → 5 rounds (the `PAUSE_OPEN` episodes) → closing
dialogue (`CONV_POSE`/`TEXT_ANIM`) → first real customer.  NOT a new-game prologue.

**BLOCKER (the port can't reach cc08==4 in this trace yet):** driving the port shows it stays
**cc08==1 the whole window**, player **frozen at the back-center pose-init** (px −0.30, facing
+π/2, panim 0).  Frame-by-frame: post-load the port renders HOUSE from ~f310 but **free-roam
(cc08==1) doesn't begin until f466** — a ~156-frame gap with NO active fade and NO
`dialogue_tick` (it never fires).  The `0x48670f` emit + the walk arm share the gate
`s_cc08==1 && !intro_dialogue_active/_loading/_posing`; it only clears at f466, so the port is
in a **dialogue posing/loading state (or cc08≠1) for ~156 frames after the Continue-load**,
which gates the walk arm OFF.  The recorded walk/turn input is **relative frame 66-156 to the
LOADING_END anchor** — it fires DURING that gap and is wasted; only the Z@156 lands as free-roam
begins.  ⇒ the player never turns/walks to the counter ⇒ the bVar3 entry never fires ⇒ the
caprange's haggle-inputs then drive free-roam movement (the player drifts to the door).  Retail's
post-load gap is short (the walk input lands in free-roam), so it reaches the counter.

This is the known PORT-DEBT (`scene1_player_ctrl.c:1814` "cc08 timing — it should flip to 1
only at the real free-roam boundary FUN_004850ec, after the prologue"): after a Continue-load
the port's free-roam boundary lands ~156 frames late (lingering `intro_dialogue` posing/loading
or a late cc08=1).  **The NEXT step is to fix the post-load free-roam-boundary timing** (WHY is
`intro_dialogue_posing()`/`_loading()` true — or cc08≠1 — for ~156 frames after a Continue-load
with no cutscene?  Likely the port emits the `LOADING_END` anchor at raw load-complete while
retail emits it at the free-roam boundary — align the anchor emit, OR shorten the post-load
settle) so the walk input lands in free-roam — THEN the bVar3 entry fires + the state machine/
render can be verified.  The cc08==4 logic itself is host-verified (`test_cs_scripted_first_offer`
= capture trajectory 1:1).  Per user direction (2026-06-18): port the full tutorial up to the
first real customer; don't move to real (kind-2) haggling until this trace plays in full on
both sides.
