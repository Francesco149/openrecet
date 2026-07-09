# Live playthrough — day 1 → day 2 (probe-harness ground truth)

Discovered driving retail LIVE via the `openrecet` MCP / probe daemon
(2026-07-09). Ground truth for the new-game → day-2 path; feeds future
deterministic-trace anchoring.

## Boot → shop (observed)
- Title (scene 0). NEW GAME = top item; cursor starts on LOAD GAME → tap `up`
  then `a`.
- `a` on NEW GAME → scene 1 (INGAME) immediately, prologue cutscene: dlg=1,
  player_st=6 (conversation pose), gold=1000, day=0 (HUD "Day 1"),
  px=-0.30 pz=9.35. RNG consumed off the 19937 pin.
- Anchor burst at new-game: BOOT NEW_GAME LOADING_START CONV_POSE_START
  LOADING_END HOUSE_FREEROAM EXTRA_SPRITE_START/FADED_IN/FADEOUT/END
  TEXT_ANIM_START DLG_LINE_SHOW … (CONV_POSE_BLINK fires every blink — cosmetic
  noise, filter it).
- Spam `a` (~60 taps in turbo) advances the whole Tear/Recette prologue →
  dlg=0, player_st=0 (controllable), scene 1, cc08=1 free-roam, in the shop.
  HUD: Day 1, 1000 pix, Merchant Level 1.

## Free-roam controls (measured)
- Movement axes: left/right = -/+ player X (DAT_056da1d8); up/down = -/+ player
  Z (DAT_056da1e0). py≈0 (height). Walk changes player_frame (anim).
- `e` (0x100) = PAUSE MENU (scene 9): calendar (Today=1, "10,000 pix Payment
  Due!" on day 8), Items/Encyclopedia/Options/Save/Exit. `b` closes (→scene 1),
  `e` toggles. NB the `pause` VA (0x0438b150) stays 0 for this menu — scene==9
  is the signal, not the pause flag.
- `a`/`b`/`c`/`d` do nothing in empty free-roam (no interactable in front).

## Day-1 → day-2 is SCRIPTED (not free-action)
The transition rides the tutorial dispatch chain FUN_0044bd0d
(`src/scene1_tutorial_dispatch.c`), gated on story flags in the working bank
(base DAT_044e3798). The day advances at **iv2_3** (`fb84++`, DAT_0450fb84).
Chain: arrange stands (iv1_5 back-row placed / iv1_6 all filled) → open shop →
first customer haggle-sell (iv1_7 cs-close f400 → iv1_8 cs-leave f402) →
iv2_1/iv2_2/iv2_3(**DAY ADVANCE**) → iv2_5 → 190-frame "looks up at Tear" beat
(b924<0xbe) → iv2_6 (day-2 load) → day-2 brooming. So reaching day 2 REQUIRES
serving the first customer — pure `a`-spam in free-roam does not advance it.

Key flag offsets (bank-byte, rel DAT_044e3798), from the dispatch source:
- iv1_5 back-row: cond 0x2bc63, done 0x2bc64
- iv1_6 all-filled: cond 0x2bc65, done 0x2bc66/67
- iv1_7 trigger 0x2bc68 (cs-close), done 0x2bc69
- f406 0x2bc6e → iv1_8; iv1_8 trig 0x2bc6a (cs-leave f402), done 0x2bc6b
- iv2_3 done 0x2bc76 = the day-advance point
- day counter dword 0x2c3ec (DAT_0450fb84, HUD +1); shoptime dword 0x2c3f0

## Curated state VAs (probe `state`)
scene 0x0438b1c0 (0 TITLE / 1 INGAME / 8 LOADING / 9 PAUSE-MENU) · rng 0x006023a0
· cc08 0x0438cc08 (1 free-roam / 4 customer-service) · dlg 0x0438b1c8 · pause
0x0438b150 · nowload 0x06a49958 / nowload2 0x06a49960 · worker 0x06a49954 ·
player_st 0x056daafc (6=conv pose) · player_fr 0x056daaf8 · px/py/pz
0x056da1d8/dc/e0 · gold DAT_044e3798+0xC (0x044e37a4) · day 0x0450fb84 · shoptime
0x0450fb88.

## Shop-open mechanic (RE'd from all.c:87617-87632, FUN_00488xxx free-roam ctrl)
The day-1 loop, exact gates:
1. **Place an item on a display stand** (→ iv1_5). Near a stand, the interaction
   system selects a stand cell into **DAT_0438cbfc** (col) / **DAT_0438cc00**
   (row) via `FUN_004860c8(cbfc,cc00)`. Pressing **A (0x10)** at a selected stand
   arms the placement (sets DAT_074b2ec4=1), gated on `DAT_0450f3f7[iVar15]==0`.
   The stand's item slot is `&DAT_044f7030 + (cbfc + cc00*0x14)*4 + iVar15`.
2. **Open the shop** = **C button (0x40)** while a SELECTED stand HAS an item
   (`DAT_044f7030[...]!=-1`) AND tutorial gate `DAT_0450f3f2[iVar15]!=0 &&
   DAT_0450f400[iVar15]==0` AND `DAT_0438cbfc!=-1`. On success:
   `FUN_0047f1ce(); DAT_0438cc08=0x32; FUN_00468286(); FUN_004681db(2);
   FUN_00499519();` → cc08=0x32 (shop open for business).
3. First customer arrives → haggle-sell → cc08 4→1 leave sets f402 → iv1_7/iv1_8
   → iv2_1/2/3 (**DAY ADVANCE fb84++**) → iv2_5/6 → day 2.

Input edges are read from **DAT_073dddd4** (the pressed-this-frame edge mask,
derived by the engine from the held mask DAT_073dddd0 that the probe writes):
bits & 0x10=A, & 0x20=B, & 0x40=C.

## BLOCKER for auto-completion (next-session RE task)
`DAT_0438cbfc` does NOT arm on `teleport` — it's set by the proximity/facing
interaction system (FUN_004860c8 + the cc04 walk sub-state), which teleport
bypasses. To place an item live: WALK Recette into a stand facing it (not
teleport), watch DAT_0438cbfc go valid, then tap A. Map the stand world coords +
the arm condition once, save them as waypoints, and the stock→open→sell loop
becomes scriptable. Alternatively (user-endorsed) call FUN_00488xxx's placement
path / set the stand slot + iv1_5 flags directly ONCE each poke is confirmed to
reproduce the input's code path (ghidra-mcp the handler, diff state vs the input).

## ✅ DAY 2 REACHED (2026-07-09) — via the faithful flag cascade
Confirmed live: new game → prologue → day-1 free-roam → **day-2 morning bedroom
cutscene**. On a fresh day 1 the inventory is EMPTY + stocking LOCKED (f3f2=0),
so hand-placing an item isn't possible; instead the real dispatcher FUN_0044bd0d
was driven by poking the iv1_5→iv1_8 flags (it fired the REAL tutorial dialogues),
and iv1_8 auto-cascaded iv2_1→iv2_3 (**fb84++ day advance**)→iv2_5/6. Full step
list = `game-recipes.md` R3. Button layout CONFIRMED by the ghidra deep-dive:
edge mask DAT_073dddd4 bits A=0x10(Z,interact/place), B=0x20(X,cancel),
C=0x40(**opens shop** in the day-1 controller). cur=DAT_073dddd0 (probe writes),
edge=~prev&cur. Placement id source = FUN_00469a9f (carried-item from the
place-list); raw inventory = DAT_044e37b0 (=bank+0x18, entry catalogRow<<6|qual,
-1 empty). Stand-cell writer = FUN_0048619f (position→cell, needs the WALK sim;
teleport can't arm it — the reason move-to-a-stand must walk, not teleport).

## Harness status (2026-07-09)
Validated live end-to-end: title → new game → prologue → day 1 → **day 2**.
Working: button input (faithful mask path), movement, pause menu, teleport
(collider-clamped), set_facing, set_gold, move_to/waypoints, anchor stream,
memory read/poke, engine-thread call_function, screenshots, no-focus preview
window, and faithful flag-cascade tutorial advance.
