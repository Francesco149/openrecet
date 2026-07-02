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

## 8. cc08==4 WIRED (2026-06-18) + the trace-replay BLOCKER ✅ FIXED (segtrace timeout ate the walk)

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

**BLOCKER ✅ ROOT-CAUSED + FIXED 2026-06-18 — it was a TAS-REPLAY (tooling) bug, NOT the cc08 /
LOADING_END timing the first pass hypothesized.**  The cc08==4 entry + walk-to-counter work
fine; the SEGTRACE replayer was eating the walk input.

*The earlier (wrong) hypothesis* was "the port emits LOADING_END at raw load-complete (~f310)
while free-roam starts ~156f later (f466), so the walk fires in a dead gap."  **Empirically
FALSE** (probed via a truncated `_probe-cust-load` scenario + an early `{calltrace}` over the
walk window, reading the always-on `0x452cde` worker-spawn / `0x4850ec` cc08-set / `0x48670f`
free-roam VAs): on the port the Continue-load fires **`LOADING_START`@f231 → `LOADING_END` +
`HOUSE_FREEROAM`@~f476**, and `cc08=1` (FUN_004850ec) is set **1 frame BEFORE** that
(`pose_house_standing` runs in the primary worker body @~f475).  So LOADING_END IS the free-roam
boundary — the anchor is emitted correctly, cc08 is 1 right there, no 156-frame dialogue gap (no
`dialogue_tick` ever fires; the "f310 HOUSE renders" was just the load-fade window behind an
still-active overlay).  Driving the walk segment ALONE (truncated trace, no trailing `{wait}`),
the player walks px −0.30→−1.50 to the counter (panim 1, turns to face −X) and the Z@rel156
flips **cc08→4** — 3/3 runs, load-stretch-immune (LOADING_END f476/f483/f491, all reach the
counter + enter cc08==4).

*The REAL cause* — `input_segtrace.c`'s `{wait,timeout}` semantics.  A `{wait}` CLOSES a segment
(it's the terminator); the walk segment is `entries[rel0, rel66=walk, rel75=release,
rel156=Z]` terminated by **`{wait LOADING_START, timeout 60}`** (the d3e haggle-asset load the
Z is supposed to spawn).  The replayer measured the timeout from **segment ENTRY** (`base_arm` =
LOADING_END frame), so it fired at **rel60 — BEFORE the segment's own walk@rel66 and Z@rel156** —
skipping the segment entirely.  ⇒ the walk/Z never applied ⇒ player frozen at the pose-init ⇒
no counter ⇒ no cc08==4.  The truncated probe worked only because it had no trailing `{wait}` to
time out.  On RETAIL the recording's LOADING_START fired (the entry spawned the load) before any
timeout effect mattered; the port's premature timeout is the divergence.  (Confirmed by bumping
the trace's timeout 60→220 (> the rel156 span): the walk + Z + cc08==4 + the d3e LOADING_START
all return.)

**FIX (`input_segtrace.c`, tooling):** measure the optional-wait timeout from the segment's LAST
entry (`base + entries[n-1].frame + wait_timeout`), not from segment entry — a segment's recorded
inputs must ALL apply before its terminating wait can time out (the timeout's intent is "hold the
last input N frames waiting for the optional anchor").  Entries are ascending and `base==base_arm`,
so this only ever DELAYS a timeout (a no-op when the last entry is at rel0 — every OTHER committed
timeout-wait), so the blast radius is exactly this scenario.  +1 host regression test
(`test_segtrace_wait_timeout_after_last_entry`); 3331 host pass.  **Validated end-to-end**: the
EXTENDED probe with the committed **timeout 60** now reaches `LOADING_START`@~f608/661 (the d3e
load) + **cc08==4**, 2/2 runs across load-stretch.  The cc08==4 logic itself stays host-verified
(`test_cs_scripted_first_offer` = capture trajectory 1:1).

**NEXT** (per user direction 2026-06-18, port the full tutorial up to the first real customer):
with the walk/entry unblocked, drive the FULL committed scenario and verify the **caprange haggle
window** — the cc08==4 master tick + the scripted-sell machine vs the retail v3 cache
(`runs/studio-v3-cache/house-customer-tutorial-34f44b18/retail`), then port **Chip 3** (the
`FUN_0046602e` panel/portrait + `FUN_00466b7b` BARGAIN!! UI) so the 5 `PAUSE_OPEN` haggle rounds +
the closing dialogue play on both sides.  Don't move to real (kind-2) haggling until this trace
plays in full on both sides.

## 8.1 The full-window drive (2026-06-18) — fileidx BUG ✅ FIXED, then the reveal-advance BLOCKER

**Drove the port over the full caprange `[0,2700]` with `--state`** (orv3_window, retail sliced
from cache, port re-driven), then compared the customer-service state trajectory vs the retail
cache by aligning on the cc08==4 entry (db054 is the WRONG clock here — it freezes during cc08==4,
so `flow_diff --align-field db054` finds only 1 common frame; align on the entry + compare the
b534/b5a8/b56c/base/ask/b574/b584 *values* instead).  Port self-verify 1743/1743 BIT-EXACT.

**Retail trajectory** (entry frame 3195; b524 already 61 at the first traced frame ⇒ session_init
ran ~61f before the probe — the d3e load gates the house_update probe, so the cc08-offset origin is
~61f behind the master tick's b524): greeting `b534→1`@off+90 `base=3000 ask=3000 b56c=1 b5a8=2`;
op2 `base→1200`@off+969 (= `b5a0` arrival start); first offer `b574=1536 b584=1`@off+2440.

**BUG 1 — the entry seeded fileidx 2, not 0 ✅ FIXED (`2f360ff`).**  The port showed the greeting
firing with `b5a8=-1 b56c=0 base=-1` (garbage) and the offer NEVER — the customer was never bound.
Root cause: `player_ctrl_cc08_sell_counter_enter` called `set_script_file(2)`, so `cs_queue_advance`'s
f404 sell-active branch took `b5b0==2 → FUN_00460fa7` (sets `b5a8=1`) and SKIPPED the kind-select.
Retail shows `b5a8=2`, which ONLY comes from `FUN_00461303` (the `fileidx∉{1,2}` branch).
**Disassembly is decisive:** the player-Z entry (the one THIS walk-to-counter scenario uses) is at
0x488bb6-0x488bd8 and pushes **0** (`488bbf: push 0x0` → `DAT_005c6bb0=0`).  The OTHER three 461bf6
sites (0x48756a/0x4877b6/0x4884c7 = the autonomous / cutscene customer arrivals) push 2 — an earlier
note (§RE correction²) conflated those with the player-Z entry.  The host test passed only because it
uses the fileidx=0 reset default.  Fix → `set_script_file(0)`: greeting now binds 1:1
(`base=3000 ask=3000 b56c=1 b5a8=2`, off+153 vs retail off+90 = the load-phase origin, accept).

**BLOCKER 2 — the scripted machine stalls at the FIRST dialogue line (Chip 2c, unported).**  After
BUG 1, `b534→1`/greeting is correct but `b544` climbs while `base` stays 3000 and op2/`b5a0`/the
offer NEVER fire — the script never advances past its opening dialogue.  The advance gate
(`cs_scripted_tick`, the `b608==0` dialogue case) is `b55c != 0 && (Z-edge || X-held)`, but **`b55c`
(text-reveal-complete) is RENDER-COUPLED and stubbed**:
  - `b55c=1` is set in **`FUN_00465db4`@all.c:62835** (the dialogue text reveal-render) when the
    char-walk reaches the line's null *within the budget* AND `b544>0`.
  - That render is called from **`FUN_00466b7b`@all.c:63744** (the BARGAIN-UI render) with
    **`param_6 = DAT_0730b548`** (disasm `467d22: push ds:0x730b548`) — i.e. the **reveal budget IS
    `b548`**, so the rate is **1 display-char / frame** (SJIS 2-byte lead = 1 char, FUN_00465db4
    62845).  ⇒ `b55c=1` ⟺ `b548 ≥ sjis_char_count(line up to <C>)`.
  - `b548` increments in the master-tick pose section **all.c:60198-60234** (once per *active* speaker
    `cust_active[i]≠0` per frame, gated on the pose-in being settled `b278[i]==0xf`; the pose-in is a
    ~15f ramp `b278` 0→0xf BEFORE the reveal starts).  The port tagged this whole region
    `PORT-DEBT(cs-pose-anim)` "inert for the trajectory" — it is NOT inert: it drives `b548→b55c`.
  - `FUN_0046098f`@all.c:58723 (the line buffer, also stubbed) copies `g_tuto[pc].text` into `b31c`,
    splits at `<C>` (sets `b558=1` + stashes the post-`<C>` tail in `b41c`), zeroes `b548`.  The port's
    `struct tuto_record` ALREADY carries `text[0x100]` (tables_tuto.h), so the line + its char-count
    are available with no new parsing.

**Chip 2c = port the dialogue reveal / script-advance:** FUN_0046098f (line setup + `<C>`→b558) +
un-stub the master-tick pose section (b278 pose-in timers + b548 reveal counter, gated on cust_active)
+ the reveal-complete (`b55c=1` when `b548 ≥ char_count`, `b544>0`) + the `<C>`-pause (b558) continue.
This is the gateway: without it the script can't reach op2 / the offer / the 5 rounds / closing.  It
is verifiable purely on the call_trace state (op2 `base→1200`@~off969, offer `1536`@~off2440 vs
retail) BEFORE any visual render.  Chip 3 (the visual: FUN_0046602e panel/portrait + the FUN_00466b7b
BARGAIN price panel + the FUN_00465db4 glyph draw) renders what Chip 2c advances.

## 8.2 Chip 2c LANDED (`5c48508`) — script advances to op2/arrival ~1:1; the offer is window-blocked

**Chip 2c works.** Re-drove the port over the full caprange and compared the state trajectory to
the retail cache: the script now advances greeting → opening dialogue → **op2** (base 3000→1200,
customer arrival `b5a0` ramps 1→0x3c) at **off+1026** (retail off+969).  Crucially the
**greeting→op2 SPAN is 873 frames (port) vs 879 (retail) = 6 frames over ~880 (0.7%)** — the
1-char/frame reveal + the pose-gap timing is essentially 1:1.  (`b534→1` greeting binds
`base=3000 ask=3000 b56c=1 b5a8=2`, off+153 vs retail off+90 = the load-phase origin, accept.)

**The offer (`b574=1536`) still doesn't appear — but it is OUTSIDE the port's captured run, not a
logic stall.**  Empirics: the port's cc08==4 run is frames 634-2446 = **1812 frames**; retail's is
3195-5684 = **2489 frames**.  The offer is at retail **off+2440** (frame 5635) — beyond the port's
1812-frame run (ends off+1812).  Root cause = a **LOAD-SEAM / anchor-occurrence misalignment**, NOT
the customer-service code:
  - The v3 join matches port `HOUSE_FREEROAM#2` ↔ retail `#1` (load-stretch makes the sides emit a
    different number of HOUSE_FREEROAM events), but WITHIN the matched anchor the cc08 entry sits at
    a different offset: the port's calltrace window opens at frame 477 (cc08==1, **157 pre-cc08
    walk frames in-window**) and cc08→4 at 634; retail's window OPENS at the cc08 entry (frame 3195,
    0 pre-cc08).  Worse, the port's run *ends* ~520 window-frames earlier than retail's.
  - Net: the haggle Z is at a fixed segment-frame (2440), which lands at **cc08-off ~1806 on the
    port vs off+2440 on retail** — so the port's script is ~634 frames short of PRIA when the Z
    arrives ⇒ no offer.  The port loads faster, so fewer frames elapse between the cc08 entry and
    the fixed-segment-frame Z; the same span gives the script less time on the port.
  - The op2-span being 1:1 (873 vs 879) proves the reveal RATE is right; the shortfall is purely
    the cc08-entry-to-Z window length, i.e. the trace alignment.

**NEXT (the offer-verification fix, a tooling/scenario task, NOT port logic):** add a
**customer-service-entry anchor** (emit on cc08→4 in `anchor_trace` + the retail Frida agent) and
re-anchor the caprange/calltrace + the haggle inputs on IT, so both sides' windows open at the cc08
entry and the Z's cc08-offset matches (off+2440 both sides).  Then the offer/round-1 (and the 5
PAUSE_OPEN rounds) can be verified.  The customer-service STATE machine itself is confirmed correct
to op2/arrival; op2→PRID→PRIA→offer reuses the host-tested (`cs_scripted_first_offer`) path + the
now-verified dialogue reveal, so it should reach `1536` once the window covers it.  THEN Chip 3
(the render) — and PORT-DEBT(cs-reveal-in-render) folds the `cs_dialogue_reveal_tick` count into the
real FUN_00465db4 glyph-walk at that point.

## 8.3 CUSTOMER_SERVICE_ENTER anchor LANDED (2026-06-18, `e72daa8`) — port offer fires; retail offer 1548≠1536 = the unported 2nd d3e load

**Landed:** the `CUSTOMER_SERVICE_ENTER` anchor (cc08 non-4→4) on the port (`anchor_trace.{c,h}` +
main.c snapshot + host test) AND the retail Frida agent (`openrecet-agent.js`, `var_cc08 0x0438cc08`),
and re-windowed the haggle segment on it.  The old `{wait LOADING_START/END, timeout 60}` chain
expected retail's SECOND d3e load (occ3) the port lacks ⇒ the port's window opened ~158f off and the
Z missed PRIA.  New structure: `{wait CUSTOMER_SERVICE_ENTER}` → `{wait LOADING_END}` (occ2 = the
d3e-load-end = master-tick start, both sides) → caprange/calltrace/inputs occ2-relative (+60 from the
old occ3-relative values).

**Port ✅:** the offer FIRES — `b574=1536 b584=1` (round 1) @ occ2+2501, with greeting (occ2+157),
op2/base→1200 (occ2+1030) and ask→1300 (occ2+2406) all on retail's EXACT offsets.  Verified via the
fast trigger-only port drive (full-turbo, no BMP-stall perturbation).

**OPEN — retail offer is `1548`, not `1536` (an RNG-pillar gap from a LOAD-STRUCTURE divergence, NOT
haggle logic):** the new retail v3 drive (cache `fe530872`, anchor CUSTOMER_SERVICE_ENTER) reaches the
offer with the SAME base/ask (1200/1300) but `b574=1548`.  Root cause: **retail spawns TWO d3e loads
(occ2 @ entry, occ3 @ occ2+60), the port spawns ONE.**  The 2nd is **`FUN_00452d3e()` @ all.c:60999**,
gated `(DAT_0730b520==0) && (0 < DAT_0730b56c)` (a queued customer ⇒ load its assets), in the master
tick — the port ported only the `session_init` spawn (all.c:58250).  occ3 sets `b1cc=2` ⇒ the master
tick goes inert for the load's ~1-2 frames; the port (no occ3) runs those frames, so its haggle-window
RNG consumption is shifted ~2 frames vs retail ⇒ the first-offer RNG tilt differs (1536 vs 1548).  The
port's 1536 MATCHES the recording (the old occ3-anchored retail cache + the host test both give 1536);
the new retail 1548 is the free-run-without-the-occ3-reseed.  A shared frame-keyed `{rngseed}` CANNOT
fix both (occ2+60 is occ3-end on retail but a plain frame on the port — forcing the recorded occ3
value there gives the PORT 1560).

**NEXT — Chip 2d: port the 2nd d3e load (`FUN_00452d3e@60999`)** — add the `b520==0 && b56c>0` spawn +
`b1cc=2` gate to the port's master tick so the port also pauses ~1-2f for occ3 ⇒ the load structure
(and the haggle-window frame count / RNG) matches retail ⇒ offer 1536 on both.  THEN the 5 PAUSE_OPEN
rounds + closing, THEN Chip 3 (render).  **v3 tooling note:** the port drive no longer dumps BMPs
(`ec6b494`, proxy-only via `--capture-trigger-only`) — full-window drive is 29 s, 0 BMPs, replay
2699/2699 bit-exact.

## 8.4 Chip 2d LANDED (`99214a8`) — occ3 ported & verified, but the offer is UNCHANGED (1536); §8.3's occ3 hypothesis is REFUTED — the real gap is a cc08==4-SPECIFIC per-frame RNG consumer the port STUBS (a real logic gap, NOT bg-NPC phase / NOT a phasepin)

**Chip 2d landed + verified faithful.** Ported `FUN_00452d3e(1)` at the master-tick queue-advance
tail (all.c:60998-61000); param **1** confirmed by disasm (`0x463435 push 0x1`, NOT session_init's
`push 0x0`), picking the b13 thread proc.  Re-drove the port (`fe530872`, bit-exact 2698/2698) and
**confirmed the port now emits `LOADING_START/END` occ3 @ frame 687-688** (occ2+61, a **1-frame**
inert load) — structurally matching retail's occ3 @ 3060-3061 (occ2+59, also **1 frame**).  The occ3
inert DURATIONS match (1 frame each).  +1 host test (`cs_occ3_second_load_gates_at_queue_advance`).

**But the offer is UNCHANGED — port still `b574=1536`, retail (fe530872) `1548`.**  Since occ3 now
matches on both sides, occ3 was **NOT** the cause of the 1536-vs-1548 split.  §8.3's hypothesis is
refuted by the experiment.

**What IS 1:1 (the deterministic pillars):** base=1200, ask=1300, b56c=1, b5a8=2, b534=1, and — proven
by the rng-probe (`0x47be92`, carries `rng`/`rngcalls`) joined to the `0x48670f` state probe — the
offer fires at the **IDENTICAL occ2-relative offset (occ2+2501) on BOTH sides.**  The state machine +
the whole timeline are bit-aligned occ2-relative.  (The raw cc08==4 frame index is NOT comparable —
the port carries ~157 pre-`session_init` frames in-window that retail doesn't; align on occ2 =
LOADING_END occ2, port frame 626 / retail 3001.)

**The actual divergence — a ~2× per-frame RNG-RATE gap in the cc08==4 window:** between occ2 (the
`{rngseed}` pin) and the offer (occ2+2501), **retail draws 25051 rng, the port only 13812** (a
~11000-draw gap).  The offer `b574 = ftol(1200·init_eff/100)`; **init_eff = 129 (retail→1548) vs 128
(port→1536)** = a 1-step rng-phase difference, the downstream symptom of the 11000-draw gap.  Per-frame
rate (period-8):
- **retail (cc08==4)** = `7` SMOOTH baseline EVERY frame + a `31`-spike every 8th ⇒ **10/frame**.
- **port (cc08==4)**   = `1` baseline + a `7`-burst every 4th frame + a `25`-spike every 8th ⇒ **5.5/frame**.

The **8-frame +24 spike MATCHES** (retail 31−7=24, port 25−1=24 — the sparkle emitter, present on both).

**It is NOT the bg-window-NPCs (hypothesis tested + REFUTED).** The port's per-frame rate is BURSTY
(`1` + a `7`-burst every 4th + a `25`-burst every 8th) and **CONTINUOUS across the cc08 1→4 transition**
(frame 625→626, identical period-8 pattern before/after) ⇒ the bg NPCs are NOT frozen by cc08==4, they
keep drifting/respawning at the same cadence.  And **retail's HOUSE FREE-ROAM (cc08==1) is ALSO bursty**
— measured in `house-loaded-display-pinned-26e5aec3/retail` (cc08==1): `4.8/frame`, pattern
`[7,1,1,19,7,1,1,1,…]` = the SAME bursty bg-NPC shape as the port.  So the **bg-NPC drift/respawn rng
MATCHES between port and retail**; the bursts are the shared, correct bg-NPC consumer.

**ROOT CAUSE — `FUN_0047019f` (the cc08==4 on-screen-character pump) — the port SKIPS it entirely
(PORT-DEBT(cs-arrival-anim)).**  Retail's cc08==4 baseline is `7` SMOOTH/frame (not bursty), constant
from the FIRST cc08==4 frame (off 0, b534==0 idle, BEFORE greeting/arrival) ⇒ an idle/prologue consumer.
Narrowed by static analysis (high confidence):
- The cc08==4 arm (all.c:87432-87433) calls **`FUN_0047019f()` THEN `FUN_00462403()` every frame.**
  The port's cc08==4 arm (`scene1_player_ctrl.c`) ports only the master tick (`FUN_00462403`) and
  **skips `FUN_0047019f`** (the arm comment: "FUN_0047019f … is likewise deferred (unported)").
- `FUN_0047019f` (0x47019f, 486 B, all.c:69510) **loops over the on-screen-character array
  `DAT_073a6ea8`** (stride 0x24, ≤30 slots) calling **`FUN_0046fbee` per active character** — and
  `FUN_0046fbee` (1457 B) draws **3 rng (1 `rng_next_unit` + 2 `rng_next15`)** per char.  The player +
  companion + the arriving customer ⇒ ~2-3 active chars × ~3 = the smooth ~7/frame, present every cc08==4
  frame including the idle (the characters are resident before the haggle).
- **Alternatives RULED OUT:** the master tick `FUN_00462403` DOES draw rng (2 unit + 2 rng15) but those
  are CONDITIONAL (haggle/arrival states, already ported + wired) — NOT the smooth idle stream.  The
  prologue *"customer-spawn refresh"* (`FUN_0046f914`) is gated `f404=='\0'` (all.c:69562) ⇒ INERT in
  the sell-active tutorial (f404==1).  `FUN_00461068` (cs-walk-setup) fires ONCE at b524==0x14, not
  per-frame.  So **`FUN_0047019f` is the only cc08==4-only per-frame rng draw the port omits.**

**This is a REAL LOGIC GAP, not an accepted RNG/phase pillar.**  The methodology accepts an RNG offset
only when the consumption COUNT matches and just the seed/phase ORIGIN differs.  Here the COUNT differs
(the port draws ~HALF), so it is a DRIFT (missing consumer) to FIX.  The port's 1536 coincidentally
equals the older `34f44b18` retail capture, but that is phase coincidence — the haggle FORMULA is
correct (`1200·init_eff/100`), yet init_eff's rng draw lands on the wrong LCG value because ~11000
cc08==4 draws are missing upstream.  (The per-anchor `{rngseed}` re-pins bound the drift per PAUSE_OPEN
round, but the first offer sits in the occ2→PAUSE_OPEN#1 segment and diverges.)

**NEXT (corrected) — Chip 2e: port `FUN_0047019f` (the cc08==4 on-screen-character pump):**
1. **(optional, to nail the exact per-frame count) rng-drill** the cc08==4 idle window (per-callsite
   `ret_va`, `frida_capture.py --rng-callsites` + `flow_diff.py --rng-drill`) — should show the ~7/frame
   coming from `FUN_0046fbee` (called by `FUN_0047019f`).  Caveat: `--rng-callsites` keys off the
   `{phasepin}` (this trace has none) — use its absolute `[lo,hi)` mode over [occ2, occ2+150], or add the
   phasepin first.  The static case above is already high-confidence, so this is confirmation, not
   discovery.
2. **Port `FUN_0047019f`** = the on-screen-character pump (`DAT_073a6ea8` array, stride 0x24) + its
   per-char `FUN_0046fbee` (the f405 player/companion/customer arrival-anim integrate + the ~3 rng/char)
   + `FUN_00482a71` (chr_anim_tick, already ported).  Skip the `f404==0` periodic-spawn branch
   (`FUN_0046f914`, inert here).  Wire it into the cc08==4 arm BEFORE the master tick (matching
   all.c:87432).  This retires PORT-DEBT(cs-arrival-anim) AND closes the rng-rate gap ⇒ the offer (and
   every later round's rng) aligns.  NB this is a SIZEABLE arc (the cc08==4 character simulation), not a
   one-liner — it models the customer/player/companion on-screen actors during the haggle.
3. The trace ALSO lacks `{phasepin}` (separate real policy gap — add `{phasepin N}`+`{rngseed [N,19937]}`
   +`{tutloadpin 8}` for clean db054/anim/sparkle phase), but that is NOT the cause of THIS rng-rate gap
   (the bg-NPC pattern already matches), so it will not by itself converge the offer.
4. Chip 2d STAYS — occ3 is a correct, faithful port independent of the offer value.
THEN the 5 PAUSE_OPEN rounds + closing, THEN Chip 3 (render).

## 8.5 ⚠ §8.4 REFUTED BY PROBES (2026-06-19) — the cc08==4 idle consumer is the PARTICLE SYSTEM, NOT FUN_0047019f/FUN_0046fbee. The floor-walker array is EMPTY; the gap is ambient-particle integration the port lacks.

§8.4's "port `FUN_0047019f`" plan was a STATIC-ANALYSIS GUESS. Five probes this session
disprove it. **DON'T port FUN_0047019f for the rng gap — it draws ~nothing here.**

**The probes (all on `house-customer-tutorial`, occ2-relative):**
1. **rng-drill WITH `{phasepin}`** (the §8.4 recipe) → blamed `FUN_00447f4f` @ ~4.3/f +
   `FUN_0046fbee`. **Both wrong** — the phasepin's bg-NPC warmup re-seed CONTAMINATES the
   very rates it measures (it re-runs the 180× `FUN_0046f2a3` warmup + its spawn effects).
   *Lesson: never rng-drill through a `{phasepin}` — it fabricates consumers.*
2. **Array dump** (`DAT_073a6ea8` slots via a temp 0x48670f field-probe extension):
   **the floor-walker array is EMPTY in the tutorial** — `DAT_005c7dd0=8` is the eligible-
   *roster* count, but **0 slots are active** (the spawn `FUN_0046f914` is `f404==0`-gated,
   inert when `f404==1`). ⇒ `FUN_0047019f`'s loop body NEVER runs; `FUN_0046fbee` is never
   called ⇒ **drawing it is a no-op.** (§8.4 inferred "~3 chars" from the contaminated drill.)
3. **CLEAN per-frame rngcalls** (win-0-240, the committed trace, NO phasepin, from the
   `0x47be92` sched probe): **retail cc08==4 idle draws `7` SMOOTH/frame + `31` every 8th**
   (the 8th = `7`+`24` sparkle). This IS the real §8.4 pattern — confirmed, un-contaminated.
   (The PORT side of that same capture reads `rngcalls` FROZEN at 1408 / rng-state = the
   *title* seed 208096806 — a **broken port `0x47be92` probe** in this scenario; ignore the
   port column here, use the RE-doc port baseline.)
4. **CLEAN rng-callsites drill** — needed a new `{rngcs:[off,len]}` segtrace op (lands this
   session: `tools/frida/openrecet-agent.js` + `tools/frida_capture.py`) that arms the
   who-consumed-the-LCG drill at an absolute frame **WITHOUT the phasepin reset**. Over 300
   un-contaminated idle frames the smooth stream decomposes (accounting for the
   `rng_next_unit`→`rng_next15` double-count the drill shows as the `0xbfb1533` proxy):
   - `FUN_00442cef` — **1 `rng_next15`/frame** (the scene tick; the port HAS this).
   - **code @ `0x44a750`** — **~5 `rng_next_unit`/frame** (6 callsites). This is the
     DOMINANT smooth consumer and **the gap.**
   - `FUN_0048670f`+`FUN_00414345` — the **8th-frame sparkle** (+24; the port HAS this).
   - `FUN_0046f2a3` — bg-NPC, ~1/f (the port HAS this, matches).
5. **`0x44a750` identity** — `objdump`: no `call` anywhere in the exe targets `0x44a4xx-7xx`,
   and there is no prologue/`ret` boundary near it ⇒ it is reached by an **internal jump**
   inside the 11826-byte blob `functions.csv` labels `FUN_00447f4f` (`0x447f4f`). That blob
   is the **3D particle pool** code (pool `DAT_069b2f80`, stride 0x94, type@+0x30
   `DAT_069b2fb0`); `0x44a750` is a **camera-yaw-trig particle-behavior branch** (uses the
   camera global `0x73de39c`, sin/cos `FUN_00503a44/994`). The per-frame DRIVER that reaches
   it is the **particle integrator `FUN_0040fb3a`** (0x40fb3a, the 8071 B integrator that
   walks the pool every frame and re-emits sub-particles — all.c:9184-9583+, calls
   `FUN_00447f4f` @ 9803/9886/10654). *(A direct `0x447f4f`-entry call-trace hook read 0
   calls, consistent with the rng arriving via the integrator's internal path, not a fresh
   top-level `scene1_spawn(...)`.)*

**CONCLUSION — the real gap:** in cc08==4 idle, retail has **ambient particles resident in
the 3D pool** that the **integrator `FUN_0040fb3a` updates ~5 rng/frame**. The port draws
less because **its pool is empty in cc08==4 idle** — the cc08==4 ambient-particle SPAWN (the
seed) is unported (it is NOT `session_init`/`FUN_0045edaa`/`FUN_00461bf6` — those have no
`FUN_00447f4f` call). The port already HAS the integrator (`scene1_particles_tick.c` =
`FUN_0040fb3a`) and the spawn (`scene1_spawn` = `FUN_00447f4f`); the missing piece is
**whatever cc08==4 effect seeds the pool**, + then the integrator's rng flows for free.

**STILL OPEN (next probe):** name the cc08==4 ambient-particle EMITTER + its type/position.
The `0x447f4f`-entry call-trace hook this session was silently overridden by the
retail_fields.json VA-merge (`frida_capture.py:488-503`) so its ret_va was never captured —
re-probe with `--no-call-trace-fields` (or hook `FUN_0040fb3a` and read the pool's live
particle types). Candidates: a cc08==4 counter/customer ambiance spawned during the d3e load
or the first master-tick frames.

**STRATEGIC REFRAME:** the port's offer is **`1536`, which MATCHES the user RECORDING + the
host test** (`cs_scripted_first_offer`); the `1548` §8.3/§8.4 chased is the *free-run* retail
value (no recording). So the rng-rate gap is a **downstream-parity** concern (keeps the 5
rounds + closing in sync) — NOT a visible offer bug. Decide per the human: (a) drill the
emitter + port the cc08==4 ambient particles; (b) pivot to the VISIBLE **Chip 3** (the
`FUN_0046602e` portrait + `FUN_00466b7b` BARGAIN!! panel + `FUN_00465db4` glyphs); or (c)
log the rng-rate gap as a known divergence and proceed. **PORT-DEBT(cs-arrival-anim) stays —
but it is NOT the rng-gap cause; do not conflate them again.**

## 8.6 USER DIRECTION 2026-06-19 → render the cc08==4 SCENE first (the port shows NO customer-service view at all — it stays in free-roam). The state machine is invisible.

**User redirect (chosen over §8.5's options a/b/c):** "we don't even get to the haggling
tutorial dialogue yet … port that to be 1:1 up to the actual haggling UI first," and "we
already render other dialogue correctly btw so inspect retail's render path, see if it
differs from normal dialogue, port gap if so." So the rng gap is DEFERRED; the front is the
**cc08==4 visual scene render.**

**Confirmed visually (feed "cc08==4 tutorial dialogue gap"):** at a b534==1 dialogue frame
(offset ~250, base=3000), RETAIL renders the **customer-service STAGE** — a front/counter
camera angle, **Recette + Tear as large 2D character art**, and a **dialogue box** (speaker
"Tear" + the typewriter line "actually sell things to…"). The PORT renders the **free-roam
top-down HOUSE** (the "Button 4: Change Camera" HUD is up) — i.e. **the port's cc08==4 render
is entirely unported; it never leaves the free-roam presentation** even though the cc08==4
STATE machine (master tick, offers, b534 reveal) runs correctly underneath. So Chips 1/2a-e
built an INVISIBLE state machine.

**It is NOT the normal dialogue path** (the user's question, answered by probe): retail's
normal `dialogue_tick` `FUN_0046c320` fires **0×** in cc08==4 (v3 `--state` drive over the
window — only `0x48670f`/`0x47be92` ever hit). The cc08==4 dialogue is the **scripted
machine's own render**: the text reveal the port already advances (Chip 2c, `b548`) is drawn
by **`FUN_00466b7b`** (calls the glyph renderer `FUN_00465db4` = the PORT'S EXISTING
`font_draw_text_box`, used for the guild bubble — reusable!) with the reveal budget; the
characters/panel by **`FUN_0046602e`** (gated `b1cc==1 && b7b0!=0`). Draw-program diff at the
frame: retail 179 draws / 2761 tris vs port 105 / 2599 — retail-only = a big panel quad
(`b494`, 80 tris) + ~23 extra font-atlas glyph quads (the dialogue line) + the 2D character
sprites.

**The render dispatch** (who calls the cs render): `FUN_0046602e` ← `FUN_00409925`
(0x409925, 3434 B); `FUN_00466b7b` ← `FUN_0040a765` (0x40a765, 7558 B) — both in the 2D-UI /
overlay render system. So the port's 2D-UI render needs a cc08==4 branch dispatching to the
cs render, the same way it already dispatches the guild/menu/dialogue overlays.

**PORT PLAN (the fresh arc):**
1. **The cc08==4 scene presentation / camera** — the dispatch arm all.c:87366-87434 the port
   STUBS as `PORT-DEBT(cs-arrival-anim)` sets the stage camera + the player/companion pose
   (`DAT_056daafc` anim=5, `DAT_056dab00` octant, `DAT_056da1d8/1e0` camera, `DAT_056db05c`
   angle). Port it so the view switches from free-roam top-down to the counter angle.
2. **`FUN_0046602e`** (2668 B) — the 2D Recette/Tear character art + the cs panel; gate
   `b1cc==1 && b7b0!=0`. Wire into the port's 2D-UI render (mirror of `FUN_00409925`).
3. **`FUN_00466b7b`** (5305 B) — the dialogue box + the typewriter text via the EXISTING
   `font_draw_text_box` (`FUN_00465db4`) with budget `b548` (already counted by Chip 2c);
   speaker-name + box. This is "Tear's dialogue." (The BARGAIN!! PRICE layout is the later
   part of the same fn — do the dialogue-text part first, price after, per the user order.)
4. Verify in Trace Studio v3 (identity-align on the cc08==4 entry, NOT the LOADING_END anchor
   — the port carries ~157 pre-cc08 walk frames so same-offset frames are mismatched; the
   port's b534==1 starts ~off+153, retail ~off+90).

**NB the frame-pairing caveat:** the v3 identity join (LOADING_END anchor) pairs the port's
pre-cc08 WALK frame with retail's cc08==4 DIALOGUE frame (load-stretch + the 157-frame walk).
Render-compare by cc08-entry-relative offset, or just eyeball a port b534==1 frame vs a retail
b534==1 frame directly.

## 8.7 Chip 3a LANDED (2026-06-19) — the cc08==4 DIALOGUE BOX now renders; character art + camera are Chip 3b/3c

**Chip 3a ✅ LANDED + v3-VERIFIED** (`src/customer_service_render.c` new, + the snapshot
accessor `customer_service_get_render_state` + the two HUD dispatch sites): the port now draws
the haggle **dialogue box + typewriter line**. Ported FUN_0046602e sections (a) letterbox bars
/ (b) the two 512² character sprites (NULL-tex-guarded) / (c) the '!' bubble, and FUN_00466b7b
**section 6** — the offer-card backdrop (shopmode.tga) + speaker name plate (chrname.tga, slot 0)
+ Z-prompt + the **typewriter line via the existing `font_draw_text_box`** (FUN_00465db4, x=250/130
y=346 scale 1.0 budget b548). Dispatch: `customer_service_render_chars` at the TOP of FUN_00409925
(scene1_merchant_hud), `customer_service_render_overlay` AFTER the top HUD in FUN_0040a765
(scene1_hud, gated INGAME && HOUSE — FUN_00466b7b is at all.c:7044, *after* FUN_00406d50@6980).
Reuse: `ive_box_scale` == FUN_0046c86f (the pop-in wobble, already 1:1 in scene1_dialogue_run.c).
**Verified** (`house-customer-tutorial-a361c768`, port frame 1582 vs retail 4192, both cc08==4
b534==1): the port renders *"and then they will come over to the counter to say for…"* — draws
**105→152** (retail 161). Was: nothing (free-roam top-down only). Feed: "cc08==4 render — Chip 3a".

**REMAINING GAPS (the v3 material verdict, port 1001 vs retail 1114):**
1. **The big character art (Recette/Tear 2D sprites) — Chip 3b ✅ LANDED (`fed54cf`).** Ported the
   `grp:` parser (`scene_buy_parse_stage_buffer` + the `tables.c load_stage_files` driver) — the
   port now populates `g_scene_buy_names`/count from each defined customer's `file:` data at startup,
   and AE8/B13 load the standees (v3-verified: Recette+Tear render over the dialogue box; +2 host
   tests). PORT-DEBT(cs-stage-msg): the `seNN:`/`msg%02d:` arms (customer's normal dialogue + the
   per-line grp/se index) are NOT parsed (the tutorial uses the scripted machine). NOTE the names
   table holds 20 slots/record engine-side but the port clamps storage to SCENE_BUY_SLOT_COUNT=10
   (the dense low slots; count still tracks every grp line so the loader clamp matches) — expand if
   a customer needs poses ≥10. _Original analysis (kept for reference):_ The two 512² sprites come
   from `g_scene_buy_sprites[page][slot]` (page 0 via AE8 = shopkeeper/Recette+Tear; page b56c via
   B13 = the customer), loaded from `g_scene_buy_names`. The retail WRITER is a **startup per-stage
   `grp:` file parser**:
   **FUN_00475270 block #4 (all.c:74568-74716)** — for each customer record (stride 0x2c670, 50
   records) with valid(+0x514c)!=0, it opens the record's `file:` data file (+0x5044, captured by
   the port's `tables_kyaku.c apply_file_path`) and parses lines: **`grpNN:` → names[NN]+count**
   (record+0x44 = `g_scene_buy_names`, +0x5144 = `g_scene_buy_count`, NN≤0x13), `seNN:` →
   se-names (+0x1444, ≤0x3b), `msg%02d:` → the per-line dialogue text (+0x6e70) + the per-line
   grp/se index (+0x194/`DAT_06a63c68` / +0xaf4). Pose filenames are `bmp/<grp>` e.g.
   `ivent/01recette_NN.tga` / `ivent/02tear_NN.tga` (512²), read at runtime from the user's data
   files (NOT in the repo). Plan: port the `grp` parser (+ msg/se) into a startup step driven
   from `tables_load_all` after `tables_parse_kyaku`; **expand `scene_buy` names to 20 slots/record**
   (grp NN 0-19; sprites stay flat 10/page — left idx=b54c, right idx=b550+b56c*10, render already
   does `sprites[flat/10][flat%10]`); then AE8/B13 load the art. Also wire b54c/b550 from the
   tuto script's CHR ops (the port sets `s_b54c=chr_arg`; verify chr_arg == the grp slot). Full RE:
   the `ae83062f` agent report (per-stage parser = the standee-names writer, a NEW finding).
2. **The "Tear" name plate (slot-1)** — PORT-DEBT(cs-nameplate-slot1): the right-speaker name
   plate uses the customer-record name index (`*(&DAT_06a5ea90 + b56c*0x2c670)`); deferred with
   the customer record (Chip 3b). Slot-0 (fixed cell 0,32-128,64) is ported.
3. **The counter CAMERA/pose — Chip 3c.** The port stays in free-roam top-down; retail's cc08==4
   arm (all.c:87366-87434, objdump 0x487e..0x488085, PORT-DEBT(cs-arrival-anim) in
   scene1_player_ctrl.c:1659) sets the player octant (`DAT_056dab00`=ftol(-yaw)&7) + facing
   (`DAT_056db05c`) + anim 5 (arrival)→6 (at counter) + companion anim 4, and ramps the player
   pos `DAT_056da1d8`(X−0.125/f)/`056da1e0`(Z±0.05/f toward tier-based target)/`056da1dc`=0.5 once
   `DAT_056db04c>10`. Best done WITH the character art (zoomed counter view + characters together).
4. **FUN_00466b7b sections 1-5** (pose panel + speech line / arrival panel + price labels /
   BARGAIN!! price / choice buttons): PORT-DEBT(cs-render-rest) — the "actual haggling UI" the
   user ordered AFTER the dialogue (appears in the haggle rounds, not the intro dialogue).

### 8.7.1 USER NOTES (2026-06-19, after Chip 3a/3b verify) + Chip 3c RE (camera/pose/arrival anim)

The user verified Chip 3a/3b 1:1-correct (feed + studio) and flagged 2 viewer notes (the
authoritative gap list):
- **#1 `HOUSE_FREEROAM#1+79` "exclamation tooltip we don't render"** — a "!" speech bubble above
  Recette's head, in FREE-ROAM (frame 3157, BEFORE the cs entry at 3234) as she's at the counter.
  Likely the free-roam interaction/approach emote (the port has the emote-bubble system in
  scene1_hud_emote_bubble = FUN_0040a765 inline — investigate why this "!" isn't emitted; it's
  NOT the cs section-c b53c bubble, which is post-load 2D-overlay).
- **#2/#3 `HOUSE_FREEROAM#2+12/+32` "recet jumps on stool"** — the cs ARRIVAL animation: Recette
  hops onto the merchant stool behind the counter + the camera ZOOMS to the counter view (note #3
  shows retail much more zoomed than the port's wide top-down).  = **Chip 3c, the camera/pose arm**.

**Chip 3c RE — the cc08==4 camera/pose arm (all.c:87366-87434, objdump 0x487e8a-0x488085) FULLY
DECODED, ready to port:**
- Gates: **f405 = save_record[0x2bc6d]** (arrival-complete), **f407 = save_record[0x2bc6f]**
  (companion-arrival).  esi = the save record base (DAT_044e3798 + slot·0x2dfc8).
- **f405==0 (arriving):**
  - `DAT_056db05c = -DAT_073de39c` (player facing angle = -camera_yaw).
  - **player octant `DAT_056dab00` = ftol((( (DAT_056db05c+yaw) + π/8)/2π)·8 + 8) & 7**.  Since
    `DAT_056db05c+yaw = -yaw+yaw = 0`, this is **a FIXED 0** (ftol(0.5+8)=8, &7=0) — Recette faces
    octant 0.  Consts: 0x519b78=π/8(0.3926991), 0x519398=2π(6.2831853), 0x519378=8.0; 0x503954=__ftol.
  - if f407==0: `DAT_056dab58 = DAT_056dab00` (companion octant = 0).
  - if `DAT_056daafc != 5`: reset the actor-0 anim record (`DAT_056daaf8=0` FRAME, `DAT_056daaf4=0`
    COUNTER, `DAT_056daaf0=0.0` TIMER) + set **anim 5** (`DAT_056daae8=5` ANIM, `DAT_056daafc=5`
    STATE).  Anim 5 = the stool-jump.  (Map the DAT fields to the port's CHR_ACTOR_* in
    s_actor_record[0]; verify the offsets — wrong breaks the free-roam anim.)
  - `DAT_056db04c++` (port `s_db04c`); **if `>10`: camera-pos ramp** by shop tier
    `iVar7 = (&DAT_068dd3fc)[DAT_0438b4dc·0x6cf]` (= 0x1b3c-stride per shop): tier<3 → X_target=0,
    Z_target=6.9; tier 3-4 → X=10.7, Z=15.0.  Then ramp the player pos (= camera target,
    g_scene1_player_pos): if `da1e0 < Z_target+1.69`: `da1e0 += 0.05`; if `Z_target+1.7 < da1e0`:
    `da1e0 -= 0.05`; if `X_target-4.5 < da1d8`: `da1d8 -= 0.125`; `da1dc = 0.5`.  (Tier-0 tutorial:
    da1e0 9.35→8.6, da1d8 -1.5→-4.5, da1dc→0.5.)  Consts: 0x5198f8=0.05, 0x519a64=1.7, 0x519f8c=1.69,
    0x519a1c=4.5, 0x51998c=0.125, 0x51935c=0.5, 0x519f90=6.9, 0x519f94=10.7, 0x5194e4=15.0.
- **f405!=0 (arrived):** set anim 6 (056daafc) + companion anim 4 (056dab54) + octant by
  `da1d8 <= _DAT_056da1f0 ? (dab00=6, db05c=π/2) : (dab00=2, db05c=-π/2)` (objdump 0x487fed-0x488075).
- then `FUN_0047019f()` (the char pump — §8.5 EMPTY in the tutorial, PORT-DEBT) + `FUN_00462403()`
  (master tick, already ported).
- **Port site:** scene1_player_ctrl.c:1659 (the PORT-DEBT(cs-arrival-anim) stub in the cc08==4 arm),
  BEFORE customer_service_master_tick.  Port equivalents: yaw=DAT_073de39c, facing=DAT_056db05c,
  octant=DAT_056dab00, companion octant=DAT_056dab58, anim=s_actor_record[0], s_db04c, player
  pos=g_scene1_player_pos (da1d8/dc/e0), f405/f407=save bank.  **Verify:** Recette jumps on stool +
  camera zooms to counter (notes #2/#3), without breaking the free-roam walk anim.  Risk: the
  actor-record field mapping + the player-pos ramp interacting with the controller.

### 8.7.2 Chip 3c LANDED (2026-06-19) — the cc08==4 arrival anim + camera ramp; the PLAYER POSE/POS is BIT-EXACT 1:1.  The CAMERA FRAMING has a residual (separate, probe-needed).

Ported FUN_0048670f's cc08==4 arrival arm (all.c:87367-87432) into
`scene1_player_ctrl.c::player_ctrl_cs_arrival_tick`, dispatched in the cc08==4 arm BEFORE
`customer_service_master_tick`; retires the player-pose part of PORT-DEBT(cs-arrival-anim).
- **f405==0 (arriving)** branch: player facing = `-camera_yaw`; octant 0 (the `ftol` formula
  collapses to 0 since `facing+yaw==0`); anim 5 (stool jump) on the `daafc!=5` gate; companion
  octant = player octant when `f407==0`; the `db04c>10` camera-pos ramp by `scene_type` (HOUSE
  view_mode 0 <3 → x_target 0/z_target 6.9): `px -=0.125` to -4.5, `pz ±=0.05` to 8.6, `py=0.5`.
- **f405!=0 (arrived)** branch ported for faithfulness (anim 6 + companion anim 4 + side-facing
  octant); the tutorial's f406 entry leaves f405=0, so it always arrives (never this branch).
- **Also:** gated the room-bounds **px-clamp on `cc08!=4`** (FUN_00486435 itself gates the px
  stop so the ramp can slide past -1.5 in cc08==4; the port's clamp had assumed cc08!=4); and
  advance the player sprite anim each frame via `chr_anim_tick` (mirroring retail's draw-leaf
  FUN_0045a56f — pcnt++ + frame-by-LUT; the companion is advanced by the scene1_sim non-walk
  fallback).  No RNG consumed (chr_anim_tick is rng-free), so the haggle stream is untouched.

**VERIFICATION (v3 `house-customer-tutorial-a361c768`, --state, aligned on the cc08-entry
anchor):** the player arrival fields **`panim/pframe/pcnt/poct/px/py/pz` are BIT-EXACT to retail
across ALL 2569 cc08==4 frames** under a constant **+1-frame shift** (0/2569 mismatches; the raw
+0 trace had only the ramp/anim shifted 1 frame — the px/pz/py values + cadence are otherwise
identical, e.g. off100 port==retail px/py/pz = -4.5/0.5/8.6).  The +1 is the **arrival-origin
PHASE pillar** (load-stretch: the port enters cc08==4 ~1 frame off retail relative to the anchor;
the f406 entry gotos the tail + `FUN_0045edaa` writes no anim, so it is NOT an arm-logic gap) —
accept as CONST-OFFSET.  **The arrival ARM logic is confirmed data-1:1.**  Build clean, 3335 host
tests pass.  Visual: the port now renders Recette on the merchant stool (anim 5) at the counter
view (feed "cc08==4 Chip 3c arrival").

**OPEN — the camera FRAMING residual (note #3 follow-up).**  At the settled counter view (offset
~100, both `px=-4.5 pz=8.6` bit-exact, `char_mode` loads 0 on both per scene1_postload.c:399,
`stage_view_mode`=0) the rendered frames still differ ~25px / 91% px (`orv3_shot` pixel_diff at
the identity-paired frame).  The shift is small (same shop, same angle, slightly panned) — the
big "wide-top-down → counter-zoom" gap the user flagged is CLOSED by the position ramp (the camera
follows `g_scene1_player_pos` to -4.5), but a residual framing offset remains that is **NOT
explained by any probed field** (px/pz/py/char_mode/view_mode all match).  Candidate causes (need
a camera **eye/lookat** probe on both sides to disambiguate): the camera smoothing eye-history,
the bias_z clamp (`if (bias_z>1) bias_z=1` — pz=8.6 clamps to 1 on both, so the look-at z is the
shop-front not the counter), or retail decoupling/fixing the camera target during cc08==4 rather
than tracking the player to -4.5.  **Next:** extend the 0x48670f probe with the camera eye/lookat
(both sides), re-capture, and diff — then the framing residual resolves to a concrete field.

### 8.7.3 Chip 3d LANDED (2026-06-19) — the cc08==4 cinematic COUNTER camera (the §8.7.2 residual ROOT-CAUSED + ported; camera now BIT-EXACT at the settled view)

**The §8.7.2 residual was a genuine NEW camera mode the user flagged ("investigate this new
camera mode").**  Extended the 0x48670f probe with the final camera eye/lookat (engine
`_DAT_073de31c/324/328/330`) + char_mode on BOTH sides; the diff was decisive: at the settled
counter view the **port follows the player** (lookat (-4.5, 1.0), eye (-4.5, 15.0)) while **retail
pins a FIXED counter target** (lookat (-3.0, 0.0), eye (-3.0, 14.0)), `char_mode`=0 on both (NOT
the cause).

**RE — the cc08==4 cinematic counter camera (`FUN_00462403` all.c:60280-60314):** the
customer-service master tick DECOUPLES the camera from the player — it pins the lookat to a fixed
per-**shop-tier** counter target (`iVar8 = (&DAT_04510578)[slot*0xb7f2]` = the tier): **tier 0/def
→ (X -3.0, Z 0.0)**, tier 1 → (0, 0), tier 2 → (5.5, -1.0)+eye-height ramp→25, tier 3 → (5.5,
5.5)+ramp→29 — smoothing `_DAT_0438cc50/58` toward it at **0.1/frame**, then orbits the eye
(`cc38 = (b774+b695ef70)·sin(yaw)+cc50`, `cc40 = cc58-(…)·cos(yaw)`, radius `b774+b695ef70`=14),
and sets **`DAT_0438b4e8` (stage_class) = 1**.  The camera function `FUN_00441c3e`'s class-1 branch
(0x441880) then uses those cinematic eye/lookat VERBATIM (its 0.2 lerp re-reads the same values =
no-op) instead of the player-follow.  **The port had stubbed the master-tick cc38/3c/40 writes as
"cs-bubble-pos" (a misread — they're the camera EYE) and hardcoded `stage_class = 0`** (always
player-follow), so the camera tracked the player to -4.5.

**PORT (`d?`):** `scene1_camera_cs_counter_cam(tier)` (new, scene1_camera.c — writes the smoothed
eye/lookat + sets stage_class=1, reusing the validated free-roam radius/eye-height params) + the
**`stage_class==1` pass-through** in `scene1_camera_pose_compute` (the global replaces the
hardcoded 0) + the **master-tick call** in `customer_service_master_tick` (replacing the
"cs-bubble-pos" stub, tier from the save bank) + the **free-roam reset** (`s_cc08 != 4 →
scene1_camera_set_freeroam_class()` so the camera resumes tracking after CS).  **VERIFIED (v3,
house-customer-tutorial-a361c768, the camera-eye/lookat probe):** camex/camez/camlx/camlz =
**(-3.0, 14.0, -3.0, 0.0) BIT-EXACT vs retail at the settled view (off 80-120)**; the ramp (off
0-50) tracks retail within the same +1-frame arrival-origin phase (e.g. off20 port (-2.797,
14.135) vs retail (-2.836, 14.109)).  3335 host tests pass; free-roam camera unaffected (the
stage_class global defaults 0).  PORT-DEBT(cs-cam-tier): the tier-2/3 eye-height (b778) ramps
to 25/29 are stubbed (tutorial is tier 0).

### 8.7.4 Chip 3e LANDED (2026-06-19) — the cc08==4 COMPANION (Tear) at-counter pose; canim 4 + position BIT-EXACT 1:1 (notes #8/#9 fix)

**The note #8/#9 gap (the 3D companion) ✅ PORTED.**  Diagnosis (FRONT, `02d89d4`): the cc08==4
dialogue STATE is bit-identical port==retail, so the gap is the **3D COMPANION (Tear, actor 2):
retail walks her to the at-counter "ready" pose `canim=4` at (-3.2, 8.6) octant 2, the port left
her at the free-roam `canim=0` (-3.0, 8.8) octant 0** — the port ran the free-roam spring-follow
through cc08==4 instead of the customer-service branch.

**RE — FUN_0048a833's `local_c != 0` (at-counter) branch** (by-address 0x48ace7-0x48aeda, objdump
2026-06-19; consts verified: 0x519b90=1.3, 0x519314=2.0, 0x5193a0=0.1, 0x5198c4=0.04, 0x5198d8=0.2,
0x519438=3.0).  `local_c` is set 1 when **f404 (sell-active) is set** (also db048∈{0xe,0xf}); the
`else` arm:
- **canim 4** (`DAT_056dab54/40`, reset cycle on transition) — the at-counter ready pose.
- **target** beside the player: `comp.x <= player.x ? (player.x−1.3, oct 6) : (player.x+1.3, oct 2)`;
  `target_z = player.z`.  Tutorial: comp.x(-3.0) > player.x(-4.5) ⇒ **+1.3, octant 2, target (-3.2,
  player.z)**.
- **Y target** `sin(db054·0.04)·0.2 + 3.0` — **no ground_y term** (unlike the free-roam bob).
- `dist = √(dx²+dz²)`; **dist ≥ 2.0 → walk anim 1 + octant from atan2(dx,dz)** (the shared
  `player_ctrl_facing_octant`); else hold anim 4.  Tutorial is < 2.0 from frame 1 (walk branch
  unexercised here — it serves the autonomous-customer stages).
- **move 0.1/frame**: `comp += delta·0.1` (a flat lerp, NOT the spring).
- **No RNG** (sqrt/sin/atan2/ftol only) ⇒ the §8.8 haggle stream is untouched; the wing-sparkle
  (which DOES draw rng, frozen-db054 every-frame, §8.8) is emitted by the shared tail, unchanged.

**Port** (`scene1_companion_ctrl.c::co_at_counter_tick`, branched in `scene1_companion_ctrl_tick`
on `player_ctrl_cc08()==4`): the companion is ticked once/frame via the scene1_sim non-walk
fallback, so the cc08==4 branch takes over there.  The player arrival arm
(`player_ctrl_cs_arrival_tick`, run earlier this frame) writes the companion octant=0; the
at-counter branch overwrites it to 2/6, exactly as retail (where FUN_0048a833 runs from the master
tick AFTER the arrival arm).  +2 host tests (`companion_at_counter_pose/_settle`); 3337 pass.

**VERIFICATION (v3, house-customer-tutorial-a361c768, --state, cc08-entry aligned, the
cx/cz/coct/canim probe):** over all **2546 cc08==4 frames — `canim` 2546/2546 BIT-EXACT (4),
`coct` 2546/2546 BIT-EXACT (2), and `cx/cz` settled (off≥120) BIT-EXACT (max |Δ|=0.0000) at
(-3.2, 8.6)**.  The ramp transient (off 10-110) trails by the **inherited player-px arrival phase**
(px is a clean CONST-OFFSET: shift −3 ⇒ 101/101 px-exact, no drift — the accepted Chip-3c pillar);
the companion correctly lerps toward `px+1.3 @0.1/f` (self-consistent on both sides), so it inherits
that phase and re-converges bit-exact at settle.  **The companion logic is confirmed data-1:1.**
Visual (feed "cc08==4 companion at-counter pose — Chip 3e"): the port renders the counter view with
Recette+Tear; the remaining visible diff is the **manga-lines (集中線) radial effect = note #8, the
RT-based follow-up** (replays empty in v3 — needs the v3 RT-capture extension).

## 8.8 FULL-TRACE DIAGNOSTIC (2026-06-19) — the cc08==4 trajectory is largely 1:1; the remaining SYNC blocker is the §8.5 RNG-RATE gap (port draws HALF retail's rng)

Drove the FULL port `0:2700` state window vs the retail `a361c768` cache (cc08-entry aligned,
+1-frame probe phase) and compared the whole haggle trajectory to the first customer.  **What's
1:1:** `panim`, `b584`(round), `b590` ALIGNED every frame; the camera (camlx/camlz) bit-exact at
the settled view (off 90+); the haggle base/ask trajectory matches (idle→greeting 3000→op2 1200,
aligned by ~off 300) modulo the master-tick-start phase (the binding/greeting fire a few frames
apart — port greeting @off154 vs retail @off150, a reveal-timing residual, not a logic gap).
**The +1-frame "phase" is a PROBE-TIMING asymmetry, NOT a logic/render gap:** retail reads the
0x48670f payload at Present-onEnter (post-render, agent.js:1562); the port reads it at
CALL_TRACE_BEGIN (top of the tick, pre-arm).  The ANCHOR is recorded post-sim on BOTH (main.c:2772
/ agent Present), so the **d3d RENDER is frame-aligned** — the studio's apparent "camera off" is
the arrival RAMP frames (mid-hop), not a real offset; the SETTLED camera is exact.

**THE blocker — the §8.5 rng-rate gap, CONFIRMED with the current build:** in cc08==4 the **port
draws ~5.53 rng/frame, retail ~10.02/frame** (constant rates, measured over 2000 frames).  The
This desyncs everything rng-driven: the **first-customer offer diverges ONLY at the end** (`b574`
port 1536 vs retail 1548 @off2500 — init_eff 128 vs 129, a 1-step phase off the rng gap) + the
resident particles (a subtle visual).

**★ BREAKTHROUGH (2026-06-19, the pool-dump probe — §8.5's "unidentified seed" RESOLVED):** a
temporary agent hook (`g_pdump`, openrecet-agent.js) dumped the 3D pool's active particle
type-counts (DAT_069b2f80, 4096 slots, stride 0x94, type@+0x30) every 40 frames across a retail
drive.  **The pool is EMPTY in free-roam (cc08=0, active=0 for 2200 frames), then fills with
TYPE 0x1f (NOT 0x43 — §8.5's 0x43 was a red herring): 7 @cc08=1 (the counter approach, f2280) →
31 @cc08=4 (f2440+, constant).**  Type-0x1f particles are **fading drift particles** (integrator
FUN_0040fb3a @0x40fb3a line 10090: pos += vel, vel *= 0.97, vel.y -= 0.001, die at life==0x20=32
frames — the **integration is rng-FREE**).  So the ~4.5/f rng is at **SPAWN** (random pos/vel): a
**continuous ~1/frame emitter** (31 alive × 32-frame life ⇒ ~1 spawned/frame) that the port
LACKS in cc08==4.

**★ RESOLVED (2026-06-19) — the emitter is Tear's COMPANION wing-glow sparkle; the gap is a db054
FREEZE the port missed.**  A spawn-hook (FUN_00447f4f onEnter, template + caller) pinned every
type-0x1f spawn to **caller 0x48b393 = FUN_0048a833 (the COMPANION controller) +0xb60** — Tear
drops a type-0x1f particle at her cam-yaw-offset position (LAB_0048b2a0), gated on
`DAT_0438b8f8 != 0 || DAT_056db054 % 4 == 0`.  The port HAS this emit (`co_emit_wing_sparkle`,
scene1_companion_ctrl.c) but at **1/4 the rate**, because: **retail FREEZES db054 in cc08==4**
(the engine bumps db054 at the free-roam-only FUN_0048b850 tail, which doesn't run in cc08==4) —
**frozen at 156, and 156 % 4 == 0 ⇒ the sparkle fires EVERY frame**; the port kept INCREMENTING
db054 (scene1_sim.c's non-walk companion fallback advanced it), so `db054%4==0` held only every 4th
frame.  **FIX (`scene1_sim.c`):** gate the db054 advance on `player_ctrl_cc08() != 4` — freeze it
in customer service like retail.  **VERIFIED:** db054 now frozen at **156** (port==retail), the
cc08==4 rng rate **5.53→10.03/f == retail 10.02/f**, the type-0x1f particles emit every frame.
3335 host tests pass; only cc08==4 is affected (free-roam/dialogue db054 unchanged).  The
first-customer **offer is non-deterministic** (recording 1536 / free-run capture 1548 / post-fix
1572 — retail itself varies run-to-run; §8.5), so the RATE+order matching is the correct goal, not
a single value.  **The cc08==4 trace is now rng-rate-1:1** (camera+anim+state+particles+rng-rate
verified); residuals = the non-deterministic offer phase + the few-frame dialogue-reveal timing.

## 8.9 Chip 3f LANDED (2026-06-19) — the dialogue NAMEPLATE (slot-1) + the HAGGLE UI (FUN_00466b7b §2-4) render 1:1

User-flagged 2 remaining tutorial gaps: (1) "dialogue character names not showing for a lot of the
lines", (2) "haggling ui missing entirely". Both ported + v3-verified this session.

### Nameplate slot-1 (`feb2254`) — retires PORT-DEBT(cs-nameplate-slot1)
FUN_00466b7b §6 (the per-speaker pose loop) draws BOTH speakers' name plates from `chrname.tga`
(`DAT_073cc8d0` = `g_scene_buy_chrname`), alpha = `pose_timer[slot]*0x20 - 0xe1`:
- **slot 0** (left, Recette): FIXED cell src {0,32,128,64} → dst {308,300,128,32}. (Was already ported.)
- **slot 1** (right, the customer): the cell is indexed by the **kyaku record's name_index** =
  `*(int*)(&DAT_06a5ea90 + DAT_0730b56c*0x2c670)` (all.c:63 466b7b 427-443). Layout: `ni<=0x15` →
  col=`ni/7`, src.top=`(ni%7)*32`; `ni>0x15` → col=`(ni-0x16)/8`, src.top=`((ni-0x16)%8)*32 + 256`;
  src.left=`col*128`; cell 128×32 → dst {204,300,128,32}.
The port already parses `name_index` (`tables_kyaku` from `名前番号:`); the snapshot now carries
`cust_name_index = g_kyaku.records[b56c].name_index` and the render computes the cell. So customer
(slot-1) lines now show the name plate (was: only Recette's lines did).

### Haggle UI FUN_00466b7b §2-4 (`12d668e`) — retires PORT-DEBT(cs-render-rest)
The scripted tutorial sell drives these (verified state: b5a0→0x3c, b598→0xf, b58c→5, b59c=1, ask=1300,
base=1200). **b5d0 (the autonomous "name a price" digit panel, §1 + FUN_0046602e e) is NEVER set on
the scripted path** ⇒ inert (PORT-DEBT(cs-render-priceinput)).

- **§2 price-INFO panel (b5a0)** (466b7b 131-250): armed by script op-2 (price-set), ramped to 0x3c by
  the master tick. Slide-in `slide = sin(b5a0*2.5132742/15)/sin(2.5132742)` (=1 once b5a0≥0xf);
  arrival flash `pa = 0x7f - ftol(sin((b5a0-0x1e)*π/15)*-128)` for 0x1e<b5a0<0x2e. Backdrop = shopmode
  src {432,0,607,175} → dst {304-slide·88, 120-slide·88, slide·176, slide·176}, grey 0xff7f7f7f
  (ADDSIGNED). At b5a0≥0x26: the item-name line (304,80 / 0.8), "Base Price N,NNN" (304,168 / 0.6,
  comma-grouped via `FUN_00469abb`), "Showcase Item" (304,64 / 0.8 if b564), the item icon (cat atlas
  `item_icons[category]`, cell=subindex, dst {280,96,48,48}), the data_win frame (src {288,320,480,352}
  → dst {440,440,192,32}).
- **§3 BARGAIN!! banner (b598)** (466b7b 251-311): armed by b59c (PRID/PRIA), ramped by the master
  tick. `FUN_0046c86f(b598, &bx, &by, &alpha, b59c==0)`; alpha 0xff (or 0x7f while a speaker poses).
  Banner = shopmode src {0,0,432,176} → dst {284-bx·220, 306-by·88, bx·432, by·176}. At b598≥0xa: the
  asking-price NUMBER via **`FUN_00468034`** (new port — `"%7d"`, 7 cells, shopmode digit row src-y
  352-392, 32×40 @ 36px pitch +8 after cells 0/3/6) at (176,290) grey 0x7f7f7f; the digit cursor
  (shopmode {448,176,496,224} → {cx,288,32,48}, cx=`409-b560·0x24` − group gaps, pulse
  `0x7f-ftol((sin(b5b4·0.2)+1)·-32)`); the prompt (b51c≠0: fileidx==1 → "What should I pay?" yellow
  0xffff37, else "How much should I?" white) at (312,250 / 1.0) gated `b598==0xf||b59c>0`; the markup
  "`(ask/base)·100`% Of Base Price" right-aligned at (400,342 / 0.8).
- **§4 BUTTONS (b58c)** (466b7b 312-381): b58c climbs to 5 during the PRIA confirm poll. ybase =
  (b5a8==3 ? 186 : 362). Per button i∈{0,1}: panel = item_win src {400,240,640,304} → dst
  {312-scale·96, i·0x30+ybase+24-scale·24, scale·192, scale·48}, scale=`b58c·0.2`; col grey (selected
  0xff7f7f7f else dim 0xb97f7f7f). While b590≥1 the selected button pulses
  (`0x7f-ftol(sin(b590·π/15)·-128)`) and the other shrinks+fades (`scale=1-b590·0.1`, alpha
  `0xff-b590·0x19`, skip if <0). Label (b5a8==3 → "Accept Order"/"Refuse", else "Okay!"/"Start Again")
  centered x=312 / 1.0 at ybase+(i==0?12:60).

**Const recovery:** the decompile drops the x87 FP consts (NaN garbage / dropped call args); recovered
the trend tint (objdump `467104` `mov $0xff7f7f7f` for trend 0; >0 red 0xffff0000/0xffff4d4d, <0 blue
0xff0000ff/0xff4d4dff), the panel ramps, the cursor/button pulses, and every `flds 0x519XXX` geometry
const via the new **`tools/decode_exe_const.py`** (VA→float over the PE sections). Text helpers map to
the existing `font_draw_text_centered` (FUN_0047d14c), `font_draw_text_right` (FUN_0047d2db),
`font_draw_text` (FUN_0047ca05); the number formatters to `cs_format_grouped` (FUN_00469abb, comma
groups) + `snprintf` (FUN_005038ff); item lookup to `tables_item_find_slot_by_id` (FUN_004681f6).

**v3-VERIFIED 1:1** (`house-customer-tutorial`, content-matched at ask=1300/base=1200, port kept 2552
vs retail 2563): the BARGAIN!! banner, "Base Price 1,200", the "1 300" number, "108% Of Base Price",
and Okay!/Start-Again all render bit-identical to retail (feed "cc08==4 haggle UI ported"). 3337 host
pass. The 0x48670f probe gained b598/b59c/b58c/b560/b540 so the v3 state panel + flow_diff verify the
UI — but the RETAIL Frida hook still lacks b598/b58c (the port-side state matches retail's ask/base/
b5a0; extending the retail hook for full state-panel parity is a noted follow-up).

**Remaining cs-render PORT-DEBT:** cs-render-priceinput (b5d0 digit panel, inert on the scripted path),
cs-price-trend (FUN_004361b2 High/Low tint → 0), cs-haggle-prompt-live (the b51c==0 live-machine
prompt), cs-stage-msg (per-line grp/se), the FUN_0046602e (d) item-want panel (b56c∈[2,9], inert).

**⚠ OPEN BUG (user-flagged, ROOT-CAUSED 2026-06-19) — the NUMBER + buttons render DIM (MODULATE vs
ADDSIGNED).** The LAYOUT/CONTENT is 1:1 (verified), but retail draws the ADDSIGNED-designed elements
under `SetTextureStageState(0,COLOROP,D3DTOP_ADDSIGNED=8)` while the port's `render_quad_bind` defaults
COLOROP=MODULATE (render_quad.c:269); a 0x7f7f7f diffuse → full-brightness under ADDSIGNED but HALF
under MODULATE ⇒ dim (same class as the sold-out-text bug). Objdump ADDSIGNED brackets in FUN_00466b7b:
price TEXT `0x46714b`→`0x467240`, NUMBER+cursor `0x46749b`→`0x4675db`, BUTTONS `0x4677de`→`0x4678f9`
(all `push 8;push 1;push ebx;call *0xfc`; resets `push 4`). FIX: bracket those draws with COLOROP
ADDSIGNED/MODULATE in customer_service_render.c (pattern: scene_guild.c:833/906/1039) — override COLOROP
after `render_quad_bind` for the number/cursor/buttons; the panels + item icon are MODULATE on both
sides (verify whether the "dim icon" the user saw is a separate read). **Side-by-side caveat:** the v3
join is PARTIAL (119/2698) — the HOUSE_FREEROAM anchor + the +1507-frame load-stretch misalign the
cc08==4 scene. Re-anchor the join on **CUSTOMER_SERVICE_ENTER** (captured on both sides; §8.4/§8.5 prove
port==retail align occ2-relative) so the haggle frames pair → then the dim fix is in-tool verifiable.
First `--anchor CUSTOMER_SERVICE_ENTER` attempt yielded no join (the cached extent is HOUSE_FREEROAM-
relative; orv3_window likely needs a re-slice/re-key to a stored non-base anchor). Next-session tasks.

---

## 9. BUY-ROUND (tuto2) haggle bug ✅ REAL ROOT CAUSE FOUND + FIXED 2026-06-20 (§9.8) — the parser stride was 50, should be 200

**⚠⚠ READ §9.8 FIRST — it supersedes §9.1-§9.7.**  The whole §9.1-§9.7 trail rests on a FALSE premise:
that the parser writes tuto files at a 50-record stride while the consumer reads at 200 (a "stride
mismatch" that packs tuto1/2/3 into the fileidx-0 region with overlap corruption).  **A runtime Frida
dump of retail's `g_tuto` (§9.8) proves the parser stride is ALSO 200** — tuto1@0, tuto2@200, tuto3@400,
NO overlap.  The "mismatch" was a Ghidra decompile error (`imul …,0xe740` rendered as `local_c * 0x32`).
So: the buy round is NOT reached by "PC walks tuto1→tuto2 in fileidx-0" (§9.7) — that path only existed
because the PORT's wrong 50-stride corrupted the data into a fake tuto1→tuto2 bridge.  In retail tuto1
ends cleanly (sentinel ~slot 78) and the buy tutorial is a separate fileidx=1 entry reading tuto2@200.
The `5c0493a` 値段→op5 fix (§9.7) was correct-but-incidental; the SOFTLOCK the user kept reporting was
the stride-50 corruption.  **Trust §9.8.  §9.1-§9.7 are kept only as the (wrong-premise) trail.**

**User directive (2026-06-20):** extend into the BUY round, diagnose, fix. **DONE** — see §9.7.

### 9.1 Empirical: the whole trace is b5a8=2 (SELL), b5b0=0 (tuto1)

Re-drove RETAIL over the full trace with the probe extended (b51c/b5b0/b5bc/b150/f404/gold added to
`tools/flow/retail_fields.json` — the 0x48670f hook). Extracted the 19351 `house_update` rows
(retail frames 13952→33951). The **only** transitions of the buy-discriminating fields across the
ENTIRE trace:

```
f13952: b5a8=-1 b5b0=0 b51c=1 f404=1 gold=55 b56c=0     ← customer 1 entry (scripted)
f14012: b5a8=2  b56c=1                                   ← SELL
f19984: b51c=0                                           ← scripted round ENDS (b51c reset)
f23152: f404=65536 / f23153: b5a8=-1                     ← inter-customer reset
f23220: b5a8=2  b56c=13                                  ← next customer = SELL (kyaku 13)
```

**`b5a8` is ALWAYS 2 (sell); `b5b0` (fileidx) is ALWAYS 0 — never the buy values.** All 5 PAUSE_OPEN
rounds (raw 2920/4785/6099/7344/8386) are SELL haggles. The user's "2nd PAUSE_OPEN @line 130" maps to
raw **7344 = round 4 = the first REAL sell customer after the scripted demo's load** (raw 6512), NOT a
buy round. The recording (`rec-20260617-051426`, anchors raw 0–9678) is ONE day — the SELL tutorial —
ending at "first real customer". The buy tutorial (tuto2: *"people will want to sell items TO you …
Haggle DOWN … name a price lower than the base"*) is a **later day** not in this recording.

### 9.2 Machine structure (empirical b534 trajectories)

- **Customer 1** = the SCRIPTED machine `FUN_00461c00` (`b51c=1`): b534 stays in {0,1}; base 1000→3000→
  1200; 3 BARGAIN rounds at b150 0→1 (f16452/f18317/f19631). At close (b534=12→) **b51c resets to 0**
  (master tick, all.c:60597 `if (b51c!=0){ b51c=0; b524=0; b534=0; }`).
- **Customers 2+** = the **REAL kind-2 sell machine `FUN_004658ab`** (`b51c=0`): b534 cycles
  **1→2→6→15→7→10→12→20→21→0** (greeting→reaction→decision→accept/leave), offer b574=3870/3960…
  **The port has NOT ported `FUN_004658ab`** — only the scripted machine. So even the SELL trace's
  customers 2+ are unported (a separate gap from the buy round).

### 9.3 Static: fileidx is ALWAYS 0 — tuto2 is reached by PC-progression, NOT fileidx=1

`DAT_005c6bb0` (fileidx) is written by exactly ONE instruction (objdump `recettear.unpacked.exe`):
`0x461bfa mov ds:0x5c6bb0,eax` inside `FUN_00461bf6(param)`. Its 4 call sites push **0, 2, 2, 2**
(0x48756a / 0x4877b6 / 0x4884c7 = 2; 0x488bc8 = the player-Z sell-counter entry = 0). **Never 1.**

The reason fileidx need never be 1: the **parser/consumer STRIDE MISMATCH** (`src/tables_tuto.h`):
parser writes each file at `file_idx*50` (tuto1→0-49, tuto2→50-99, tuto3→100-149) but the consumer
reads at `(fileidx*200 + PC)*0x128` (`FUN_00461c00` 0x461c08). **With fileidx=0 the PC (b604) walks
slot 0,1,2,… so PC≥50 reaches tuto2's records, PC≥100 reaches tuto3 — all three tutorials live in the
fileidx=0 region.** So the tutorial progression tuto1→tuto2→tuto3 is **by PC**, and fileidx stays 0.

**⇒ The `FUN_00461c00` op-5/op-0xd threshold branch gated on `DAT_005c6bb0==1` (all.c:59843) is
effectively DEAD** (fileidx never 1). The buy haggle direction does NOT come from that branch. (The
FRONT's earlier "fileidx=1 / tuto2 / buy thresholds" root-cause was PARTLY wrong on this point.)

### 9.4 What the buy round (tuto2) actually IS — the corrected port target

- **Entry/trigger:** the buy-tutorial day sets up the scripted session with **b5a8=0** (buy; the
  transaction-type, set NOT by `FUN_00461bf6` but by the kind selector path — `FUN_00461792` writes
  `b5a8=0` at all.c:59722 / `FUN_00460fa7` writes `b5a8=1`; which one the buy tutorial uses is STILL
  UNRESOLVED statically) and the **PC (b604) seeded to the tuto2 offset (~50)**, b51c=1, fileidx=0.
- **Buy direction comes from:** (a) **b5a8==0** → the gold-cap in `FUN_0045ff31` (all.c:58323: `if
  (b5a8==0 && f404[player]==0 && gold < offer) offer = gold` — clamp Recette's bid to her gold) +
  the line-type `uVar18=7` (all.c:60618); (b) **tuto2's own GOTO-target script** (the "lower than
  base" structure is encoded in the tuto2 records, not a global threshold flag); (c) the down-math
  `FUN_004603cf`/`offer_down` (already in `customer_haggle.c`) — **but WHERE the scripted machine
  calls offer_down vs offer_up for the buy round is UNVERIFIED** (FUN_00461c00 op-4 calls
  `FUN_00460161`/up; the b5a8-gated down path is not yet located in the scripted machine).
- **Prompt:** "What should I pay?…" (`DAT_005c6e28`) vs sell "How much should I?…" (`DAT_005c6e40`) —
  the render selects by … (also unverified which flag).

### 9.5 ⛔ BLOCKER + next-session plan

**The buy round CANNOT be ported correctly from static analysis alone** — too many subtle interacting
flags (fileidx-always-0, the dead fileidx==1 branch, b5a8=0 vs =1 ambiguity, the script-encoded
direction, the up/down call site). Per the porting-loop rule ("don't guess — synthesize a trace and
probe"), the buy port is **BLOCKED on a buy-tutorial (tuto2) trace**, which requires HUMAN PLAY:
day-1 sell tutorial → sleep → day-2 buy tutorial (Frida-spawned retail isn't keyable; the port can't
navigate even the sell trace yet — see 9.6).

**Next session (needs the human):**
1. **Record a buy-tutorial trace** (`F2/F3` recorder): play to the buy-tutorial day, capture the buy
   haggle. Pin {phasepin}+{rngseed}. This is the long pole and the ONE thing only the human can do.
2. Re-drive both sides with the now-extended probe (`b5a8/b5b0/b51c/b150/f404/gold/b5bc`), read the
   ACTUAL buy-round state (resolve the b5a8=0-vs-1, the up/down call site, the prompt/threshold gate).
3. Port the buy path 1:1 from that ground truth + host-test the gold-cap (all.c:58323) + the down-math.

### 9.6 PAUSE_OPEN/b150 at the BARGAIN ✅ FIXED for round 1 (2026-06-20, `2fb5b39`); round 2-5 nav UNVERIFIED (a harness WM_DESTROY early-exit, not a confirmed port gap)

**Was:** the port couldn't navigate any haggle trace past round 1 — it never fired `PAUSE_OPEN`
(=`pause_active` 0→1 =retail's `DAT_0438b150`, which retail sets via `choice_box_open`/`FUN_00434def`
when the BARGAIN Yes/No choice opens; the port split b150 so the haggle's choice never set the anchor's
flag). The trace's `{wait PAUSE_OPEN}` exhausted and the exe exited right after the offer (~f3131).

**Fix (`2fb5b39`):** OR `customer_service_bargain_active()` (the scripted machine's `b608==4`
price-confirm state, b51c!=0) into the anchor's `pause_active` (main.c). **Anchor SIGNAL only** — does
NOT touch the engine's real pause (`g_scene_pause_state_b150`) ⇒ zero gameplay/render effect, can't
fire outside a cc08==4 haggle. **VERIFIED:** the port now fires `PAUSE_OPEN` @f3128 + `PAUSE_CLOSE`
@f3259 (131-frame span) at round 1's BARGAIN — matching retail's b150 (f16452-16582 = 130f), exactly at
the offer commit (b608→4, b574 set, b58c ramps 0→5 then 5→0). (The earlier "b58c=0 at sim-end" read was
the OLD slow BMP-dumping drive dying the very frame the choice opened; `--capture-trigger-only` made the
drive 37 s vs 481 s and showed b58c ramping cleanly.) The probe now carries b51c/b608/b5b0.

**REMAINING — round 2 navigation UNVERIFIED (a HARNESS early-exit, NOT a confirmed scripted gap):**
after round 1's PAUSE_CLOSE (f3258) the exe self-exits cleanly at ~f3360, ~3 frames after the round-2
segment's FIRST input (X `0x0020` @seg-frame 99 = f3357), with cc08 STILL 4 and the scripted machine
progressing normally (b608 0→-1→0 = a dialogue advance). The exit is NOT max-frames (40000), NOT
max-duration (480s), and NOT the caprange/calltrace window end (verified: stop @3360 with BOTH windows
[627,7627)) — so it's a `WM_CLOSE`/`DestroyWindow` (main.c only PostQuits on max-frames/duration/
window-destroy). So the harness quits before the round-2 segment's later inputs (the re-haggle commit
@seg-frame 1735) ever apply ⇒ round 2's BARGAIN never gets a chance to open. **The early-exit pattern
tracks the first UNFIRED `{wait}`** (pre-fix the exe stopped ~4f after the offer/line-50 wait; post-fix
~100f after the round-2/line-68 wait) — i.e. the harness appears to give up shortly after a no-timeout
`{wait}` stops resolving, BEFORE the segment's inputs finish applying. **Next-session (HARNESS first,
then re-verify):** find what posts WM_CLOSE on an unfired no-timeout `{wait}` (the segtrace itself only
spams sticky inputs + breaks — input_segtrace.c:737-825 — it does NOT quit; check scenario-test /
run-openrecet supervisor + main.c's WndProc for a stuck-wait/diverged watchdog); OR add a `timeout` to
the round `{wait PAUSE_OPEN}`s (measured from the segment's LAST input, like the committed walk fix) so
the round-2 inputs apply before the wait gives up. THEN re-drive to see if the port actually re-haggles
rounds 2-5. The BUY round is still separately blocked on a buy-tutorial recording (§9.5).

### 9.7 ★ THE BUG: 値段 (tuto2 buy threshold) was parsed as op 12 (2-way), should be op 5 (7-tier) — FIXED `5c0493a`

**Symptom (user):** the 2nd price prompt — tuto2, where Tear talks about BUYING — "always says I need to
price it lower even though I'm below the baseline" / uses the sell UI/logic.

**Repro (in-tool):** drive the committed trace through round 1, then a held-X segment fast-forwards the
dialogue into tuto2; the PRIA opens at PC 81 (b608=3), commit a price → the `値段` threshold at PC 82.
(Probe b604 = the PC; `customer_service_b604()`, added to both hooks — `4ce0a30`.)

**Root cause (confirmed from the engine BINARY, not just decompile):** the engine's tuto-parser opcode
.data table (`by-address/475270.c:2987-3046`; strings dumped from `recettear.unpacked.exe .data`):
```
0x5cb3d8  42 55 4e 30  "BUN0"  → op 5   (7-tier, fileidx-gated threshold)
0x5cb3e0  92 6c 92 69   値段    → op 5   (SAME handler — tuto2's BUY branch)
0x5cb3e8  8d 82 82 ad   高く    → op 12  (2-way PRICE compare — tuto1's sell check)
```
The port's `tables_tuto.c` opcode table WRONGLY put 値段 with 高く under op 12 (TUTO_OP_PRICE). So the
port ran tuto2's 7-tier `0,値段,10,11,12,13,14,15,16` branch through the 2-way op-12 handler (only
args[0]/args[1] = ids 10/11 reachable). Combined with the **stride overlap** (§9.3: parser writes each
file at slot `file*50`, consumer reads at `file*200`, so tuto1+tuto2+tuto3 all live in the fileidx-0
region 0-159, and `cs_goto`/`FUN_004623bc` searches from `g_tuto[fileidx*200]`=`g_tuto[0]`), `cs_goto(11)`
found **tuto1's** id-11 record FIRST (slot 42 = tuto1's "Yes" → "it is a sale, you get experience" — the
SELL success path), because tuto1's 高く only defines ids 10/11. ⇒ the buy round fell into tuto1's sell
dialogue.

**Fix (`5c0493a`):** map 値段 → `TUTO_OP_BUN0` (op 5). Verified on the held-X drive: the tuto2 threshold
GOTO now lands at **PC 90 (tuto2's id-13 feedback "…go somewhat lower")** instead of PC 42 (tuto1). The
7-tier targets the buy round actually uses for near-base offers (12-16) DON'T collide with tuto1 (which
only defines 10/11). 3337 host pass; test `tables_tuto_nedan_alias_takaku` corrected.

**✅ RETAIL-CONFIRMED (held-X drive, `retail-…012335Z`, b604+b5b0 in the probe):** retail runs the tuto2
buy practice under **fileidx=0** — the PC walks the SAME 42→…→50→…→**81 (tuto2 PRIA)** path as the fixed
port, fileidx 0 the whole way, base=1200/ask=1200 default. So op 5's fileidx-gate picks the **SELL tier
boundaries (0.5/0.7/1.0)** for the buy practice on BOTH sides — that's the engine's actual behavior (the
tuto2.txt designer comment's 20%/70%/90% is the buy-tier *intent*, but fileidx never becomes 1 so the
engine never uses it). **So the port now MATCHES retail** — `cs-buy-fileidx` is NOT a debt; using the
sell tiers under fileidx=0 IS parity. (The retail drive hit the 480 s ceiling AT the PRIA before the
commit, so the post-commit GOTO target wasn't directly captured — but it's deterministically identical:
same op 5, same fileidx=0, same `cs_goto` base, same parsed `g_tuto`.)

**Accepted residual (parity, not a bug):** for LOW offers (<70%, op-5 targets 10/11) `cs_goto` collides
into tuto1's id-10/11 on BOTH sides (same base `g_tuto[0]`, same data) — an engine quirk of the stride
overlap, identical port↔retail ⇒ accept. The near-base 70-100% case (targets 12-16) is collision-free.
**Pending (human, next session): eyeball the fixed buy dialogue in the viewer** — drive the held-X
scenario (or just hold X past round 1) and confirm tuto2 now shows its own "go lower"/"good price"
feedback instead of tuto1's "Yes, it is a sale". This is the user-deferred visual check.

### 9.8 ★★ THE REAL ROOT CAUSE: parser stride was 50, must be 200 — FIXED 2026-06-20

**User report (2026-06-20):** the `5c0493a` (§9.7) fix did NOT work — holding X past the first haggle
prompt still reaches a "wrong prompt" that **softlocks** ("Tear always says price lower than base even
when you price it lower").

**Method:** per the porting loop, stop trusting the decompile and OBSERVE retail.  A Frida dump of
retail's parsed `g_tuto` (`&DAT_005d1fc8`, spawn the unpacked exe, hook `FUN_00475270` onLeave):

```
retail g_tuto regions (non-empty):  0..78,  200..260,  400..440
   slot   2  id=9  op=4 (PRIA)            ← tuto1
   slot   3  id=0  op=5  args=[…]          ← tuto1 first practice
   slot  54  id=29 op=4  + slot 55 op=5    ← tuto1 値引 haggle  (slots 50-78 are TUTO1, not tuto2!)
   slot 224  id=9  op=4  + slot 225 op=5   ← tuto2 (base 200)
   slot 245  id=19 op=4  + slot 246 op=5   ← tuto2 値上
   slot 400+                                ← tuto3 (base 400)
```

**The smoking gun:** retail's tuto1 has 79 records (0-78).  If the parser stride were 50, tuto2 (file 1,
base 50) would have OVERWRITTEN slots 50-78 — but those slots hold *tuto1's own* 値引 haggle (id 29-37).
So **tuto2 is NOT at slot 50; it's at slot 200.  The parser stride is 200, identical to the consumer.**
There is NO stride mismatch and NO overlap.  Ghidra mis-decompiled the parser's per-file `imul …,0xe740`
byte stride (`0xe740 == 200*0x128`) as `local_c * 0x32` (= 50); the earlier port + §9.3 trusted that.

**Consequences of the port's wrong stride-50:**
- tuto2/tuto3 records collided into tuto1's tail (slots 50-159), garbling op-5 args, `cs_goto` targets,
  and dialogue text.
- Walking the PC past tuto1 (which, corrupted, never hit a clean sentinel) fell into a *fake* tuto2
  fragment at slot ~82 — fileidx=0, so it ran the SELL tiers + `cs_goto` collided ids 9/10/11 into
  tuto1 → the unescapable "price lower" loop the user saw.  **That tuto1→tuto2 "bridge" only existed
  because of the corruption; it is not a real retail path.**

**Fix:** `TUTO_PARSER_STRIDE = 50 → 200` (`src/tables_tuto.h`).  Now the port's parsed `g_tuto`
bit-matches retail's layout: tuto1@0-134, tuto2@200-289, tuto3@400-459 (English build), each file clean.
Verified: tuto2's buy `値段` lands at slot 232 (`op 5`, args `[10,11,12,13,14,15,16]` = the script
verbatim), slot 235 = id-11 "Excellent, this is a good price" → `GOTO 17` (the proceed path), with
tuto2's OWN text (no stale tuto1 leftovers).  With fileidx=1 the op-5 BUY tiers fire and `cs_goto`
resolves from slot 200 (tuto2) — no tuto1 collision.  Host: 2 stride tests rewritten
(`tables_tuto_file_index_stride` → slot 200, `…_no_overlap_into_next_file`); 3337 pass.

**Behavioral evidence:** re-driving the committed sell-tutorial trace, the scripted PC now walks tuto1
to **b604=131** (高く@39 → 値段-sell@74 → 値引@107 → closing@131) — it was STUCK at b604=69 before,
because the corrupted GOTOs looped.  The corruption was *blocking* the sell tutorial's later half too.

**fileidx CAN be 1 (§9.3 corrected):** the `FUN_00461bf6` call sites push `0`, `2`, or **a register
`ebx`** (the conditional-branch arms at `0x487565` / `0x4877b1`), not the constant `2` §9.3 reported.
The buy-tutorial entry sets `ebx=1`.  The op-5 `b5b0==1` BUY-tier branch is therefore LIVE, not dead.

**Still open (separate task, needs a trace):** the BUY tutorial is a separate day (tuto3 = the
item-recommendation tutorial is yet another).  This sell-tutorial recording never sets fileidx=1, so the
buy round is not *reachable* here to drive end-to-end — porting/verifying the fileidx=1 buy-tutorial
ENTRY needs a recording of that day (the §9.5 human escalation still stands, but now for the ENTRY, with
the data-layout + op-5/cs_goto logic already proven correct).

## 10. The LIVE kind-2 sell machine FUN_004658ab — Chip L1a (un-softlock) LANDED 2026-06-20 (`7dfc611`)

The "softlock once Tear tells you to sell her something" = the FIRST REAL CUSTOMER.  After the scripted
tutorial closes (b534=0xc→0, b51c→0; §9 + the `0c0331c` closing port), the master tick idles and the first
customer greets at **b534=1, b51c=0** (confirmed @port-fr7115, full-probe drive).  The port's b534==1 arm
only handled b51c!=0 (the scripted machine); the b51c==0 branch was a bare PORT-DEBT return ⇒ b534 froze.

### 10.1 What was ported (by-address transcription)

- **master tick `FUN_00462403`** (all.c:60397-60668): the **b51c==0 live greeting** (FUN_00460a1a line, Z
  after reveal → b534=2), the **b5a8==2 dispatch** → FUN_004658ab, and the live **closing/queue** states
  (0xa "thanks" → 0xc; 0xb leave → 0xd; 0xc/0xd close → f404 ? 0x14 queue-advance : idle, f406 → b520=1
  leave/dissolve; 0x14 → 0x15 → idle).  The scripted close (b51c!=0 reset) + the ESC-skip b520 leave are
  preserved.
- **`FUN_004658ab`** (cs_live_machine): 2 greeting → 6 reaction/price-edit → 0xf decision → 7 accept /
  8 pushback / 9 reject.  Decision: `offer(b574) < ask(b8)` → (ask<floor(b580) || f406 → 8) else 9;
  `offer>=ask` → (base·0.8<ask || !f404 → accept 7) else 8.  Reuses cs_offer_up (FUN_00460161),
  cs_digit_count/edit, cs_input_poll.
- **`FUN_00460672`** (cs_accept_eval, like-grade): returns 1 if ask within **±0.5%** of b588, 2 within
  **−5%/+5%**, else 0.  The 4 bands = ftol(b588 × {1.005f@0x519e08, 0.995@0x519e00, 1.05f@0x5198ac,
  0.95@0x519df8}); if b588<0x6e the 0.995 band collapses to the 1.005 band.  (Ghidra dropped the x87 mults —
  objdump-transcribed @0x460672.)
- **`FUN_00460f16`** (cs_pushback_line → 2/3/4) + **`FUN_00460a1a`** (cs_pick_line — the live line picker:
  ONE rng draw `rand % count` for f404==0 customers, line 0 for the tutorial).

### 10.2 Verification + what's left (Chips L1b/L1c)

Un-softlock proven on the real flow (first-customer **b534 1→2→6→0xf, offer b574=3870** = the §9.2-observed
value) AND deterministically (host `cs_live_machine_sell_cycle`, the forced-sale f406 path: greeting→2→6→
0xf→7→0xa→0xc→leave).  A bit-exact port↔retail trace comparison is **blocked on the multi-round nav** (§9.6):
the port's scripted tutorial closes after 3 rounds, retail after 5, so the two diverge BEFORE the first
customer — a clean shared trace to the live sell needs the round-2..5 navigation fixed first.

- **L1b — the accept side-effects** (PORT-DEBT(cs-live-sale-fx), all gated f404==0): gold `(&DAT_044e37a4)
  [player·0xb7f2] += ask` (= bank dword 3), the stock-short decrements (DAT_045109a8), and FUN_00460d52
  (payout float + the +/- money anim), FUN_00460b3a (best-sell-price record), FUN_004606fc (the exp/payout
  ticker), FUN_00460083 (inventory add + the displayed-stock wishlist), FUN_0046002a (remove from a list),
  FUN_00460b93 (447B — the sold-item catalog/like records).  For the FORCED sale (f404=0) these DO run.
- **L1c — the per-customer dialogue buffer** (PORT-DEBT(cs-kyaku-dialogue)): the engine record's 0x6e70 text
  / 0x6df8 per-type count / 0x51d8 sprite / 0x5b38 voice, loaded from `kyaku/fN.txt` (the `file:` path the
  port already parses).  Currently a placeholder "..." drives the reveal so the state advances (rng-correct,
  text-wrong) — porting the loader gives the real lines + frame-exact reveal timing.

## 11. ★★ §9.6 "scripted closes after 3 rounds" was a MISDIAGNOSIS — the "5 rounds" = 3 SCRIPTED + 2 LIVE (2026-06-21)

**The whole "multi-round nav gap" / "harness WM_CLOSE early-exit" framing (§9.6) is WRONG.**  Driven to
ground truth by the porting loop (a wide-window `--call-trace` drive of BOTH sides on the recording's own
inputs), the picture is:

**(a) §9.6's "f3360 early-exit" was a SLOW-DRIVE TIMEOUT, not a port gap.**  The exe never posts WM_CLOSE
mid-haggle.  The f3360 stop was a *BMP-dumping* `scenario-test` drive hitting the 480 s wall-clock ceiling
(`--max-duration-ms` → the `AUTO_EXIT_TIMER` `DestroyWindow`) while writing capture BMPs over 9p at ~0.4 s/f
— it never even reached round 1.  A `--capture-trigger-only` drive runs the whole flow in ~40 s with NO
early exit.  **Always drive the haggle with `--capture-trigger-only`** (or the v3 window tool); a naive BMP
drive times out long before the haggle.

**(b) The SCRIPTED tutorial is exactly 3 rounds and BIT-IDENTICAL port↔retail.**  `tuto1.txt` has three
`PRIA` price-input steps (ids 9 / 19 / 29 — the 高く / 値段 / 値引 checks), split by two `TOUT`s.  With the
recording's inputs the PC walk is identical on both sides: `PC 0→17→19 (base 1200)→38 (R1, ask 1300, 高く
ok)→42→…→70→72→73 (R2, ask 1400, 値段 100-130% tier)→84→…→106 (R3, ask 1330, 値引)→…→131 = the −1 sentinel →
b534=0xc → close (b51c→0)`.  Three `PRIA` = three `b608==4` BARGAINs = three `PAUSE_OPEN`s, then close.  The
port already does this frame-for-frame (port R1/R2/R3 @offset 2500/4366/5681 vs retail @offset 2502/4367/
5681).  **There is no scripted-round bug; the port matches retail.**

**(c) Retail's "5 rounds" = the 3 scripted + the 2 LIVE first-customer (Tear) practice-sale BARGAINs.**
After the sentinel closes the scripted machine (`b51c→0`), the master tick starts the live kind-2 machine
(`FUN_004658ab`, §10) on Tear as the practice customer (`b56c=1`, `f404=1`): `b534 1→2→6→0xf (decision) →7
(accept) →0xa→0xc→0x14 (queue-advance) → next practice customer → …`.  **This is bit-identical port↔retail
too** — both reach `b534 1→2→6`, **offer `b574=3870`**, ask-climb 3000→3400.  Retail opens the *same*
`DAT_0438b150` choice box at the LIVE decision (`b534==0xf`, the `FUN_004622d9` poll) that the scripted
machine opens at `b608==4`.  So retail's full anchor sequence is:
`R1,R2,R3 (scripted) → LOADING(TOUT) → R4 (live) → LOADING → R5 (live) → LOADING+CONV_POSE (wrap-up) → free-roam`.
The recording's anchor log reproduces it exactly (R1@2920 … R5@8386, LOADs after R3/R4), so the recording is
a faithful retail trace, NOT a buggy-record-time artifact.

**(d) The port's gap was ONE missing signal.**  `customer_service_bargain_active()` only returned
`b51c!=0 && b608==4` (the scripted price-confirm), so the LIVE BARGAINs never raised the `PAUSE_OPEN` anchor
→ the trace's round-4/5 segments (gated on `{wait PAUSE_OPEN}`) never activated → the live haggle's Z/confirm
inputs never applied → it stalled at `b534==6`.  **Fix (`e42921a`):** OR in the live decision —
`bargain_active() = (b51c!=0 && b608==4) || (b51c==0 && b534==0xf)`.  Both states run the same
`cs_input_poll` (`FUN_004622d9`) that retail backs with `b150`.  **Anchor-verified:** the port now fires 5
`PAUSE_OPEN`s (3134/5000/6315 scripted + 7559/8600 live) + the scripted→live and inter-customer LOADINGs at
retail's offsets (±~1%); both live sales complete and the port exits `cc08 4→1` to free-roam @~9124.  +host
assertion in `cs_live_machine_sell_cycle` (the live decision drives the signal, and ONLY there); 3341 pass.

**(e) REMAINING (the real next gap, NOT §9.6): the post-sale CONV_POSE wrap-up.**  After the last practice
sale, retail plays Tear's free-roam wrap-up dialogue *"And that is, essentially, how it goes…"*
(`LOADING_START@22962 + CONV_POSE_START@22963`, the existing `scene1_conversation_pose` cutscene), THEN loads
to free-roam (`@23615/23634`).  The port instead takes the live-close `f406` branch (master tick all.c
60590-60661 → `s_b520 = 1`, the leave/dissolve) straight to free-roam @9124, SKIPPING the wrap-up.  So the
user-directive's P2 (the wrap-up trigger) is the next chip; P1 ("finish the tutorial / round-2..5 nav") is
**already satisfied** — the tutorial reaches the −1 sentinel after its full 3-round script, and the live
practice rounds now navigate.  The L1b accept side-effects (real pix) + L1c per-kyaku dialogue still stand.

## 12. P2 — the wrap-up cutscene IDENTIFIED (iv1_7), trigger mapped, but BLOCKED on a flag-conflation (2026-06-21) — ★ RESOLVED §12.1: the "blocker" was a MISDIAGNOSIS

**The wrap-up dialogue is `iv/iv1_7.ivt`** — *"And that is, essentially, how it goes.  You are quite good
for someone who has never done this before."* → Recette *"Eheheh… really?"* → Tear *"We still have a little
bit of time left today, so let us go ahead and open the store proper.  …handle them in the same way that we
just practiced."* → … → *"Now then, I will open us up.  Go on and sit at the counter."* → Recette *"Okey-day!"*
(a multi-line, two-speaker `scene1_intro_dialogue` script; voice `se/01ti/event/tea_sodesu.bin`).

**Trigger chain (decompile, fully mapped):**
- The cs leave/dissolve (master tick `FUN_00462403` @ all.c:60385-392) does, when the sell tutorial closes:
  `if (f404==1) { f404=0; if (f405==0) { f3ff=0; DAT_0450f400=1; } }`.  **The port ALREADY ports this** —
  `customer_service.c:1401-1406` (the byte is labelled `CS_F400_DISPLAY_SUPPRESS_OFF` = 0x2bc68).
- In free-roam the iv-dispatch (`FUN_0044bd0d` @ all.c:45715-724) fires iv1_7:
  `if (f401==0 && f400==1 && DAT_0438b1c8==0) { scene=1; sub=7; FUN_00452d07(0); f401=1; f406=1; }`.
- A later tutorial-chain block clears `DAT_0450f400=0` (all.c:45781).

**★ THE BLOCKER (why the naïve port HANGS at the scene load):** `DAT_0450f400` (0x2bc68) is **dual-use** —
it is BOTH the iv1_7 trigger AND the shop-display interaction gate (`all.c:87703`/`scene1_player_ctrl.c:1286`:
"displays present AND f400==0 ⇒ the cc04 remove-menu").  When I added the iv1_7 branch to
`scene1_tutorial_dispatch_tick` (mirroring 45715: fire on `f400==1 && f401==0`), the **`house-customer-tutorial`
LOAD save (cad868) already has 0x2bc68 set**, so iv1_7 fires PREMATURELY during the NEW_GAME/scene load →
`start_single(1,7)` collides with the load → **hang at frame 231** (reproduced 3×; the committed P1 exe loads
fine).  The port's `scene1_tutorial_dispatch_tick` runs every sim tick (scene1_sim.c:199), including during
the load, where retail's `FUN_0044bd0d` is NOT yet driving — and the `_busy()` gate doesn't cover the load.

**⚠ CAVEAT (the hang diagnosis is CONFOUNDED — re-test in a fresh env first):** late in the same session
the COMMITTED P1 exe (iv1_7 reverted, source unchanged) ALSO began hanging at frame 231 on a clean drive —
yet that exact exe had reached frame 8650 with 5 PAUSE_OPENs earlier (`runs/…001630Z`).  So the late-session
frame-231 hangs are at least partly an **environmental/WSL-interop degradation** from this session's heavy
concurrent exe activity (many overlapping port/retail/studio drives), NOT necessarily the iv1_7 fire.  The
`f400` premature-fire is still a real THEORETICAL risk (the decompile's dual-use is genuine), but it was NOT
proven to be the hang — the iv1_7+DBG drive's "iv1_7 FIRING" print never appeared (stderr was block-buffered,
inconclusive).  **Next session, in a FRESH shell:** (1) confirm the committed exe drives clean again; (2) THEN
re-apply iv1_7 with a one-shot `fprintf`+`fflush` of `bank[0x2bc68]`/`[0x2bc69]` to settle whether it actually
fires at load before blaming/gating it.

**Next-session plan (HARNESS/RE first, then port):**
1. **Confirm the save state:** probe `bank[0x2bc68]`/`[0x2bc69]` of the loaded cad868 at frame 0 (a one-shot
   `fprintf` in tutdisp, with `fflush` — stderr is block-buffered when redirected, which hid the probe this
   session).  Decide: does retail's equivalent save have f400==0 here (⇒ the PORT wrongly has 0x2bc68 set —
   find/fix the spurious write), or f400==1 (⇒ retail must gate the iv-dispatch by call-context the port's
   flat per-tick dispatch lacks — replicate that gate, e.g. only when the scene is in steady free-roam /
   `FUN_0044bd0d`'s actual caller, not mid-load)?
2. Port the iv1_7 dispatch branch (mirror 45715: `f401=1`, `f406=1`) ONCE the gate is right.
3. Verify in the trace studio: after R5's sale the port should fire `LOADING_START` + `CONV_POSE_START` +
   the iv1_7 dialogue (the "And that is…" lines) before free-roam, matching retail (R5 close @22433 →
   wrap-up @22962-23 → free-roam @23634).
4. Then the iv1_7→iv1_8 chain (`f406→f402`, all.c:60381-383 + 45726: "sit at the counter") leads into P3
   (the first REAL customer).

## 12.1 ★ RESOLVED 2026-06-21 — the "f400 flag-conflation BLOCKER" was a MISDIAGNOSIS; iv1_7 PORTED + host-tested

Re-probed in a FRESH shell (the §12 caveat's precondition; the user had also rebooted, clearing the 9p
degradation).  The "blocker" dissolved on two findings:

**(a) f400 has EXACTLY ONE writer — it is 0 at the cad868 LOAD, so iv1_7 CANNOT fire prematurely.**  A
decompile sweep of every `DAT_0450f400` write: set to 1 ONLY at `all.c:60389` (the cs leave/dissolve inside
`FUN_00462403`, when `f404` sell-active clears — the port already mirrors it in `customer_service.c`), cleared
to 0 ONLY at `all.c:45781` (deep in the scene-2 chain).  There is **no prologue / display-tutorial writer**, so
on the pre-haggle tutorial-day save f400 is 0.  The §12 claim "the cad868 LOAD save already has 0x2bc68 set"
was WRONG — a confounded read (stderr was block-buffered then, hiding the value).  **Empirically confirmed:**
a `--call-trace` probe of f400/f401/f406 on the loaded bank (`scene1_player_ctrl.c` 0x48670f block) over the
whole pre-haggle + haggle window = **3014/3014 free-roam rows f400==0, f401==0**.  So the iv1_7 gate
(`f401==0 && f400==1`) is false at load ⇒ no premature fire.

**(b) The "frame-231 hang" was an ENV / 9p confound, not iv1_7.**  Frame 231 = the NEW_GAME / LOADING_START
of the cad868 load.  The committed P1 exe (with a read-only probe, no iv1_7 branch) drives CLEANLY past 231
in the fresh shell.  Root cause of the original "hang": the call-trace was crawling over the 9p
`\\wsl.localhost` mount (line-buffered + a per-frame `fflush` = ~150 write syscalls/frame; the scenario arms
`{calltrace}=[0,9500]` ≈ ~100 MB).  On a degraded 9p that looked like a hang; the user's reboot + this
session's call-trace I/O fix (NTFS staging + full-buffering, see the harness changes) removes it.  The §12
caveat's suspicion ("re-test in a fresh env first") was correct.

**iv1_7 PORTED** (`scene1_tutorial_dispatch.c`): an independent `if` after the iv1_5/iv1_6 block mirroring
all.c:45715 — `if (f401==0 && f400==1 && !busy) { start_single(1,7); f401=1; f406=1; }`.  Same `_busy()`
(= `b1c8==0`) gate iv1_5/iv1_6 use.  **Host-tested** (`test_cs_iv1_7_wrapup_trigger`, 3342 pass): f400==0 ⇒
no fire / f401 untouched; f400==1 ⇒ fires + latches f401/f406; once-only after f401 latches.  The post-haggle
fire is at f400's flip (the cs close).  **★ INTEGRATION-VERIFIED 2026-06-21** by a full `--capture-trigger-only`
drive to the cs-exit (raised ceiling via the new `--max-duration-ms`): the port navigates all **5 haggle
rounds** (PAUSE_OPEN 4289/6155/.../8714/9755 = 3 scripted + 2 live, no mid-haggle collision — iv1_7 correctly
stays dormant while f400==0), then after R5 (`PAUSE_CLOSE@9805`) fires **`LOADING_START@10278` +
`CONV_POSE_START@10279`** — i.e. iv1_7 fires at `LOADING_START+1`, **BIT-MATCHING retail's §11e pattern**
(`CONV_POSE_START@22963 = LOADING_START@22962 + 1`).  The wrap-up cutscene then runs its multi-line script
(several `TEXT_ANIM_START/END` + `DLG_LINE_SHOW/CLEAR` + `CONV_POSE_BLINK`) and ends cleanly
(`CONV_POSE_END@10927`); the drive completes (exit 0) — **no hang, no collision**.  So iv1_7 fires at the right
moment via the same conversation-pose system retail uses.  **Remaining (human/studio):** eyeball the wrap-up
RENDERING 1:1 vs retail (the "And that is…" text content + conv-pose framing) in the trace studio.  Then P3
(the iv1_8 `f406→f402` chain → first real customer).

## 13. L1c — per-kyaku dialogue buffer PORTED 2026-06-22 (retires PORT-DEBT(cs-kyaku-dialogue))

The user-flagged gap "the live first-customer haggle dialogue is **`...` PLACEHOLDER**" (the rounds-4/5 LIVE
practice-sale lines from `cs_live_machine`/`FUN_004658ab`, NOT the wrap-up).  Root: `cs_pick_line`
(`FUN_00460a1a`) drew the variant rng but stubbed `s_b270 = "..."` because the per-kyaku dialogue buffer
(engine record tail) was unmodelled.  Now ported.

**Picker `FUN_00460a1a`** (all.c:58772-58837, the by-address transcription): flat slot **`s = variant +
type*0x14`** (MAXVAR=0x14=20).  Reads, relative to the record base (`DAT_06a5ea90 + kyaku*0x2c670`):
text `+ s*0x100 + 0x6e70` (stride 0x100), sprite `+ s*4 + 0x51d8` (→ `(&b54c)[slot]`), voice `+ s*4 + 0x5b38`
(-1=none, else play `+ voice*0x100 + 0x1444` via `FUN_0049933c`).  Variant = `rand % count[type]`,
`count[type] = + type*4 + 0x6df8`; the rng (`thunk_FUN_005041f6`) is drawn ONLY when f404==0 AND not a
`DAT_073dddb8` scripted-override (PORT-DEBT(cs-dlg-override), inactive here).  Tail (0x460a77+) = the **`<C>`
split** into `b31c`/`b41c` — identical to `FUN_0046098f`'s, so the port factors `cs_split_line()` shared by
both.  Speakers: slot-0 lines come from **record 0 (Recette, `&DAT_06a5ea90`)**, slot-1 from the customer
(`b56c = g_scene_buy_current_page`).

**Loader** (the dialogue half of `FUN_00475270`, all.c:74568-74715): per record with a `file:`, storage-read
`kyaku/<name>.txt` and parse `msgNN:` lines.  **Format is FIXED-WIDTH** (ground-truthed from the real
`tear.txt`/`recette.txt`): `msgNN:SS:Vvv:text` — NN=type(2 digits @+0x23/`atoi line+3`), SS=sprite(@line+6),
Vvv=voice (`sno`=none / `s`+2digits=id, @line+9/+10), text @line+0x2d (raw, `<BR>`/`<C>` kept).  The Ghidra
`local_18 + s*0x40 + 0x78b` in the text copy is int*-scaled byte offset `0x1e2c` (`0x78b*4`); `0x5044 +
0x1e2c = 0x6e70` ⇒ confirms the picker's stride.  grp (standee art → scene_buy) + se (audio) blocks ignored.

**Port:** new `customer_dialogue.{c,h}` (the `kyaku_dialogue_t` slot grid 30×20×0x100 + the pure
`kyaku_dialogue_parse` + a per-record heap store); `tables.c::load_kyaku_dialogue` (after `load_kyaku_txt`,
reuses `load_via_storage`); `customer_service.c` (`cs_split_line` factored, `cs_pick_line(rec,type,slot)`
reads the buffer, all 9 call sites pass `(rec,type,slot)` per the by-address comments — incl. the close-line
type `uVar18 = b5a8==3?0xb:b5a8==0?7:8`, all.c:60627).  RNG STEP unchanged (one draw when !f404 either way) ⇒
the verified-1:1 LCG holds; only the now-used variant VALUE selects the real line.

**Verified:** loader on the user's real data = **18 scripts / 1229 lines** (no missing/read-fail/OOM);
**3 host tests** (`kyaku_dialogue_parse_fields/_caps/_store`, 3345 pass).  Sanity: the reaction
`cs_pick_line(0,9,0)` = recette msg09 = **"How much should I?..." / "Capitalism, ho!"** (count 2, `rand%2`) —
the iconic line the `...` hid.  **v3-VERIFIED + USER-CONFIRMED 2026-06-22** (`--window 5900:2800
--join-anchor CUSTOMER_SERVICE_ENTER`): the live greeting renders **"Tear / I would like this, please."**
BIT-IDENTICAL to retail (was `...`); user "looks good".  PORT-DEBT retired: cs-kyaku-dialogue; new:
cs-dlg-override (the buysell variant table), cs-voice (playback).

## 14. Dialogue macro substitution (`<I>` item / `<Y>` pix) PORTED 2026-06-22 (retires PORT-DEBT(box-text-macros))

User flagged (viewer note #2, port `PAUSE_CLOSE#5+160`): the post-sale close line **recette msg08 "Yay! I
sold `<I>` for `<Y>`!"** rendered the raw markers (mangled to `)` / `>`) where retail shows **"Steel Sword" /
"3000pix"**.  Two gaps: (a) `font_draw_text_box` (FUN_00465db4) pass-1 macro expansion was stubbed
(PORT-DEBT(box-text-macros)) — AND the stub leaked the trailing `>` (it advanced src by 2, not past the whole
`<I>`); (b) the `<I>`/`<Y>` source buffers were never populated.

**Macro expansion (FUN_00465db4 pass 1, all.c:62697-62815).**  Each tag copies a length-prefixed buffer at
the dst cursor, then the shared `iVar4-1` (62814) + `LAB_00465f24` `iVar6++/iVar4++` (62816) tail CONSUMES the
closing `>` and nets the dst advance to the macro length (empty buffer ⇒ the written `<` is overwritten ⇒ the
whole tag drops).  Sources: `<S>`=DAT_0730b2bc/b300, `<I>`=DAT_0730b154/ac90, `<Y>`=DAT_06a5d518/0730b150,
`<D1>`=DAT_0730ac70/b274, `<Dx>`=DAT_0730ac80/b2fc (first emitted char folded `i`→`I` else `B`),
`<T>`=DAT_06a5d408/d44c; `<BR>` is NOT a tag (literal, survives to pass 2's line split).

**Port:** new `dialogue_macros.{c,h}` (the 6 buffers + `dlg_macro_set`/`_reset` + the pure host-testable
`dlg_macro_expand`); `font_draw.c` calls it (replacing the leaky stub).  The setters (engine all.c:60616-60626,
the live close branch): `cs_set_item_macro` = **FUN_004607f3**(b5a4) → `<I>` (id=b5a4>>6 →
`tables_item_find_slot_by_id` → `g_item.records[slot].singular`, `+ " %d"` if b5a4&0xf), and `<Y>` =
`snprintf("%dpix", s_price_ask)` — both set in the `b534==0xc` live close before `cs_pick_line(0,8,0)`.  +4
host tests (`dlg_macro_expand_*`/`set_reset`, incl. a guard that an unset tag drops WITHOUT a stray `>`); 3349
pass.  PORT-DEBT retired: box-text-macros; new: cs-item-macro-kinds (the b534==0x1e / b5a8==4 name sources).
**v3-VERIFIED 2026-06-22** (win-5900-2800, close line port_idx 2274 / retail 2431): the port renders **"Steel
Sword / for 3600pix"** BIT-IDENTICAL to retail (was the mangled `)` / `>`).  USER-CONFIRMED.

## 15. Two more user notes 2026-06-22 — the LIVE haggle prompt + the lingering "!" emote

**(#3) the live price-input prompt "How much should I?..." was MISSING** (retires PORT-DEBT(cs-haggle-prompt-live)).
`customer_service_render.c` gated the (312,250) prompt on `b51c != 0` (scripted only).  objdump of FUN_00466b7b
(0x4675f2-0x467664) shows the engine HAS a b51c branch but BOTH arms draw: `b51c!=0` (scripted) = the fixed
string by `price_fileidx` (0x4675fa, DAT_005c6e28 "What should I pay?..." / DAT_005c6e40 "How much should
I?..."); **`b51c==0` (live, 0x467629) = the active dialogue line `DAT_0730b270`** (e.g. recette msg09 "How
much should I?..." = the reaction `cs_pick_line(0,9,0)`), coloured yellow if `b5a8==0` else white.  Fix: the
render's `b51c==0` branch now draws `s.line` (b270) with the b5a8 colour.  v3-verified.

**(#4) the counter "!" affordance emote LINGERED through cc08==4 idle** (retail cleared it).  Root: the "!"
(db000) ramps up in free-roam (`player_ctrl_cc08_proximity_detect`, at the counter), but on the Z-entry into
cc08==4 the engine CLEARS it (`DAT_056db000 = 0`, all.c:87696) — the port didn't, so it froze (the cc08==1
free-roam arm that would decay it stops running during cc08==4).  Fix: `player_ctrl_cc08_sell_counter_enter`
sets `s_emote_level = 0` after `cc08=4`/session_init.  Does NOT affect the free-roam approach "!" (note #1).
3349 host pass.  v3-verified 1:1 (#3 prompt port_idx 670==retail 826; #4 "!" gone port_idx 223==retail 379).

## 16. The queue-advance conclusion line (notes #5/#6) PORTED 2026-06-22 (retires PORT-DEBT(cs-queue-line))

After the 2 live practice rounds, retail plays Tear's scripted conclusion **"Expertly done. If you ever wish
to practice again, simply ask me<C>any time we are in the shop."** (tuto1.txt id **-4**) in the master tick's
**b534==0x14 (queue-advance)** state, before the close→wrap-up.  The port stubbed it (`s_b270="..."`,
PORT-DEBT(cs-queue-line)) → the short placeholder revealed in ~3f, so the port advanced 0x14→0x15→idle far
too fast (note #6 "ends the scene early") and showed "○○○" not the line (note #5).

Engine (all.c:60530-60549): on `b544==1`, line id `iVar4 = (b528==2)-4` (= **-4** normal / -3 / -2 if
b5b8≠0), scan g_tuto[fileidx]'s 200 records for `id==iVar4`, load its text via `FUN_0046098f(text,1,0)` (=
`cs_dialogue_line_setup`, which runs the `<C>` split).  `tuto_record.id` (offset 0) = the line's first CSV int
(the "addressed by negative id" lines, tables_tuto.c:186).  The `<C>` page-2 advance is the master tick's
shared pre-dispatch check (`b558==1 && b55c && Z → b270 = s_line_tail`, all.c:60318-60324, customer_service.c:
1417) — runs for this state too, so page 2 ("any time we are in the shop") shows.  Fix: the b534==0x14 arm
scans + loads the real line (mirrors `cs_goto`).  The long real line reveals at retail's rate ⇒ the 0x14
duration + the close timing now track retail.  3349 host pass; v3-verify pending.

## 17. Gap (2) — POST-FADE CAMERA: the autonomous first-customer cs entry (f406 arm, all.c:87485) PORTED 2026-06-22

User-flagged (FRONT gap (2), v3 viewer ~col 847): after the (1:1) wrap-up cutscene retail's camera CUTS to a
new angle; the port's "is completely wrong."  Root-caused via the e8f49cb7 cache (`call_trace.jsonl`,
camex/camez/camlx/camlz + cc08 + the anchor timeline) — it's a STATE gap, the camera a symptom.

**Retail post-tutorial flow** (anchors): 5 BARGAIN rounds (3 scripted + 2 live) → `LOADING#6@11601` →
**CONV_POSE wrap-up iv1_7** ("And that is… now go sit at the counter") @11602-12205 (11 TEXT_ANIM lines) →
`LOADING#7 + CUSTOMER_SERVICE_ENTER#2 @12249` → **cc08=4** (camera ramps eye=(-2.0,14.7)→**(-3.0,14.0)**,
look=(-2.0,0.7)→**(-3.0,0.0)** = the tier-0 COUNTER cam).  Post-wrap-up `f404` reads `0x00010000` ⇒ byte[0]=
f404=**0** (real customer, not the sell-active tutorial), byte[+2]=f406=**1** (iv1_7 latched) — the P3 signature.

**Port** (old build): same 5 rounds + the same iv1_7 (CONV_POSE_END#1@10795, 11 lines, 1:1) — then **cc08=1
FREE-ROAM**, camera settles eye=**(-1.5,15.0)** look=**(-1.5,1.0)** (the player-follow cam) and **no
CUSTOMER_SERVICE_ENTER#2**.  So "camera wrong" = the port never enters the first-customer cs session.

**Mechanism — the missing trigger is ONE branch, `FUN_0048670f` all.c:87485-87489:** in the cc08==1 free-roam
arm, AFTER the roster-arrival (b928/f428/f429) block, BEFORE the cc04==0 d-pad block:
`if (f406 != 0) { cc08=4; FUN_0045edaa(); goto LAB_004893ff; }`.  iv1_7 sets `f406=1`
(scene1_tutorial_dispatch.c:77, mirrors 45724); the next free-roam frame after the cutscene this fires →
cc08=4 + session init.  The f406 branch of `FUN_0045edaa` (the forced kyaku-13, customer_service.c:282-300)
was ALREADY ported (Chip 1) but UNREACHED — `s_b51c` stays 0 ⇒ the LIVE machine (matches retail's
first-customer `b534=1` live greeting).  Unlike the Z-entry (bVar3) / roster entries (f428/f429) the f406 arm
does NOT call `FUN_00461bf6` (no fileidx seed) and does NOT set f405 (player-arrival-complete) ⇒ the arrival
hop + camera ramp REPLAY (matching retail's post-load ramp).  cc08==4 master tick → `scene1_camera_cs_counter_cam`
→ stage_class=1 → counter cam.

**Why no spurious / early fire:** f406 is 0 at the cad868 LOAD (port call-trace: f406 flips 0→1 only at
frame 10795 = CONV_POSE_END), so it can't fire during the tutorial; and `player_ctrl_cc08_freeroam_arm` runs
ONLY when `cc08==1 && !scene1_intro_dialogue_active()` (line 2181) — iv1_7 IS an intro_dialogue
(`scene1_intro_dialogue_start_single(1,7)`; conv-pose gated by `_posing()`→`_active()`), so the arm is
suppressed THROUGH the cutscene and the entry fires the frame AFTER it ends — exactly retail's
CUSTOMER_SERVICE_ENTER#2 timing.  f406 is cleared (→f402, the iv1_8 trigger) only at the cs LEAVE
(customer_service.c:1448, already ported) ⇒ stays latched through the session (harmless; cc08==4 stops the
free-roam re-check).

**Port** (`scene1_player_ctrl.c`): `player_ctrl_cc08_f406_entry()` (reads bank[0x2bc6e], → s_cc08=4 +
`customer_service_session_init()`), called in `player_ctrl_cc08_freeroam_arm` right after the escalate stub,
before the cc04 split (engine order).  3350 host pass (+`cs_f406_entry_enters_counter`).

**✅ v3-VERIFIED BIT-EXACT (port re-drive, the e8f49cb7 caprange):** anchors — the port now fires
**`CUSTOMER_SERVICE_ENTER#2`@10956** coincident with `CONV_POSE_END`+`LOADING_START`@10955, BIT-MATCHING
retail's `CUSTOMER_SERVICE_ENTER#2`@12249 = `CONV_POSE_END`+`LOADING` pattern.  Camera — the post-fade cc08=4
ramp converges (by ~off70) to **eye=(-3.000,14.000) look=(-3.000,0.000)** = the tier-0 COUNTER cam, **identical
to retail's settled (-3,14,-3,0)** (was the "completely wrong" free-roam cam eye=(-1.5,15) look=(-1.5,1.0)).
Accepted residual: the ramp TRANSIENT origin differs (port holds (-3,14) from the cutscene then dips ~0.3 +
re-converges; retail starts (-2.02,14.66) from its load) — both converge bit-exact, a load/phase origin
difference, not logic.

NB this OPENS P3 (the first real customer INTERIOR — the live machine on kyaku-13, L1b real pix, the customer
render fidelity); the camera + cs-ENTRY is gap (2)'s target (DONE), the interior is P3's.

## 18. P3 GAP (user-flagged 2026-06-22, post gap-2) — the first-customer COMPANION (Tear): pose/position + the scolding pose + manga-lines (集中線)

After gap (2) landed (the port now enters the first-customer cc08=4), the user confirmed the camera but flagged:
**"tear does not do the scolding pose + manga lines here so thats another gap."**  Trace diff (e8f49cb7 cache,
aligned by `CUSTOMER_SERVICE_ENTER#2`: port @10956, retail @12250) shows a clear COMPANION divergence — the
port runs the WRONG companion branch because the first customer has **f404==0** (the tutorial had f404==1):

| field | RETAIL (f404==0, first cust) | PORT (runs the f404!=0 arm) |
|-------|------------------------------|------------------------------|
| canim | **0** (idle) from off~3 | **4** (at-counter pose) |
| coct  | **0** (facing FRONT) | **6** (side) |
| cx,cz | **stays (-3.0, 8.66)** | **WALKS to (-5.80, 8.60)** (= player.x−1.3) |

Root: `scene1_companion_ctrl_tick` runs `co_at_counter_tick` (= **FUN_0048a833's `local_c != 0` / f404
sell-active arm**, canim 4 + step toward player±1.3) for ALL cc08==4, but the engine gates that arm on
`cc08==4 && f404 != 0` (all.c:33434).  The first customer is **f404==0** ⇒ retail takes the ELSE arm: Tear
stays put at the counter (cx≈-3.0), canim 0, octant 0 (facing the camera/customer) — and at the reaction beat
does the **SCOLDING POSE + the manga-lines (集中線)** the user saw.  The port's at-counter-follow walks her off
to cx=-5.80 + never poses/emits.

**Next-session plan (P3 companion):**
1. RE `FUN_0048a833` (all.c:89131, the companion controller — large) — find the `local_c`(=f404) branch and
   the **f404==0 (real-customer) arm**: the idle-at-counter pose (canim 0, oct 0, hold cx≈-3.0), the scold
   pose trigger (a special anim/overlay on the reaction beat), and the **manga-lines (集中線)** draw.  NB the
   manga-lines were noted earlier as a `b494` RT-based draw (RE §8.8 / note-#8) — confirm whether THIS instance
   is the same RT effect (⇒ needs the v3 RT-capture extension to replay) or a normal overlay.
2. Port: gate `co_at_counter_tick` on f404!=0; add the f404==0 first-customer companion arm
   (idle/hold + the scold pose + the manga trigger).  This is part of P3 (the first-customer interior).
3. The scold/manga likely fire on a live-machine REACTION state (b534==6) — cross-check the live machine
   (FUN_004658ab / cs_live_machine) timing.  In this trace the captured window ends ~off300 (b534 still 0,
   b5a8 0→2, b56c 1→13 at off60 = the customer asset bind); the scold beat may be just past the window — a
   re-drive with a longer caprange may be needed to capture it.

### 18.1 RESOLVED 2026-06-22 — the position/idle arm is the FREE-ROAM FOLLOW, not the scold-wander (Chip P3-companion-pos)

Read the RETAIL ground truth straight off the `e8f49cb7` call-trace (0x48670f frames, the first-customer
region is the 2nd cc08==4 run [8544..8852], 309 frames before the window ends):

| 0x48670f off | f404h    | b534 | b5a8 | b56c | canim | coct | cx     | cz   | px    | cwander | cstate |
|--------------|----------|------|------|------|-------|------|--------|------|-------|---------|--------|
| 8544 (entry) | 0x10000  | 0    | -1   | 1    | **4** | 6    | -2.96  | 8.66 | -1.5  | 0       | 0      |
| 8559 (+15)   | 0x10000  | 0    | -1   | 1    | **0** | 0    | -2.96  | 8.66 | -3.38 | 0       | 0      |
| 8574..8852   | 0x10000  | 0→1  | 2    | 13   | **0** | 0    | **-3.0** | 8.65 | -4.5 | **0**   | **0**  |

`f404h = 0x10000` ⇒ the i32 read of `0x450f404` has **byte0 (f404) = 0** and **byte2 (f406) = 1** — so the
first customer is **f404==0, f406==1** (the autonomous entry), exactly as the iv1_7 chain predicts.  The
canim=4/coct=6 at off 0 is the prior tutorial pose **leftover** (1-frame transition); by off 15 the companion
is **canim 0 / coct 0** and stays there, settling to **cx=-3.0** (= player px −4.5 + the 1.5 follow radius),
**cz=8.65**.

**`cwander` (be6c) and `cstate` (db048) are 0 the WHOLE region** ⇒ the engine is NOT in the be6c
idle-wander/scold arm (that needs `f407!=0`, and **f407's only writer is the iv1_8 start at all.c:45736, gated
`f402==1`; f402 is set ONLY at the cs LEAVE — all.c:60383, right after `f406` is cleared @60382** — so f407 is
0 during the first customer's haggle).  With `local_28 = (f407==0) = 1`, FUN_0048a833 takes the
`local_c==0 && local_28!=0` path = **`FUN_0048a4d1` free-roam spring-follow** on Tear (da1f0) — the SAME law
the port already ships for non-cc08 free-roam (validated 0.0036 mean XZ error).  So the companion just FOLLOWS
the player to the counter and idles.

**The port bug was structural:** `scene1_companion_ctrl_tick` special-cased ALL `cc08==4` → `co_at_counter_tick`
(the `local_c!=0` / f404 at-counter ±1.3 walk), so the f404==0 first customer ran the wrong arm → Tear walked
off to cx=−5.80, canim 4.  **Fix (landed): gate the at-counter arm on `customer_service_f404()`** (reads the
`0x2bc6c` bank byte = f404 bit0); f404==0 falls through to the existing free-roam follow.  No rng change (both
arms draw zero rng aside from the unchanged every-4th-frame wing sparkle).  +1 host test
(`companion_first_customer_freeroam`).

**The SCOLD POSE + 集中線 are NOT in this 309-frame window** — canim stays 0, b534 only reaches 1 (greeting);
they fire on a later LIVE-machine beat (b534==6 reaction / 8 pushback, after the haggle starts, past off 8852).
That is a SEPARATE P3 chip (the live-machine reaction overlay) and needs the `{calltrace}` window extended past
8852 (forces a retail re-drive).  When ported, the free-roam path must YIELD to the live-machine pose on the
reaction beat (mirror the `scene1_conversation_pose_active()` yield).

### 18.2 The user's "camera angle still wrong" (note #8) — the CAMERA TRANSFORM is BIT-EXACT; the real gap is a cc08==4 3D-SCENE REPROJECTION + a missing element (2026-06-22)

> **⚠ SUPERSEDED by §18.3 (2026-06-22 PM, the --d3d-trace).**  §18.2 RESOLVED THE WRONG FRAME: it
> took note #8/#9 to be the **first-customer greeting (CSE#2)**, but the note is anchored at
> **`CONV_POSE_BLINK#1+36` = the iv1_7 WRAP-UP cutscene** (~590f earlier).  The CSE#2 greeting §18.2
> analyzed is in fact **camera-1:1** (pixel diff 1.19%; counter-cam VIEW + all proj BIT-IDENTICAL — the
> "floor reprojection 54-diff" was a frame-misalignment artifact).  The REAL gap is a WRAP-UP camera
> CENTER offset — see §18.3.  Keep §18.2 only for the (real, separate, minor) first-customer retail-only
> overlay draws it noted (the 80-tri b494 element).

After the companion fix the user flagged (viewer note #8, port, CONV_POSE_BLINK#2+7 = the first-customer
greeting): **"camera angle still wrong, that's a huge part of why the companion pos looks wrong."**  Investigated
in full (extended `{calltrace}` 9500→11500, added `camey`/`camly`/`b5d4`/`b59c` to the probe, re-drove BOTH
sides → cache `4dfe654b`, retail ALL-BIT-EXACT).

**The camera TRANSFORM is 100% bit-exact port==retail** at the settled view: eye=(**-3.0, 22.2, 14.0**),
lookat=(**-3.0, 1.2, 0.0**) — ALL SIX components incl. the newly-probed Y.  FOV = 45° (DAT_073de3a0 = 0x42340000,
set only at scene init 34225/77956 — never during cc08==4), up-vector DAT_073de29c has no writer (constant).
So eye/lookat/FOV/up are identical ⇒ the view+proj matrices are deterministically identical ⇒ **the "camera
angle" is NOT an eye/lookat/FOV bug.**  (Ramp-in residual: at the counter-cam ONSET port camex=-3.0 already
settled vs retail -1.5 panning in over ~40f — a load-entry-timing transient that converges bit-exact; accept.)

**BUT the rendered 3D SCENE genuinely differs** (port off207 vs retail off205, phase-aligned by b544 port=retail−2):
| region | cross-side diff | same-side (4f) baseline |
|--------|-----------------|--------------------------|
| 3D shop center | **56** | ~5 |
| **STATIC upper floor/counter** (no chars) | **54.1** | **0.0** |
| back wall/window (far) | **87** | — |
| lower floor (near) | 51 | — |
| center-right "yellow element" | 61 | — |

The **static floor diffs 54 cross-side but 0.0 same-side** (perfectly stable within a side) with a bit-exact
camera ⇒ the 3D scene is **REPROJECTED differently** — depth-dependent (**far > near**: back wall 87 > floor 51),
NOT a uniform 2D shift (a ±4px shift-search barely moved it 56→54), NOT a blur (edge-energy 9.99 vs 9.80 equal),
NOT a tint (per-channel mean equal).  A red/green overlay shows the floor planks fringed red/green, growing toward
the foreground.  **So the user is RIGHT that it's a "camera/projection" problem — it just isn't in the captured
eye/lookat values; it's in HOW the camera is APPLIED to the 3D render** (the view/proj matrix BUILD or a viewport
during stage_class==1), a path that diverges from the verified-1:1 free-roam render.  Standees + the 3D companion
world-pos are 1:1 (the standee idle is a benign ~2f anim phase).  PLUS a localized **retail-only yellow/gold
circular element** center-right (≈full-frame 660,270) the port never draws (not a 3D actor — the port has only 3
actor slots, all accounted for; likely a cs-specific overlay/object/billboard).

**NEXT (this is the live gap — NOT yet root-caused):** capture the actual VIEW/PROJ matrix (or hook FUN_004a3b52
+ the viewport `SetViewport`) port-vs-retail at a settled cc08==4 frame, OR use the v3 draw-program panel /
`d3d_state_at_draw.py` on a `--d3d-trace` of this frame to find the differing 3D draw(s) + the retail-only yellow
draw.  Hypothesis to test first: a stage_class==1-specific viewport/projection setup the port mis-applies (since
free-roam, same FOV+pitch, is pixel-1:1).

**RULED OUT (2026-06-22, all read-only):** (1) it is NOT a v3 replay artifact — `orv3_shot` pulls the CAPTURED
BACKBUFFER (the drive's `MULTI (GetBackBuffer)` grabs, dedup-stored), not a command-stream replay, so the 54-diff
is the REAL exe framebuffer port-vs-retail.  (2) `g_scene1_camera_z_roll` (=`_DAT_006051c4`) — the ONE non-eye/lookat
term in `scene1_camera_build_view_matrix` (`out = lookAt(eye,lookat,up=(0,1,0)) × RotZ(z_roll/2)`) — has **NO writer
in the port OR the engine decompile** (const 0 both), so the roll is identical.  ⇒ eye/lookat/up/z_roll/FOV/aspect
are ALL bit-identical ⇒ `build_view_matrix` is deterministically identical ⇒ the divergence is NOT in the view
matrix as the port builds it.  So the gap is one of: (a) the port's ACTUAL render uses a different view/proj than
`build_view_matrix` during cc08==4 (an override); (b) the 3D MESH content/lighting/draw-order differs; (c) the
engine builds its view matrix (`FUN_004a3b52(up=de29c, eye, lookat)`) with a different formula/up than the port's
`mat4_lookat_rh` AND the cc08 eye position exposes it where free-roam doesn't.  **The `--d3d-trace` `SetTransform`
capture is the ONLY way left to decide** — do that first next session.  Note this is the cc08==4 SELL view; verify
whether the (verified-1:1 "everything looks 1:1") TUTORIAL cc08==4 background ALSO reprojects (if yes, it's a
general cs-view bug missed because prior checks were UI/standee-focused; if no, it's first-customer-specific).

### 18.3 RESOLVED via the --d3d-trace 2026-06-22 — note #9 = the WRAP-UP (CONV_POSE) camera CENTER offset (free-roam vs counter target); orientation/FOV BIT-IDENTICAL

Ran the decisive d3d capture by **extracting `SetTransform` (ORV3 op 12) straight from the EXISTING
v3cap.bin caches (no re-drive)** with the new **`tools/trace_studio_v3/orv3_xform.py`** (per-frame
VIEW/PROJ/WORLD dumper: decodes eye/lookat from D3DXMatrixLookAtRH + fov/aspect/near/far from
PerspectiveFovRH; `--diff` cross-side, `--draws-by-view`, `--scan-eye`).  The v3 proxy already records every
SetTransform, so the actual GPU matrices were sitting in the cache.

**Note #9 is `CONV_POSE_BLINK#1+36`** — port v3idx 8601 (exe 10465), retail v3idx 8757 (exe 12774) —
DURING the iv1_7 wrap-up cutscene (CONV_POSE_START@10409 → END@11057), NOT the first-customer greeting.
(User confirmed: "changed the note … a frame with no standee to make it easier to analyze," same wrap-up
region.)  §18.2 mis-resolved it to CSE#2 and measured the wrong scene.

**CSE#2 first-customer greeting = camera 1:1** (control): counter-cam VIEW eye=(-3,22.2,14) look=(-3,1.2,0)
**BIT-IDENTICAL** port==retail; all 5 perspective PROJ (fovY45/aspect1.333, far 500/1450/2000/350/20001)
**bit-identical** (Δ≤9.5e-7 f32 noise); the differing non-scene VIEW (port identity vs retail eye=0,0,-550)
draws ZERO under it (inert, RHW 2D ignores view).  Full-frame pixel diff **1.19%**.  ⇒ §18.2's "floor
reprojection" was a frame-misalignment artifact.

**The WRAP-UP gap (note #9):** across the WHOLE CONV_POSE the scene renders under a camera with the SAME
orientation (forward=(0,-0.83205,-0.5547), up=(0,0.5547,-0.83205)) + FOV + proj, DIFFERENT CENTER:

| side | eye | lookat | = |
|------|-----|--------|---|
| PORT | (-3.0, 22.2, 14.0) | (-3.0, 1.2, 0.0) | the cc08==4 COUNTER target (STALE) |
| RETAIL | (-1.5, 22.2, 15.0) | (-1.5, 1.2, 1.0) | the FREE-ROAM camera (default house; = §17/gap-2 free-roam eye/look) |

Δeye=Δlook=(+1.5, 0, +1.0), CONSTANT every wrap-up frame.  Full-frame pixel diff **92.8%** (feed
"NOTE #9 ROOT CAUSE").  So the user is RIGHT it looks wrong — it's a POSITION/center offset, not angle/FOV.

**Mechanism:** `house_update` (FUN_0048670f, the counter-cam driver) does NOT run during CONV_POSE on either
side (no cc08 probe rows 10383..11067 port / 12693..13377 retail — the cutscene takes over the sim); cc08==4
+ counter cam at BOTH boundaries.  So the wrap-up camera is set by the CONV_POSE path: **retail renders the
scene with the FREE-ROAM camera; the port leaves `g_scene1_camera` at the STALE counter cam (-3,0)** from the
last pre-CONV_POSE house_update.

**✅ FIXED + v3-VERIFIED 2026-06-22.**  Root cause: the port's cs LEAVE (`customer_service.c`, the s_b520
dissolve-complete block) OMITTED two things retail does inline at the leave (all.c:60349-394): (1) the Recette
HOP-DOWN reposition `DAT_056da1d8/e0` = `g_scene1_player_pos[0]/[2]` (f404!=0/tier<3 → **(-1.5, 9.0)**), and
(2) `DAT_0438b4e8 = 0` = `g_scene1_camera_stage_class = 0` (drop the counter cam → free-roam class).  Without
them the stale cc08==4 counter cam (-3,0) persisted: the OTHER stage_class reset (`scene1_player_ctrl.c:2223`,
gated cc08!=4) never runs during the cutscene because once iv1_7 arms the sim routes to the EVENT arm (no
`scene1_player_ctrl_tick`).  `house_update` doesn't run during CONV_POSE, so the cutscene renders off the
persistent `g_scene1_camera` computed by `scene1_camera_pose_compute` (free-roam branch, `cmode`=0 both sides)
— which CLAMPS bias_z 9.0→1.0 + bias_x to [-5,-1] (scene1_camera.c:212-214), so the repositioned player
(-1.5, 9.0) → camera bias **(-1.5, 1.0)** → eye=(-1.5,22.2,15) == retail.

**Ported** both into `customer_service.c` (the s_b520 leave block; RNG-safe — no draws added; the octant
DAT_056dab00 / db05c / db048 + the render FUN_ calls + the shop-FULL fb88>=4 branch remain
PORT-DEBT(cs-leave-restore)/(cs-leave-shopfull)).  +host test `cs_leave_resets_freeroam_camera` (drives the
ESC-skip leave through the dissolve, asserts player.x=-1.5 + stage_class=0); 3352 pass.  **v3-VERIFIED**
(port re-drive, 4dfe654b/port): the wrap-up scene cam is now **eye=(-1.5,22.2,15) BIT-MATCHING retail** (was
(-3,22.2,14)); full-frame pixel diff **92.8%→2.54%** (residual = the player sprite + the accepted +1f anim
phase).  Both the f406-close AND the ESC-skip leave paths set b520 ⇒ both fixed.  feed "NOTE #9 FIXED".
**✅ USER-CONFIRMED 1:1 2026-06-22** ("yes the camera looks correct") in the v3 viewer; recorded in
`confirmed-parity-ledger.md`.

### 18.4 RESOLVED 2026-06-22 — viewer note #1 ("tear position slightly off / lower") = the WRAP-UP companion HEIGHT; root = the un-wired player ground-Y (engine daf88)

Note #1 (`retail DLG_LINE_CLEAR#6+43`, box [383,525,505,695]) flagged Tear sitting too low through the
wrap-up cutscene.  Decomposed off the v3cap WORLD matrices (`orv3_xform`, the 4dfe654b cache, no re-drive)
at the note frame (port f10908 / retail f13364, the last CONV_POSE frame):

| sprite (WORLD scale) | PORT Y | RETAIL Y | Δ |
|----------------------|--------|----------|---|
| player **chibi** (-0.03) @(-1.5,_,9.0)   | 0.500 | 0.500 | **0 — bit-match** |
| player **shadow** (-0.005) @(-1.5,_,9.0) | 0.121 | 1.392 | −1.271 |
| **companion chibi** (-0.03) @(-2.96,_,8.656) | 3.097 | 4.354 | −1.257 |
| effect sprites near Tear (-0.001)        | ~4.0  | ~5.3  | ~−1.25 |

The player chibi + ALL XZ bit-match; only the *heights* of everything that derives from the player's floor-Y
are ~1.27 low.  Per-frame tracking proved the **companion-Y RELATIVE to the player floor-Y is bit-identical
port==retail** (comp−shadow: port 2.976 / retail 2.962 at the note frame, matching across the whole CONV_POSE)
— so the follow law is correct; the entire gap is **one value: the player floor-Y (engine `DAT_056daf88`),
port 0.121 vs retail 1.392.**  (Note #1's reported player drift "(-2.0,8.8)" was a MISREAD off the clobbered
4dfe654b port call_trace; the WORLD shows the player at (-1.5,9.0) bit-matching retail — XZ was never wrong.)

**Mechanism.** The companion free-roam hover Y = `sin(db054·0.04)·0.2 + DAT_056daf88 + 3.0`
(`FUN_0048a4d1` L89080-89083; port `scene1_companion_ctrl.c:315-318`).  `DAT_056daf88` = the floor under the
player, written ONLY in the house_update chain — `FUN_0048670f → FUN_0048b850 → FUN_00483170` (L84449/84458)
and `FUN_0048a833` (L89493).  NEITHER writer is reached from the conversation-pose tick `FUN_0048407f`; the
top dispatcher (all.c:50540) runs the pose tick **or** house_update per frame, mutually exclusive ⇒ during
CONV_POSE `daf88` **freezes** at its last house_update value = the **counter-platform floor (≈1.27)** the
tutorial-cc08 player stood on.  Retail's companion therefore floats at 1.27+3.0 ≈ 4.35 through the wrap-up;
the shadow + floor-pinned particles sit on the same frozen floor.

**The port left `g_scene1_player_ground_y` UN-WRITTEN (always 0).**  In normal flat-floor HOUSE free-roam the
true floor ≈0 so the companion at 3.0 was coincidentally right and the bug was invisible — only the frozen
**raised** counter floor exposed it (companion 3.097 vs 4.354).  The HOUSE floor is a raised platform on the
left (counter): at px=−4.5 floor≈1.27, at px=−3.25 floor≈0.03.

**Fix (RNG-safe — the floor query is deterministic; no draw/anim/rng change).**  Two writers mirroring the
engine, both gated off during CONV_POSE (the player tick doesn't run there ⇒ correct freeze):
- **free-roam** (`collision_resolve.c::collision_resolve_player`, the FUN_00483170 port): record the snapped
  floor `g_scene1_player_ground_y = h.height` alongside the existing player-Y snap — the literal daf88 write.
- **cc08==4** (new `collision_set_player_ground_y`, called from `scene1_player_ctrl.c`'s cc08 arrival arm
  inside the `b1cc!=2` gate): the free-roam writer doesn't run while cc08==4, but the engine still updates
  daf88 in the cc08 block (FUN_0048b850 @ all.c:87749), so the value ramps to the counter floor 1.27 and
  freezes there for the wrap-up.  The cc08 at-counter companion is UNAFFECTED — `co_at_counter_tick` uses a
  fixed hover height with NO ground_y term (asm 0x48ad93), so cc08 parity (confirmed 1:1) is untouched.

+2 host tests (`ground_y_set_from_floor_under_stool` = the cc08 stool-above-floor writer; `resolve_player_
records_ground_y` = the free-roam writer); 3354 pass.  **✅ v3-VERIFIED** (port re-drive, 4dfe654b/port): the
wrap-up companion chibi Y is now **port 4.08–4.46 == retail 4.09–4.46** across the whole CONV_POSE (was a
flat 3.0; note frame 3.097→4.332 vs retail 4.354), matching within ±0.03 = the bob sin-phase / accepted +1f
arrival-anim phase.  The cutscene effect sprites near Tear (scale −0.001) likewise rose 3.7–4.6 → 4.7–5.9 ==
retail 4.9–5.9.  Player chibi (-1.5,0.5,9.0) + all XZ stay bit-exact.  **✅ USER-CONFIRMED 1:1 2026-06-22**
("yes that looks correct") in the v3 viewer; recorded in `confirmed-parity-ledger.md`.

**Follow-up — the player CONTACT SHADOW frozen-floor (user: "fix that port debt too, if it's structurally
equal").**  The player shadow had the SAME un-frozen-floor root: `scene1_chr_shadow_render` did a per-frame LIVE
`collision_query_ground` per actor, so at the wrap-up it sat on the real floor under (-1.5,9.0) (Y 0.12) instead
of retail's frozen counter floor (Y 1.39).  The fix IS structurally faithful: the engine's shadow (FUN_0045aa36)
is a pure READER of the cached per-actor floor `DAT_056daf94` (height) / `DAT_056daebc` (normal), filled by
FUN_00483170 / FUN_0048a833 every house_update frame — NOT a live query.  And daf94[0] == daf88[0] ==
`g_scene1_player_ground_y` (the SAME FUN_00483170 query, same pre-snap +1.5 probe).  **Fix:** cache the player's
full floor hit `g_scene1_player_floor` (= daf94[0]/daebc[0]) where collision_resolve_player +
collision_set_player_ground_y already query it; the shadow reads it for actor 0 instead of live-querying.  On
flat floors the cache == a live query (so the shadow stays bit-exact in every other scene), and it FREEZES
during CONV_POSE (the player tick doesn't run) ⇒ matches retail.  **✅ v3-VERIFIED**: the wrap-up player shadow
is now **port (-1.5,1.392,9.0) == retail (-1.5,1.392,9.0)** EXACT (was 0.121).  Remaining
PORT-DEBT(cs-shadow-frozen-floor): the OTHER shadow actors (companion, actor 1) still live-query — the engine's
FUN_0048a833 per-actor daf94 cache is unported — but the companion draws NO contact shadow in this scene (fairy
hover), so it is invisible here; a future chip can port FUN_0048a833's floor loop for full per-actor fidelity.
**✅ USER-CONFIRMED 2026-06-22** ("can confirm the shadow is correct"); commit `8bd2bc2`; ledger.  So the WHOLE
post-haggle wrap-up cutscene — camera (§18.3), companion height + cutscene sprites, and Recette's contact
shadow — is now 1:1.

## 19. In-shop browsing-customer chibi-NPC pump (cs-walker-rng-phase) — FIXED 2026-06-22 (`0268aaa`)

Back-fill stub (the FRONT carries the full story).  The cc08 first-customer NPC pump (`FUN_0047019f`) had
two RNG bugs, drilled via `tools/cs_walker_drill.py`: (1) **pump gating** — the engine runs the pump
UNCONDITIONALLY in the cc08==4 arm (all.c:87432; the caller gates house_update on the be94 load-SCREEN
counter, NOT b1cc), but the port wrapped it in the `b1cc != 2` load-suppression block ⇒ the spawn cadence
mis-phased through the f406-entry d3e load; (2) **ghost slots** — the port never reset the NPC array
(`FUN_0046f892`) at the HOUSE load / cs leave, leaving 30 zero-init "active" ghost slots the pump ticked.
Fixes: run the pump in engine order unconditionally; reset the array at the top of
`customer_service_session_init`.  3364 host pass.

## 20. ★★ The cc08 d3e load-duration pin ({csloadpin}) — gap-2 reproducibility SOLVED 2026-06-23

**The problem (FRONT gap-2 / the first-customer reaction-line+face variant).**  `cs_pick_line(0,9,0)` does ONE
`rng_next15() % count[9](=2)` draw that indexes BOTH the reaction TEXT and the face SPRITE; the port landed on the
wrong parity ("How much should I?" + open-eyes vs retail "Capitalism, ho!" + closed-eyes), and the offer drifted
119 vs 123.  Root = the rng-VALUE at that draw differs because the **目玉商品 sparkle**
(`player_ctrl_display_sparkle_emit`, gated `g_sim_frame_count % 8 == 3`, 3 `rng_next_unit` × occupied display
column) fires THROUGHOUT the cc08 d3e asset-load window (b1cc==2) — it is `%8`-gated, NOT load-gated — and the
load DURATION is **non-deterministic**: the engine load is a raw `CreateThread` race (`worker_load_spawn_d3e` →
LAB_00452ae8/b13) with **NO minimum-duration frame gate** (`nowloading.c` has only a fade-alpha counter), so the
b1cc==2 window is ~15-18f on the port (varies run-to-run) vs ~7f on retail ⇒ a different sparkle FIRE-COUNT ⇒ an
odd cumulative-draw drift ⇒ the rand%2 variant flips.  Non-deterministic in retail too (same CreateThread race);
the recording captured one realization (123).

**The fix — `{csloadpin:N}`, a trace-harness load-bracket pin (sibling of `{tutloadpin}`).**  Hold b1cc==2 for
exactly N frames on BOTH targets so the sparkle consumes an equal rng count.  *Port* (`customer_service.c`):
`customer_service_load_pin_elapsed` — a pure frame counter ANDed with the async-done check in the cc08 load-gate
bridge; N=24 chosen > the port's worst-case async load so the gate clears at N with the d3e assets already in
(the sparkle reads the SAVE grid, not the d3e assets, so its rng is deterministic regardless of asset
readiness).  *Retail* (`openrecet-agent.js`): a SECOND instance of the tutloadpin worker-tail blocker CModule on
the two d3e tails **0x452af9** (LAB_00452ae8, param-0/session_init) + **0x452b24** (LAB_00452b13, param-1/occ3),
VAs objdump-verified against the known 0x452ac2 AAB reference; armed on the b1cc 2-rising edge, released N frames
later from the Present hook (extend-only — retail's ~7f extends to 24).  The CModule source is factored into one
`WORKER_TAIL_BLOCK_CM_SRC` const shared by both pins (so `tools/test_tutloadpin_cmodule.py` validates both).

**VERIFIED 2026-06-23** (`scenario-test --target both`, house-firstcust-cutscene-day2, csloadpin only): all 4
cc08 loads hold **EXACTLY 24f on BOTH sides** (port 4×24; retail b1cc==2 off 0-23 → 1 @24); the port is now
**reproducible run-to-run** (was 15-18, varying); and the first-customer reaction offers MATCH — **port 119/150
== retail 119/150** (the variant text+face are decided by the same now-aligned rng).  `cs_walker_drill` shows
npcfr/b534 aligned frame-for-frame.

**★ NO phasepin.**  The standard `{phasepin}` was tried and BREAKS retail's wrap-up cutscene (the skip-path
iv1_7 CONV_POSE stalls — 1176 blinks, never reaches the first customer): its bg-NPC RNG re-seed disrupts the
cutscene's RNG-dependent advancement (the port survives it; retail does not).  And it is UNNECESSARY — csloadpin
alone aligns the offer/variant.  **Accepted residual:** a CONSTANT 1-frame sparkle phase (port fires the
25-draw sparkle at off N+1, retail at off N — the `g_sim %8` ORIGIN differs because the port skips the ~14228f
intro).  It does NOT affect the offer/variant (same fire-count, just shifted) ⇒ a phase-pillar constant-offset,
accepted.  A targeted g_sim-only pin (sim_phasepin without the bg-NPC re-seed) could zero it for finer future
rng/phase work, if needed.  PORT-DEBT(cs-walker-rng-phase): the npcsp/NPC-pump divergence at off~30 is the
separate in-shop browsing-customer residual (§19), not this.

**★ CORRECTION 2026-06-23 — the "VERIFIED MATCH / accepted residual" above was OVER-CLAIMED off ONE drive.**  The
`orv3_window` v3 capture (a 2nd harness) gave retail first-customer offer **117**, not 119 (the port stays 119),
so the offer is NOT robustly reproducible — `{csloadpin}` is a NECESSARY piece but NOT the full rng-match.  Two
causes: (a) the v3 harness fired only **1 of 4** csloadpin brackets (the `--target both` 119==119 had all 4) — a
TOOL gap in the v3 csloadpin coverage (the early-exit/v3_arm vs the arm tick); (b) other rng consumers diverge —
the shop-WINDOW NPCs (bg_npc, user-flagged 2026-06-23), the in-shop cs-walker NPC, the sparkle g_sim phase, none
of which `{csloadpin}` touches.  The 1-frame sparkle phase is NOT "accepted" — per the user directive (CLAUDE.md
"phase-matched DETERMINISTIC trace is the FOUNDATION") every divergence is a port/tool gap to CLOSE.  The full
phase-match is the new active arc (FRONT): fix the `{phasepin}`-breaks-wrap-up tool gap (so bg_npc+g_sim CAN be
pinned), add a wall-clock pin, fix the v3 csloadpin coverage, close the cs-walker NPC; verify ≥2 captures + both
harnesses bit-frame-by-frame.

### 20.1 ★ cause (a) FIXED 2026-06-23 — the Frida worker-tail re-arm RACE (v3 "1 of 4 brackets")

**Root cause (proven from the two cached agent logs, NOT guessed):**  the `--target both` retail agent.log armed
**4** csloadpin brackets (14151/14235/14672/14756, two pairs ~84f apart); the `orv3_window` (house_capture) retail
agent.log armed only **1** (3093) — the 2nd-4th cc08 loads ran 1-frame (LOADING_START@3176→END@3177 etc.).  The
"early-exit/v3_arm vs the arm tick" guess was WRONG: the v3 early-exit (@5613) is well AFTER all 4 loads
(3092-3651), and `csloadpinTick` runs every pre-sim frame in both harnesses.

The CModule blocks the worker tail **iff `tlp_flags[0]==0` when the worker thread enters** (a fail-open spin).
The tutloadpin comment's own guarantee — *"the worker can't advance b1cc itself while blocked, so the 2-rising
edge is never missed"* — **depends on flags defaulting to 0 (blocked)**.  At init `flags[0]=0` (line 5596 when a
csloadpin is active), so bracket #1's worker ALWAYS blocks.  But `csloadpinPresentRelease` leaves `flags[0]=1`
(open) after granting a release, and **nothing re-blocks it between loads**.  For a FAST load (warm cache — the v3
drive) the next load's worker thread reaches the tail BEFORE the main-thread `csloadpinTick` re-arms (writes
`flags[0]=0`) → it passes through → `b1cc` clears in a single frame → the 2-edge is gone → the bracket never arms →
the pin is silently skipped.  `--target both` only fired all 4 by LUCK: its slower (cold-disk) retail loads let the
main thread win the race every time.  So "a matches off one drive was a lucky alignment" was literally a thread
race, harness-timing-dependent.

**Fix (`tools/frida/openrecet-agent.js`, `csloadpinTick` + the twin `tutloadpinTick`):**  add a 3rd branch
`else if (!armed && b1cc !== 2) flags[0] = 0` — restore the default-BLOCKED invariant between loads, so the next
load's worker blocks on entry regardless of load speed.  Extend-only is preserved: the `b1cc===2 && !armed`
pass-through window (a real load longer than the pin, after release) is left untouched by the `!== 2` guard.  No
CModule change ⇒ `test_tutloadpin_cmodule.py` unaffected.

**VERIFIED 2026-06-23** (`orv3_window … --force-retail --state`, the 2nd harness): retail now arms **4** brackets
(2923/3007/3444/3528, the `--target both` pattern) + the first-customer offer is **bit-identical port↔retail**:
b574 = **119**(reaction b534=6) → 119(decision 15) → 119(pushback 8) → **150**(round-2) on BOTH (was retail 117 /
port 119).  b56c=13 (f406 walnut-bread), base 100, db054 274 frozen on both.  So the v3 csloadpin TOOL gap (cause
(a)) is CLOSED — the harness now pins all 4 loads.

**⚠ CORRECTION 2026-06-23 PM (user, on the trace studio) — the offer VALUE matched but the rendered VARIANT did
NOT; matching one rng output is NOT rng-stream parity.**  Despite b574=119 port==retail, the viewer STILL shows the
port reaction line **"How much should I?..."** (retail "Capitalism, ho!").  The `cs_pick_line(0,9,0) %2` variant
draw lands on the wrong parity because OTHER rng consumers between the cc08 entry and that draw still diverge — a
single matched downstream value (b574) does not prove the stream is aligned at the variant draw.  **User directive:
the minimum FOUNDATION is a FULL RNG SURVEY at the consumer level — identify + port EVERY rng consumer in this
window until the per-frame draw COUNT+ORDER matches frame-for-frame, AND make the phase pinning solid; only then
does the variant/face fall into place.  Do NOT report a single matched value as the fix.**  csloadpin remains a
necessary phase-pin piece, not the variant fix.  Plan: `docs/plans/rng-consumer-survey.md`.  Open consumers to
survey: the 目玉 sparkle (g_sim%8, the +50 phase), bg_npc (window NPCs), cs-walker (in-shop chibi), the cs
machine's own draws (`cs_pick_line`, `cs_accept_eval`, pushback), db054-gated companion sparkle — plus the
`{phasepin}`-wrap-up tool gap (so bg_npc+g_sim CAN be pinned).

## 21. ★★ The 目玉-sparkle %8 phase pin (`{gsimpin}`) — pillar-B of the rng survey LANDED 2026-06-24

**Problem (survey pillar B).**  Clean rng-callsite drill (NO phasepin — §8.5 lesson), `cs_walker_drill.py` with a
new `gsim`/`g8` column, on `house-firstcust-cutscene-day2` aligned at the f406 entry (cc08==4,b51c==0): the **目玉
display sparkle** (`player_ctrl_display_sparkle_emit`, gate `g_sim_frame_count%8==3`, ~24 LCG draws/fire over the
occupied display columns) fires at a DIFFERENT frame port↔retail.  Cause = the port's `g_sim_frame_count` ORIGIN
differs (it skips the intro) AND is **non-deterministic run-to-run** — entry gsim 785 / 792 over two drives (= +1 /
+2 sparkle phase), because g_sim freezes during loads whose duration varies.  Retail's is deterministic (entry gsim
**810** both drives).  A shifted sparkle makes every OTHER per-frame consumer read different LCG values ⇒ the
`cs_pick_line %2` variant flips.  (§20's "1f sparkle phase doesn't affect the variant" was the over-claim the user
corrected — a shift, not just a count, IS load-bearing.)

**Fix — `{gsimpin:[F,V]}`, a clean g_sim phase pin (sibling of `{rngseed}`).**  Force `g_sim_frame_count=V` at
base+F, pre-sim, fires once.  Port: `segtrace_gsimpin_cb` (main.c) + the seg_gsimpin machinery (input_segtrace.{c,h},
mirror of `{gframe}`); frida: `DAT_0438b8cc` write (mirror of the `{rngseed}` setrngs loop).  Touches ONLY g_sim —
NOT the bg-NPC LCG re-seed that `{phasepin}` bundles (which stalls the wrap-up cutscene).  V = retail's recorded
gsim at the anchor ⇒ a no-op for retail (preserves its natural phase), the port snaps to match.  Placed at the
first-customer entry: the LOADING_END segment carries `{gsimpin:[0,811]}` next to its `{rngseed}` (the pin VALUE =
the house_update gsim at the fire frame directly — empirically 809→off1=809, so 811→off1=811=retail).  +2 host
tests (3368).

**Verified (port-only drive vs natural-retail, `cs_walker_drill --anchor-b51c 0`):** the port gsim is now
DETERMINISTIC + bit-identical to retail from off 1 (811,812,…; reaches **1200 at the b534=6 reaction, off ~390**, 1/
frame, NO load-freeze in the window) — the sparkle fires 1:1 at the same offsets (off 2/10/18/…/74).  `gsim%8`
diverges only at off 0 (the unpinned entry frame — no sparkle there, both draw 2; harmless micro-residual; pin it
via a CONV_POSE_END-seg `{gsimpin}` if a fully-1:1 off-0 is ever wanted).

**REMAINING survey gap (pillar A — the sparkle pin is NECESSARY, NOT SUFFICIENT):** see §21.1 — it is the **bg_npc
NPC-position phase** (NOT the cs-walker spawn, which is aligned).

### 21.1 Survey continued 2026-06-24 — cs-walker spawn is ALIGNED; the remaining gap is the **bg_npc position phase**

**Method (the authoritative survey metric — use THIS, not the drill's per-frame rngΔ).**  `cs_walker_drill`'s
per-frame `rngΔ` is a delta-of-cumulative ⇒ it is **off-by-one** (the value shown at "off N" is the draws consumed
during frame N−1) AND its retail npcsp column reads one place right of npcfr (the PORT row has an extra npcn col —
don't misread b534 as npcsp).  The truth is the **cumulative rng consumed from the aligned entry**: read `rngcalls`
(the **0x47be92 = VA 4701842** probe, NOT 4733074 — that digest was wrong) at entry vs entry+off on each side,
subtract.  Attribute a divergent frame via the retail call_trace's **`FUN_005041f6` (0x5041f6 = VA 5259766)** LCG-step
entries: each carries `ret_va` = the **caller as an RVA** (Ghidra VA = ret_va + 0x400000); group by caller, map via
functions.csv.  (`0xcef6033`-class huge ret_va = the `rng_next_unit` thunk's internal frame — the float-variant
double-count proxy, = 1 logical `rng_next_unit`.)

**Findings (port V=811 drive vs natural retail, both at the f406 entry):**
- **cs-walker spawn CADENCE is ALIGNED** — npcsp 0→1→2→3→…→13 at off 30/60/90/120/150/190/250/270/300/350 on
  BOTH (an earlier "retail npcsp=0" read was a drill-column MISREAD; direct probe confirms).  The
  `PORT-DEBT(cs-walker-rng-phase)` "spawn-cadence phase" worry is RESOLVED for this trace — the spawn FRAMES match.
- **The remaining cumulative diff oscillates +1..+17, ≈+10 at the b534=6 reaction (off ~390)** — bounded (phase
  jitter of SHARED consumers), not monotonic (no wholesale missing consumer).
- **ROOT = bg_npc (`FUN_0046f2a3`, the shop-WINDOW townsfolk).**  The FIRST clean divergence is at **off 7**
  (retail frame 15274): retail draws 6, port draws 1 (just the `FUN_00442cef` scene tick).  The +5 = a bg_npc
  boundary-RESPAWN — 3 LCG steps from `FUN_0046f2a3` callsites 0x46f587/0x46f5b9/0x46f5dc + a `rng_next_unit`.  The
  port ticks bg_npc every frame (`scene1_bg_npc_tick`, scene1_player_ctrl.c:2236, ungated by cc08) but its **6 NPCs
  sit at DIFFERENT positions** than retail's (the warmup `FUN_0046f621` seeded them off a different LCG origin — the
  intro skip), so respawns cross boundaries on different frames.  This off-7 +5 then mis-aligns the LCG at the off-30
  `cs_spawn_one` ⇒ the spawned in-shop NPC starts at a different position ⇒ its `cs_npc_tick` draws differ ⇒ the
  cascade that makes the diff oscillate.  So bg_npc is the ROOT; cs-walker divergence is DOWNSTREAM of it.

**Next (the deep pillar-B/A item — user work-list #1/#5).**  Pin the bg_npc to retail's NATURAL positions at the
entry (NOT a synthetic warmup re-seed — that corrupts the variant vs the recording, the same reason `{gsimpin}`
pins to retail's recorded gsim).  Options: (a) capture retail's bg_npc SoA `DAT_073a7f80` (6×0x64 B) at the entry +
a `{bgnpcpin}` op writing it to `g_scene1_bg_npc` (needs port-struct↔SoA byte-compat check); (b) fix the
`{phasepin}`-breaks-wrap-up interaction (the bg_npc LCG re-seed forces `DAT_0438b4e0=0` ⇒ retail spawns through the
wrap-up CONV_POSE ⇒ stall) so the canonical warmup pin can run.  Until then the variant stays ~+10 LCG off at the
reaction — the sparkle pin alone does NOT make the rendered line 1:1.

### 21.2 `{bgnpcpin}` LANDED 2026-06-24 (option (a)) — pin bg_npc to retail's captured natural SoA; rng-drill verification BLOCKED by the instrumentation-determinism gap

**Built + committed (`2207c1a` op + tests, `d9abe4e` capture + bake).**
- **`{bgnpcpin:[F,[150 dwords]]}`** segtrace op (clone of `{gsimpin}`): at base+F, pre-sim, overwrite
  `g_scene1_bg_npc` from SCENE1_BG_NPC_COUNT raw engine records (0x64 each = 150 dwords) captured from retail's
  NATURAL `DAT_073a7f80`.  `scene1_bg_npc_pin` translates each field engine-dword-offset→port-struct (objdump
  map @0x46f2a3: arec dw0-10, x@dw11/+0x2c, y@12, z@13, dir@dw17/+0x44, visible@18, type@19, speed@20, pause@21,
  vthresh@22, mode@23, prob@24; dw14-16 unmodeled) — the port struct is NOT byte-compatible (reordered, packs to
  0x58).  Floats memcpy-reinterpreted.  Marks warmup done + cursor>=COUNT so the next tick neither re-warms nor
  spawns.  +5 host tests (translation, warmup-done, parse+fire, reject, + the 2 orphaned {gsimpin} tests that
  were written but NEVER registered in TESTS()).  PORT-ONLY (retail is the un-pinned capture SOURCE).
- **Capture = a CONDITION-gated agent dump, NOT {memsnap}.**  The cross-target wrap-up anchor desync stalls every
  segment-gated capture (the retail segtrace lags ~700f past the entry; `{memsnap}` on the LOADING_END segment
  never fires).  So the agent (`segtraceTick`) dumps `DAT_073a7f80` (0x258 B) → `frames/bgnpc_soa.bin` the FIRST
  frame `cc08==4 && b51c==0` holds (the cs_walker_drill alignment point) — desync-immune.  `frida_capture` now
  SKIPS `{gsimpin}`/`{bgnpcpin}` (PORT-ONLY pins; they were KeyError'ing the input-entry else).
- **Captured SoA (light/uninstrumented drive, entry frame 3711):** NPC1-5 textbook (types match the {_,1,6,7,9,8}
  table, z∈[-11,-15], speed∈[0.5,1), vthresh in the ±bands); **NPC0 = dir=0/type14/y=1.2 INERT** (the cs-leave
  reset slot — consumes NO rng, so rng-irrelevant; pinning it exactly matches retail).  Baked into the
  house-firstcust-cutscene-day2 scenario next to {gsimpin}/{rngseed}.  The pin FIRES at off 0 + applies (port
  stderr "pinned bg-NPC SoA (150 dwords)"); port reaches the reaction (b534==6) at off 365.

**★★ THE rng-drill VERIFICATION IS BLOCKED — instrumentation breaks the trace's determinism (the FOUNDATION gap).**
The per-frame rngcalls drill needs retail's cumulative-rng over the f406 window, which needs the rng-callsite hook
(`installRngCallerHook` → `Interceptor.attach` on `FUN_005041f6`/`FUN_00471089`).  But:
- **Retail's load-wait is COMPLETION-based, not time-based** (objdump/decompile, the wall-clock-pin RE): the async
  worker sets `DAT_0438b1cc`/`b1c8 = 1` at tails `0x452af9`/`0x452b24`/`0x452ac2`; the main thread gates on the
  flag (`cmp`); the predicate `FUN_0046c320` reads only flags — **NO time API in the wait**.  So a wall-clock /
  turbo virtual-clock pin does NOT change a load's frame-count (it only sets virtual-ms/frame).  **The wall-clock
  pin canNOT fix this** (correction of an earlier mis-framing).
- **The stretch is the rng hook taxing the load WORKER's rng draws.**  Measured (the `--call-trace` retail drive,
  RD `…-retail-20260624T203209Z`): the ENTIRE pre-entry stretch is ONE bracket — a **14161-frame gap NEW_GAME@206
  → HOUSE_FREEROAM@14367 = the initial cad868 Continue-load** (full save deserialize + scene-init record spawns =
  massive rng, every draw paying the Frida trampoline).  Uninstrumented that load is ~3500f (entry 3711); the hook
  inflates the worker's real runtime → the completion-based pacer runs ~10650 extra frames.  This load is NOT a
  d3e/dialogue worker (zero csloadpin/tutloadpin brackets fire before 14449) — an UNPINNED 3rd load mechanism.
- **Consequence:** the esc-skip input (anchored at a LOADING_END the trace expects 3× but retail fires only 1×,
  @14367) mis-times under the stretch ⇒ retail runs the **SCRIPTED tutorial (b51c==1)** instead of the skip→f406
  path ⇒ `cc08==4 && b51c==0` NEVER holds ⇒ the SoA dump + rng-log never fire ⇒ no entry to align.  (The LIGHT
  uninstrumented drive reproduces f406 1:1 — that's how the SoA was captured.)

**Fix options (the determinism FOUNDATION, user-directed next):**
- (1) **Condition-gated rng hook** — defer `installRngCallerHook` from init to the f406 entry (in `segtraceTick`
  when `cc08==4 && b51c==0`).  Pre-entry stays hook-free ⇒ the initial load is fast ⇒ skip times right ⇒ f406
  reached ⇒ rng counted from the entry (the committed `bgnpc-rng:` agent log captures off 0-200).  The CLEANEST
  unblock; the measurement proves it (no worker tax = no stretch).
- (2) **Pin the initial Continue-load** — find its worker/gate (NOT the 0x452af9 d3e tail) + clamp it.  But
  extend-only clamping needs N >= the instrumented worst-case (≈14161f) ⇒ every drive pays a 14000-frame load ⇒
  impractical for ONE huge load.  (Would also need to reconcile the 1-vs-3 LOADING_END anchor-structure mismatch.)
- **Verdict: option (1) is the right fix; (2) (the user's "pin all loads" pick) is impractical because the stretch
  is a single 14000-frame load, not many small ones.**  Surfaced to the user.
- The bgnpcpin pin itself is CORRECT-by-construction (host-tested translation + fires at the verified off-0 anchor
  + pins retail's TRUE captured SoA); whether it fully aligns the downstream stream awaits the unblocked drill.

### 21.3 Condition-gated rng hook ✅ LANDED 2026-06-25 — the rng-drill is UNBLOCKED (first verdict captured)

**Fix = §21.2 option (1): defer `installRngCallerHook` from boot to the f406 entry.**  A boot hook trampolines
EVERY LCG draw; the initial cad868 Continue-load's rng burst then inflates that ONE load so retail never reaches
the entry (runs the SCRIPTED tutorial). Deferred ⇒ pre-entry hook-free.
- **Agent (`tools/frida/openrecet-agent.js`):** `config.rng_hook_defer` → skip the boot install (log "rng LCG hook
  DEFERRED to the f406 entry"); `segtraceTick` ARMS the hook the FIRST frame `cc08==4 && b51c==0` holds — the SAME
  gate as the bgnpc SoA dump — logging "rng LCG hook ARMED at the f406 entry". `g_rng_hook_wanted` (decided at the
  install gate, folds in the call-trace `src:rngcalls` field) gates the deferred arm ⇒ no-op when no rng requested.
  `g_rng_count_total` is then cumulative-FROM-the-entry (= the bgnpc-rng / cs_walker_drill alignment origin).
- **`tools/frida_capture.py`:** `--rng-hook-defer` flag + AUTO-enable when the segtrace carries a `{bgnpcpin}` (the
  f406-trace marker) ⇒ the canonical `scenario-test … --target both --call-trace` defers with NO new flag / footgun.
- **VERIFIED (`…-retail-20260624T215600Z`):** agent log fires DEFERRED (boot) → ARMED @frame 14658 (`cc08==4
  b51c==0`) → bgnpc SoA dump @14658 → `bgnpc-rng: off=0 rng=0 … off=199 rng=830` (cumulative-from-entry, 2599
  rngcalls rows). The OLD boot-hook run (`…203209Z`) NEVER dumped/reached the entry (NEW_GAME@206 → HOUSE_FREEROAM@
  14367 stretch, ESC@14639 → scripted tutorial). So the deferral genuinely fixes the blocker. NB the initial load
  STILL stretches ~14000f under the 2000+ call-trace trampolines (§21.2's "no-tax=no-stretch" framing was
  imprecise) — but the rng-hook tax SPECIFICALLY was enough to mis-time the ESC; removing it reaches the entry.
- **FIRST DRILL VERDICT** (`cs_walker_drill` port `203038Z` ↔ retail `215600Z`, `--span 200`, both bgnpcpin+gsimpin
  applied): **14/200 frames diverge in per-frame rngΔ; 1/200 in gsim%8 (only off=0, the pre-pin/arm boundary).**
  The gsim sparkle phase is ALIGNED off≥1 (gsimpin holds — port==retail gsim 811…1009); the cs-walker spawn cadence
  (npcsp) is ALIGNED (0→6 same offsets both sides). Remaining rngΔ gaps (off 8,30-34,57,60,82,107,132,191,198; net
  retail +3 over off 1-199; biggest = the off 30-34 spawn cluster, retail +11) = consumer-level COUNT gaps, the
  pillar-A survey work-list. off=0 is a measurement boundary (retail rngcalls=0 at the arm), NOT a real gap.

**NEXT (pillar A survey, `docs/plans/rng-consumer-survey.md`):** re-drive with `--rng-callsites` over the entry
window → `FUN_005041f6` ret_va attribution (§21.1 recipe) for each diverging offset → port the consumer 1:1 →
re-drill until per-frame rngΔ is bit-identical. Start with the off 30-34 spawn cluster.

### 21.4 The survey on the unblocked drill (2026-06-25) — TWO roots: bg_npc off-by-one PIN TIMING + the cs-walker GRID

**Method upgrade (the per-frame attribution, no re-drive needed).** The existing `215600Z` retail call_trace
ALREADY carries the `FUN_005041f6` LCG-step rows with `ret_va` (the calltrace `[0,2600]` window logs them
post-entry) — no `--rng-callsites` re-drive needed. Tool `/tmp/rng_sxs.py` (candidate for `cs_walker_drill`
upgrade): retail per-frame draws = the exact `lcg_rows` COUNT + the `ret_va`→function attribution; port per-frame
draws = the `rngcalls` cumulative DELTA (port logs no per-callsite rows). **The off-by-one is real:** retail
`rngcalls_delta[N] == within-frame-draws[N-1]`, so align port `rc[off+1]-rc[off]` against retail `lcg_rows[off]`.
Consumer map (ret_va+0x400000→fn): `0x443606`=FUN_00442cef (1/frame base), `0x46f587/b9/dc`=FUN_0046f2a3
(bg_npc tick respawn), `0x46f9c3/cd/ee`=FUN_0046f914 (cs_spawn_one), `0x46fe91/9c`=FUN_0046fbee (cs_npc_tick
retarget pair), `0xcf06033`-class=the float `rng_next_unit` wrapper internal frame (=1 logical float draw).

**Baseline drill (bgnpcpin@LOADING_END, light canonical):** sparkles (off 1/9/17/25 = 24 floats) + base align
1:1 (gsimpin solid). FIRST divergence **off 7**: retail does a bg_npc boundary-respawn (FUN_0046f2a3 ×3 + 2
float = +5), the port does NOT. Then the off 29-33 spawn/tick cluster (retail +11 cum). 

**ROOT 1a — retail's bg_npc entry state is NON-DETERMINISTIC run-to-run.** Two retail drives dumped DIFFERENT
`DAT_073a7f80` at the f406 entry (NPC1 x=11.96 vs 17.09, NPC2 x=20.07 vs 14.60…; identical types/speeds, drifted
positions). Cause: the 6 NPCs tick a variable # of frames during the variable-duration cad868 load (a CreateThread
race; not fixable by a wall-clock pin — the load-wait is completion-based, §21.2). ⇒ the **PORT-ONLY {bgnpcpin}
pins the port to ONE stale capture that cannot match a fresh retail drive** — off 7 (and the whole downstream
cascade) diverges. Fix = BILATERAL pin (pin retail too, the {rngseed} pattern).

**ROOT 1b — the bgnpcpin was applied ONE TICK LATE (effective off1, not off0).** Even pinning the port to a drive's
OWN dump, off 7 still diverged — but with the tell: **port respawns bg_npc at off 8, retail at off 7** (port bg_npc
1 tick behind), everything else aligned. Proven by the gsim trace: port off0 `gsim=785` (natural), off1 `gsim=811`
(pinned) ⇒ a segtrace `base+0` op fires at **anchor_frame+1** (the frame after the base is established), so the
LOADING_END-segment bgnpcpin (LOADING_END@2148=off0) actually landed at off1's tick. **The dump captures retail's
off0 PRE-tick state (D₀); applying it effective-off1 lands D₀ at the port's off1 ⇒ +1 tick lag.** Unlike {phasepin}
(which re-derives the whole layout from a SEED, so the absolute frame is immaterial), {bgnpcpin} applies a SNAPSHOT,
so its effective tick MUST equal the dump's tick. **Fix:** move the bgnpcpin op to the **CONV_POSE_END segment**
(CONV_POSE_END@2147=off-1; base+0 → off0) so it lands effective-off0, matching the dump. Scenario-only, no rebuild.

**STEP-1b VERIFIED (port bgnpcpin@CONV_POSE_END, re-baked to 215600Z's own dump, vs natural retail 215600Z):**
off 7 bg_npc respawn now **ALIGNS** (port 6 == retail 6); off 0-28 bit-identical. So the port's bg_npc spawn/
respawn/tick LOGIC is correct (verified) — the only bg_npc issue was the off-by-one pin timing.

**BILATERAL {bgnpcpin} implemented (RE §21.4):** agent (`openrecet-agent.js`) WRITES `config.bgnpc_pin_soa`
(150 u32 → DAT_073a7f80) at the f406-entry gate (pre-sim = input_poll.onLeave, ahead of the bg_npc tick → off0-
effective, the SAME point as the existing dump); `frida_capture.py` forwards the scenario's {bgnpcpin} SoA as
`bgnpc_pin_soa` by default (`--no-bgnpc-pin-retail` = capture mode, dump the natural SoA to re-bake). Both sides
now pin to the same canonical at off0. (Bilateral drive validation: see the drill result below.)

**ROOT 2 — the cs-walker retarget GRID (off 29-32) is a SEPARATE gap, NOT a bg_npc cascade.** With off 7 fixed and
the stream bit-identical through off 28 (so identical LCG VALUES at off 29 under the pinned seed), the off 29-32
cs_npc_tick (FUN_0046fbee) retarget STILL diverges: port retries the `0x46fe91/9c` pair a different # of times than
retail. That loop (the `iVar15==-1` branch) is rejection-sampling — break on the first walkable cell
(`DAT_074b28e8[cell] ∈ {0,9}`), commit on a furniture neighbour (∈[2,8]). The port's retarget LOGIC matches the
decompile (verified: `scene1_shop_walker_helpers.c::cs_npc_tick` ↔ `46fbee.c`). So the divergence is the **grid
CONTENT** — the furniture-layout grid `DAT_074b28e8` (rebuilt every frame by `shop_display_grid_rebuild`/FUN_0048960d
from the save's shop-tier template + placed furniture) differs port↔retail in the cs-walker probe region (cols 1-8,
rows 1-7). **NEXT ARC:** dump retail's `DAT_074b28e8` at the f406 entry (add a grid dump to the agent) + the port's
grid, diff them, and root-cause the rebuild/furniture divergence. This is the next pillar-A consumer to close.

### 21.5 The cross-target WRAP-UP DESYNC (§21.4 ROOT 3, the determinism BLOCKER) — ROOT-CAUSED + FIXED 2026-06-25

**Root cause (from the agent logs, NOT the doc's vague "DLG_LINE_CLEAR doesn't fire").** The post-tutorial
iv1_7 wrap-up CONV_POSE is SKIPPED via ESC (arms the "Skip this event?" box, FUN_0046c2cb) + a CB_BTN_A "Yes"
confirm.  The recording's confirm = ONE blink-relative timed X (`{wait CONV_POSE_BLINK}`→X@+34).  Compared the
STALLED bilateral `both-20260624T230440Z/retail` ↔ the OK single `retail-20260624T215600Z`:
- **STALL:** `TEXT_ANIM_START@14624` → **`TEXT_ANIM_END@14682`** (the line ran FREE to FULL reveal) → then ONLY
  `CONV_POSE_BLINK` to `max_frames@90000`.  `TEXT_ANIM_END` firing ⇒ the skip box NEVER OPENED (an open box
  FREEZES the dialogue, so it can't fully reveal).  So the `ESC@14649` (TEXT_ANIM_START+25) **failed to ARM**.
- **OK:** `TEXT_ANIM_START@14587` → `ESC@14612` → **`DLG_LINE_CLEAR@14656` (NO `TEXT_ANIM_END` — cleared
  mid-reveal = the skip fired)** → `CSE#2@14657` (the f406 entry) → `CONV_POSE_END@14658`.
- **The arm gate** (all.c:67129): `if (1 < DAT_073a3e18(skip_prompt) && DAT_073a3dec(box)==0) box=1`, and
  `skip_prompt` RESETS to 0 on each dialogue re-init (all.c:67083).  So under retail's load jitter the single
  ESC@+25 intermittently lands when `skip_prompt<=1` ⇒ no arm ⇒ the wrap-up runs FREE ⇒ the segtrace's
  skip-structure waits (`DLG_LINE_CLEAR`/`CONV_POSE_END`) never fire ⇒ the **1176-blink deadlock**.  The PORT
  (turbo fixed-clock, jitter-free) lands the timed confirm reliably; only retail's jitter breaks the arm.

**Fix — a CONDITION-GATED, ARM-ONLY skip DRIVER in `openrecet-agent.js`** (the CLAUDE.md "improve the tool"
line; AGENT-ONLY — no scenario/port change, the port's deterministic timed inputs work).  Gated to the
post-tutorial wrap-up (a line up `DAT_073a6a38>=0`, `cc08!=4`, AFTER the tutorial [latch `cc08==4`-seen], BEFORE
the f406 entry [`g_bgnpc_soa_dumped`]): while the box is CLOSED (`DAT_073a3dec==0`), re-post a real WndProc ESC
(`g_esc_post`, the {esc}/skip-probe path) EVERY frame — opens the box the instant `skip_prompt` clears 1, robust
to the resets.  **Does NOT touch the input mask + does NOT confirm** — the scenario's recorded X@(blink+34)
confirms the now-open box at its intended time, so the skip TEARDOWN + the f406 entry stay bit-identical to the
recording.  `frida_capture.py`: `skip_wrapup` cfg auto-on with a {bgnpcpin} (the f406 marker; mirrors
`rng_hook_defer`) + `--skip-wrapup`/`--no-skip-wrapup`.

**★ 1st-attempt MISTAKE (kept as a lesson):** a FULL skip driver (agent ARMS *and* CONFIRMS via a 0x10 pulse +
suppresses the mask) un-stalled the blink but **diverged the flow** — drive `retail-20260625T000936Z` skipped
clean (`DLG_LINE_CLEAR@15534`, mid-reveal, 0 errors) yet hit a 286f LOAD → free-roam → **NO f406 entry**
(`CONV_POSE_END`=0, CSE=1, no bgnpc PIN, 79 spurious PAUSE_OPEN to max_frames).  The agent's EARLY confirm
(@15534) preceded + collided with the scenario's own `ESC@15545`, hitting the post-skip state.  Lesson: the
intermittent failure is ONLY the ARM; the confirm must stay the recording's (the skip outcome is
timing-sensitive).  ⇒ ARM-ONLY.

**★★ VERIFIED (the blink-stall is FIXED) BUT a DEEPER softlock surfaced — the skip→f406-entry is itself
load-jitter-fragile (2026-06-25, `retail-20260625T001837Z`).**  The ARM-ONLY driver works: `TEXT_ANIM_START@14614`
→ **`DLG_LINE_CLEAR@14682` (+68, mid-reveal, NO `TEXT_ANIM_END`)** — a clean skip, exactly the SUCCESS shape, 3
blinks total (NOT 1176), 0 driver errors.  **BUT the f406 entry STILL did not fire:** no CSE#2, `CONV_POSE_END`=0,
no bgnpc PIN; after the skip the drive enters a SOFTLOCK LOOP (`HOUSE_FREEROAM`→`PAUSE_OPEN`→`LOADING`→repeat,
~280f cycles, to `max_frames@90000`).  The segtrace stalls at `{wait CONV_POSE_END}` (never fires).  Findings:
- **NOT the skip TIMING** (the +68 confirm matches the recording) and **NOT the bilateral {bgnpcpin}** (its gate
  cc08==4&&b51c==0 never holds — it never fired).  Both my drives (full-driver `000936Z` + ARM-ONLY `001837Z`)
  softlocked identically.
- **A SECOND scenario fragility surfaced:** the wrap-up `{esc:25}` is in the TEXT_ANIM_START segment terminated
  by `{wait CONV_POSE_BLINK}`; when a blink fires BEFORE +25 (load jitter) the segment ends early and the esc is
  **ABANDONED** (it did NOT fire in `001837Z` — only the tutorial esc@14353 logged).  In the OK 215600Z run the
  blink fired AFTER +25 so the esc fired.  (My driver's ESC armed the box anyway, so the skip still happened — so
  the abandonment isn't the proximate softlock cause, but it shows the region is multiply timing-fragile.)
- **Conclusion: the wrap-up skip→f406-entry transition is load-jitter-sensitive end-to-end** — fixing the ESC-arm
  (the blink-stall) is necessary but NOT sufficient; whether the skip lands the f406 entry vs a free-roam softlock
  depends on the (CreateThread-race, non-deterministic) wrap-up LOAD durations.  This is the load-determinism
  FOUNDATION (pillar-B), not an input-driver problem.  The §21.2 note stands: a wall-clock pin can't fix a
  completion-based load.  **NEXT (needs direction / the load-determinism work):** pin the wrap-up LOAD brackets
  (a csloadpin-analogue for the iv1_7/cs-leave loads) so the skip timing is reproducible, OR a condition-gated
  confirm that matches retail's exact cutscene phase.  The ARM-ONLY driver is committed but EXPLICIT-only
  (`--skip-wrapup`, NOT auto-on) so the canonical drive keeps its known intermittent behaviour meanwhile.

### 21.6 ★★ The post-skip softlock was the DRIVER's OWN ESC spam, NOT load-determinism — FIXED 2026-06-25 (§21.5's "needs a wrap-up loadpin" was a MISDIAGNOSIS)
**Re-examined the §21.5 softlock loop (`001837Z` agent.log + the call_trace `f`-state rows) and found it is SELF-INFLICTED.**
- **The loop = `b150` (the modal-box flag) toggling** (`PAUSE_OPEN`/`PAUSE_CLOSE` track `b150`; the state probe showed
  `b150` 0→1 at the tutorial esc@14353).  In the post-skip free-roam there is **no game source for `b150`** (no live
  customer, no BARGAIN modal) — the **ONLY active ESC source after the tutorial esc is the skip DRIVER** (the diagnostic
  240-frame spammer was NOT armed; the segtrace is stalled at op71 `{wait CONV_POSE_END}`, injecting nothing).
- **The driver's continuation gate was `!g_bgnpc_soa_dumped`** — it kept evaluating (and posting ESC) through the ENTIRE
  post-skip window, until the f406 entry.  After the skip fired and the box closed (`DAT_073a3dec`→0), the gate **RESUMED**
  posting ESC; in the post-skip free-roam each ESC opens the pause box (`b150`→1 = `PAUSE_OPEN`) → triggers a scene reload
  (`LOADING`) → and the f406 entry arm (gated to NOT fire mid-pause/cutscene) is **blocked** ⇒ the entry never fires ⇒
  `!g_bgnpc_soa_dumped` stays true ⇒ the driver keeps posting ⇒ the self-reinforcing `HOUSE_FREEROAM→PAUSE_OPEN→LOADING`
  loop.  **Tell-tale the doc missed:** the post-skip loop ONLY ever appeared WITH the driver (`000936Z`/`001837Z`); the
  no-driver runs blink-stalled EARLIER (box never armed), so the loop was never seen without the driver = it is the driver.
- **Fix (`openrecet-agent.js`):** latch **`g_wrapup_box_was_open`** once the **post-tutorial** wrap-up skip box
  (`DAT_073a3dec != 0`) is seen open — then NEVER post ESC again.  The box opening = the arm SUCCEEDED; the recording's
  X@(blink+34) confirms it; the driver goes silent so the post-skip teardown→free-roam→f406-entry window is undisturbed.
  **Gate the latch on `g_wrapup_seen_tutorial && cc08!=4`** (1st attempt latched it unconditionally → the TUTORIAL's own
  "Cancelling tutorial?" box ALSO touches `DAT_073a3dec`, tripping the latch pre-wrap-up ⇒ the driver never armed ⇒ the
  blink-stall came back: a buggy-latch drive showed `TEXT_ANIM_END` + 1087 free-running blinks).
- **★ SECOND root, found while validating: the driver was NOT auto-on through `scenario-test`.** `frida_capture` auto-on'd
  `rng_hook_defer` from `has_bgnpcpin` but the §21.5 commit left `skip_wrapup` **explicit-only** ("keeps its known
  intermittent behaviour until the load-determinism fix lands").  So my first re-drives (`011313Z` etc.) ran the driver
  OFF — their wrap-up arm was the **recording's own esc@25**, which works ONLY on a LONG (call-trace-stretched) load and
  MISSES on a short one (skip_prompt<=1 at +25) ⇒ the apparent "1 OK, 1 stall" was load-phase luck, NOT the driver.  Fixed:
  re-instated the **auto-on with `{bgnpcpin}`** (`frida_capture.py`, mirroring `rng_hook_defer`; `--no-skip-wrapup` forces off).
- **Why the DRIVER arm is load-robust (the recording's esc is not):** the driver re-posts ESC EVERY frame while a wrap-up
  line is up (`DAT_073a6a38>=0`); by the time the line shows, the CONV_POSE dialogue has ticked `skip_prompt`(`DAT_073a3e18`)
  to **121** (>>1), so `FUN_0046c2cb` arms the box on the FIRST post — `box=1` the very next frame — independent of load
  duration.  Added throttled `wrapup_dbg` logging (`openrecet-agent.js`, gated to the wrap-up) that prints this live.
- **✅ VERIFIED with the DRIVER ON, 2/2 reproducible (`retail-20260625T014156Z` call-trace + `014427Z` no-call-trace =
  different load phases):** `wrapup_dbg` both show `line=0 skipP=121 posted=1` → next frame `box=1 latched=1 posted=0`; then
  `TEXT_ANIM_START → DLG_LINE_CLEAR` (clean mid-reveal skip, **NO `TEXT_ANIM_END`**) `→ CUSTOMER_SERVICE_ENTER#2`; the **f406
  entry SoA dump fires** (`bgnpc-rng off 0..199`, `cc08=4 b51c=0`, `b534`→1 first-customer greeting at off199); **2-3
  `PAUSE_OPEN` (not 79), 3 `CONV_POSE_BLINK` (not 1176)** — no loop, no stall, both load phases.  ⇒ the f406 entry is now
  RELIABLY reached; **§21.5's "load-determinism FOUNDATION / wrap-up loadpin" was a misdiagnosis** — no loadpin needed.  The
  ARM-ONLY driver is the canonical retail path (auto-on with `{bgnpcpin}`).
- **NEW, OUT-OF-SCOPE blocker surfaced:** a SEPARATE blink-stall in the **DAY-2 cutscene-series region** (post-iv1_8,
  ~frame 21259+ → free-running blinks to `max_frames`).  Same ARM-jitter pattern at a LATER skippable cutscene the driver
  does NOT cover (it auto-disables at the f406 entry, `!g_bgnpc_soa_dumped`).  This is OUTSIDE the cc08==4 survey window +
  caprange `[0,2600]` ⇒ it does NOT affect the rng survey; it is the future "iv1_8 → cutscene series → day-2 brooming" work
  (FRONT).  When that arc starts, generalise the ARM-ONLY driver to re-arm at each post-entry skippable cutscene.

### 21.7 ★★ §21.4 ROOT 2 "the cs-walker GRID differs" was a MISDIAGNOSIS — the grid is BIT-IDENTICAL; the residual drift is an rng-VALUE (seed-origin) gap, NOT a missing consumer (2026-06-27)
On the now-deterministic entry (§21.6), dumped BOTH sides' furniture-layout grid `DAT_074b28e8` (300 int32) + its five
inputs at the f406 entry (new diagnostics: port `shop_display_grid_dump` gated `customer_service_active()&&b51c==0`,
`src/scene1_shop_display.c`; retail one-shot in `openrecet-agent.js` next to the bgnpc SoA dump; differ via `/tmp/grid_diff.py`).
**Result — the grids are BIT-IDENTICAL (0 differing cells), inputs identical:** `tier=0 count=3 origins=[[3,3],[1,0],[0,1]]
mesh=[3,4,4] rot=[0,0,π/2]` on both (port frame 791 / retail frame 15388).  So §21.4 ROOT 2's "the grid CONTENT differs
in cols 1-8 rows 1-7" was an INFERENCE ("logic+values match ⇒ must be the grid") that was never actually dumped — and it
is FALSE.  The grid rebuild (`FUN_0048960d`), `cs_spawn_one` (`FUN_0046f914` — 4 draws: 3 int15 incl. the **discarded
2nd draw** + 1 float `FUN_00471089`; the port models the waste draw), the retarget burst (`FUN_0046fbee` — cx=(rng15&7)+1,
cy=(rng15%7)+1, break on first walkable {0,9}), and the pump order (`FUN_0047019f`: spawn-before-tick) are ALL verified
faithful vs the decompile.
**The actual picture (`cs_walker_drill` port `…125602Z` ↔ retail `…130047Z`, --span 200):** off 0-28 COUNT-aligned;
ONLY **13/200 offsets diverge**, clustered at the cs-walker SPAWN+BURST (off 29-32, ret_va-attributed via `/tmp/rng_sxs.py`:
both sides spawn `FUN_0046f914`@off29, then the spawned NPC re-bursts `FUN_0046fbee` a DIFFERENT # of iterations —
retail 1/3/4/2, port 3/1/1/0 — drift retail **+11** cumulative).  Since the grid is identical + the burst logic is
identical + the spawn count matches (4) + off 0-28 are count-aligned, the burst's (cx,cy) samples can only differ if the
rng **VALUES** feeding the burst are misaligned — i.e. this is **pillar B (seed/phase ORIGIN), NOT pillar A (a missing/
mis-counted consumer)**.  The burst is the FIRST value-dependent-COUNT consumer in the window (the base `FUN_00442cef`
is 1/frame and the sparkle is gsim-gated — both count-deterministic regardless of value), so a value-misalignment stays
INVISIBLE until off 29.
**Root lead — the entry-boundary pin lands off-by-one (same shape as §21.4 ROOT 1b).** At off 0 (the entry frame) gsim is
NATURAL/unpinned: **port 791 vs retail 810**; the `{gsimpin:[0,811]}`+`{rngseed}` (placed at `LOADING_END+0`) only take
effect at **off 1** (both jump to 811).  So off 0 is an unpinned frame whose draws use each side's natural pre-entry LCG
state; if the `{rngseed}` re-pin then lands at a 1-frame-skewed effective offset port-vs-retail (the segtrace base+0 →
anchor+1 off-by-one differs between the port's `input_segtrace` and the agent's), the post-pin streams sit at a constant
value-offset that surfaces at the off-29 burst.  **NEXT:** confirm the seed value at off1 port-vs-retail (instrument the
LCG state `DAT_006023a0` both sides at the entry) and align the `{rngseed}`/`{gsimpin}` to off0-effective (move to the
entry's own segment / fix the base+0 anchor+1 skew), mirroring the §21.4 ROOT 1b `{bgnpcpin}`→CONV_POSE_END fix.  The
cs-walker GRID arc is CLOSED (no port gap there).

### 21.8 ★★★ CONFIRMED 2026-06-27 — the value gap is a `{rngseed}` PIN-OFFSET SKEW: the per-segment seed pins fire at port↔retail-skewed entry-relative offsets
Instrumented the LCG STATE `g_rng_seed`/`DAT_006023a0` both sides (port: `CALL_TRACE_I32("rngst")` in the 0x48670f
probe; retail: `rngst=` in the agent's bgnpc-rng log) + re-drove (`…132607Z`).  Compared via `/tmp/rngst_cmp.py`.  The
smoking gun — the SEED-PIN VALUES land at DIFFERENT entry-relative offsets:
```
        port    retail
off 0   807420856   449161817(natural)
off 1   2300378890  807420856   ← retail applies CONV_POSE_END seed (807420856) at off1; port at off0
off 3   …           2300378890  ← retail applies LOADING_END seed (2300378890) at off3; port at off1
```
So the scenario's `{rngseed}` ops (807420856 @ CONV_POSE_END, 2300378890 @ LOADING_END, …) are applied **1-2 frames
EARLIER on the port than on retail** ⇒ after the last pre-entry seed the two streams sit at a **constant ~+2-frame value
offset** (port ahead).  The per-frame draw COUNT then matches (off 0-28), so the offset is invisible until the first
value-dependent-COUNT consumer — the cs-walker burst @off29 — samples (cx,cy) from the +2-skewed state ⇒ the +11 drift.
**Root mechanism:** the `{rngseed}` pins are anchored to engine events (CONV_POSE_END / LOADING_END / LOADING_START)
whose timing RELATIVE TO the f406 entry DIFFERS between the port (compressed flow) and retail (load-stretched), so a
`base+0` op lands at a different entry-offset on each side.  (The retail trajectory also RE-pins LOADING_END's value at
~off24 — the b1cc customer-asset load completing — which the port doesn't mirror at the same offset: an asymmetric
load-event re-pin, a second skew source.)  This is the `{bgnpcpin}`-vs-segtrace asymmetry generalised: the `{bgnpcpin}`
is CONDITION-gated at the f406 entry (off0 both sides) and is correct; the `{rngseed}` is SEGMENT-gated and skews.
**FIX direction (pillar-B foundation, mirrors `{bgnpcpin}`):** an **entry-gated bilateral seed pin** — capture the
natural LCG state at the f406-entry CONDITION (`cc08==4&&b51c==0`, the bgnpc-dump point) and re-apply it off0-effective
on BOTH sides at that same condition, so the entry is a single clean value-sync point independent of segment/load timing.
Must also neutralise the asymmetric in-window load-event re-pins (the off24 LOADING_END) so they don't re-skew.  This is
the next pillar-B chip; needs a design call on the pin mechanism (extend `{bgnpcpin}` to carry the seed, or a sibling
`{entryseed}` op condition-gated like the bgnpc pin).

### 21.9 ★★★ FIXED 2026-06-27 — the user's "do the load the SAME as retail so the pin plays out the same": the port's d3e load-overlay didn't span the load → the load-anchored {rngseed} re-sync was MISSING
**User directive** (chosen over the entry-seed-pin band-aid): *"both sides should pin the same way and produce the same
results for each pin… the pin should cancel anything that doesn't have to do with our port's accuracy.  If a load seam
behaves differently for us because we do the load differently then we should do the load the same as retail down to how
it waits for it so the pin plays out the same as on retail."*
**Root (anchor timeline, both sides):** at the f406 entry retail keeps `loading_active` up THROUGH the 24-frame d3e
customer-asset load — `LOADING_START@off-1 → LOADING_END/HOUSE_FREEROAM@off23`; the **port closed it at off0**
(`LOADING_END@off0`, a 1-frame scene reload), running the d3e load (b1cc==2, off0-23) as a BACKGROUND load with no
overlay.  Why: the engine d3e spawn `FUN_00452d3e` sets `DAT_0438b1cc=2` **AND** the secondary load-overlay gate2
`DAT_06a49960=1` (cleared by the worker reap `FUN_00452917`); retail's `loading_active = gate1||gate2` so it spans the
load.  The port modelled the d3e load (`worker_load_spawn_d3e`, b1cc=2) but never raised the overlay ⇒ `loading_active`
dropped at off0.  **Effect:** the `{rngseed}` anchored to LOADING_END RE-PINS retail at off24 (re-syncing the seed right
before the cs-walker burst @off30, wiping the upstream skew) but **NEVER the port** (its LOADING_END was at off0) ⇒ the
port stream stayed skewed ⇒ the §21.8 burst drift.
**Fix 1 (the load):** `customer_service_d3e_loading()` = (b1cc==2), mirroring gate2's lifetime, OR'd into
`g_frame_loading_active` (anchor/capture ONLY — verified it does NOT gate the sim/rng).  Now LOADING_END/HOUSE_FREEROAM
fire at ~off24 like retail ⇒ the load-anchored `{rngseed}` re-syncs at off24 on BOTH ⇒ **the burst aligns**.
**Fix 2 (the gsim side-effect):** moving LOADING_END moved the `{gsimpin}` (it rode on `{wait LOADING_END}`) to off24,
un-pinning gsim off0-23 ⇒ the sparkle (gsim%8) mis-fired.  Moved `{gsimpin}` to the **CONV_POSE_END segment**
(off0-effective, the entry pin alongside `{bgnpcpin}`), value **810** = retail's off-0 gsim, so gsim is corrected at the
entry independent of the load.  The `{rngseed}` re-sync stays on LOADING_END.
**✅ VERIFIED (`cs_walker_drill`, port `…141719Z` ↔ retail `…132607Z`, span 200):** **13/200 → 3/200** diverging offsets;
**0/200 gsim%8 divergence**.  The cs-walker burst off30-34 is **BIT-IDENTICAL** (port `7,7,9,5,26` == retail, was
`11,3,3,1`); sparkles align frame-for-frame.  Residual 3 (all ±1, the tail): off0 (the pre-pin entry-frame boundary),
off189 (a bg_npc respawn `FUN_0046f2a3` 1-frame phase), off199 (`cs_pick_line` the greeting variant draws 1 frame later
on the port).  Files: `src/customer_service.{c,h}` (`customer_service_d3e_loading`), `src/main.c` (the loading_active OR),
`tests/scenarios/house-firstcust-cutscene-day2/trace.jsonl` (`{gsimpin}`→CONV_POSE_END).
**✅ BILATERAL re-drive CONFIRMED** (port `141719Z` ↔ retail `142225Z`, BOTH new scenario, the bgnpcpin PINNED on retail):
still **3/200, 0/200 gsim%8**; off199 now ALIGNS (the earlier off199 was a port-new-vs-retail-old scenario artifact).
**NEXT-ARC LEAD — the bg_npc respawn ±1 (the only real residual, off189/197/269/271, a separate CONSUMER):** the respawn
`bg_npc_respawn`/`FUN_0046f2a3` (scene1_bg_npc.c:156) branches on `r1 < m->prob/2`; with the stream ALIGNED into the
respawn AND the bilateral pin giving both sides the same off0 SoA, `r1`(=rng15%100) matches but the **branch differs ⇒
`m->prob` differs port↔retail** ⇒ the port takes the `else` branch (extra `r2`) ⇒ +1 (a draw-COUNT diff WITHIN the respawn,
NOT a respawn-frame shift — both respawn @off189).  **CORRECTION:** the prob FIELD map is CORRECT (`r[24]`→`m->prob`, both
engine +0x60, scene1_bg_npc.c:357), so the prob VALUE itself diverges — either the baked canonical SoA mis-captured this
slot's prob, OR the respawning NPC was SPAWNED post-entry (`bg_npc_seed` rolls `prob`) with a divergent roll rather than
pinned.  NEXT: probe the slot's `prob` port↔retail at off189 (add a prob field to the SoA dump / drill).  This ±1 cascades
to `cs_pick_line` firing 1 frame early (off199/270), so it MAY shift the reaction variant ⇒ close it before declaring the
variant 1:1.  The cs-walker + sparkle + gsim + load-seam are DONE.

### 21.10 ★★★ NEW ARC (user directive 2026-06-27) — frame-by-frame anchor determinism: the port runs ~1 FRAME FASTER per non-load segment ⇒ a WALL-CLOCK pin
**User (after flagging 7 studio notes):** *"continue methodically working on the anchors and fixing port on real divergences
until it all plays out 1:1 in the studio frame by frame.  And it needs to be GENERALIZED to other scenes triggering the same
anchors, not specific to this one trace.  If an anchor methodology isn't deterministic we don't accept the residual, we fix
the anchoring method.  Running in non-turbo mode is also acceptable if that makes it easier to make it deterministic."*
**The 7 notes (`orv3_notes house-firstcust-cutscene-day2 --render`), triaged:** #2 (LOADING_END#2+13, Tear manga-lines +
scolding pose) = a **REAL port render gap** (the scold-reaction render, unported); the other six are **phase/timing**: #1
(stool-jump arrival anim → camera off till settled), #3 (sparkles desync), #4 (bg-window NPCs desync), #5/#6 (standee slide
AHEAD@PAUSE_OPEN#1+30 / BEHIND@CONV_POSE_BLINK#1+62 — not a constant offset), #7 (retail early on the skip-event prompt).
**GROUNDING MEASUREMENT (anchor timelines, port `141719Z` ↔ retail `142225Z`, aligned at CUSTOMER_SERVICE_ENTER#1):** the
timeline DRIFTS **0 → −3** across the window — port AHEAD.  The load DURATIONS MATCH (every LOADING_START→END is 24 frames,
deterministic via csloadpin), so the drift is NOT the loads; it accumulates in the **non-load gameplay/cutscene segments**
(LOADING_START#3 −1, HOUSE_FREEROAM#4 −3, DLG_LINE_SHOW#1/TEXT_ANIM_START#1 −3, CONV_POSE_END#1 −3, CSE#2 −2) ⇒ the **port
runs ~1 frame FASTER per ~100-150-frame segment**.  This is why the rng-pins (which re-sync at the f406 entry CSE#2) leave
the wrap-up/skip region (notes #5/#6/#7) visually mis-framed: the rng is re-synced AT the entry but the FRAME alignment
already drifted −2 getting there.
**Root — NOT wall-time (the wall-clock-pin hypothesis is RULED OUT):** grepped the engine + port for GetTickCount/QPC/
timeGetTime — the engine has only **3**, all **FPS-stats / timing utilities** (`DAT_073dde28`=the FPS meter @51312; QPC
@78849 = an elapsed-ms helper), **NONE feed gameplay/cutscene**; the port drives gameplay off the virtualized
`g_tick.frame_count`.  So gameplay/cutscene logic is **FRAME-COUNT-driven**, and the per-segment ~1-frame drift is a
**frame-count DURATION off-by-one** — a fade/cutscene/dialogue segment that runs 1 frame SHORT on the port (consistent
with the loading-screen-fidelity directive: the port's fades/transitions are ~1f shorter than retail's).  ⇒ **non-turbo /
wall-clock-pin would NOT help** (clock-independent).  **GENERALIZED fix = segment-by-segment frame-count corrections:** for
each anchor→anchor segment that drifts, root-cause why the port is 1f short (which fade/transition/cutscene/dialogue
duration) and match retail; each fix is a per-effect duration (generalizes to ANY scene using that fade/cutscene).  The
drift concentrates in the **wrap-up/skip cutscene region** (LOADING_END#3→HOUSE_FREEROAM#4 grows −1→−3 = the ESC-skip +
CONV_POSE wrap-up is ~2f short) and the tutorial segment (−1).  **NEXT (methodical, in flow order):** (1) the first −1
segment (CSE#1→LOADING_START#3, the tutorial); (2) the −2 skip/wrap-up region (notes #5/#6/#7 live here); diff the port↔
retail per-frame sub-anchors (DLG_LINE_SHOW/CONV_POSE_BLINK/fade phases) to find the 1f-short effect each; (3) the real
render gap #2 (the scold-reaction manga-lines + pose).  Tools added this arc: port `grid_dump`, `rngst` probe,
`/tmp/grid_diff.py`, `/tmp/rngst_cmp.py`, `/tmp/port_anch.txt`+`/tmp/ret_anch.txt` anchor-timeline diff.

### 21.10.1 ★★★ FIXED 2026-06-27 — the first −1 + note #1 are ONE root: the cc08==4 arm's d3e-load timing
**Tooling first (the off 0-84 region was UN-MEASURABLE):** retail's `{calltrace}`/`{caprange}` were bound to LOADING_END
**occ3** (= HF occ3, off ~109) so retail captured NO state for the CSE#1 scripted-tutorial region — the port has full
coverage (captures from boot), retail didn't.  **Fix:** moved both ops to **HF occ1** (the post-Continue free-roam, after
the first `{"wait":"LOADING_END"}`), window `[0,3000]`, so BOTH sides cover walk→tutorial→skip→wrap-up→first-customer.
Added a trimmed probe **`house-firstcust-arrprobe`** (lines 1-132, ends after the first-customer BARGAIN → no day-2 stall,
max_frames 6000) for fast bounded `orv3_window --state` iteration, + **`tools/anchor_drift.py`** (port↔retail anchor-drift
map + per-frame 0x48670f state diff, aligned at any anchor:occ).
**ROOT (proven, port↔retail per-frame, aligned at CSE#1):** the cc08==4 arm (`scene1_player_ctrl.c`) ran the WHOLE body
gated on the LIVE `b1cc != 2`.  But `notify_loaded` clears b1cc INLINE at the top of the arm, so on the load-release frame
the gate saw b1cc==1 and ran EVERYTHING that frame.  Two consequences vs retail:
  **(a) the master tick ran ON the release frame** → the `b524` idle counter `++`'d a frame early → the 2nd d3e load
  (`b524==0x3c`) fired 1f early = the §21.10 **first −1**.  Retail's house_update is gated on the load-SCREEN counter
  (`be94<0x78`, all.c:40591), which the async worker drops a frame AFTER it clears b1cc, so retail's master tick (b524)
  resumes the frame AFTER the release (verified: retail b524 first `++` at LOADING_END+2, the port at +1).
  **(b) the arrival anim + camera + sprite were SUPPRESSED during the d3e load** → the stool jump + camera zoom played
  ~23f LATE (= studio note #1).  Retail's d3e customer-asset load is a BACKGROUND load (b1cc==2) that does NOT raise the
  load SCREEN (be94 stays <0x78), so retail's house_update keeps ticking: **panim 5 from entry+2, camex ramp −1.5→−2.65
  ACROSS the 24f load, pcnt climbing** — only the MASTER tick is inert (it self-gates on b1cc==2).
**FIX (`scene1_player_ctrl.c` cc08==4 arm):** capture `b1cc_pre` at frame-start; (1) gate ONLY the master tick on
`b1cc_pre != 2` (resumes the frame after release ⇒ b524 in lockstep w/ retail); (2) UNGATE the arrival_tick + ground_y +
`chr_anim_tick` (run through the load like retail).  All rng-neutral (arrival/sprite are rng-free; the pump stays
unconditional, §19).
**✅ VERIFIED (probe, port↔cached retail):** anchor drift **0** from CSE#1 through CONV_POSE_START#1 (was constant −1);
end-to-end −4→−2 (wrap-up residual = task #2).  Arrival **BIT-IDENTICAL through the load**: panim 0→5@off2, pcnt
23→1→28, camex −1.5→−2.85 == retail.  rng survey **3/200→1/200, 0/200 gsim%8** (cs_walker_drill).  3372 host pass.
**REMAINING in this window (next):** (a) the COMPANION (Tear) is the OPPOSITE — retail keeps her IDLE (canim 0, frozen
cx) DURING the load, arriving only at off26+ (after b1cc clears), while the port runs her arrival through the load
(canim 4 from off1).  So `scene1_companion_ctrl_tick` (FUN_0048a833) self-gates on b1cc on retail (inert during load) —
the port's companion ctrl must gate the same.  Pre-existing (NOT this fix).  (b) task #2: the wrap-up load occ4 is 2f
(port) vs 1f (retail) → LOADING_END#4 +1; DLG_LINE_SHOW +2 (dialogue reveal); then the first-customer region drift.

### 21.10.2 ✅ FIXED 2026-06-27 — the companion (Tear) arrival: idle THROUGH the load, then walk in (matches retail)
Completes the arrival region (§21.10.1 did the player; this does the companion).  Root: the port runs the companion ctrl
(`scene1_companion_ctrl_tick`, from scene1_sim) EVERY frame, so its cc08==4 at-counter branch (`co_at_counter_tick`,
canim 4 + step) ran THROUGH the d3e load — Tear walked to the counter while loading.  Retail calls FUN_0048a833 from the
cc08 arm but its arrival is INERT during the load (b1cc-gated): verified canim 0 + cx FROZEN (−0.0221) across the 24f load,
only the idle ANIM advancing (cframe 1→2→3→0 via the draw leaf), then she walks in (canim 1 @ off26-27 → 4 @ off28) — the
walk branch the port's co_at_counter already has (dist≥2.0) but never hit because it pre-walked her in during the load.
**Fix (`scene1_companion_ctrl.c`):** gate the companion ctrl INERT during the cc08==4 load — run only `chr_anim_tick` (the
idle anim) + return.  Gate = `(customer_service_d3e_loading() || customer_service_load_at_frame_start())`: the d3e load
both SPAWNS (entry) and CLEARS (release) mid-frame, and the companion runs AFTER scene1_player_ctrl_tick, so the SPAWN
frame shows only the LIVE b1cc==2 (the cc08 arm's note isn't set — the entry path returns early) and the RELEASE frame
shows only the frame-start SNAPSHOT==2 (notify_loaded already cleared the live b1cc).  New `customer_service_note_frame_load`
/`_load_at_frame_start` (snapshot set in the cc08 arm before notify_loaded — same b1cc_pre idea as the master tick, shared
to a later-in-frame consumer).  **✅ VERIFIED:** companion canim + cx + octant + the walk-in (canim 1→4) BIT-IDENTICAL to
retail (cx −0.2274→−0.9759 == retail across the walk); rng survey still 1/200, 0/200 gsim%8; drift 0; 3372 host pass.
**Residual (tiny):** the idle wing-flap `cframe` is ~1f ahead during the load (the free-roam→load entry leaves the idle
anim-counter 1 step further along than retail) — position/arrival are exact; sub-frame anim-cycle phase, revisit if visible.

## 21.11 ★★★ The first-customer OFFER (b574 120 vs 119) ROOT-CAUSED 2026-06-27 — an in-window {rngseed} re-pin fires 1f early

**Gap (FRONT A):** port first-customer offer `b574=120` ⇒ ACCEPTS (b534 15→7) where retail `119` ⇒ PUSHES BACK
(15→8→6 round-2 150).  Scenario `house-firstcust-arrprobe` (current arrprobe caches, both post-§21.10).

**Method — LCG-value forensics (not just rngcalls COUNT).**  Added a TEMP reveal/settle probe
(`customer_service_*_dbg`: b548/b55c/b558/ca0/ca1/ip1/ip2) + the 7 retail fields (DAT addrs).  KEY tool: both sides'
0x47be92 `rng` = the LCG state `DAT_006023a0`/`g_rng_seed` at frame-start ⇒ compare the LCG **VALUE** frame-by-frame
(retail emits it; the per-frame `rngcalls` COUNT does NOT catch a value/phase skew).

**Findings (entry = f406 cc08==4&&b51c==0; offsets entry-relative):**
- b534 0→1 @off199, 1→2 @off270 BOTH ALIGNED.  2→6 (the OFFER draw, `cs_offer_up`) PORT @off389 / RETAIL @off390.
- LCG value BIT-IDENTICAL off30→**off270**, then DIVERGES @**off271** (the b534==2 line-load frame).  Per-frame
  rngcalls COUNT matches (+2 @off270 both) — so it's a VALUE/phase skew, invisible to the count survey.
- Actual LCG STEPS off270→off271: **PORT 16, RETAIL 2** (port +14).  retail off271=LCG²(off270); port off271=LCG¹⁶.
- The 14 "uncounted" advances = a `rng_seed()` JUMP (only uncounted LCG path).  **The pin value 3464877067 =
  LCG¹⁴(off270-value)** — i.e. the `{rngseed:[0,3464877067]}` re-pin at the FIRST **PAUSE_CLOSE** anchor (trace L90).
- ⇒ on the PORT the PAUSE_CLOSE re-pin fires @off270 (port seed there = k0), JUMPING k0→k14, then +2 natural → k16.
  On RETAIL the pin is a near-no-op: retail reaches k14 NATURALLY @off271-272 via a **bg_npc respawn CLUSTER** (~12
  draws @off271, the shop-window NPCs `scene1_bg_npc` — the only other rng consumer in the cc08==4 path besides cs;
  cs-walker npcsp/sparkle/dust all ALIGNED).  The pin was COMPENSATING for the port doing that bg_npc cluster a frame
  later (port off271 draws only the cs_pick_line variant), but it fires 1f EARLY ⇒ the variant pick @off271 reads a
  skewed value ⇒ wrong variant (poseR 1 vs retail 3 ⇒ K=14 vs 49 char line) ⇒ wrong offer 120 vs 119.

**This is the §21.8 SEED-PIN SKEW recurring at PAUSE_CLOSE** (an asymmetric in-window re-pin; §21.8 said "neutralise
the asymmetric in-window load-event re-pins").  Between off270 and the offer (off389) the trace has NO other re-pin, so
the natural bg_npc 1f-cluster-phase SELF-CORRECTS (both do 15 draws by off273) — the pin is the only non-self-correcting
perturbation.  **Hypothesised fix: drop the in-window PAUSE_CLOSE {rngseed} re-pin (keep the entry pin); the port's
consumers matched off30-270 so the entry pin alone holds, and the variant/offer re-converge.**  Testing next.

### 21.11.1 ★ EXPERIMENT + CORRECTION (2026-06-28) — drop the re-pin ⇒ VARIANT robustly fixed; the off271 cluster is the CS-WALKER (NOT bg_npc)

**Dropped the PAUSE_CLOSE {rngseed} re-pin (trace L90), re-drove the port (no-pin):**
- ✅ **VARIANT robustly FIXED** `poseR 1→3` == retail (off271-START LCG bit-identical k2 ⇒ the variant DRAW reads the
  aligned value) + offer `120→119` + outcome `b534 7-accept→8-PUSHBACK` == retail.  BUT the offer match is
  **COINCIDENTAL**: the port stays **−12 draws behind from off271 through the offer** (port draws 1 @off271 vs retail 13);
  119 lands only because the −12 shift hits the same `init_eff % (2·random+1)`.  (anti-pattern: single matched value ≠ stream parity.)

**★ The −12 consumer is the CS-WALKER, not the bg_npc (corrected via a bg_npc position probe + retail SoA fields):**
- Instrumented `g_scene1_bg_npc[i].x/.dir` (port) + retail `DAT_073a7f80` SoA.  **The bilateral {bgnpcpin} APPLIES
  CORRECTLY** — port NPC positions BIT-MATCH retail @off0 (the canonical −2.80/+17.09/+14.60/+5.50/+7.87/−11.51).  And
  house_update + the bg_npc tick run EVERY frame through the d3e load (no suppression; the no-pin drive's 0x48670f emits
  every frame off0-22 b1cc=2).  The bg_npc RESPAWNS (NPC1 x>25 dir flip @off~190) IN SYNC with retail (LCG matched off25-270).
- **At off271 the retail bg_npc positions are nowhere near the respawn thresholds** (NPC1 +21.6, NPC2 +2.5, NPC3 −7.9,
  NPC4 +20.6, NPC5 −3.2; |x| ≪ 15/25) ⇒ the 12-draw cluster is NOT a bg_npc edge-respawn.  Not the cs machine (1 draw =
  the variant) and not the sparkle (%8-gated).  **By elimination + the port's `npcdr=0` @off271 ⇒ it's the cs-walker burst
  (`FUN_0046fbee`/`FUN_0047019f`).**  §21.9's drill was `--span 200` (off0-199); the off271 burst is OUTSIDE it — the
  un-surveyed consumer the FRONT predicted (`PORT-DEBT(cs-walker-rng-phase)`).
- **TOOLING NOTE:** the wide-caprange (2400) + bg-probe capture DESTABILISES the d3e-load CreateThread race (the CS stalls
  at b534=0; §20.1 worker-tail re-arm class) ⇒ couldn't capture the bg positions through the full reaction window that way.
- **NEXT (next session):** per-draw rng-callsite attribution (§21.1: retail `FUN_005041f6` ret_va) @off271 to CONFIRM the
  cs-walker + locate the exact draw; then port the cs-walker burst at off271 1:1 so the stream is bit-identical → offer
  robustly 119; then neutralise the in-window PAUSE_CLOSE re-pin (§21.8).  TEMP probes (`*_dbg`, `bg{i}x/d`, retail_fields)
  to remove after.  **The bg_npc arc is a CONFIRMED NON-ISSUE (pin works, integration 1:1).**

**EXPERIMENT (2026-06-27) — dropped the PAUSE_CLOSE {rngseed} re-pin, re-drove the port (`-openrecet-…211003Z`):**
- ✅ **VARIANT robustly FIXED:** `poseR 1→3` == retail (the off271 line variant + the customer face/line); off271-START
  LCG is bit-identical (k2) so the variant DRAW reads the aligned value.  This is the user's "expression different".
- ✅ offer `b574 120→119` + outcome `b534 7-accept→8-PUSHBACK` == retail — but **COINCIDENTAL**: the stream is NOT
  re-aligned.  Per-frame LCG steps off271: **PORT draws 1, RETAIL draws 13** ⇒ the port is **−12 draws BEHIND from off271,
  CONSTANT through the offer** (retail +12 ahead off272-388; +9/+11 around the offer draw).  119 matches only because the
  −12 shift happens to land the same `init_eff % (2·random+1)`.  Anti-pattern guard (plan): a single matched value ≠ stream
  parity.
- **ROOT (real, pin-independent):** the port's **bg_npc respawn CLUSTER (~12 draws) is ABSENT @off271** where retail's
  fires (the shop-window NPCs walking off-screen + respawning).  The in-window re-pin was a +14 MASK over this −12 gap (net
  +2, mis-phased 1f).  bg_npc was bit-aligned off25-270 (drew nothing, just integrated positions) ⇒ at off271 retail's NPCs
  cross the respawn threshold but the port's don't → port's NPC positions diverge from retail's despite the bilateral
  `{bgnpcpin}` (off0).  Suspects: the {bgnpcpin} apply-timing (§21.4 ROOT-1b "1 tick late") OR a port bg_npc integration
  gap OR the known retail bg_npc run-to-run non-determinism (`openrecet_bgnpc_nondeterministic`).
- **NEXT (needs the bg_npc position compare):** instrument bg_npc x-positions + per-frame bg_npc rng-draws on BOTH, find
  where the port's respawn cluster lands vs retail's off271, align it (port the cadence / fix the pin-apply) so the stream
  is bit-identical → offer robustly 119.  **CHECKPOINT w/ user (bg_npc depth + the non-determinism question).**

### 21.11.2 ★★★ RESOLVED 2026-06-28 — the offer gap is the L90 PAUSE_CLOSE {rngseed} RE-PIN; the cs-walker "−12 @off271" + bg_npc were BOTH MISDIAGNOSES (destabilised-capture artifacts)

**Self-verified bilateral drill** (port 224108Z committed + 211003Z no-pin vs retail cache d43dafe9; entry-aligned
cc08==4&&b51c==0; the STABLE caprange-1500 captures, reproducible across 2 port drives; tool `/tmp/cs_offer_probe.py`
dumps b534/rngcalls/npc*/b548/b55c/b574/poseR entry-relative):
- **cs-walker is ALIGNED** — `npcdr=0` on the PORT for EVERY frame off265-393; npcfr/npcsp track retail 1:1; per-frame
  rngcalls Δ MATCHES retail off265-388 (incl. the +25 %8-sparkle spikes).  There is NO cs-walker burst anywhere in the
  reaction window.  **The §21.11.1 "−12 cs-walker @off271" was read off the DESTABILISED wide-caprange+bg-probe capture**
  (the §20.1 worker-tail-race / CS-stall class).  In the stable captures there is no −12, no burst.  bg_npc also confirmed
  a NON-ISSUE ({bgnpcpin} 1:1 @off0, no respawn near off271).
- **★ THE BILATERAL TABLE (all 4 configs CLEAN-driven + self-verified; the offer is NOT a one-line fix):**
  |               | **L90 present**       | **L90 dropped**          |
  | port          | poseR **1**, off **120** | poseR **3**, off **119** |
  | retail        | poseR 3, off 119 (d43dafe9) | poseR 3, off **122** (c26f011f) |
  Dropping L90 FIXES the port VARIANT (poseR 1→3 == retail, the user's "expression different") + makes the port match the
  RECORDING's offer 119 — **but the OFFER VALUE still does NOT bilaterally match a FRESH retail (119 vs 122)**: retail is
  NON-DETERMINISTIC run-to-run (119/117/122; §20-CORRECTION), and L90 was PINNING RETAIL to the recording's 119.  So
  neither bilateral config is a clean deterministic match: with L90 → port 120 ≠ retail 119; without L90 → port 119 ≠
  retail-fresh 122.  (The earlier "drop L90 ⇒ robustly 119" read compared the L90-DROPPED port against the L90-PRESENT
  cached retail d43dafe9 = both pinned-to-recording 119 — a config mismatch, not a true bilateral.)
- **WHY the re-pin perturbs only the PORT (the real root):** the port carries a CONSTANT cumulative-phase offset (abs
  rngcalls port ~+3536 vs retail, from the skipped intro + the g_sim sparkle ORIGIN) ⇒ its absolute LCG seed at
  PAUSE_CLOSE differs from retail's captured 3464877067.  The bilateral {rngseed} re-pin force-resets BOTH sides to
  retail's value = a NO-OP for retail, a JUMP for the port (k0→k14) ⇒ skewed variant draw @off271 ⇒ poseR 1 ⇒ offer 120.
  (= §21.8 SEED-PIN SKEW.)  **The bilateral {rngseed} pattern ASSUMES both sides are at the same seed-state at the anchor;
  the port's 1f load-phase breaks that** ⇒ the port applies the pin 1f early (@off270 vs retail's k14 @off271-272) ⇒ +2
  draws ⇒ poseR 1.  But the re-pin is NOT removable either — it's the REQUIRED retail-determinism pin (the lint's `no-rngseed`
  ⇒ retail's sparkle/dust/bg-NPC RNG desyncs; drop it ⇒ retail floats 119→122).  The conflict is structural: the port needs
  L90 GONE (phase-jump), retail needs L90 KEPT (float).  Clean model: the ENTRY {rngseed} is bilateral (aligns both to the
  recording's seed), but IN-WINDOW re-pins should be RETAIL-ONLY — the harness has NO target-scoping for {rngseed} (it is
  bilateral by design, `frida_capture.py:476`).
- **Residual (accepted — load-origin phase): b534 2→6 (the offer reaction) fires 1f EARLY on the port (off389 vs
  off390).**  Transition logic is BIT-1:1 (decompile FUN_004658ab @62489: `(DAT_0730b55c!=0)&&((DAT_073dddd4&0x10)!=0)`
  ⇒ b55c=0; b534=6 — port customer_service.c:1306 identical).  b55c=1 (line revealed) on BOTH since off384, so the gate
  is PURELY the Z down-edge.  The port hits PAUSE_CLOSE (the greeting b150 close, the SAME Z-advance) 1f early ⇒ the
  frame-relative offer-Z (trace L93 `frame+119`) lands 1f early ⇒ 2→6 1f early.  = the known +1f arrival/load-origin
  phase (cc08 d3e load); offer VALUE + variant + OUTCOME (→6 reaction, pushback) all match.  A targeted g_sim/load
  phase-pin (sim_phasepin, §20) could zero it (future, if the user wants the reaction-start bit-aligned too).
- **OPEN DECISION (escalated to the user 2026-06-28) — three ways to a deterministic bilateral 119/poseR3 match:**
  (1) **Fix the port's 1f PAUSE_CLOSE phase** (the root) — find why the port reaches PAUSE_CLOSE/the b150 close 1f before
  retail (a §21.10.1-family load-frame off-by-one? or the legitimate skipped-intro g_sim origin?).  If fixable, the
  bilateral L90 applies correctly ⇒ port-with-L90 = poseR3/119 = retail; bilateral-philosophy-clean.  (2) **Target-scoped
  retail-only in-window re-pin** — add `{rngseed,target:retail}` to the harness; port deterministic (no in-window corruption)
  + retail pinned ⇒ both 119/poseR3.  Small tooling add, but asymmetric (vs the user's bilateral-pin preference, cf
  `openrecet_bgnpc_nondeterministic`).  (3) **Full g_sim/wall-clock foundational pin** (user directive, §20/CLAUDE.md) —
  pin the g_sim/sparkle/load ORIGIN bilaterally so the port's whole phase matches retail; broadest, fixes this + future
  phase residuals; biggest effort.  **State (UNCOMMITTED, reverted to clean):** trace L90 RESTORED (= HEAD); the TEMP
  `*_dbg`/bg probes removed; the cs-walker debunk + this table are the durable findings.  cs-walker arc CONFIRMED 1:1
  regardless (no port gap there).  **Lesson: a value/phase forensic must run on the STABLE capture — the bg-probe's own
  destabilisation manufactured the −12 it was added to find** (`feedback_subagent_parity_exact_caution` / "diff-before-
  theories"); and **always re-verify a "fix" against a FRESH same-config bilateral, not a cached opposite-config side**
  (the drop-L90 "119" was a config-mismatch read).

### 21.11.3 ★★★ RESOLVED 2026-06-28 (user chose option 1) — the port's 1f PAUSE_CLOSE was a REAL b150-clear off-by-one; deferring it 1f gives a CLEAN bilateral 119/poseR3 under the existing L90

**The 1f phase is a PORT BUG, not the skipped-intro g_sim phase.**  `anchor_drift` measurement (port 230726Z vs retail
d43dafe9): CUSTOMER_SERVICE_ENTER is consistently ENTRY−1 on BOTH sides ⇒ the frame-END anchor sampling is SYMMETRIC (not
the cause); b534 0→1 and 1→2 are bit-aligned (+199/+270 from ENTRY both) ⇒ the greeting reveal is fine.  The ONE divergence
is **`PAUSE_CLOSE − b534_1to2`: PORT −1, RETAIL 0** (and the b534==2 dwell 119 port / 120 retail).  Root: the port cleared
`s_skip_modal` (the held ESC-skip b150 model) **INLINE at the b534=1→2 transition** (customer_service.c:1651), but **retail's
b534 1→2 (all.c:60409-425) has NO FUN_00435612 — retail clears b150 via the choice-box system the frame AFTER**, so retail's
PAUSE_CLOSE lands ON the b534==2 frame.  The port's inline clear fired PAUSE_CLOSE 1f early ⇒ the input-replay's frame-relative
offer-Z (trace L93 `frame+119`) AND the L90 {rngseed} re-pin (applied right after PAUSE_CLOSE) both landed 1f early ⇒ the
re-pin jumped the seed BEFORE the +2 natural draws ⇒ k16 not k14 ⇒ poseR 1 / offer 120.

**FIX (committed):** defer the `s_skip_modal=0` clear by 1 frame — move it from the b534=1→2 transition (master tick) to
cs_live_machine's FIRST b534==2 frame (`if (s_b544==1)`, the frame after).  Now PAUSE_CLOSE fires on the b534==2 frame like
retail; the re-pin applies @off271 AFTER the +2 draws ⇒ the variant draw reads the aligned k14 ⇒ poseR 3 ⇒ offer 119.
RNG-neutral (a tool-latch defer; s_skip_modal only feeds the PAUSE anchor, never the sim).

**✅ VERIFIED bit-identical (fixed port WITH committed L90 vs retail cache d43dafe9, entry-aligned cc08==4&&b51c==0):**
offer `b574 120→119` == retail; variant `poseR 1→3` == retail; b534 2→6 `off389→off390` == retail; PAUSE_CLOSE off269→off270
(= the b534==2 frame, == retail).  **699 overlap offsets, ZERO per-frame rngΔ mismatches** (was 4 @off389-391) — the WHOLE
first-customer window (wrap-up → offer → round 2) is now BIT-IDENTICAL under the bilateral L90.  3372 host pass; no regression
(the confirmed-1:1 wrap-up/arrival is BEFORE PAUSE_CLOSE, untouched).  **The bilateral {rngseed} now works as designed (no
target-scoping needed) — option 1 (fix the port) was the right call.**  cs-walker-rng-phase PORT-DEBT + the offer arc are
CLOSED.  PENDING USER STUDIO CONFIRM (v3 win 580:620 join CSE — offer 119 + reaction render 1:1).

## 21.12 ★★★ The cc08 haggle HAND CURSOR ported + USER-CONFIRMED 1:1 2026-06-28 (`9f6c19e`)

The shared menu hand-pointer never drew in the cc08==4 haggle prompt.  TWO gaps (both grounded in the
decompile, no guess):

**(1) the top-level DRAW was skipped.**  Retail draws the cursor `FUN_00435747()` at the TAIL of the
house render aggregator FUN_0040a765 (all.c:7498 / LAB_0040c1e4), AFTER everything incl. the cc08
overlay (FUN_0046602e→FUN_00466b7b) — it is ungated and self-gates on the visibility flag DAT_0438b150.
The port's `scene1_hud_render` (the FUN_0040a765 tail) called the overlay last but never the cursor.
The driver `title_save_dialog_cursor_render` was ALREADY ported (nowloading.tga cell {192,0,232,40},
40×40, drawn at (shake_x − |sin(b154·0.1)|·8, shake_y − 20)); only the call site was missing.
**Fix:** append `title_save_dialog_cursor_render(dev)` as the last statement of `scene1_hud_render`.

**(2) the machine's show/hide/snap/slide were stubbed `PORT-DEBT(cs-cursor)`.**  Map (engine → port):
- SCRIPTED FUN_00461c00: `FUN_00435612` @59967 (b608==3 edit → HIDE), `FUN_00435693(0x43400000,
  b540·0x30+386)` @59985 (offer → SNAP to the Yes row, also shows), `FUN_00435612` @~59955 (b608==4
  poll==1 commit → HIDE).
- POLL FUN_004622d9 @60068: up/down toggles b540 then `FUN_00435710(0x43400000, b540·0x30 + (b5a8==3 ?
  210 : 386))` (the 6-frame ease SLIDE between Okay!/Start-Again; b5a8==2 sell ⇒ base 386).
- LIVE FUN_004658ab: `FUN_00435612` @62494 (b534==2 greet → HIDE), @62515 (b534==6 edit → HIDE),
  `FUN_00435693(0x43400000, local_c+386)` @62526 (b534 6→0xf → SNAP), `FUN_00435612` @62659 (b534==0xf
  commit → HIDE).  (The `FUN_0043561a` @62484 SHOW is inside the FUN_004681e6 detail-card overlay =
  PORT-DEBT(cs-details-overlay), inert in steady state — NOT one of these.)
`0x43400000`f = 192.0; `0x30` = 48.  At each snap b540 is freshly 0 ⇒ y=386 (Okay!/Yes); the poll
slides to 434 (Start-Again/No) on the first up/down.

**rng-safe:** the cursor driver draws NO rng.  Re-driven `house-firstcust-arrprobe` (win-0-1500, --state,
join CSE) shows the rng VALUE bit-identical at the decision (b574=119, poseR=3 == retail; only the
constant intro-skip rngcalls phase differs).  **✅ v3-verified** (orv3_shot, decision frame port#1062 /
retail#1162): the white hand renders pointing at "Okay!" 1:1 with retail.  +host test
`cs_cursor_snap_and_slide` (drives the scripted offer → asserts HIDE during edit, SNAP visible to (192,386)
on the offer, SLIDE to (192,434) on up/down; 3373 pass).  PORT-DEBT(cs-cursor) retired (the SE 0x143/0x146
audio stays as cs-offer-fx / cs-poll-fx).  **USER-CONFIRMED 1:1 2026-06-28 ("can confirm the cursor matches").**

**NEW lead surfaced on this drive (≈ FRONT gap (i)):** the b534==6 REACTION line VARIANT diverges — port
renders recette msg09 "Capitalism, ho!" vs retail "How much should I?..." (the two rand%2 variants of
msg09).  NOT a seed gap: rng is bit-identical at every b534==6 reaction-ENTRY (the `cs_pick_line(0,9,0)`
frame), yet the chosen text differs ⇒ a VARIANT-ORDERING / draw-COUNT mismatch in `cs_pick_line`
(FUN_00460a1a) or the parsed `customer_dialogue` records.  Next arc.  **→ RESOLVED in §21.13 — the lead's
"NOT a seed gap" premise was WRONG (a config-mismatch read); it IS the same retail-roll non-determinism.**

## 21.13 ★★★ RESOLVED 2026-06-28 — the msg09 REACTION-variant "divergence" (FRONT gap iii) is a CONFIG-MISMATCH artifact, NOT a port bug; the variant LOGIC is bit-identical 1:1

**The §21.12 lead was a config-mismatch read** (the exact §21.11.2 lesson: "re-verify against a FRESH
same-config bilateral, not a cached opposite-config side").  Ground truth, with a NEW probe **`b5e0`**
(`DAT_0730b5e0` = the `cs_pick_line`/FUN_00460a1a chosen variant; port emits via `CALL_TRACE_I32`, retail
via `retail_fields.json`; rng-neutral — a static mirror + accessor + one trace field):

- **The picker is `rand % count[type]`, NO hidden re-roll** (decompile FUN_00460a1a @58792-93 transcribed:
  one `thunk_FUN_005041f6() % *(count+type*4+0x6df8)`).  The `DAT_073dddb8` scripted-override branch
  (58783-91) is **OFF in normal play** — the shipped `data/buysell.txt` COMMENTS OUT its `ok:` activator
  (`/ok:`, skipped at the 74749 `/`-guard) ⇒ `DAT_073dddb8==0` ⇒ always the rng branch.  So the variant is
  a PURE function of (rng-value, count).  `recette.txt` msg09 = exactly 2 variants in file order:
  **variant 0 `msg09:04:s10:How much should I?...`, variant 1 `msg09:00:s10:Capitalism, ho!`** (count[9]=2,
  parse-order identical port↔retail).  ⇒ same rng-value + same count ⇒ same variant.  There is NO
  variant-ordering/draw-count puzzle.
- **Committed port (fresh drive, current build), L90 present — DETERMINISTIC:** greeting **120f (b544=119)**,
  offer **119**, poseR **3**, reaction **b5e0=1 ("Capitalism, ho!")**.
- **Bilateral vs retail cache d43dafe9 (L90 present):** rng VALUE **BIT-IDENTICAL at every frame** through
  the reaction (off −3..+5: 242031234 / 527423293 / −591770356 / 863977872 / 973298826 / 1280258341 / …),
  per-frame rngcalls Δ identical (the +4 offer, +2 pick, +25 %8-sparkle).  ⇒ retail d43dafe9 draws the SAME
  `r` at the pick ⇒ **retail d43dafe9 variant = 1 = port** (= §21.11.3's "699 offsets, ZERO rngΔ" extended
  to the variant draw).  **The port matches the L90-pinned reference BIT-FOR-BIT incl. the variant.**
- **Bilateral vs cache c26f011f (L90-DROPPED):** rng DIFFERS at every frame (offer **119 vs 122**) ⇒ a
  DIFFERENT `rand%2` roll ⇒ variant 0 "How much" = **the line the §21.12 lead saw.**  The lead compared the
  committed **L90-present port** against the stale **L90-DROPPED c26f011f retail** = config mismatch.
- **VERDICT:** the reaction-variant logic is **confirmed 1:1** (same `rand % 2` off the same bit-identical
  stream as retail's L90-pinned reference).  The visible "Capitalism vs How much" is **retail's OWN run-to-run
  `rand%2` non-determinism** (the §21.11.2 119/117/122 float, same root), which the port faithfully
  reproduces — NOT a logic gap.  A DETERMINISTIC bilateral variant-match vs a FRESH retail is the SAME
  open §21.11.2 decision (option 2 retail-only re-pin / option 3 full g_sim+wall-clock pin); the committed
  config already matches the cached L90 reference (d43dafe9) + the user's "aligned up to haggle prompt".
- **(infra aside)** a fresh `--target retail --frida-remote` drive HUNG at LOADING_START@206 (load never
  completed, 0 frames) — a transient early-load stall on the remote retail; the d43dafe9 cache (a clean prior
  capture) carries the proof, so the conclusion doesn't depend on it.  New permanent probe `b5e0` retained
  (closes the variant blind spot in the v3 state panel / flow_diff).

## 21.14 ★★★ VERIFIED 2026-06-28 (user chose "determinism foundation") — the committed config is RUN-TO-RUN DETERMINISTIC; the offer+variant ALREADY match a FRESH retail bit-for-bit. §21.13's "open §21.11.2 decision" is CLOSED for this scenario.

The user picked the determinism-foundation arc to get the offer AND variant matching a FRESH retail (not just the
cached d43dafe9).  **Empirical test (the §21.11.2 "fresh same-config bilateral"): TWO independent fresh retail
drives** (`orv3_window … --window 0:1500 --state --force-retail`, committed L90 config — the lean path; the heavy
`--call-trace` full-1979-VA path starves the load worker → the LOADING_START@206 hang, NOT a retail bug):
- **retail#1 vs retail#2: 700/700 rng VALUEs BIT-IDENTICAL** over rel[−200..+500] around the reaction; both
  **offer 119, b5e0=1 "Capitalism", rng@pick=973298826.**  (Absolute reaction frames 3700 vs 3627 — a 73-frame
  load-duration drift — but the ANCHOR-RELATIVE stream is identical; the v3 identity-join (anchor#occ, offset)
  is load-stretch-immune by design, so this is a non-issue.)
- **retail#1 vs the committed port: 46/46 rng bit-identical** over the reaction window; same b534/b574/b5e0=1.
- ⇒ retail is run-to-run deterministic in the committed config AND the port matches it bit-for-bit, **incl. the
  directly-captured variant b5e0=1.**  So the offer+variant DO match a fresh retail deterministically.

**WHY it's already deterministic (the foundation was built incrementally, not in one "wall-clock pin"):** the §20
"117 float" was the csloadpin worker-tail re-arm RACE — FIXED §20.1 (all 4 brackets arm); §21.9 made the port's
d3e load SPAN the load so the load-anchored {rngseed} re-syncs; §21.11.3 fixed the port's 1f PAUSE_CLOSE phase.
With those + the trace's pin stack ({rngseed}×26 / {csloadpin:24} / {gsimpin} / {bgnpcpin}) the anchor-relative
rng stream is bit-identical bilaterally.  The §21.11.2 "122 float" was the L90-DROPPED experiment (not the
committed config); the §21.13 "Capitalism vs How much" was the L90-DROPPED c26f011f cache (config mismatch).

**Remaining (OPTIONAL, the user's call — NOT needed for offer/variant correctness):** the CLAUDE.md WALL-CLOCK pin
(hook QPC/GetTickCount/timeGetTime → a virtual clock synced at anchors) would additionally pin the ABSOLUTE
load-duration drift (the 73-frame retail#1↔#2 spread) so the two sides share absolute frame numbers + time-based
anims are deterministic.  The port already virtualizes the clock for the MAIN-TICK dispatch only (`g_turbo_virtual_now_ms`,
tick.c:206); `tick_now_ms()` (FUN_0047be2f QPC) and worker/timer reads still see the real clock.  But the
v3 anchor-relative identity-join already absorbs the absolute drift, and the offer/variant/rng are bit-identical
anchor-relative ⇒ the wall-clock pin is a robustness/cleanliness improvement (and would let some ad-hoc load pins
retire), NOT a correctness requirement here.  **VERDICT: gap (iii) + the offer are CONFIRMED 1:1 vs FRESH retail,
deterministically — done.**

## 21.16 ★★★ FIXED 2026-06-30 — the cc08 d3e LOAD ran +4f vs retail (racy CreateThread vs frida-pinned) = the dominant free-roam phase drift (notes #20/#21)
**Symptom:** the two remaining viewer notes on `house-firstcust-arrprobe` — #21 "tear wing flap phase" (HOUSE_FREEROAM#5+39),
#20 "customer walk phase" (HOUSE_FREEROAM#6+153) — are the free-running anim counters (companion `cframe`, the chibi
walk-cycle) carrying a phase offset.  Both live in free-roam, downstream of the wrap-up.
**Measurement (anchor_drift, port↔cached retail d00f5a90, aligned at CSE#1):** the drift is a CONSTANT **+4** from the
FIRST anchor after entry (LOADING_END#2) and ~constant through the window, settling to **+3** in free-roam (HF#5/#6).
NOT gradual accumulation — it appears whole at the first load.
**Root (ground-truth b1cc timeline both sides):** the cc08==4 d3e customer-asset load ran **28 frames** on the port
(b1cc 2→1 at off 28) vs retail's **24** (off 24).  The port's d3e load is a **real `CreateThread`** (worker_load.c
`sec_spawn_common`); its completion (`g_worker_sec_state_1cc` 2→1, written by the worker's post_body) lands whenever the
OS schedules the thread — **racy, non-deterministic run-to-run** (the §21.10 "loads are deterministic 24f" was a LUCKY
sample; the arrprobe drive got 28).  `{csloadpin:24}` only set a MINIMUM on the port (the release gate ANDs
`load_pin_elapsed()` with the racy `1cc != 2`), so the 24-min never clamped the 28; retail is frida-HELD to exactly 24
(`openrecet-agent.js:3151` worker-tail blocker).  ⇒ the +4 surplus, and it's the SAME root as §21.9's incompleteness
(the load-anchored `{rngseed}` re-pin lands at the port's off 28 vs retail's off 24 unless the load durations match).
**Fix (`worker_load_force_d3e_complete` + the `scene1_player_ctrl` release bridge):** when a pin is active and the gate's
pinned frame N is reached with the worker still pending (`1cc==2`), FORCE the worker to completion — spin (Sleep(0)
yield) on the COMPLETION FLAG `g_worker_sec_state_1cc`, **never `g_worker_handle`** (the thread self-closes it in
`secondary_thread_cleanup` → use-after-close).  The body work is ms, so the spin returns in a few scheduler slices with
the assets actually in; the load then clears DETERMINISTICALLY at exactly N = retail's pinned N.  **Unpinned**
(`customer_service_load_pin_active()`==0, `load_pin_elapsed()` always 1) it reduces to the original `1cc != 2` async
release — zero behaviour change in normal play (harness-only).  Non-Win32 (host): no thread → no-op.
**✅ VERIFIED (fixed port vs cached retail d00f5a90):** LOADING_END#2 **+4→+0**; the WHOLE pre-wrap-up region
(LOADING_END#2/#3, HF#2/#3, PAUSE_OPEN#1 = the offer decision, LOADING_START#4, CONV_POSE_START#1, the BLINKs,
TEXT_ANIM_START#1) now **frame-aligned +0**.  Offer `b574=119` + variant `b5e0=1` (+ round-2 150) **bit-identical**
both sides; `cs_walker_drill` **1/200 rngΔ, 0/200 gsim%8** (= the pre-fix baseline — rng-neutral); 3375 host pass.
**Residual:** free-roam +3→**−1** (not 0).  The remaining −1 is the WRAP-UP cutscene running ~1f SHORT: `CONV_POSE_END#1`
lands at port off 520 / retail off 522 = **−2** (the DLG_LINE_CLEAR#1→CONV_POSE_END#1 final beat is 0f on the port vs
retail's 2f), partly compensated by the post-wrap-up LOADING#5 (+1) to −1 in free-roam.  = the §21.10 "task #2 / wrap-up
runs short" segment — the NEXT chip to drive notes #20/#21 to drift 0.  (NB this is a REAL port duration gap, the §21.10
methodical "fix the 1f-short fade/cutscene" lever, NOT a harness pin.)

## 21.17 ★★★ FIXED 2026-06-30 — the §21.16-residual −1 = the ESC-skip teardown BYPASSED the D_TUT_DONE settle ⇒ the whole post-wrap-up region ran 1f ahead
After §21.16 the pre-wrap-up region was +0 but the FREE-ROAM region (notes #20 walk / #21 wing-flap / #11 window NPCs)
sat at a uniform **−1** (port 1f AHEAD); the user confirmed all three still visibly desynced.
**Localisation (port↔retail, d00f5a90):** every first-customer `b534` haggle transition was **+1 on retail** (port 1f
early) ⇒ a single uniform −1 across the WHOLE post-wrap-up region, rooted at the cc08 leave/dissolve.  The leave KICKS at
the SAME off both sides (`b520` 0→1@237, 1→2@238 — the fade kick aligned) but COMPLETES (`b520` 2→0) at port **520** vs
retail **521**.  The dissolve condition (`b5c0==0 && 0xf0<b5b4 && b520==1`, decompile all.c:60325) + the fade (`fade_is_done`
= `counter==duration`, exact 90f) are FAITHFUL, and the fade finished ~off 328 — so the completion is NOT gated on the fade
but on the **master tick RESUMING when the iv1_7 wrap-up cutscene ends** (it self-`return`s while the dialogue is `_busy()`).
The cutscene ended 1f early ⇒ master resumed 1f early ⇒ everything downstream −1.
**ROOT:** the wrap-up is ESC-SKIPPED; `scene1_intro_dialogue_skip_to_end` for a RUNNING `D_TUT` (iv1_5/6/7) went straight
to **D_IDLE**, BYPASSING the 1-frame **D_TUT_DONE settle** that the NATURAL `g_rt.complete` path (D_TUT→D_TUT_DONE, line 224)
includes — the engine's `DAT_0438b1c8` gate clears 1→0 only the frame AFTER `FUN_0044bd0d`, so `_busy()`/`_posing()` must
hold one more frame.  The skip dropped it ⇒ `_busy()` cleared 1f early ⇒ master tick resumed 1f early.
**FIX (`scene1_intro_dialogue.c`):** split the skip teardown — `D_TUT` → **D_TUT_DONE** (the settle), `D_TUT_LOAD`/`D_TUT_DONE`
→ D_IDLE (no script ran / already settling).  Mirrors the natural-completion settle + retail's gate lag.
**✅ VERIFIED (re-drive d00f5a90):** LOADING_START#5 / CSE#2 / LOADING_END#5 / **HOUSE_FREEROAM#5 / #6** / PAUSE_OPEN#2 /
PAUSE_CLOSE#1/#2 all **−1→+0** — the WHOLE post-wrap-up + first-customer region frame-perfect.  Offer `b574=119` + variant
`b5e0=8` bit-identical; `cs_walker_drill` **1/200** rngΔ, 0/200 gsim%8; 3375 host pass (no prologue-skip regression).
**Residual:** `CONV_POSE_END#1` still **−1** (port 521 / retail 522) — the POSE's final frame ends 1f before retail's
(the conv-pose teardown wants a 2nd settle, vs the dialogue's 1).  Absorbed for the free-roam ANIM phase (HF#5/#6 = +0), but
CONV_POSE_END is the anchor the **`{bgnpcpin}`** rides ⇒ the window bg_npc (#11) may still be 1f off — chase next.

## 21.18 ★★★ FIXED 2026-07-01 — CONV_POSE_END −1 = the iv1_7 conv-pose released the SAME frame as the f406 first-customer entry; retail holds it 1f PAST
**Setup:** after §21.17 the post-wrap-up region is +0 EXCEPT `CONV_POSE_END#1` (port off 521 / retail 522 = −1).  A reverted-§21.17
re-drive (d00f5a90) confirms it is FULLY DETERMINISTIC — LOADING_START#5/CSE#2/HF#5/#6 +0, CONV_POSE_END −1 reproduced — so NOT a
cad868 race.
**Structure (retail anchors):** DLG_LINE_CLEAR#1@520 → LOADING_START#5 + **CUSTOMER_SERVICE_ENTER#2**@521 (the **f406 first-customer
entry**, cc08 1→4, `player_ctrl_cc08_f406_entry`, in the cc08==1 FREEROAM arm) → CONV_POSE_END#1@522 (1f AFTER the entry).  So retail
holds the wrap-up conv-pose ONE frame PAST the f406 entry; the port releases it AT the entry.
**Root:** `_posing()` drops at off 521 (D_TUT_DONE→D_IDLE, the iv1_7 ESC-skip teardown).  conv_pose (driven by `_posing()`, ticked
BEFORE player_ctrl_tick) releases the player record state 6→0 the SAME frame the f406 entry fires ⇒ CONV_POSE_END AT the entry (521).
Retail's pose-release is the talk-event flag DAT_0450f470 (FUN_004852fb sets it at the cc08 entry/transition); the conv-pose driver
reads it the NEXT frame ⇒ release at entry+1 (522).  The cc08==4 arrival_tick (f406 path, f405 unset ⇒ arriving branch anim 5) takes
the actor over at 522 IF the pose survives 521.
**Why a §21.17-style "2nd settle" REGRESSES it (TRIED + REVERTED):** holding `_posing()` one more frame (a D_POSE_SETTLE state) ALSO
delays the f406 entry — the freeroam arm (where the entry lives) is gated `cc08==1 && !_active() && !_loading() && !_posing()`
(`scene1_player_ctrl.c:2360`), so a held `_posing()` runs the unported arm instead ⇒ LOADING_START#5/CSE#2 + the WHOLE region slip
+1 (HF#5/#6 +0→+1, a USER-VISIBLE regression; measured: leave + CONV_POSE_END both → 522, still coupled).  **[Corrects §21.17's
"master tick gated on _busy()": the post-wrap-up transition that LOADING_START#5/CSE#2 actually track is the f406 ENTRY (freeroam
arm, gated on !_posing()), NOT the earlier b520 leave.]**
**Fix (`scene1_conversation_pose.c` + new `player_ctrl_cc08_f406_pending`):** DECOUPLE — hold the conv-pose STATE (the LOCAL `posing`
in conv_pose_tick) one frame WITHOUT touching the GLOBAL `_posing()`.  When `!_posing() && s_pose_active && player_ctrl_cc08()==1 &&
player_ctrl_cc08_f406_pending()`, force the local posing=1 for that ONE frame.  `cc08==1 && f406-pending` is true ONLY at the entry
frame (conv_pose runs BEFORE the entry flips cc08 1→4; next frame cc08==4 drops the hold + the arrival_tick anim-5 takes over ⇒
CONV_POSE_END@522).  `_posing()` STILL drops at 521 ⇒ the freeroam arm + f406 entry fire on time (LOADING_START#5 +0).  Scoped to
f406 (only iv1_7 sets it) ⇒ the iv1_5/iv1_6 inter-dialogue pose blip (§21.10) + the prologue are untouched.  RNG-neutral (conv_pose
draws no LCG; the 1-frame hold only advances the rng-free blink anim).  +host test `cs_f406_pending_is_pure`.
**✅ VERIFIED (re-drive d00f5a90):** `CONV_POSE_END#1` **−1→+0** (522/522); LOADING_START#5/CSE#2/HF#5/#6/PAUSE_OPEN#2/PAUSE_CLOSE#1/#2
+ EVERY anchor in the window **+0** (frame-perfect); cs decision fields bit-exact (offer b574=119, poseL/poseR/b53c/b5d0/b59c/b5d8/
b1cc/npcfr/npcsp); post-pin bgx0 BIT-IDENTICAL; 3376 host pass.
**★ NEW finding (UNMASKED) — the gsim/sparkle freeze = the next #17 chip:** with CONV_POSE_END now at the RIGHT frame (522), the
`{gsimpin}`/`{bgnpcpin}`/`{rngseed}` (all ride CONV_POSE_END) land at 522 — `{gsimpin}` (PORT-ONLY, pins the port's gsim to retail's
recorded value) now at 522 like retail (was port 521).  This EXPOSES a pre-existing port bug the old CONV_POSE_END−1 pin
ACCIDENTALLY MASKED: **the port short-circuits `sim_step_a` (returns before `g_sim_frame_count++`, sim.c:492) for 1 frame at off
523 — gsim freezes 810→810 while retail increments 810→811 ⇒ port gsim runs 1 BEHIND from off 523 ⇒ gsim%8 (the 目玉 sparkle phase,
#17) mis-fires.**  cs_walker_drill: the §21.16/17 "0/200 gsim%8" was the early pin (CONV_POSE_END−1=521) CANCELING this freeze (now
53/200 rngΔ, 199/200 gsim%8 — all the bg_npc/sparkle/#17 region, the cs decision stays bit-exact).  Root TBD: `session_init` spawns
only the d3e SECONDARY worker (no primary), and no primary-worker spawn (0x452cde) is captured at the f406 entry — so the off-523
`worker_load_busy()`-style short-circuit source needs a fresh probe.  = the NEXT chip (#17 gsim-origin phase, now localized to the
f406-entry off-523 sim short-circuit).

## 21.19 2026-07-01 — the {gsimpin} was STALE (removed, a correct cleanup); but #17's VISIBLE sparkle is downstream of the rng-value foundation, NOT this pin (USER DISCONFIRMED)
**The §21.18 "sim short-circuit" hypothesis is DEBUNKED (probe + code, per the porting loop — not a guess):**
- **No primary worker at the f406 entry.**  `sim_step_a`'s ONLY early-return (the one that skips `g_sim_frame_count++`, sim.c:275)
  gates on `worker_load_busy()` = the PRIMARY flag `g_worker_busy` (DAT_06a49954).  The cc08 d3e load is the SECONDARY worker
  (`g_worker_busy_secondary`, DAT_06a4995c) — it does NOT gate that early-return.  `session_init` (FUN_0045edaa) spawns ONLY
  `worker_load_spawn_d3e(0)` (secondary); no primary `worker_load_spawn` (0x452cde) fires at the entry (all 6 primary spawn sites
  grep-verified: worldmap-exit / pause case-9 / title — none in the cc08 path).  ⇒ `worker_load_busy()`==0 through the entry ⇒ NO
  short-circuit ⇒ gsim++ DOES run on the entry frame.  The "freeze" was never a sim skip.
- **The real cause = the `{gsimpin:[0,810]}` riding the CONV_POSE_END anchor.**  The pin fires at **input-poll (pre-sim)**
  (`segtrace_input_poll` → `input_segtrace_tick` → `fire_gsimpins`), so it OVERWRITES `g_sim_frame_count` before that frame's 0x48670f
  probe reads it.  cs_walker_drill on the COMMITTED (pinned) port: off0 (the f406 entry, cc08==4 b51c==0) port==retail==810 NATURALLY
  (the pin has not fired — it rides CONV_POSE_END = off1, the frame AFTER the entry per §21.18); at off1 the pin forces port **810**
  while retail is **811** ⇒ port 1 BEHIND from off1 on.  The pin VALUE (810) was calibrated when CONV_POSE_END sat at the f406-ENTRY
  frame (drill off0, gsim 810 — the PRE-§21.18 CONV_POSE_END−1 position).  §21.18 correctly moved CONV_POSE_END +1 (to off1, gsim 811),
  so the pin now fires 1f late with a now-stale value = over-correct −1.  The §21.16/17 "0/200 gsim%8" was the pin AT off0 matching the
  natural value (a no-op that LOOKED like it pinned); §21.18 exposed it was never correcting a real offset.
- **Proof (no-gsimpin port drive vs the d00f5a90 retail cache, `cs_walker_drill --span 200`):** removing the pin ⇒ port gsim
  **BIT-IDENTICAL to retail** — **0/200 gsim%8** diverge (was 199/200), and the per-frame 25-draw rng burst realigns to the SAME
  offset both sides ⇒ **3/200 rngΔ** (was 53/200).  A mid-game save-load has **NO gsim origin offset** — the port doesn't skip an intro
  here; both sides count `g_sim_frame_count` from the cad868 scene load, and the loads are `{csloadpin}`/`{tutloadpin}`-deterministic —
  so the gsimpin's stated premise ("the g_sim origin differs port↔retail, the port skips the intro") is FALSE for this trace; the pin
  was always redundant.
**FIX (TRACE/TOOL, no port-code change):** removed `{gsimpin:[0,810]}` from `house-firstcust-arrprobe` + `house-firstcust-cutscene-day2`
(replaced with a `#`-comment breadcrumb: do NOT re-add — #17 is a NO-PIN natural match).  The port's `g_sim_frame_count` LOGIC is
confirmed correct (naturally == retail).
**Residual (3/200 rngΔ, DOWN from 53):** the in-shop chibi `npcn` (active-NPC slot count) reads 1 where retail reads 0 around off≥190 =
the pre-existing `PORT-DEBT(cs-walker-rng-phase)` (the walk-cadence velocity-input phase, RE #14 residual (2)), SEPARATE from #17 and
untouched here.
**★ USER DISCONFIRMED 2026-07-01 (studio re-drive 361eb3ce) — the gsimpin removal is a CORRECT cleanup, but it does NOT deliver the
VISIBLE #17 parity, and the aligning gsim%8 GATE was necessary-not-sufficient.**  The user re-flagged the fresh drive (rendered
port|retail|diff): note **#24** (sparkles diverge, PAUSE_OPEN#1+66) — the 目玉 sparkles fire at DIFFERENT POSITIONS port↔retail;
**#20/#22** (customer walk phase) — the chibi is at a DIFFERENT POSITION (not just a pose-cycle phase; #20 shows it lower-left vs
retail's centre); **#23** (npcs diverge, HOUSE_FREEROAM#1+53) — the pre-pin window NPCs.  **Root (`flow_diff --verdict` on 361eb3ce):
the rng VALUES are NOT bit-exact** — `rngcalls DESYNC +1766 net, first @frame 3`; `bgx0..5 DRIFT from frame 1 (spread up to 20.7)`;
raw rng state only **336/407** frames bit-exact (npcfr/npcsp/poseL/poseR/b53c/b1cc + the cs decision stay ALIGNED bit-exact — the
divergence is the AMBIENT NPCs/sparkle, not the haggle).  The sparkle POSITION (3 rng draws for the cell) + the chibi WALK depend on
the rng VALUES, which desync from the **PRE-PIN bg_npc = the cad868 PRIMARY-load non-determinism (the (b) item)**, and the un-pinned
chibi/sparkle ACCUMULATE the drift (only bg_npc is bilaterally pinned, at the LATE f406 entry).  ⇒ **#17 (sparkle) + the chibi walk
(#20/#22) + the pre-pin NPCs (#23) are ALL ONE root: the pre-pin rng-value foundation.**  The stale-gsimpin removal STAYS (correct — it
aligns the gate, rngΔ 53→3), but #17 is **REFRAMED as downstream of (b), NOT independently fixed**.
**NEXT = the DETERMINISM FOUNDATION:** extend the §21.16 force-complete pattern (which fixed the d3e SECONDARY load) to the cad868
PRIMARY worker — a BILATERAL primary-load pin (port force-complete + retail frida-hold to the same N) so the bg_npc gets a
deterministic frame-count and the rng stream is bit-exact FROM FRAME 1; the chibi + sparkle then follow.  (Alt: an earlier bilateral
`{bgnpcpin}` at the load, needs a frame-0 SoA recapture.)  This is the (b) arc — a fresh, substantial effort (frida-side primary-load
hold + port force-complete), a good /clear boundary.

## 21.20 2026-07-01 — `{primaryloadpin}` BUILT (determinism foundation, KEPT) but the §21.19 load-duration hypothesis is DISCONFIRMED; the real bg_npc root is the WARMUP LAYOUT (port-logic gap, next arc)
**Built (the §21.19 next-step, bilateral):** `{primaryloadpin:N}` — drain the cad868 PRIMARY worker (worker_load_spawn / FUN_00452cde, `g_worker_busy`
= DAT_06a49954) to a deterministic N frames.  Port: `worker_load_force_primary_complete()` (spin until g_worker_busy clears, mirror of
`worker_load_force_d3e_complete`) + a `worker_load_primary_pin_active/elapsed` counter + the drain bridge at the `sim.c` load gate
(`if (worker_load_busy()){ if(elapsed()&&active()) force_primary_complete(); pump; return; }`) + `input_segtrace`/`main.c` plumbing (reset the
counter in `worker_load_begin`).  Frida (`openrecet-agent.js`): a MAIN-THREAD drain — arm on the DAT_06a49954 rising edge in `segtraceTick`,
spin at `Present.onEnter` until 49954 clears (NO worker-tail CModule — the worker runs on its own thread, so the main thread just waits; the
d3e's extend-only worker-tail hold is impractical for the huge primary load, §21.2).  +`frida_capture.py` forward; +4 host tests (3380 pass).
**Verified MECHANICALLY (arrprobe, N=16):** retail replayed **bit-exact 1500/1500**; frida log `bracket armed at frame 207 (release at 222) …
drained at 222`; the load is deterministic and the **entry frames align 826==826** (were port 2309 / retail 3066 — a **757f CreateThread race**).
**★★★ DISCONFIRMED as the bg_npc fix — the §21.19 "load-duration non-determinism → bgx/rng drift" hypothesis is WRONG (per the porting loop:
probe, don't rationalize).**  An ENTRY-aligned probe on the pinned cache (48ae642d) shows the **raw rng state is BIT-IDENTICAL pre-entry**
(off −600/−400/−200/0 all MATCH) **yet bgx DIVERGES anyway** (port bgx0 walks 1.83→10.1→18.4, retail sits idle at −2.8).  ⇒ the load-duration
is **cosmetic** to rng/bg_npc: `sim_step_a` early-returns during the load (`sim.c:272`, `sim_loading_pump` is rng-free) so nothing rng-relevant
ticks, and the v3 anchor-join already absorbs the duration.  The "**+1766 rngcalls DESYNC**" (§21.19) was a **MEASUREMENT ARTIFACT**: retail's
deferred rng-caller-hook (§21.3) counts **0 pre-entry**, port counts from boot; post-entry the delta is a **CONSTANT +3536** and the per-frame
rng advance MATCHES (both +185/40f).  And `flow_diff --verdict --align-field db054` mis-aligns here (db054 PLATEAUS at 81 for ~500f → a bogus
81-frame window + spurious +1766) — **use the entry-aligned drill / probe, NOT the db054 verdict, for this scenario.**
**★★★ REAL ROOT — the bg_npc WARMUP LAYOUT.**  The 180× warmup (`FUN_0046f621`, `scene1_bg_npc.c`) builds the initial 6-NPC layout from the rng
state AT the warmup.  With **identical rng** the layouts still differ ⇒ the two sides warm up from a **different rng STATE at the warmup instant**
(no `{phasepin}` to canonicalize it to seed 19937 — neither arrprobe nor cutscene-day2 has one).  The port's NPC0 ends up WALKING where retail's
is idle: an initial-STATE gap (not tick-logic-with-rng, which would agree given the matched rng).  The `{bgnpcpin}` snaps only at off+1
(CONV_POSE_END), so the whole PRE-entry window stays diverged = viewer notes **#23** (window NPCs) / **#20**/**#22** (chibi walk), with **#24**
(sparkle) downstream.  The canonical fix — `{phasepin}` (warmup re-seed 19937, bilateral) — **breaks the skip-path wrap-up** (its bg_npc LCG
re-seed; CLAUDE.md "a TOOL gap to FIX"), and **arrprobe IS the skip path**, which is exactly why these scenarios use `{bgnpcpin}` instead.
**USER DECISION 2026-07-01:** (1) **KEEP** `{primaryloadpin}` as a determinism foundation (it pins a real non-det source — the racy CreateThread
load-duration — and aligns the entry frames; matches "pin EVERY non-deterministic source").  (2) Fix the real root by **PORTING THE bg_npc WARMUP
LOGIC 1:1** — find why identical rng yields a different warmup layout (a pre-warmup rng-sync gap, or a warmup-logic gap), fix the code so the
pre-entry bg_npc match with NO snapshot pin.  = the NEXT arc.  (`{primaryloadpin}` on cutscene-day2 still TODO — needs a verify drive.)

## 21.21 2026-07-01 — `{bgnpcseed}` BUILT+VERIFIED — the bg_npc warmup ORIGIN fix (closes the §21.20 next-arc lead)

**Per the porting loop (probe, don't guess):** extended `installBgNpcPinHook` (already-installed, unconditional) with a diagnostic —
log `DAT_006023a0`+`DAT_073a8bb4` (cursor) at the FUN_0046f621 entry gated on `DAT_073a8bb8==0` (the TRUE first-ever call, no
{calltrace}-window dependency, unlike the earlier port-vs-retail call_trace comparison).

**★ Finding 1 — the port=223/retail=224 timing read (§21.20 framing) was a MEASUREMENT ARTIFACT, not a real gap.**  Both sides'
bg_npc warmup fires on the SAME frame as LOADING_END/HOUSE_FREEROAM (223) — confirmed via the unconditional hook (`NATURAL
pre-warmup seed = 3502407629 @ frame 223`).  The earlier "port call_trace shows 0x46f621 @223, retail's only starts @224" read
was an artifact of the {calltrace} window's own boundary semantics: `call_trace.c`'s "trace everything" fallback (only window-gated
once a {calltrace} op ARMS a window) let the port's frame-223 entries through for free, while retail had no such fallback — NOT
evidence retail's warmup fires later.  Confirmed structurally too: `sim_step_a`'s `worker_load_busy()` early-return and retail's
own FUN_004536cb equivalent both gate the ENTIRE scene dispatch (incl. whatever calls the warmup) behind the SAME busy flag, so
both sides necessarily resume — and warm up — on the exact frame that flag clears.

**★ Finding 2 — the generic `{rngseed}` pin structurally CANNOT reach a same-frame consumer, on EITHER side.**  Retail's own log
proves it: `[anchor] LOADING_END @ frame=223` then `[agent] segtrace: forced rng seed = 912526909 at frame 224 (base+0)` — the
op's base IS the anchor's own frame (223, `base+0`), but it doesn't mechanically fire until frame 224.  Root: the anchor is
recorded post-sim (Present/render_dispatch, end of frame 223), so the {wait} can't resolve until the NEXT tick's pre-sim segtrace
pass (frame 224) — a hard 1-frame lag baked into "detect-post-sim, apply-next-pre-sim", identical on port (`input_segtrace_tick`)
and retail (`segtraceTick`/`anchorTick`).  Since the warmup consumes RNG ON frame 223 (Finding 1), the pin is always one frame
late for it — explains why §21.20's bit-identical-pre-entry-yet-bgx-diverges observation is NOT a contradiction: the shared
register re-syncs at the NEXT anchor regardless, but the 6 NPCs' own `speed`/`dir`/`mode` (rolled once, at the mis-timed warmup)
never get corrected by a later register re-sync.

**★ Finding 3 — retail's NATURAL pre-warmup state differs from the port's in TWO independent ways, not one.**  (a) **Seed**:
retail's natural value at the frame-223 entry is **3502407629** — NOT `912526909` (that's the LOADING_END-sampled, already-past-
this-point value) and NOT `3132701474` (the NEW_GAME `{rngseed}` pin's value, which is what the PORT's own pre-warmup state
equals unchanged — confirmed via the `0x48670f` probe's `rngst` field at frame 223, `-1162265822` signed = `3132701474`
unsigned).  ⇒ retail's REAL primary-load path consumes hidden RNG between NEW_GAME and the warmup that the port's
`sim_loading_pump()` doesn't replicate (RNG-stream-completeness gap; PORT-DEBT, not chased further this session — candidates:
shop/kyaku/news/order generation, per `scene1-rng-stream-parity.md`'s "0x49018c/0x490e56 cluster... new-game save/news/order
generation").  (b) **Spawn cursor**: retail's `DAT_073a8bb4` reads **1**, not 0, at the same instant.  A raw SoA dump (`0x073a7f80`,
hex over the Frida message channel) showed slot 0 already `STATE=-1, dir=0, type=14` with leftover x/y/z — **NOT** the product of
`bg_npc_spawn(0)` (which would set `type=BG_NPC_TYPE_TABLE[0]=0`, not 14) — so some EARLIER activity (a title-screen bg render of
the shop scene?) had already spawned-then-frozen slot 0 before scene1's own warmup ever runs.  `dir==0` is the permanent
"unspawned"/dead sentinel both `bg_npc_tick` (no position update) and the two renderers (`visible==-1 || dir==0` skip) treat
identically regardless of the stored x/y/z/type — so the leftover VALUES are behaviorally inert; only the CURSOR OFFSET (which
slot the warmup's real spawn sequence starts from) is observable.

**FIX — `scene1_bg_npc_seed_pin(seed, cursor)` + `{bgnpcseed:V}` / `{bgnpcseed:[V,C]}` (RE §21.21).**  Generalizes the ALREADY-
PROVEN `{phasepin}` consumer-latch pattern (a pending flag consumed INSIDE `scene1_bg_npc_tick()` itself, at the top, before any
RNG consumption — sidesteps Finding 2's frame-lag entirely since it doesn't ride the generic frame-counted op path) but WITHOUT
`scene1_bg_npc_phasepin()`'s `scene1_bg_npc_reset()` + the `{phasepin}` bundle's db054/anim/b154/rmb reset (the exact reason
{phasepin} "stalls the skip-path wrap-up cutscene" and couldn't be used on this skip-path scenario).
- **Port** (`scene1_bg_npc.c/.h`): `g_bg_npc_pin_seed`/`g_bg_npc_pin_cursor` latched by `scene1_bg_npc_seed_pin()`, applied inside
  `scene1_bg_npc_tick()`'s existing `g_bg_npc_pin_pending` branch (`rng_seed(seed); g_bg_npc_spawn_cursor = cursor;`).
  `scene1_bg_npc_phasepin()` now calls it internally (`SCENE1_BG_NPC_PHASEPIN_SEED`, cursor 0) — unchanged behaviour, no regression.
- **Trace grammar** (`input_segtrace.h/.c`): new trace-global `{bgnpcseed}`, scalar `V` (cursor defaults 0, back-compat) or array
  `[V,C]`, parsed like `{calltrace}`'s scalar-or-array form.  Wired in `main.c` at trace-load time (before the main loop — the
  pending latch just waits, immune to frame timing).
- **Retail** (`tools/frida/openrecet-agent.js`): `installBgNpcPinHook` (already always-installed for `{phasepin}`) extended with
  a parallel `g_segtrace_bgnpcseed_active`/`_cursor` latch, applied at the SAME `FUN_0046f621` onEnter, gated on the SAME
  `DAT_073a8bb8==0` — forces `DAT_006023a0` + `DAT_073a8bb4`.  `tools/frida_capture.py` forwards the op (scalar or `[V,C]`) —
  the initial retail-both drive hit a `KeyError: 'buttons'` because the new op key wasn't in the trace-op dispatch `elif` chain
  (fixed: added a `"bgnpcseed" in rec` branch mirroring `primaryloadpin`'s).

**✅ VERIFIED** (`house-firstcust-arrprobe` + `{bgnpcseed:[3502407629,1]}`, `scenario-test --target both --call-trace` /
`flow_diff --field-timeline`): **bgx1..bgx5 now bit-exact retail from frame 224 THROUGH frame 825** (602 frames) — was diverging
from the FIRST captured frame (224) pre-fix (`bgx0 retail=-2.8 port=-6.54…-8.49` depending on which half-fix was live).  `bgx0`
(the dead/frozen slot) still numerically differs (retail's leftover garbage `-2.8` vs the port's BSS-zero `0`) but is confirmed
BEHAVIOURALLY inert per Finding 3(b) — both sides render nothing and never tick its position; not chased further (chasing retail's
exact uninitialized-adjacent leftover would be reproducing incidental memory garbage, not game logic).  **Residual (pre-existing,
UNCHANGED across all 3 drives — baseline, seed-only, seed+cursor):** from frame 826 on, bgx1-5 (+ `panim`/`pframe`/`pcnt`/`cx`/
`coct`/`canim`/`cframe`/`db054`/`b5e0`/`poseL`/`poseR`, none bg_npc-related) show a **PURE 1-FRAME PHASE LAG** — port's value at
frame N+1 == retail's value at frame N, exactly (e.g. bgx1: retail@826=17.0876713 == port@827=17.0876713) — the SAME "resumes
one frame off" signature §21.16-§21.18 already fixed elsewhere in this scenario, evidently NOT yet fixed for whatever
load/transition sits at ~825 (the d3e/tutorial bracket or the wrap-up transition are the likely candidates, shared symptom
across pose+dialogue fields too).  **This is a SEPARATE, already-characterized class of residual — out of scope for the bg_npc-
origin arc** (a fresh next-arc lead, not a new mystery).  +1 host test (`bg_npc_seed_pin_forces_seed_and_cursor` — proves the
seed override via a bit-for-bit match against a direct `rng_seed()` run, and the cursor skip via slot dir==0/±1 partition);
3381 host pass; no regression.

**NEXT:** (1) the frame-826 phase lag (fresh arc — find which load/transition resumes 1f off here, mirror the §21.16-18
fix pattern).  (2) apply `{bgnpcseed:[3502407629,1]}` to `house-firstcust-cutscene-day2` (same savefile ⇒ same natural values —
NOT done this session, kept conservative pending that scenario's OWN verify drive per §21.20's outstanding
`{primaryloadpin}`-on-day2 TODO).  (3) PORT-DEBT: what retail's real primary load consumes RNG for between NEW_GAME
(3132701474) and the warmup (3502407629) — a "port it 1:1" arc if ever prioritized over the pragmatic trace-normalization pin
(candidates: shop/kyaku/news/order generation).  (4) **PENDING USER STUDIO CONFIRM** — re-drive win-0-1500 in the viewer,
confirm notes #17/#20/#22/#23 (sparkle/chibi-walk/window-NPCs) now read 1:1 visually within the fixed [224,825] window.

## 21.22 2026-07-01 — user-confirmed #17/#20/#22/#23 1:1; found + fixed the stray dead-slot contact SHADOW (note #25)

**USER STUDIO CONFIRM (win-0-1500, RE §21.21's pending item): "the sparkles and npcs align"** — #17/#20/#22/#23 all read 1:1 per
the user, closing that pending-confirm item.  **New note #25 (port-only): "stray contact shadow on port which is likely
supposed to be the barely visible shadow at the edge of the window on retail"** (`HOUSE_FREEROAM#1+23`, box `[506,336,595,415]`).

**Root — the §21.21 "leftover x/y/z don't matter" claim was WRONG; CORRECTED here.**  §21.21 reasoned slot 0's dead-slot
leftover position was behaviourally inert because `dir==0` makes both `bg_npc_tick`'s position update AND
`scene1_bg_npc_sprite_render` skip it (`m->visible==-1 || m->dir==0`).  **But `scene1_bg_npc_shadow_render` only checks
`m->visible==-1`** (`scene1_bg_npc.c` — grepped both render functions side by side, RE checked not assumed) — `dir` never
enters its skip test.  Slot 0's `visible` is 0 on BOTH sides (confirmed in the §21.21 SoA dump), never -1, so the shadow pass
draws it on BOTH sides regardless of `dir` — at whatever x/y/z it holds.  Retail's leftover `(x=-2.8, y=1.2, z=-10.5)` puts its
shadow barely visible near the window's far edge (per the user's own description); the port's BSS-zero default (`0,0,0`, since
`{bgnpcseed}` only pinned the seed+cursor, leaving slot 0 at its struct-zero initial state) draws it at the world origin — a
loud, out-of-place shadow blob.

**FIX** — extended `scene1_bg_npc_seed_pin(seed, cursor, dead_soa, dead_n_dwords)` with an optional `dead_soa`: raw
`{bgnpcpin}`-format engine records (`BG_NPC_ENGINE_DWORDS`=25 dwords each) for the `cursor` dead slots, written via a NEW
shared helper `bg_npc_write_record_from_dwords` (factored out of `scene1_bg_npc_pin`'s per-record copy loop — same code, no
duplication) inside `scene1_bg_npc_tick()`'s existing pending-check branch, right after the seed/cursor force (same "before
any RNG consumption" timing guarantee as §21.21).  `{bgnpcseed}` grammar extended: `V` / `[V,C]` / `[V,C,[d0..d(25*C-1)]]` —
the 3rd array is optional (omit → old §21.21 behaviour, BSS-zero dead slots).  **Retail mirror**
(`installBgNpcPinHook`): writes the same dwords into `DAT_073a7f80` at the FUN_0046f621 entry — a no-op on retail (it's
already the source these values were captured from) but keeps the pin fully bilateral/self-consistent, matching the
seed+cursor precedent rather than special-casing retail-skips-it like `{bgnpcpin}` does.

Captured the exact dead-slot dwords from the §21.21 SoA hex dump (decoded via a small node script, cross-checked the 3 float
fields decode back to the expected -2.8/1.2/-10.5): `[0,0,0,0,0,4294967295,4,0,0,0,0,3224580915,1067030938,3240624128,0,0,0,
0,0,14,0,0,0,0,0]` — dwords 11/12/13 are the x/y/z bit patterns, dword 5 (`arec` STATE) is `-1`, dword 6 (FACING) is 4, dword
19 (type) is 14 (not `bg_npc_spawn`'s expected `BG_NPC_TYPE_TABLE[0]=0` — still confirms this record was never produced by
the normal spawn path).  Baked into `house-firstcust-arrprobe`'s `{bgnpcseed}` op.

**✅ VERIFIED** (`scenario-test --target both --call-trace` / `flow_diff --field-timeline`): **`bgx0 ✓ aligned`** (was `✗
DIVERGES first @224 retail=-2.8 port=0`, 410 frame(s) of mismatch) — the full [224,1722] common window is now bit-exact for
the dead slot.  **bgx1-5 UNCHANGED** (still bit-exact 224→825, still the same pre-existing frame-826 phase lag, same values
— confirms no regression from the extension).  Host test extended in place (`bg_npc_seed_pin_forces_seed_and_cursor`, +check
(c): a synthetic dead-slot record survives the warmup verbatim while slot 1 spawns normally); 3381 host pass.

**Retires:** the §21.21 doc claim that dead-slot leftover fields "don't matter" (corrected in `scene1_bg_npc.h`'s
`scene1_bg_npc_seed_pin` doc comment).  **Open:** the frame-826 phase lag (§21.21's queued next arc, untouched by this fix) —
still the sole remaining known residual in this scenario's [224,1722] window.

## 21.23 2026-07-01 — the "frame-826" residual PINPOINTED to frame 632, CONV_POSE_START: db054 ticks one frame too early (diagnosed, NOT yet fixed)

**USER re-flagged via the viewer:** note #20 (`HOUSE_FREEROAM#6+153`, deep in the scenario, well past the §21.21/22-fixed
[224,825] window) shows BOTH the customer NPC's walk phase AND the 目玉 sparkle position visibly diverging — the exact
downstream symptom the §21.21 "residual pure 1-frame phase lag from frame 826 on" note predicted (sparkle position + chibi
walk both ride the shared LCG stream, so a frame-off db054 upstream cascades into both).

**Root-caused the ORIGIN, not just the symptom.** The only db054 probe (`0x48670f`/`house_update`) is gated to the free-roam
DEFAULT arm and goes dark for the whole cc08≠1 tutorial/wrap-up dialogue stretch — exactly where the bug lives — so the
earlier "first mismatch @825" read was just "the first frame the probe resumed sampling," not the true origin.  **Built a
continuous probe** (RE §21.23): converted `sim_step_a`'s existing bare `CALL_TRACE_ENTER_STUB(0x4536cb)` into a field-bearing
`CALL_TRACE_BEGIN_STUB` carrying `db054`, so it fires on EVERY sim-ticked frame regardless of which scene1 arm runs (even
`worker_load_busy()` ones) — added the matching retail declarative field-spec entry in `tools/flow/retail_fields.json`
(the `{va: {name, fields:[{name,src,va,type}]}}` mechanism `docs/plans/execution-flow-trace.md` documents; a NEW VA entry is
a JSON edit, no Frida/JS code needed — confirmed by finding `retail_fields.json` already has a `db054` field descriptor
under `0x48670f` to copy from).

**✅ PINPOINTED:** re-captured `house-firstcust-arrprobe`, `flow_diff --field-timeline` now shows continuous
`sim_step_a.db054` coverage — **first mismatch is `frame 632, retail=81 port=82`** (retail catches up to 82 on frame 633) —
NOT 825.  Frame 632 is exactly the `CONV_POSE_START` anchor (bit-identical on both sides — confirmed the FULL anchor
sequence 631/632/639/639/652/716/760/760/780/824/825/825/826/849/849 matches port↔retail frame-for-frame; this is a
"anchors align but a shared counter's per-frame tick COUNT differs by one" bug, not a timing/anchor bug).

**Working hypothesis (not yet confirmed by a probe — the honest state):** `scene1_ingame_tick()` dispatches the DEFAULT arm
(no db054 tick beyond its own once-per-frame `_advance_phase()`) vs the TRANSITION/event arm
(`scene1_ingame_transition_arm_tick → scene1_event_actor_tail_tick → scene1_companion_ctrl_advance_phase_event`, an
UNCONDITIONAL db054++) based on `scene1_intro_dialogue_busy()` — which returns 1 for the same three states
(`D_TUT_LOAD`/`D_TUT`/`D_TUT_DONE`) `scene1_intro_dialogue_posing()` does, so on the port both flip together the instant
`g_state` becomes `D_TUT_LOAD` (frame 632) — the event arm's unconditional db054++ fires the SAME frame the pose activates.
Retail's CONV_POSE_START anchor (an OBSERVABLE proxy — the actor's own STATE field) matches on 632, but its db054 doesn't
move until 633 — suggesting retail's REAL arm-dispatch (or the specific db054-increment call) reads a state ONE STEP behind
the actor-pose write on this exact transition frame (the same class of "notify_loaded clears inline at the top" /
"D_TUT_DONE settle" ordering bug as §21.10.1/§21.17), OR the default arm ALSO ticks db054 and something suppresses BOTH
paths on retail's frame 632 specifically.  **NOT YET DISTINGUISHED — needs one more probe** (arm-selector state +
`g_state` value, sampled alongside db054, on both sides at 630-634) before attempting a fix; do not guess-fix from the
hypothesis alone (per the porting loop: probe, don't rationalize — this is exactly the trap §21.18 warned about with the
"clean 2nd-settle" guess that regressed).

**Host test / regression:** 3381 host pass (diagnostic-only change, no logic touched yet).  **NEXT (fresh arc, not
completed this session):** add the arm-selector/g_state probe at 630-634, confirm which side's tick fires 1 frame off and
why, then apply the minimal fix (almost certainly a "hold on the OLD arm for exactly the transition frame" pattern, mirroring
§21.10.1/§21.17's shape) — verify db054 (and downstream bgx/sparkle/chibi) bit-exact THROUGH the whole [224,1722] window,
not just [224,825].  **→ RESOLVED §21.24 (the arm-selector hypothesis was REFUTED; the real root is a cc08==4 mode-boundary
re-read).**

## 21.24 2026-07-01 — RESOLVED: the +1 db054 is a cc08==4 mode-boundary RE-READ (the `cc04_at_dispatch` bug, doubled for cc08) — NOT the arm selector

**The §21.23 arm-selector hypothesis was REFUTED by the probe** (the porting-loop discipline paid off — probe, don't
rationalize).  Built the arm-selector probe it called for — sim_step_a (0x4536cb) carrying `busy`/`gstate`/`posing`/`cc08`/
`b520` on the port + `b1c8`/`b1d0`/`b1d8`/`cc08`/`b520` on retail (declarative retail_fields.json, no JS).  Result: the arm
switch is **SYNCHRONOUS** — port `busy` and retail `b1c8` both flip 0→nonzero at frame 632 together, and **`cc08` + `b520` are
`✓ aligned`** the whole window.  So it is NOT an arm-timing bug.

**Root — db054 rides the `FUN_0048b850` (free-roam MOVE) tail, and retail's `FUN_0048670f` is an if/else on the FRAME-START
cc08 that skips that move on BOTH `cc08==4` edge frames:**
- **the 4→1 LEAVE (frame 631):** `if (cc08==4) { …arrival + master tick FUN_00462403, which sets cc08=1 the frame the b520
  dissolve completes… } else { …walk arm → FUN_0048b850 tail db054++ }` — an if/else decided at the top, so the leave frame
  (cc08==4 at entry) takes the cc08==4 branch and **never reaches the walk/db054 path**, even though cc08 flipped to 1
  mid-frame.
- **the 1→4 ENTRY (frame 304):** the walk arm (cc08==1 branch) detects the cs trigger, sets `cc08 = 4`, and `goto`s the tail
  **SKIPPING FUN_0048b850** (all.c:87485-89) ⇒ no db054++.

⇒ retail's db054 stays FROZEN across the whole cc08==4 span AND its two edge frames.  The **port splits that single if/else**
across `scene1_player_ctrl_tick` (the cc08==4 arm / freeroam arm flip cc08) and the default-arm companion FALLBACK
(`scene1_ingame_default_arm_tick`, scene1_sim.c), whose db054-advance gate re-read the **LIVE** cc08 — so on the leave it saw
the flipped-to-1 value and advanced db054 a frame early (the frame-632 +1).  *(The first fix gated on the DISPATCH snapshot
ALONE — that caught the leave but broke the ENTRY: dispatch cc08=1 there, so it wrongly advanced; verified by the −1→+305 shift.
Both directions need catching.)*

**FIX (scene1_sim.c, the `cc04_at_dispatch` pattern doubled for cc08):** snapshot `cc08_at_dispatch = player_ctrl_cc08()` at
the top of the default arm and gate the fallback advance on `cc04_at_dispatch==0 && cc08_at_dispatch != 4 &&
player_ctrl_cc08() != 4` — cc08 must be `!= 4` at BOTH the frame-start snapshot (catches the leave) AND the live value
(catches the entry) = "no cc08==4 involvement this frame" = FUN_0048b850 actually ran.

**✅ VERIFIED (fixed port vs cached retail, `scenario-test --target both`, `flow_diff --field-timeline`):** **db054 `✓ aligned`
across the whole [224,1722] window** — entry frame 304 + leave 631→632 both freeze correctly (both hold 81, then advance
together at 633 via the event arm).  Clean win: the before→after diff leaves **every OTHER field byte-identical** (no
regression) and pushes the raw-LCG (`rng`) divergence from @635 → **@1016**.  3381 host pass.

**REMAINING — corrects §21.23's optimistic "fixing db054 should close #20/#22":** it did NOT.  The raw `rng` still diverges at
**frame 1016, which is INSIDE the FIRST customer's cs** (`cc08==4` steady, `db054=274` ALIGNED there) — a SEPARATE
cs-walker / customer-NPC rng root, downstream of db054, not the mode-boundary counter.  notes #20/#22 (well past 1016) ride
that stream ⇒ the next arc is the frame-1016 rng consumer, not another db054 pin.

**Probe left in place:** sim_step_a trimmed to `db054` + `cc08` (the arm-selector fields were the refuted hypothesis; cc08 is
the mode that gates db054's freeze and goes dark on 0x48670f past the arm switch — keep it continuous).  **No host test:** a
faithful mid-frame-flip test must drive the full cs machine + player controller + companion THROUGH the default arm (the
flip is only produced by the master tick inside the cc08==4 arm); that spans three subsystems with fragile shared-state
teardown, so — like the `cc04_at_dispatch` precedent it mirrors — the fix is **scenario-verified**, not unit-tested.

## 21.25 2026-07-01 — RESOLVED: the frame-1016 rng divergence = the `{bgnpcpin}` SoA inject lands 1f LATE (superseded by `{bgnpcseed}`; skip it)

**The §21.24 "next arc" (raw rng @1016) was NOT a cs-walker root — it's the shop-WINDOW bg_npc townsfolk.**  `scene1_bg_npc_tick`
(FUN_0046f621) runs EVERY frame in the player-ctrl PROLOGUE (scene1_player_ctrl.c:2306, BEFORE the cc08 dispatch), so it ticks
THROUGH cc08==4 too — only the free-roam WALK arm is cc08==1-gated.  Its bound-cross **reversals/respawns draw the shared LCG**
(not counted in `npcdr`, which is the cs-walker pump only).

**Drill (`--target both` @191405Z, `rngdump`/`bgdump`/`cs_walker_drill`):** rng bit-identical through frame **1015**; at 1016 retail
draws 5 / port 1 (cumΔ −4), then port 6 / retail 1 (a phase-shuffle).  `npcfr`/`npcsp`/`gsim%8` all ALIGNED (spawn cadence + sparkle
fine); `npcdr`=0 both (NOT the walker).  Root pinpointed via `bgdump`: **ALL 6 bg_npc x-positions JUMP simultaneously at frame 826
on retail, at 827 on the port** — `Port_bgx[N] == Retail_bgx[N−1]` exactly = the port's bg_npc runs **one tick behind** retail,
onset **at the cc08==4 f406 entry (826)**.  Both tick bg_npc once/frame (counted 0x46f621/0x46f2a3 — not a tick-skew).  The jump is
the **BILATERAL `{bgnpcpin}` full-SoA inject** (trace line 84, `{bgnpcpin:[0,SoA]}`): retail's frida writes it on-frame (826), the
port's segtrace `scene1_bg_npc_pin` cb lands it 1f late (827).  Positions then drift 1f-apart HARMLESSLY (no rng) until the first
bg_npc **reversal at 1016** draws the LCG one frame apart → the streams desync (notes #20/#22 ride the desynced stream).

**Why 1f late = the fix direction: `{bgnpcpin}` (a93413a, 2026-06-27, §21.4-era) is SUPERSEDED by `{bgnpcseed}` (d0ccaf6, §21.21).**
The bgnpcseed pins the warmup ORIGIN (seed+cursor+dead-slots) and REGENERATES the canonical layout deterministically from the 180×
warmup — it already aligns bg_npc bit-exact [224,825] (§21.22) with NO per-frame inject.  The proof the inject is redundant: **rng is
bit-identical through 1015 ⇒ NO divergent bg_npc event happens at the entry** — the only thing putting the sides out of phase is the
1f-skewed inject itself.  So the full-SoA re-inject at 826 is not just redundant, it's the SOLE source of the skew.

**FIX (RE §21.25, 2 files): skip the `{bgnpcpin}` SoA inject when the trace ALSO carries a `{bgnpcseed}`; keep it as the f406
MARKER (wrap-up-skip arm + rng-hook defer).**  Port (`main.c`): gate `input_segtrace_set_bgnpcpin_cb` on `!g_segtrace.has_bgnpcseed`
(fire_bgnpcpins is NULL-cb-safe ⇒ no inject; `input_segtrace_has_bgnpcpin` marker unchanged).  Retail (`frida_capture.py`): a new
`has_bgnpcseed` flag gates `bgnpc_pin_soa` forwarding (`bgnpc_pin_soa=None` ⇒ agent `g_bgnpc_pin_soa=null` ⇒ no inject; `has_bgnpcpin`
marker for the rng-hook defer unchanged).  **Both sides skip in lockstep** ⇒ both ride the `{bgnpcseed}` drift.  Blast radius: ONLY
arrprobe (has both); **day2 has `{bgnpcpin}` but NO `{bgnpcseed}` ⇒ keeps its inject, no regression.**

**✅ VERIFIED (`--target both` @195002Z, fixed):** port stderr `{bgnpcpin} SoA inject SKIPPED (…marker only)`; **bg_npc bit-identical
both sides 825→831+ (NO jump — natural drift lockstep)**; **raw rng `==` at EVERY frame past 1016 (cumΔ=0)**; `cs_walker_drill`
**1/900** (was 38/1100 — the lone residual is the benign off-0 entry-boundary rebasing artifact: retail's rngcalls counter starts at 0
at the entry) + **0/900 gsim%8**; **offer b574=119 / variant b5e0=1 / poseR bit-identical port==retail** (unchanged); 3381 host pass.
**PENDING USER STUDIO CONFIRM** notes #20 (customer walk path) / #22 (sparkle position) now visibly track retail.

## 21.26 2026-07-02 — gap (ii) dialogue-under-ESC-modal = ALREADY FIXED (§21.15, stale-window flag); #7/#19 REDIAGNOSED: NOT an RT composite — a doubled skip-prompt draw block (2× FUN_0046c090 dispatch)

**Part 1 — FRONT gap (ii) ("should the port hide the dialogue under the ESC box?"): NO — and no port change needed.**
Retail `FUN_0046c090` = FUN_0049b425 + **FUN_0046c9a2 (FULL dialogue draw, unconditional)** + box (FUN_0043537e/FUN_00435747)
iff `DAT_073a3dec==1` — retail draws the dialogue UNDER the box every frame; there is no hide/clear.  The observed "retail
shows no dialogue behind the box" = **arm TIMING**: retail's skip_wrapup driver arms at line+1 (`wrapup_dbg fn=761 line=0
posted=1` → `fn=762 box=1`, day2 both-run @220513Z) ⇒ reveal≈0 when frozen ⇒ nothing visible under the box.  Note #7
("retail early on skip dialogue prompt") was flagged on the **Jun-27 win-0-700 window — one day BEFORE 98cbf08 (§21.15)**
landed the port's mirror re-post; that stale port armed at TEXT_ANIM_START+24 (the scripted `{esc:25}`) with 24f of reveal
under the box.  Post-98cbf08 evidence (Jul-1 run): port dialogue-VA samples stop at frame 760 (open box freezes the tick —
the §21.15 tracing caveat) == retail's box set during 761 ⇒ **both sides arm the same frame now**.  Verdict: (ii) resolved
by §21.15; the viewer window was stale.  Do NOT add a dialogue hide/clear.

**Part 2 — #7 "stronger edges" / #19 "weird blending" REDIAGNOSED: the RT-composite theory is REFUTED; the fix is NOT
blocked on a v3 RT-capture extension.**  `orv3_rt.py --scan` on the retail day2 capture (dd353329): **0/2600 frames use
SetRenderTarget**; the modal frame 537 = 185 draws ALL → BACKBUFFER, **0 SetRenderTarget, 0 CopyRects** (P5 capture support
landed fa1d03e 2026-06-13, so absence is real, not a blind spot).  The actual structure (orv3_draws, frame 537):
- generic choice-box art ONCE: 6c15 9-patch ×8 + 3392 scroll block;
- **the skip-prompt block TWICE, verbatim** ([118-149] == [153-184]): strip 2781[512x128] + the 30×50 prompt-glyph run
  ("Do you want to skip this event?") + b8b7[256x64] label;
- the singles SANDWICHED between the passes: 3717/3cf5/5d80 [512x512] (Yes/No/hand-cursor pages).
Port (stale modal frame 367, 147 draws): same content, prompt block **ONCE** ⇒ the missing 2nd pass IS #7/#19 (glyph alpha
compounds under BLEND).

**ret_va PROBE (runs/probe-skipbox-callers — frida_capture --call-trace-vas-file {43537e,435747,435117,46c090,46c9a2,
4820ba} --call-trace-frames 745..805 on the day2 replay) — DEFINITIVE:** per modal frame (761+): `FUN_0046c090` ×1,
`FUN_0046c9a2` ×1, **`FUN_0043537e` ×2 + `FUN_00435747` ×2** — ret_vas **0x40b0df** (= inside **FUN_0040a765, the 2D-HUD
aggregator**: its tail calls FUN_0043537e UNGATED at all.c:7046, then FUN_00435747+FUN_00435117 at 7498-7499) and
**0x46c0a8** (= the FUN_0046c090 `DAT_073a3dec==1` tail).  `FUN_004820ba` (pause render) fires **ZERO** times — the
two-FUN_0046c090-sites hypothesis AND the pause-block(51223) theory are both REFUTED (the retail modal frame Clears
normally, consistent: the pause no-clear ramp isn't active).  Pre-modal frames (755-760): each fires ×1 (the HUD-tail
pass alone, box closed ⇒ FUN_0043537e's af34 gate makes it a no-op).
**PORTED 2026-07-02 (3 files) + ✅ v3-VERIFIED bit-1:1 draw program:**
1. `scene1_hud.c` — `choice_box_draw` inserted in the scene1_hud_render tail after the FUN_00466b7b overlay (mirrors
   7046) + `title_save_dialog_render()` after the cursor (mirrors 7499).
2. `choice_box.c` — the inline cursor call REMOVED: retail's FUN_0043537e never draws the cursor; every engine site is
   the explicit PAIR `FUN_0043537e(); FUN_00435747();` — the inline call double-drew the hand at paired sites (+1 b8b7).
   `scene1_dialogue_draw.c` gets the explicit cursor after its choice_box_draw (the FUN_0046c090 pair).
3. `customer_service_render.c` — the "Cancelling tutorial?" compensation draw at the overlay tail REMOVED (retail has
   no FUN_0043537e call in the CS family; the 7046 HUD-tail mirror covers it, immediately after the overlay).
**VERIFIED (v3 re-drive win-0-1000):** the box-UI draw region (first 6c15 →) is **81 == 81 draws, per-texture draws+prims
ALL equal — bit-identical draw program**; modal frame 158 vs 185 = Δ27 == the pre-existing pre-modal baseline (95 vs 122);
box-region pixels max ≤2 LSB (the #7 "stronger edges" now reproduce).  Residual pixel diff (y 541-641) = the standee
blink/pose phase (notes #5/#6/#21 class) — present identically on NON-modal control frames, unrelated.  3381 host pass.
**PENDING USER STUDIO CONFIRM** #7/#19 on the refreshed win-0-1000.
## 21.27 2026-07-02 — viewer note #8 FIXED: the choice-box commit FLASH (chosen label brightens 3 frames) — bit-exact incl. the 254-peak

**User-flagged (note #8, PAUSE_OPEN#1+36): retail flashes "Yes" on confirm, the port didn't.**  Root: the commit-time
close anim the port had explicitly deferred (the old choice_box.c "(DAT_0438ac14<4 … deferred)" comment).  Retail
(`FUN_0043537e`, objdump 0x435476-b1): while `DAT_0438ac14 < 4`, the CHOSEN label (`DAT_0438af30` 1=Yes / 2=No; cancel 3
neither) draws at `rgb = 0x7f − __ftol(sin(ac14·π_f/4)·(−128.0@[0x519468]))` → 0x7f/217/254/217 — brightening under the
inherited ADDSIGNED.  **x87 subtlety (engine-quirk #128): the peak is 254 NOT 255** — the float-rounded π_f/2 argument
through the CRT **double** sin gives 0.99999999999999905 → ·−128 → ftol −127; `sinf` would give 255 (1 LSB off).
**PORTED** (choice_box.c: per-label RGB off `cb_close`/`cb_result`, double sin off the float-rounded argument).
**✅ VERIFIED bit-exact (v3 win-0-1000):** pause "go to bed" confirm PAUSE_OPEN#1+34..41 Yes-region **max px diff 0**
(mean pulses 157.9→162.0→163.6→162.0→157.7 identically) + the ESC-skip confirm CONV_POSE_BLINK#3+33..39 box-band max 1
(sub-LSB).  3381 host pass.  **PENDING USER STUDIO CONFIRM** note #8.

## 21.28 2026-07-02 — the ★★★ chr_anim SEED-ORIGIN arc CRACKED: 4 tick-cadence roots (conv-pose release gate, cc08==4 set-then-tick, entry-frame no-tick, cs-walker set-anim never ported) — notes #20/#21/#22

**The FRONT "anim seed-origin" diagnosis (cframe 1f-ahead-in-load / 1f-behind-free-roam) resolved into FOUR concrete
roots, each probe-proven** (new declarative probe fields: companion `ccnt` 0x56dab4c + `ctimer` 0x56dab48, cs-walker
slot0 `n0anim/n0frm/n0cnt` 0x73a6e50/60/5c — port CALL_TRACE + retail_fields.json, no JS).

**(1) The conv-pose latch-release ignored FUN_0048407f's `cc08 != 4` gate** (the biggest: the 898-frame solid cframe
regime 825→1722).  Retail gates the WHOLE pose/release block on `DAT_0438cc08 != 4` — at the f406 entry (cc08 1→4 @825)
it neither re-applies the pose NOR runs the idle release: Tear keeps the STALE talk anim (canim 4, cframe cycling via the
per-frame tick) through the d3e load, and only the resumed idle law rewrites her to canim 0 at 851 — WITH a cycle reset.
The port's latch-release instead forced anim 0 + cycle reset at 826, reseeding the wing cycle 25f before retail ⇒ the
permanent offset.  FIX: mirror the gate in `scene1_conversation_pose_tick` (cc08==4 ⇒ drop the latch, touch NO records;
the player's cc08==4 arrival arm overwrites state 6→5 later the same frame, so CONV_POSE_END still fires @826).  Also
fixes the 1f panim glitch @826 (retail samples 6, port sampled 0).

**(2) cc08==4 frames order anim-SET before ONE unconditional frame-tail tick — a transition frame ends counter=1, NOT 0**
(ccnt probe: retail 850→851 = reset+tick ⇒ 1; walk-in churn 330-332 pins at 1; the port's skip-on-transition rule ended
at 0 ⇒ 1 tick behind for the rest of the scene = the 861/871/881… boundary blips).  **Free-roam cc08==1 is the OPPOSITE
order** (FUN_004897c6 runs inside FUN_0048b850 BEFORE FUN_0048a833's anim-set — probe frames 273/286: BOTH sides end a
transition at counter 0 ⇒ the §81-era skip rule is CORRECT there and stays).  FIX: `scene1_companion_ctrl_tick` ticks
unconditionally under cc08==4 (both the f404 at-counter arm and the f404==0 spring-follow fall-through).

**(3) the cc08 1→4 ENTRY frame ticks NOTHING** (probe: retail ccnt+pcnt freeze across 304 AND 825 — the engine entry
arms `goto LAB_004893ff`, PAST the FUN_004897c6 per-actor tick loop and the companion ctrl; FUN_0048407f doesn't run
either).  The port's d3e load gate ticked @304 (⇒ the +1 blips 307-327) and the §21.18 pose-hold ticked @825.  FIX: new
`player_ctrl_cc08_entered_this_frame()` (set at both cc08=4 entry sites, cleared per frame); the companion ctrl sits the
entry frame out entirely, and the §21.18 hold applies the pose RECORDS but skips the anim ticks (`f406_hold`).

**(4) the cs-walker set-anim (FUN_00482a51 ×3 in FUN_0046fbee) was never ported — the browsing chibi SLID in the idle
pose while retail WALKS** (= notes #20/#22 "customer walk phase", NOT a phase slip at all: n0anim retail=1/0 alternating
walk/dwell vs port ALWAYS 0).  The two walk-state calls were deferred as "render-only" with Ghidra-dropped args
(gotcha #1); objdump ground truth: wstate==0 @0x46fd74 `push 1` (after WTIMER++), wstate==1 @0x46fd09 `push 1` (before
WTIMER++), wstate==2 @0x46fc48 `push 0`.  FIX: `cs_npc_set_anim` (the 482a51 body) called per arm; the pump's existing
per-slot chr_anim_tick after cs_npc_tick gives the same set-then-tick order as retail (walker transitions land n0cnt=1 —
observed in the retail probe: walk cycle 36 ticks 1/10/19/28, dwell 78 ticks 1/61/67/73).

**RNG-safe:** all four touch only anim/frame/counter/timer record fields (no LCG draws, no position writes).

### 21.28.1 2026-07-02 — chip (a) RESOLVED: the pose-era +20 = the cs-LEAVE frame ran the free-roam companion law; Tear's wing cycle now bit-exact across the whole window

**Probe (continuous sim_step_a ccnt/canim/pcnt/panim, retail_fields.json mirror):** the +20 materializes in ONE frame —
during 631 (the b520 dissolve completion / LOADING_START#4) retail ticks the companion 20→21 with NO anim/position write
(the frame took the cc08==4 arm; the master tick flips cc08 4→1 mid-frame), while the port's spring-follow saw the
flipped cc08, MOVED her and set the WALK anim (canim 1, cycle reset).  Downstream, the 632 conv-pose enter
(`conv_pose_enter(comp, 4)`) is a **no-op on retail** — her state is already 4, the at-counter pose shares the id — so
retail's cycle free-runs (the 652 "reset" was a natural 40-tick wrap), but on the port (state 1 from the 631 glitch) it
was a REAL reset ⇒ the +20 offset, carried through the pose era into the [825,850] stale-anim window.

**FIX:** `player_ctrl_cc08_left_4_this_frame` marker — set inside `player_ctrl_cc08_enter_freeroam` only on a genuine
4→1 flip; the companion ctrl runs TICK-ONLY on that frame (mirroring retail's cc08==4-arm leave frame).  **The marker is
cleared at the frame top by `scene1_ingame_tick` (`player_ctrl_cc08_markers_frame_clear`), NOT in the player tick** —
the player tick does not run on EVENT frames, and the first attempt (clear-in-player-tick) left the marker latched
through the whole 632-824 dialogue era, freezing the spring (cz stuck 8.600 vs retail's 8.6564 drift — caught by the
verify drive, the exact trap the porting loop's re-verify exists for).  `player_ctrl_debug_set_cc08` clears both markers
(host-test ordering).

**✅ VERIFIED (port 035725Z vs full retail 025322Z):** **cframe / ccnt / ctimer / canim ALL `✓ aligned` across the whole
[224,1722] window** — the ★★★ wing-flap arc is CLOSED end-to-end (930 divergent cframe frames → 0).  cz re-aligned
(regression gone); cx/coct back to the pre-existing @389 blips (33/48f, tracked); raw rng bit-exact 225→1722
(0 mismatches — the leave frame emits no sparkle on either side); 3381 host pass.

## 21.29 2026-07-02 — note #23 vase shadow FIXED: fade.c ALPHAREF mistranscribed as ALPHATESTENABLE-off (leaked z-writes through alpha fringes) + 2 walker ALPHAOP value-vs-name fixes

**Note #23 ("vase shadow slightly off", PAUSE_OPEN#1+47, box [512,500,615,594], 125 px max-Δ77).**  Probe chain
(headless, single-frame `slice_window` + `orv3_shot` draw-bisect): the diff = the display-stand SHADOW DECAL
(mesh submesh, 32×32 tex `95ab`, 2 tris @ ib-start 4233 — retail R[93] ↔ port P[66]); decal quads BIT-IDENTICAL
(same y=8.7684, same tris; retail nv=3005/base=0 vs port nv=6/base=4233 = same vertices via SetIndices
baseVertex — orv3_draws ignores baseVertex, so the "replace" pairing was benign).  Pre-decal color BIT-IDENTICAL
⇒ Z-buffer divergence.  Root: **RS 15 ALPHATESTENABLE retail=1 port=0** at the mesh pass — the flower item's
semi-transparent fringe texels z-wrote on the port (no alpha-test kill) and clipped the decal's upper arcs
behind the stand/leaf silhouette.

**Root site — fade.c (FUN_00453e8f):** the engine's in-branch `SetRenderState(0x18,0)` = **ALPHAREF(24)=0** was
mistranscribed **ALPHATESTENABLE(0xf)=FALSE**; every pause/fade frame ends with alpha-test globally OFF, and the
NEXT frame's mesh pass inherits it (retail carries ATE=1 across frames; nothing in the engine's frame ever
writes it off).  Also fixed while there, same objdump read: (a) engine L16-18 run UNCONDITIONALLY (fog off +
TSS 0x10=MAGFILTER/0x11=MINFILTER→LINEAR before the counter gate; the port's counter early-return skipped them;
port also had them as MIPFILTER+MAGFILTER), (b) walker FUN_004552d0 L327+L353 `TSS(0,4,4)` = ALPHAOP=**MODULATE**
was ported D3DTOP_BLENDDIFFUSEALPHA(12) — the enum value-vs-name gotcha AGAIN (4th+5th instance in this file;
pixel-neutral here since diffuse α=255 makes the two ops agree, but a real render-program divergence); Pass-E's
dormant comment "4 // MODULATE2X" corrected to MODULATE.

**✅ VERIFIED (re-drive win-0-1500, same retail cache cd47d68a):** note #23 crop diff BLACK; pause frames
125→**2 px** (2 isolated 1-px sprite-edge speckles at (856,263)/(340,543), scattered), CONV_POSE_BLINK#2+6
0 px; HOUSE_FREEROAM#1+39 diff = ONLY the Recette sprite blob = residual (A), untouched.  3381 host pass.
ALPHAOP timeline now 4-throughout on both sides at the pause frame.  NB the ubiquitous retail-only `b494`
80-tri first-draw is the KNOWN inert 0-px overlay (PROGRESS 2026-06-30), not a lead.

## 21.30 2026-07-02 — note #24 "recette phase at the very start" FIXED: pose_house_standing seeded a steady-state anim snapshot (counter 25/frame 2/timer 5) instead of retail's fresh reset

**Residual (A).**  State-panel probe (win-0-1500 --state): the intro walk-in ticks BIT-ALIGNED through the whole
load era (anim 5, pcnt lockstep, px -0.125/frame); at HOUSE_FREEROAM#1+0 retail writes a FRESH set-anim reset
(pframe 0 / pcnt 0 / timer 0 ⇒ +1 reads 0/1; frame steps every 10 ticks, first flip at +11) while the port
re-applied `player_ctrl_pose_house_standing`'s seed — **counter 25 / frame 2 / timer 5.0f, the runs/cchr2b leaf
snapshot of HOUSE frame 17544 (a steady-state capture, NOT the entry state)**.  Port's cycle wrapped 15 early
(+16 vs retail's +41) ⇒ the constant 15-tick idle-phase offset for ~45f until the first pause realigned both.

**FIX:** seed = 0/0/0 (anim 0, facing 6 unchanged).  Updated the two host tests that asserted the snapshot
(`player_pose_seeds_actor0`, `player_ctrl_idle_animates_and_holds_position` — the latter also documents the
tick law: `dur<=timer` checks BEFORE the increment ⇒ frame flips on tick 11, matching retail's +11).

**✅ VERIFIED (re-drive --state):** player panim/pframe/pcnt divergent frames **45 → 0 over the WHOLE window**;
companion canim/cframe/ccnt/ctimer 0 divergent; rng bit-exact (the 1 flagged row is a missing retail probe
field, not a value gap); note #24 crop diff BLACK; full-frame freeroam+39 2973→3 px (scattered 1-px speckles,
same class as the pause 2 px).  3381 host pass.

## 21.31 2026-07-02 — day2 "+261-rng day-end" lead CRACKED: it's the SALE-COMMIT coin shower (FUN_00460d52 → Table-A alloc → 69-particle burst), NOT next-day regen; f404==0 accept block ported

**Lead (FRONT day-end arc, chip a):** retail draws +261 rng in ONE frame at "the day-end Z (PAUSE_CLOSE#3+89)";
suspected next-day layout/roster regen.  **REFUTED — it's the walnut-bread SALE commit.**  Retail frames
14845/14860/15060 (both-run 20260701T213115Z): "Heh, this was good shopping!" → Z → coin/sparkle shower over the
shop, gold 55→180 count-up, EXP popup, then "Glee! I sold Walnut Bread for 125pix!".  The PAUSE_CLOSE#3 segment's
three Zs = commit sale (+89), advance line (+209), advance line (+338) → fade → LOADING_START (+~428) = the
scripted post-sale tutorial cutscene chain (the iv1_8 arc), NOT a menu-driven day-end.

**Chain (probe-proven):**
1. `FUN_004658ab` b534==7 accept, `f404==0` block (f404 IS 0 on the tutorial sale — the port comment claimed
   "inert for the forced/tutorial sale", wrong): gold `(&DAT_044e37a4)[slot·0xb7f2] += ask`, SE 0x14d,
   `FUN_00460d52(0)`.
2. `FUN_00460d52` (asm 0x460d52..0x460e4f; Ghidra dropped the x87): bank stat `+0x2c3e0 += ftol((f(ask)/f(base)
   − 1.0)·100.0) + signed ftol(sqrt(|ask−base|))` (.rdata consts 0x519364=1.0, 0x519368=100.0); gate
   `DAT_0438b1a0==0` (ini s_easydisp) → **`FUN_004132c1(304.0, 128.0, entry 100, 1.0, −1, 4)`** (Table-A
   projected alloc; consts 0x519374=128.0, 0x5194b8=304.0); SE 0x17b; SE 0x156.
3. Same-frame `FUN_00414929` Table-A tick: parent effect entry 100 (ef/effect2.dat slot 0) = **5 sub-records,
   ALL age_match=0** → 5 `FUN_00414345` spawn calls (ret 0x414a71), templates 173/170/171/172/176, spawn_count
   dw3 = 28+8+8+8+17 = **69 particles**.  Per-particle rng: 3 float draws (u:0x41460e/1c/8d ×69 = 207; each
   float draw is +1 int LCG internally — the "0xd325f33" callsite = FUN_00471089's Frida-relocated prologue) +
   52 modulo draws (0x414474; templates with DAT_007338a8>0) + 2 periodic = **+261 int / +207 float** in one
   frame.  Slot ages to 300 inert, then dies.

**Probes:** rng-callsites `{rngcs:[1690,40]}` (runs/probe-dayend-rngcs — spike frame 468 draws);
call-trace VAs {0x414345,0x4147d5,0x414766,0x412c73,0x414929} (probe-dayend-spawncallers — 5 burst spawns ret
0x414a71, 4/8f ambient ret 0x41480e); allocator VAs {0x4132c1,0x41331d} (probe-dayend-alloc — ONE call, ret
0x460e34 = inside FUN_00460d52); memsnap @seg+85/+95 (probe-dayend-memsnap2 — Table A empty pre-Z; slot0
parent=100 pos=(304,128,−520) mode=1 age=6 post-Z; entry-100 sub-records + template spawn-counts read from the
dump).  NB memsnap ops are PER-SEGMENT — they drop when the segment's {wait} passes; arm them in the segment
that contains the target frame.

**PORTED (this session):** `cs_sale_commit_stats_fx()` (= FUN_00460d52) + gold + SE 0x14d wired into the
b534==7 accept in customer_service.c, gated f404==0 exactly like retail.  PORT-DEBT(cs-live-sale-fx) narrowed
to the remaining helpers: FUN_00460b3a (per-item max/min sale records at bank +0x13d48/+0x13d4c, item id via
FUN_004681f6(b5a4>>6)), FUN_004606fc (combo counter b5c4 + popup queue DAT_06a5ea78/DAT_0730b194),
FUN_00460083 (stock decrement), FUN_00460f59, FUN_0046002a, FUN_00460b93 (catalog).

### 21.31.1 2026-07-02 — first port drive: burst MISSING (Δ1 @1934) — root: template sets 1-3 never loaded

SEs 0x14d/0x17b/0x156 fired in engine order at frame 1934 == retail PAUSE_CLOSE#3+89 and the alloc landed,
but rngcalls Δ1 at the commit (no 261-burst): `pfo_load_one_file` loaded ONLY effect1.dat's secondary chunk
("only set 0 is read" — a stale scoping note from the sparkle chip), so templates 100..399 were zero ⇒
spawn_count 0 ⇒ zero particles.  Engine truth (FUN_00412a89 file loop): each file's 0x4330 secondary chunk
freads to `DAT_00733820 + file_idx·0x4330` — ONE contiguous 400-template table.  FIX: TEMPLATE_COUNT 256→400,
`scene1_overlay_templates_load_chunk_at(set,…)` lands each file at id set·100, loader passes every file.
Host test `overlay_templates_load_chunk_at_set1`.

### 21.31.2 2026-07-02 — post-sale rng verdict: burst 261==261, bit-exact 141f into the sale segment; the +141 residual = the COIN-LANDING fanfare (Table-B landing pulse + FUN_00406584 jitter arm), unported

**Verdict (drive-3, all three fixes in):** port burst **261 @ commit+1** == retail **261 @14847** (same
SE-frame→spike+1 attribution both sides).  Per-anchor-segment raw-rng compare: the sale segment
(PAUSE_CLOSE#3, 428f) is **bit-exact for 141 frames** — covers the commit, the whole 69-particle shower
spawn, and the money-roll count-up.  Post-load day-end segments are 1-frame-seam shifted (s37 shift+1 →
120/121 match) — the known pinned-vs-natural load duration.

**The +141 divergence (port 1986 / retail 14898, real at every shift):** retail's rngcs over seg+80..+200
(runs/probe-dayend-rngcs2) shows the LATE window adds **4 draws/frame ×~30f from INSIDE FUN_00406584**
(u:0x406775, u:0x40678c, 0x4067a3, 0x4067b8 — asm 0x406762..0x4067c8: while `DAT_00648280>0`, decrement +
jitter `DAT_00648284/88 = ±ftol(2·u+1)` = the screen-shake/popup wobble) + a few shop-walker draws
(0x46fe9c etc.).  The armer: **FUN_0040656e** (`DAT_00648280=4; SE 0x29d`) called from **FUN_00414929's
Table-B integrator** (all.c:12732) when a drag-mode-1.2 particle hits the floor (`sqrt<0.5 || below-floor`
→ slot kill + pulse) — i.e. **each landing coin pulses a 4-frame shake + coin SE**, repeatedly topping the
timer over ~30f.  Related: **FUN_00406159** (`0x43ce0000=412.0, 0x42e00000=112.0`, SE 0x174 first/0x172
repeat, from FUN_00485861) = the TOTAL-EXP popup arm; DAT_00648258 counts to 0xb4 with SE 0x174 (FUN_00406584
head).  The port's `scene1_pfo_table_b_tick` (PFO.3/4) lacks the landing branch; the 406584 jitter arm and
the popup machinery (FUN_004606fc → FUN_00485861 → FUN_00406159) are unported.

**OPEN sub-chips (one coherent "sale fanfare" arc):**
1. RENDER: the 69 particles spawn (rng-exact) but draw NOTHING (retail: coins + glow ring).  Templates
   170-176 = tex_type 28/29/30/26/20, shape 0, layer 0, blend 0/2, **mode 1 (projected)** — the sparkle
   (tex 19, mode 0) draws, so the layer-0 shape-0 pipeline works; suspect the shapes-table (grp.idx) entries
   for tex 20-30 / the emit filter's TEX_GROUP↔outer-layer mapping / the mode-1 sites.  Probe: v3
   draw-program diff at the burst frame.
2. PHYSICS+FX: the Table-B landing branch (kill + FUN_0040656e pulse per coin) + the FUN_00406584 jitter arm
   (4 draws/frame while DAT_00648280>0) — closes the +141 rng residual.
3. POPUP: FUN_004606fc (combo/queue) → FUN_00485861 → FUN_00406159 (TOTAL-EXP popup + SE 0x174/0x172) + the
   0x648258 fanfare timeline.

### 21.31.3 2026-07-02 — fanfare chip landed (shape_mode PARAM8 fix + shake jitter + landing-pulse default); coin aim/landing verified host-side; live-game landing still under investigation

**PORTED:** (a) Table-A tick `shape_mode` arg mistranscription FIXED — both spawn arms push `[esi+0x10]` =
slot dw8 **PARAM8** (asm 0x41499c/0x414a1f), not slot[10] MODE; the sale alloc passes 4 ⇒ the coins now
spawn with SHAPE_MODE=4 and the PFO.4 aim/landing physics engages (2 host tests updated to the PARAM8
contract).  (b) `scene1_top_hud_shake_pulse/_tick` (FUN_0040656e timer + the FUN_00406584 jitter block
asm 0x406762-0x4067c8: 4 LCG draws/shake frame), wired in sim.c INGAME before the money roll.  (c) the
PFO.4 terminal-kill production default = shake_pulse + SE 0x29d.

**Host-verified end-to-end** (scratch probe, real effect2.dat template values): alloc(304,128,100,1,−1,4)
→ tick → 8 coins spawn sm=4 u48=−0.008, fly (1.3,5.4)→(13.2,−10.8), ALL 8 terminal-kill at age ~48-60 —
matching retail's 15 landing SEs at burst+50..+73 (se_069_id029d 14897-14920).

**Live-game gap (OPEN):** drive-4 port memsnap @seg+95 shows the 24 coin slots PERFECT (active=170/171/172,
sm=4, u48 set, staggered ages) — yet NO 0x29d SEs and no 4-draw jitter run in the drive (one stray Δ5@1989).
The terminal kills don't fire in the live loop despite identical state + host-verified physics.  Memsnap
@seg+130/+175 probe in flight; suspects: a mid-flight slot kill/clobber by another live system between +95
and +140 (records reset? spawn reuse?), or a tick-ordering/loop-gating difference vs the host harness.

**§21.31.3 RESOLVED (drive 074133Z):** two port bugs — the PARAM8 mistranscription (above) AND the PFO.4
terminal gate `factor == 1.2f` never passing under GCC x87 -O2 excess precision (the clamp's assignment
keeps 80-bit through the compare; MSVC fstp-spills to float32 first — **gotcha #19**, bit-pattern-compare
fix, reproduced+verified with an `-mfpmath=387 -O2` host probe: 0/8 kills before, 8/8 after).  VERDICT:
**24/24 coins land** (SE 0x29d frames 1985-2012; start frame == retail's aligned 14897), gold count-up
matches frame-exact (129==129), and the SALE SEGMENT (PAUSE_CLOSE#3, 428 frames: commit → burst → roll →
coin flight → landings → shake jitter) is **raw-rng 428/428 BIT-EXACT** vs the retail capture.  Whole-trace
per-segment sweep: remaining diffs = the wrap-up-dialogue segments' mid-segment blink re-seeds (script
alignment artifact, pre-existing confirmed-1:1 territory) + the two known 1-frame load seams.  NB retail's
audio shows 15 se_069 lines vs the port's 24 — same window, retail dedups same-frame repeats of one SE
(presentation-only; the jitter rng aligning bit-exact proves the landing TIMELINE matches).
**Still OPEN on this arc:** the shower RENDER (task: retail draws ~30 extra overlay quads at the burst
frame — coins/glow invisible on the port) + the TOTAL-EXP popup chain (FUN_004606fc → FUN_00485861 →
FUN_00406159).

### 21.31.4 2026-07-02 — coin-shower RENDER cracked: TWO FUN_00452f58 port bugs (HUD eye/lookat swap + atan2 arg order) — coins now pixel-1:1

**Method (new tool):** `orv3_draws.py --verts N` (UP-draw vertex decode + WORLD/VIEW/PROJ in effect +
projected screen footprint) — built for this chip; answers "draw issued, paints nothing: WHERE is it".

**False leads eliminated first:** material diff at burst frame showed coins NOT missing — port emits the
same 41 tex-3288 + 24 tex-183e quads (per-texture tris EQUAL; the pass differs invisibly to material).
Retail draws 105-112 @1731 = the mode-0 window sparkles (scene camera, FUN_004176ff head dispatch (0,0),
ZENABLE=1 additive); 113-128 = fanfare UI flashes; the COINS are 129+ — AFTER the FUN_00417504 HUD
SetTransform (VIEW timeline probe).  Both sides dispatch coins from the SAME shell (mode-1 layer-0).

**Bug 1 — HUD camera eye/lookat SWAPPED.**  DAT_06a47120=(0,0,−550) is the EYE, DAT_06a475f0=(0,0,0)
the lookat (port had the roles reversed → identity VIEW).  Capture-proven: retail HUD VIEW = rot diag
(−1,1,−1), translation (0,0,−550) = lookat_rh(eye=(0,0,−550), at=origin).  Effect: mode-1 particles live
on the z≈−520 plane → retail sees them w≈30 (18-px coins), port w≈520 (sub-pixel dots).

**Bug 2 — pre-matrix atan2 ARG ORDER.**  Engine FUN_00503dd0(hyp, dy) = atan2(y=hyp, x=dy): state
(550, 0) → π/2 → rot_y_angle = π/2−π/2 = **0** → DAT_0438cdf8 = IDENTITY (not RotY(π/2) — that came from
the port's atan2(dy, hyp) and turned every shape-0/5 HUD quad EDGE-ON = 0.5-px slivers; the pre-O.11
identity stand-in had been correct; PHC #16 value-check queue entry can close).  Capture proof: retail
coin worlds are pure T×S (diag scale, no rotation), port pre-fix had the x/z-swapped RotY(π/2) factor;
translations/scales bit-identical either way (slot sim state was always perfect).
DAT_0438cdf8 is the SHARED billboard pre-matrix: scene passes set camera orient (mode-0 sparkle billboards
by it — its world carries the 56° camera pitch), FUN_00452f58 resets identity for the HUD pass.

**VERDICT after both fixes:** burst frame port 1626 vs retail 1731 pixel diff = coins GONE from the diff
(coin field pixel-1:1); whole fanfare sweep (+89/+120/+141/+157/+166) coin materials aligned.  Remaining
at these frames = the known opens: TOTAL-EXP popup chain (§21.31.2 #3), sold-item display quad (tex cde5
port-only FROM THE COMMIT FRAME ON — retail stops drawing the sold item's display-stand quad at commit;
= the FUN_00460083 stock-decrement/display-clear debt, viewer note #18), merchant-bar flash (tex 3392
22vs28 tris, note #19, part of the popup chain), and a NEW pre-existing lead: **tex b494 80tris/1draw
retail-only EVERY frame** (first draw of frame, VB-based, colorop=4, paints 0 px solo — suspect a
shape-8/9/10 strip warm-up or an always-on 0-alpha overlay; benign-invisible, unchased).

### 21.31.5 2026-07-02 — accept-block bank helpers PORTED (sold-item display clear + popup queue/timeline + merchant EXP); PORT-DEBT(cs-live-sale-fx) retired → (cs-news-suggest)

**PORTED (engine order all.c:62538-62548):** FUN_00460b3a (per-item max/min sale records, bank
0x4f52/0x4f53 + slot·0x14), FUN_004606fc (EXP popup queue: b584==1 extends combo b5c4; FUN_00460672
classifies — 1→type-0 30exp, 2→type-2 15exp, else base 10; combo type-1 2^b5c4 cap 0x80; type-3 TOTAL
closes; clears DAT_0730b304..314), FUN_00460083 (sold list append — the LIST region = exactly 3 types
(0x450f6b4→0x450fb64), counts at 0xb0f3+type, count[8] gates the news short-pairs at +0x2dde4),
FUN_00460f59 (encyclopedia sold mark, (slot,3) pairs at 0x9dae), FUN_0046002a (display-grid clear →
**the sold walnut bread vanishes from the stand at commit** — note #18; tex cde5 port-only draw GONE
at +109/+166).  Remaining: FUN_00460b93 news suggestion (needs FUN_00468ddc/468d6b chain) =
**PORT-DEBT(cs-news-suggest)**.

**The popup TIMELINE (master tick all.c:60257-60277) is REQUIRED, not optional:** b5c0 counts frames;
entry i's display counter b304[i] advances while b5c0 > i·0x3c; at b5c0 > b5bc·0x3c the queue closes
(b5c0=0) and **MERCHANT EXP (bank 0xb0fd, DAT_0450fb8c) += the type-3 TOTAL** (the merchant-bar fill
target, note #19).  Landing FUN_004606fc alone (b5c0=1) STALLED the b520 leave dissolve — its gate
(all.c:60326) requires b5c0==0 — the first re-drive collapsed to 1945 paired/950 gaps until the
timeline landed; with it the trace realigned (2887 paired / 0+113, raw rng 1856/1856 bit-exact).

**Still OPEN (render only):** the popup TEXT draw ("TOTAL EXP 10" — FUN_00485861-style OSD via
FUN_00406159@(412,112) SE 0x174/0x172 + the DAT_00648258 timeline) + the merchant-bar fill/flash
(tex 3392 22vs28 tris @+109) + SE same-frame dedup + the b494 80-tri invisible lead.

### 21.31.6 2026-07-02 — TOTAL-EXP popup render + merchant-XP bar animator PORTED — the sale fanfare is PIXEL-1:1

**PORTED:** (a) the EXP popup ROWS (FUN_00466b7b pass-2 tail, asm 0x467db5-0x467f31 →
customer_service_render.c section 7): per-entry alpha 0.1/frame ramp, hold (type-3 0x5a, else 0x3c),
0.1 fade; y = 400 − f·16 slide; label = item_win 128×24 src {640,(type·3+30)·8}; number = FUN_00466a9a
(per-type coloured digit rows at item_win y 720+type·32, cell 0 = '+', 14px pitch, fmt "a%d" bonus /
"%d" TOTAL, x = DAT_005c6cbc{108,108,80,120}[type]+16).  (b) the merchant-XP bar animator
(FUN_00406584 all.c:4799-4848 → scene1_top_hud.c xp_tick): _DAT_0438b91c eases +(end−start)·0.01/frame
toward bank exp 0xb0fd, glow-flash DAT_0064827c wraps 0x1e, LEVEL-UP at end (level 0xb100 ++, start
0xb0fe = end, end 0xb0ff += (level+2)·0x32, banner DAT_0438b920 arms — pop render FUN_00407ab4 still
PORT-DEBT(merchant-levelup-pop) — SE 00re_sys03a); wired in sim.c INGAME before shake_tick (engine
block order; rng-neutral).  (c) merchant HUD now reads the LIVE animators (shake x/y origin, flash
pulse diffuse, eased float fill, bank level) — the const stand-ins retired.

**VERDICT (drive + pixel sweep):** raw rng 1856/1856 bit-exact; material at +109 = ONLY the b494
invisible lead; pixel diff vs retail: +89/+120/+141/+157/+166/+193 = 0 px, +94/+243 = 1-px speckle
(the accepted sprite-edge residue).  Notes #10/#12/#19 (TOTAL-EXP text, fade-in, bar fill/flash) and
#11/#13-#17 (coins) all closed pending the user's viewer confirm.  Remaining on the arc: SE
same-frame dedup (cosmetic) + tex b494 (invisible, unchased) + PORT-DEBT(cs-news-suggest) +
PORT-DEBT(merchant-levelup-pop).

### 21.31.7 2026-07-02 — SE same-frame "dedup" is STRUCTURAL: FUN_00499519 sets a request FLAG, the per-frame pump plays once — PORTED

FUN_00499519(id) → FUN_004994f3: scans the SE id table (DAT_005d1584 pairs) and sets
**DAT_0964308c[slot] = 1** — it never plays.  The pump **FUN_0049966a** (sim_b: the engine main loop
runs `FUN_004536cb(); FUN_0049966a();` per ticked frame, 0x47be92) walks the 0x6e flags at its HEAD,
plays each flagged SE once (FUN_00499c63) and clears.  Same-frame repeats collapse natively — retail's
15 se_069 lines vs the naive play-per-call port's 24.  PORTED: audio_play_se_by_id now flags (returns 1
regardless, engine behavior); `audio_se_flush()` at music_step_default's head (sim_b ✓ same slot).
Timing: SEs flagged in sim_a play the SAME frame (sim_a → sim_b); SEs flagged in render play next
frame's pump — both identical to the engine's call order.  Unit test
`audio_play_se_by_id_defers_and_dedups`.  The sale-fanfare arc is now fully CLOSED port-side.
