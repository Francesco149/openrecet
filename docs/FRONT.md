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
    1. **Tick-0 label offset.** `gsim` (`DAT_0438b8cc`) is port = retail+1 at every
       label. rhost labels retail's first tick 1; the port labels its first tick 0 and
       that tick is logic-dead (anchors still align by label). Every `%8` consumer
       (display sparkle `%8==3`) therefore fires a frame early. Decide: drop the dead
       tick + relabel, or don't count it. Check the ms clock (port vms=(L+1)·17 vs
       rhost L·17) and other per-tick counters with portstate before choosing.
    2. ✅ cs d3e load under DEFER: the port snapshotted b1cc before applying the
       worker's release (Frida-era compensation) → master tick + Tear a tick late.
       Fixed 2026-10-14; house-customer-tutorial portanchors first mismatch is now
       #20 HOUSE_FREEROAM f8933/8934 (was the 2nd d3e load, ~f448).
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
  - ★ next, in frame order (buy-40pct-accept, 2.4k frames):
    1. **Queue advance dispatches on the queue entry's KIND** (FUN_00462403,
       all_ida 65233+770..812): `k = DAT_0730aca8[6·DAT_0730b1a8[b318]]`;
       4 → b5a8 = 5; 1 → FUN_00461792 (the customer SELLS to the player: every
       buy-* scenario); 0 → [rand&3 if work byte +0x2bcd1] FUN_00461303 (kind
       select) else FUN_00460fa7; 2/3 → other arms. The port only has the k = 0
       path (`cs_queue_advance` → `cs_kind_select`, keyed on the wrong variable:
       `PORT-DEBT(cs-queue-advance-fileidx)`). f911 buy-40pct-accept: retail 1
       draw in FUN_00461792 vs the port's kind-select + 40-draw shuffle. Port
       FUN_00461792 (1124 B) + FUN_00460fa7 (106 B) + the dispatch, then follow
       the buy-side state machine (offer_down is already ported + verified).
    2. Tear diverges at f1118 (x) — recheck after item 1 (`portstate --va
       438be6c,438be70,56da1f0,56da1f8`).

### Regression gate (run before claiming a port change is safe)
- `python3 tools/rhost/rhost.py verify`: shadow hooks over all scenarios, 0 divergences.
- `rhost.py portsave` on {house-pause-save-commit, house-pause-save-commit-empty,
  day2-save-commit}: all **IDENTICAL** (2026-10-14, after PC53 + wander).
- `rhost.py verify` now also shadows floor_layout + actor_spawn_reset.
- `rhost.py portanchors house-idle`: 195/195.

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
