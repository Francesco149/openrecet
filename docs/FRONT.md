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
    1. ✅ **Tick label = tick count** (2026-10-14): the port labelled tick #k as k−1
       and fed it trace input k−1, so gsim / the title counter / the ms clock ran +1
       vs rhost (label = ticks done). Now `tick_label()` = frame_count+1 everywhere
       the harness indexes frames, and the trace's frame-0 ops fire at the boundary
       before the first deferred workers (rhost `hook_tick` g_frame==0). house-idle:
       gsim, DAT_09643518, prev_ms IDENTICAL. DAT_073dfcfc still differs — retail
       resets it in FUN_0047cbcb and never reads it (write-only), the port keeps it as
       the label counter.
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
  - ✅ Arrival FX (b564 special-slot sparkles, FUN_00414766) + the merchant-rank
    arrivals (rank 1 → f428 scripted buy lesson, rank 2 → f429; was a stub):
    **`portrng day2-shop` IDENTICAL f1–f4700**, **day2-buy-tutorial identical
    f1–f15594** (was f1283).
  - ✅ b5a8==1 request machine FUN_00464af0 + the pick menu (FUN_00468338 modes 1/10,
    FUN_00469351, opened by the master tick once the poses settle). **portrng
    IDENTICAL over the whole scenario: buy-35/40/45/50pct-accept,
    buy-lowball-walkout, day2-shop.**
  - ✅ Hidden state splits found by a portstate sweep (cs b534/b5a8/b56c/b584/b574/
    bb8/b528/b548 + gold/EXP/item count, all 8 shop scenarios): the pick menu's
    empty list is just "Nothing" (retail num_tabs 0, list[0] −1); the reveal budget
    walks the macro-EXPANDED line (FUN_00465db4 pass 1); price scalars boot at their
    .data values. All cs rows now identical; only boot-time gold differs (below).
  - ✅ Companion skip frames: FUN_0048670f arms that end at LABEL_675 (display-stand
    open, door-exit arm + the exit-fade stage 2) run no FUN_004897c6 / FUN_0048a833,
    so Tear and db054 hold; the 目玉 sparkle prologue precedes the exit stage 2.
  - ✅ **Day close + day-end summary** (`docs/findings/day-close-RE.md`): the cs
    leave's shop-full arm (shop-time 4) + the town-return period advance →
    FUN_0048526d (in-scene FUN_00436f97: floor layout + actor reset rand, NPC
    warmup re-arm) → the b4e0 night exchange (variant lines + voices through the
    actor-say driver FUN_00485861 / speech bubble FUN_00406159, Tear face-to-face,
    window NPCs hidden) → pause action 1: FUN_0048da71 / FUN_0048dbfb / FUN_0048ef00
    ("Daily Profits" count-up, 1 rand/frame). buy-counter-raise-accept: portstate
    scene / cc08 / b4e0 / variant / b764 / b76c / fb88 / player / Tear rows
    IDENTICAL; **portrng IDENTICAL over the whole run** (was f7155).
  - ✅ **The market trip + the return home + the Day-3 morning** (PROGRESS 2026-10-14
    afternoon): world-map exit (FUN_0049e163 runs FUN_00436f97 for every destination;
    home → b928 = 2, door seat), the guild tick FUN_004922c0 on retail's control flow
    (Leave fades, purchase bookkeeping), FUN_0045de68, the whole FUN_0044bd0d cascade,
    the return beat, the display-menu re-open clamp, scripted-sell ops 10/11,
    FUN_0049126b. **`multiday-tutorials-suggest`: portrng + portsave IDENTICAL over the
    whole run (f1–f34810), pin-free.**
  - ✅ **The Adventurers' Guild, mode 11** (`docs/findings/adventurers-guild-RE.md`):
    loader FUN_0045bdc2 + FUN_00473874, update FUN_0045c051 (the intro FUN_0044c7c3
    iv2_7 → iv2_8 → world map, and the whole menu machine), render FUN_0045cc85 /
    FUN_0045cc1e, the party list FUN_0047f1ce (pause "Status" after Louie joins), the
    FUN_00434ceb box state; the dialogue tick runs in every LABEL_106 mode.
    **`buy-from-seller-guild-intro`: portsave + portrng IDENTICAL f1–f26774, pin-free**
    (now in `gates`).
  - ✅ **The guild menus + the Merchants' Guild pause** (`adv-guild-pack-leave`, native,
    PROGRESS 2026-10-14 night): Continue bag size, the mode-6/11 unpause restore
    (FUN_00468338 rebuild + FUN_0049161d / FUN_0045bd36), the item-window price spill +
    trend, the 3-pass mode-11 item window, the message box FUN_00435117, row dimming
    (FUN_00468ddc). portstate mode / guild / bag VAs identical f1–f17745; portshots 7
    frames gt8 ≤ 0.04% (residual: the hand cursor's anti-aliased outline, position
    identical — texture/sampling detail, unexplained).
  - ★ next, earliest port-side split per scenario (`portrng SCEN --window 1:end`):
    1. **The dungeon pilot — all three recordings pass end to end** (PROGRESS 2026-10-15):
       dungeon0-floors-save (portrng + portsave), **dungeon0-full (the whole dungeon, the
       boss, the run's end, home, a save: portrng IDENTICAL f1–f44741 + portsave
       IDENTICAL)**, **dungeon0-wipe (portrng IDENTICAL f1–f6257)**; all in `gates`. Next
       needs recordings of dungeons 1+ (Human queue). Hidden state `portblock` still shows
       at their ends (not RNG-visible yet):
       - the house loader (`scene1_preload.c`) runs its own partial copy of FUN_00436f97
         instead of `scene1_postload_stage_entry_init`: no FUN_00435fbb(0, -1) step on a
         home load (drop rows bef4 / bf24 one step behind), the map objects bfb0.. differ;
         unify it (`PORT-DEBT(simplified, FUN_00436f97)`);
       - the house walker's history ring (party +0x224) is one entry off (legacy walker,
         item 2); DAT_00648294 (the got-item window) is never cleared;
       - records' MATRIX0 (+0xc8) come out transposed for some table-B kinds (render only);
       - the Continue init FUN_00435c98 lands at f345 (retail f329);
       Still `PORT-DEBT(stub)` on the dungeon path: the other enemy types' AI (slimes 0–5,
       type 16 and the fliers 17..21 / 56..58 done), the golem parts / multi-part anchors,
       FUN_0042e791 types 25/28/41/73, FUN_0042df55's lists for stages >= 20, the other
       chars' swings / skills, FUN_0042f7ad's boss arms, FUN_0042ea35, FUN_004426a7 (the
       cinematic camera), FUN_00474d92 (the D3D teardown), the remaining FUN_004147d5
       overlay stubs, dungeons 1..5's events / setups (FUN_0044d47d, FUN_0044d1e4 …);
       render: no dungeon renderer, the results screen, the pause Items choice / messages;
       the legacy table-B tick bodies beyond 2 / 11 / 13+21 / 31 / 34 / 44 / 84 / 86 are
       unverified (re-check each against the disasm as a recording reaches it).
    2. ★ **debt-gameover-restart f7383** (re-diagnosed 2026-10-15; not the day-8 scene —
       retail never sets work +0x2bcd8 in this run): retail opens the display-stand menu
       (cc04 0 → 1, the faced cell cbfc/cc00 = 3/4 from f7359), the port faces no cell (-1).
       Root: the legacy house walker (`scene1_player_ctrl.c`, semantic). Its facing is the exact
       ±π/2 (0x3fc90fdb) where retail's is atan2-derived (0x3fc90fda) from f2700; the
       position drifts by ulps from f6805. Port the house FUN_0048b850 / FUN_0048cdcc /
       FUN_00483170 path on the party block (the dungeon's `dungeon_party.c` /
       `party_move.c` already do it) and FUN_0048619f (the faced-cell detector) from the
       disasm, and retire `PORT-DEBT(semantic-collision)`.
       The day-8 visitor itself (FUN_004706ea / FUN_00470a46 / FUN_00470970 / FUN_004708f7,
       FUN_0048670f's +0x2bcd8 block, `src/story_visitor.c`) is ported from the disasm but no
       recording reaches it (render: `PORT-DEBT(render-coupled, FUN_00470a46)`).
    3. Still `PORT-DEBT(stub, …)` around the day: FUN_004852fb's day-15 arm, the
       payment screen (FUN_0048e49c + FUN_0044ba2c scripts), the week-end story /
       game-over scenes (FUN_0049d2e5 / FUN_0049da9e), the b924 50/110/150 morning
       voices (74b2ee8), b928 = 0 at a dungeon entry (FUN_0045c051). The night arm of
       the dungeon exit (FUN_00442cef all_ida 44950) waits for the dungeon port.
    4. rank ≥6/≥14 story triggers FUN_0044ba2c(19,1|2,0) — `PORT-DEBT(rank-arrival-tail)`.
    5. Unported machines b5a8 3/4/5 (FUN_004639f5 / 463cfb / 464a26: request
       delivery + story customers) — no scenario reaches them yet.
    6. Guild: `PORT-DEBT(guild-fusion)` (modes 5/6, the recipe list), the Sell commit's
       FUN_00468fd0, the expansion's FUN_004864fd, the variant-1 market's textures,
       FUN_004682ee (bag picker's first-wearable tab); FUN_0044ba2c's `mode` argument
       (what runs at a dialogue's end) is not modeled.
    7. "Now Loading" on a dialogue's arm frame: retail draws the disc (gate 2 →
       DAT_06a49960) at f11003 of buy-from-seller-guild-intro, the port does not — sim.c
       latches nowloading at the tick head, before the scene arms the .ivt load (render
       only; the anchors fold the same state, so re-check portanchors when moving it).
    8. Known float drift: Tear x differs by 1 ulp from f10765 in multiday (portstate
       56da1f0) — not RNG-visible yet; x87 sweep candidate (FUN_0048a833/a4d1).

### Regression gate (run before claiming a port change is safe)
- **`python3 tools/rhost/rhost.py gates`**: the port rows below in one command, one line
  each: portsave ×7 (incl. the dungeon Save + dungeon0-full), portanchors ×2, portrng over
  11 shop/day scenarios + dungeon0-floors-save / -full / -wipe + adv-guild-pack-leave, each
  to the retail run's end.  Don't build the port while it runs (each row uploads
  build/openrecet.exe).
  Don't run other rhost jobs meanwhile (they share the port exe upload).
- `python3 tools/rhost/rhost.py verify`: shadow hooks over all scenarios, 0 divergences.
- `rhost.py portsave` on {house-pause-save-commit, house-pause-save-commit-empty,
  day2-save-commit, multiday-tutorials-suggest}: all **IDENTICAL** (2026-10-14, after buy machine + shelf sales +
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
- **Record a dungeon-1 run** (unblocks the next enemy types, traps and events; dungeon 0 is
  done end to end). On maximus, the user plays on the Windows desktop:
  `python3 tools/rhost/rhost.py record dungeon1-run --save <sha>` from a save that has
  dungeon 1 open (or play dungeon 0 again first in the same recording). Play: into the
  dungeon, fight every enemy kind, open chests, use an item from the pause menu, take the
  exit portal or reach the boss, let the run end and return home.
<!-- FRONT:END -->
