# Game recipes — reusable action sequences (live-probe harness)

Growing log of INPUT/POKE sequences that get past specific game sections, so
they can be **replayed** (drive the harness back to a spot to re-probe) or
**baked into a `tests/scenarios/` trace**. Each recipe = preconditions →
steps → resulting state (+ anchors seen). Driven via the `openrecet` MCP (or
`tools/probe.py`). Buttons: Z=interact=mask `a` (0x10), X=cancel=`b` (0x20),
C=camera=`c` (0x40), `up/down/left/right`=dpad. ESC=skip/pause (keyboard path).

Conventions: prefer a **faithful poke/call** over a fragile input sequence for
navigation/setup ONCE it's confirmed to reproduce the input's code path (user
directive); pin RNG (launch `rng_seed`) when a recipe must be deterministic;
record the anchor+rng at each boundary so the recipe can seed a trace's
`{wait}`/`{rngseed}` ops.

---

## R1 — Title → new game → day-1 shop free-roam
**Pre:** fresh launch, title screen (scene 0). Cursor starts on LOAD GAME.
**Steps:**
1. `press up` (cursor → NEW GAME), `press a` (confirm). → scene 1 (INGAME),
   prologue cutscene: dlg=1, player_st=6 (conv pose), gold=1000, day=0.
2. Advance the prologue: `press a` ~60-70× (2-on/3-off taps) in turbo. The
   Tear/Recette intro plays out. → dlg=0, player_st=0 (controllable), in the
   shop. HUD: Day 1, 1000 pix, Merchant Level 1. px=-0.30 pz=9.35.
**Anchors:** BOOT NEW_GAME LOADING_START CONV_POSE_START LOADING_END
HOUSE_FREEROAM EXTRA_SPRITE_* TEXT_ANIM_* … (CONV_POSE_BLINK = noise, filter).
**Faster alt:** ESC on the prologue = "Event Skip" (shows the hint). Skipping
may alter the tutorial entry — validate before relying on it.
**Note:** RNG diverges off the launch pin during the prologue (the intro
consumes it) — for a deterministic replay, pin at a later anchor (LOADING_END).

## R2 — Free-roam controls (day-1 shop) — reference
- Move: left/right = -/+X (DAT_056da1d8), up/down = -/+Z (DAT_056da1e0). py≈0.
- `esc`/scene 9 = PAUSE MENU (calendar, Items/Save/Options/Exit). `b` closes.
  (The `pause` VA 0x0438b150 stays 0 for this — scene==9 is the tell.)
- `c` (C) = change camera. `a` (Z) = interact (contextual; nothing in open floor).
- Teleport (cheat) clamps to colliders: out-of-room targets snap to nearest
  valid spot. Central red area (2x2 grid on the rug) did NOT arm a stand cell
  from the front — likely a rug, not the placement stand.

## R3 — Day-1 → day-2 via the tutorial-flag cascade ✅ CONFIRMED (2026-07-09)
**Pre:** day-1 shop free-roam (after R1). slot 0 (DAT_0438b1e0=0 ⇒ iVar15=0).
On a fresh day 1 the inventory is EMPTY and stocking is LOCKED (f3f2=0), so
hand-placing an item isn't possible — drive the **real dispatcher FUN_0044bd0d**
by poking the tutorial flags (it fires the REAL tutorial dialogues, e.g. iv1_5 =
Tear "Recette. May I speak with you a moment?"). All flag VAs are bytes at
`<VA>+iVar15` (iVar15=0 for slot 0). Between groups, ADVANCE the dialogue with Z
(`press a`) until `dlg`(DAT_0438b1c8)==0 — never arm the next trigger while a
dialogue is up (1-frame iv1_5/iv1_6 clobber hole, port note).
**Steps (each: poke trigger byte=1 → wait for done-flag → Z-clear dialogue to idle):**
1. `poke 0x0450f3f2=1` (unlock stocking) + `poke 0x0450f3fb=1` → iv1_5 fires
   (dispatcher sets 0x0450f3fc=1, dialogue up). Z-clear.
2. `poke 0x0450f3fd=1` → iv1_6 (done 0x0450f3fe=1). Z-clear.
3. `poke 0x0450f400=1` → iv1_7 (done 0x0450f401=1; sets f406). Z-clear.
4. `poke 0x0450f402=1` → iv1_8 (done 0x0450f403=1) → **auto-cascade**
   iv2_1→iv2_2→iv2_3 with NO further pokes (keep Z-clearing dialogue). iv2_3 =
   **DAY ADVANCE**: dword `0x0450fb84`++ (day 1→2), fb88=0, resets f3f9/f408/
   f3f7/f400=0, f3f2=1, arms iv2_5.
5. Keep clearing dialogue; iv2_5 (0x0450f411=1) arms the 190-frame "looks up at
   Tear" beat (b924<0xbe), then iv2_6 loads day 2 → the **day-2 morning
   bedroom cutscene** (Recette "Nnmnmmn... Tear...").
**Result:** day=2 CONFIRMED — after the iv2_5/6 cutscene (day-2 morning bedroom
scene, reuses the prologue room — advance it fully before judging: it lands in
the day-2 SHOP with HUD "Day 2"). Driver: `scratchpad/cascade.py` pattern (poke
byte, poll done-flag, tap `a` until dlg idle).
**CAVEAT — not economy-neutral:** the poke cascade left **gold 1000→1100** (a
+100 artifact, likely an iv1_8/sale-adjacent side-effect the poke fired without
a real sale). Fine for reaching day-2 STATE; NOT a clean economy for
sale/haggle-accuracy work — for that, drive a real sale (open shop + serve a
customer), don't poke f402.
**Faithfulness:** exercises the exact dispatcher the real sale would drive; the
iv1_7/iv1_8 flags (f400/f402) are what the cs-close/cs-leave sale path sets, so
poking them = "the first customer was served" without the haggle subsystem.
**For a gameplay-faithful sale instead:** open shop = **C** (0x40 edge) while
standing on a STOCKED back-row stand (needs an item placed first: WALK—not
teleport—onto the stand so FUN_004860c8 arms DAT_0438cbfc, `press a` to open the
place-list, pick item, `press a`). cc08→0x32; customer auto-approaches; haggle
sets f400 then f402 naturally. See `findings/live-playthrough-day1-2.md`.
