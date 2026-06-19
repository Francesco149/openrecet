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
