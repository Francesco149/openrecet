<!--
  The ONE hand-edited status block. tools/gen_port_ledger.py injects everything below the
  marker into docs/STATUS.md's "Current front". Keep it short (60-second read) and ONLY
  open/forward-looking: when an item resolves, move its story to PROGRESS.md / findings in
  the same edit. Old snapshots: docs/archive/FRONT-*.md. PORT-DEBT: docs/port-debt.md.
-->
<!-- FRONT:BEGIN -->
**Methodology:** in-process retail host (rhost), adopted 2026-10-13 by owner decision
(`docs/audits/2026-10-13-methodology-reset.md` §3, Phase 0 approved). Pilot after
Phase 0: dungeon hybrid (§3 Phase 1).

### Phase 0 — foundation (in progress)
- ✅ `tools/rhost/` productized:
  - deterministic heap / handles / stacks / time / loads / file overlay;
  - quiet off-screen windows;
  - Windows queue runner via `ssh win` (WSL not needed);
  - `det` gate; corpus; record; flatten; hook table; D3D proxy arm.
  - Determinism: only DLL-pointer slots differ run-to-run (README).
- ☐ **★ NEXT: corpus triage**:
  - re-record the `cad868` first-customer / day-2 family natively (`rhost.py record`);
    their traces need Frida-era pins;
  - flatten every completing legacy trace;
  - calibrate `volatile.txt` over the corpus.
- ☐ **x87: convert the remaining 283 affected fns** (`docs/findings/x87-float-semantics.md`):
  - neither `-std` is retail-exact; mirror the disasm with `src/x87.h`;
  - verify per fn with a P1 oracle or an rhost hook;
  - go flow by flow with the re-verification program.
  - Done: the haggle family (oracle-exact under c11 + gnu11). Port now builds gnu11.
  - 2026-10-14: the port ran at PC24 (missed WinMain's PC53 step, §138) — fixed;
    earlier port-side float comparisons were made at the wrong precision.
- ◐ **Port-side determinism**: port↔retail traces should align by frame number.
  - Boot→title already aligns (`title-options` `TITLE_SETTINGS_READY` f39 both).
  - `--det-loads` (DEFER twin): loader threads run at the frame boundary, and every
    `.ivt` bracket is 1 frame like retail under DEFER (14/14 measured), including the
    first prologue load. House load: `LOADING_END` f73 both (plain turbo: f3546).
  - ✅ `rhost.py portanchors house-idle`: **195/195 anchors identical (name + frame)**
    over 4,418 frames (prologue settle frames + 1-frame iv1_2 load, PROGRESS).
  - `rhost.py portstate SCEN --va …` = frame-by-frame retail VA vs port mirror
    (`src/state_va.c` table). ★ next, in frame order:
    1. **Tick-0 label offset — now the earliest port-side split on 3 shop scenarios.**
       Measured 2026-10-14 (`portstate house-idle --va 438b8cc,73dfcfc,73de61c,44e37a0`):
       gsim, tick frame_count and prev_ms are ALL port = retail+1 from f1; playtime
       identical. rhost labels a tick by its post-increment count (first tick = 1);
       the port labels pre-increment (first tick = 0, logic-dead). The display
       sparkle `gsim%8==3` therefore fires 1 f early: day2-shop f771, day2-buy-tutorial
       f1283, day2-save-commit f9587 (pin-healed there). Plan: drop the dead tick and
       label post-increment (port label L = L ticks run, vms = L·17) — touches the
       segtrace/anchor/state-log labels, so re-run every gate after.
    2. ✅ cs d3e load under DEFER (b1cc release order) and ✅ the tutorial .ivt bracket
       (D_TUT_LOAD now ends at the frame boundary, `99d64922`):
       `portanchors house-customer-tutorial` **64/64** frame-identical.
    3. Port boots `cc08`=1; retail has 0 until the house init.
    4. RNG: legacy traces' Frida-era `{rngseed}` pins (applied to both sides) hide long
       divergences (day2-save-commit: f250–389, f445–2889, … heal only at pins).
       Re-record or strip the pins, then chase per frame (`--log-rand` retail;
       a port-side caller log is still TODO).
  - ✅ **house-pause-save-commit: port `save.dat` byte-IDENTICAL to retail.** The port
    replays rhost's flat trace with `--det-loads` (findings §11). The trace still
    re-seeds rng at f342 (`{rngseed}` pin); -empty is the one pin-free equality.
    One command: `rhost.py portsave SCEN`.
  - ✅ **day2-save-commit: port `save.dat` byte-IDENTICAL to retail.** The whole tutorial
    → end-of-day cutscene → Day 2 → save, 33k frames. **Not pin-free:** 62 Frida-era
    `{rngseed}` pins. The port also reaches
    `SAVE_PICKER_READY` on the same frame (33199). The save compare found 2 real port
    bugs, both fixed:
    - scripts were never marked viewed at END (`FUN_0046c320`), 7 bytes;
    - `FUN_0048ffd9`'s furniture seed went to the header-less save-file arena (bank 0)
      instead of the active work record, 88 bytes; the furniture readers never saw the
      loaded save either.
- ◐ **v3 viewer fed by rhost:**
  - retail D3D stream from rhost (`--d3d --cap`): ✅ captured + replayed BIT-EXACT (house-idle f600-602);
  - port stream from the port;
  - join by frame number.
- ◐ wine/Linux target:
  - ✅ `rhost.py --target wine -j N`;
  - ⚠ RNG diverges from Windows at new game: retail's SE path runs only with working
    audio (`DAT_096430fc`), and headless wine has no DirectMusic. Windows native = RNG
    truth (findings §10);
  - ☐ wine DirectMusic, or a fix for the audio flag;
  - ☐ bounded podman container.
- ◐ **Shop days (port side)** — native recordings `day2-shop`, `day2-buy-tutorial`,
  `buy-*`, `debt-gameover-restart` (retail replays exactly, shadows 0 div).
  - ✅ Shop entry: the port skipped the counter menu ("Open Store / Cancel", cc08 0 →
    10 → 4), so `CUSTOMER_SERVICE_ENTER` came 58–68 f early. Ported
    (`docs/findings/shop-counter-menu-RE.md`); entry now frame-exact on all 7 shop
    scenarios; cc08 / b74c / b754 / cc0c / help-box counter identical per frame
    (`portstate buy-40pct-accept`).
  - ✅ House-entry RNG (Continue load f652): FUN_00436f97's floor layout
    (FUN_0047df4d) + actor reset (FUN_0041f252) + bg-NPC stage reset (FUN_0046f1e9)
    ported (PROGRESS 2026-10-14). `portrng buy-40pct-accept`: per-tick draws identical
    f1–f741.
  - ✅ Tear's random wander (FUN_0048a833 `!v32` arm), FUN_004851e2 actor seats,
    b1cc release order, menu "Open Store" companion tick, WinMain PC53 (§138):
    `portrng buy-40pct-accept` identical f1–f910; Tear state identical to f1117.
  - ✅ Queue advance dispatches on the entry's KIND (FUN_00462403 0x46324b):
    f404 → by bb0; debug dddb8 → by dddc0; else kind 4/3/2 → b5a8 5/4/3 (+ text
    macros), 1 → FUN_00461792 seller (`src/customer_seller.c`), 0 → kind-select /
    FUN_00460fa7 oder. rhost shadow `seller_select` 11 calls / 0 div (kyaku≠18
    path only). `portrng buy-40pct-accept` identical f880–f1086 (was f911).
  - ✅ FUN_00465372 (b5a8==0 buy machine: customer sells; offer_down, gold-capped
    digit edit), FUN_00461068 (idle shelf sales, odd b528 && !f404), and Tear's
    FUN_0048a833 tick moved INTO the master tick head (0x4624a1, before the b534
    dispatch — retail's draw order). **`portrng buy-40pct-accept`: per-tick draws
    IDENTICAL over the whole scenario (f1–f2450)**; cs b534/b544/b584/b574/b5a8/b56c
    identical every frame.
  - ★ next, earliest port-side split per shop scenario (`portrng SCEN --window 1:N`):
    1. gsim +1 sparkle phase (Phase-0 item 1 above): day2-shop f771,
       day2-buy-tutorial f1283.
    2. buy-counter-raise-accept f725, buy-45pct-accept f1694: retail draws in
       FUN_00460a1a (cs_pick_line), the port does not — find which b534 state.
    3. buy-35/50pct-accept f1125/f1416, buy-lowball-walkout f1297: retail 6× NPC pump
       (FUN_0047019f) + 1× pick_line; port 6× Tear wing sparkle (`init_type_1f_100`).

### Regression gate (run before claiming a port change is safe)
- `python3 tools/rhost/rhost.py verify`: shadow hooks over all scenarios, 0 divergences.
- `rhost.py portsave` on {house-pause-save-commit, house-pause-save-commit-empty,
  day2-save-commit}: all **IDENTICAL** (2026-10-14, after buy machine + shelf sales +
  Tear order + D_TUT_LOAD). NB portsave/portstate/portrng do NOT rebuild
  `build/openrecet.exe` — `make -C src` first (a stale exe once faked a regression).
  day2-save-commit's equality leans on 64 Frida-era `{rngseed}` pins.
- `rhost.py verify` shadows floor_layout + actor_spawn_reset + seller_select.
- `rhost.py portanchors house-idle`: 195/195; house-customer-tutorial: 64/64.
- `portstate --va` takes any working-bank dword VA (0x44e3798 + slot·0x2dfc8 + off).

### Re-verification program (owner: "re-verify everything with the new methodology")
Every earlier "1:1"/"verified" claim is UNVERIFIED (`docs/findings/confirmed-parity-ledger.md`).
Re-check each flow by shadowing its ported functions over rhost scenarios, in this order:
1. **Customer service + haggle beyond the first customer.** `day2-shop`,
   `day2-buy-tutorial`, `multiday-tutorials-suggest`, `buy-from-seller-guild-intro`,
   `debt-gameover-restart`, `buy-{lowball-walkout,50,45,40,35pct-accept,counter-raise-accept}`:
   decide 80 / offer_up 47 / offer_down 10 calls, 0 div. Next: the port side (shop entry
   diverges, above). Buy-side accept / counter / walkout is decided in the caller
   (offer vs floor), not in `haggle_decide` (= the ±0.5% / ±5% price grade) — unhooked.
2. **End of tutorial.** One very long cutscene plus load seams — the stress test for long
   cutscenes and loaders under DEFER.
3. Title / intro / prologue; house free-roam, pause menus, save commit (`save_compare.py`);
   guild / world map; day 2.
Per flow:
1. list the ported fns it runs (rhost `--coverage` ∩ port ledger);
2. add shadow hooks (`rh_hooks.c`) and register them in `rhost.py` `VERIFY_HOOKS`;
3. `rhost.py verify` = one table of calls/divergences over all scenarios.

2026-10-13 verify: haggle_decide 20 calls / 0 div (8 scenarios); offer_up 22 / 0;
rand 1,156,264 / 0 (68 scenarios). + day2-shop, day2-buy-tutorial: decide 16 /
offer_up 15 / **offer_down 2** / rand 55,348, 0 div; + multiday-tutorials-suggest: decide 24 /
offer_up 15 / rand 126,716, 0 div; + buy-from-seller-guild-intro: decide 8 / offer_up 4 / offer_down 1 /
rand 91,150, 0 div. 2026-10-14 (+7 buy/debt scenarios): decide 32 / offer_up 13 /
offer_down 7 / rand 177,676, 0 div. Hooks now log every haggle call (`haggle … f…` in
rhost.log: base / ask / offer / floor / acc).

### Human queue (report at session end; ranked by what it unblocks)
- (empty) Haggle cases recorded 2026-10-14: refusal walkout, 50/45/40/35% accepts,
  counter-then-accept, week-1 game over.
<!-- FRONT:END -->
