# Shop "items on display" renderer — RE status (NOT yet portable)

**Date:** 2026-06-04 · **Status:** UNMAPPED — do not port blind. This doc
records two corrections that kill the obvious-but-wrong leads, and a sound plan
to actually locate the renderer.

## Why this doc exists
After the load-a-save arc landed (W1/M1/W2 — the working arena + continue
picker), the next goal is rendering the merchandise the player has placed on the
shop's display furniture in the HOUSE free-roam scene. A fan-out search produced
a confident-looking map that is **wrong in two ways**. Both are documented here
so nobody burns a session porting the wrong function.

## Correction 1 — `FUN_00456f56` is the CHARACTER walker, not item display
The search claimed `FUN_00456f56` is the "master loop over 100 display records".
It is not. Per [[openrecet_scene1_render_ladder]] / STATUS.md and the
char-sprite findings, `FUN_00456f56` is the **chr-sprite walker** (dormant; its
actor/party array `DAT_056dacc0` has no live writer). Verified: `DAT_056dacc0`
is referenced exactly once in the whole decompile — inside `FUN_00456f56`
(all.c:52465) — consistent with "actor array", not "100 bank item records". The
standing player/companion are drawn by `FUN_004552d0` (shop-walker) reading the
`DAT_056daae8` position ring. None of these touch on-display merchandise.

## Correction 2 — bank dword `0x9e76` is the RANKING table, not shop display
`save_bank.h` labels bank dwords `0x9e76..0xa586` (100 records × 18 dwords) as
"item-grid scratch", which made it the obvious display-placement candidate.
**The only engine reference to that region (byte offset `0x279d8`, based global
`DAT_0450b170`) is `FUN_0049f012`** (all.c:103801) — the **RANKING screen**
builder, called from title menu code 7 (`FUN_0049f012(1)`). It zeroes the 100
`(id,count)` record heads, then condenses the global ranking table
`DAT_095d3808` (stride `0xb3` = 179 dwords × 100 entries) into them via an
item lookup `FUN_004681f6(id*100)`. So `0x9e76` is a **per-bank ranking
summary**, not the shop-floor display. (Recommend re-labelling it in
save_bank.h: `0x9e76` = ranking-summary records, `DAT_0450b170` based.)

Net: neither the function nor the bank region the fan-out identified is the
shop display. The shop-display item state is most likely a **runtime structure**
(not a saved bank field) populated by the shop-management "place item" UI, and
drawn somewhere in the HOUSE 3D pass.

## How to actually find it (next-session plan)
The reliable method here is the project's own call-graph capture, NOT static
guessing ([[feedback_full_path_call_graph]], [[reference_tas_anchor_forcing]]):

1. **Get a retail HOUSE frame WITH items on display.** Use a save that has
   merchandise out for sale (the user's real save likely qualifies — loads at
   boot into the save arena). Drive retail to free-roam in the shop via the TAS
   anchor-forcing harness (`--input-segtrace`, FREEROAM_START anchor).
2. **Frida call-graph diff:** capture the per-frame call graph (the E.1/E.2
   tracer, `tools/frida_capture.py --call-trace`) on TWO retail states — shop
   with displayed items vs. an empty shop — and diff. The functions that fire
   only when items are displayed are the renderer + its driver loop.
3. **Find the runtime display array:** mem-watch (`tools/mem_watch.py`, the D.7
   tool) the writes that happen when an item is placed on a stand, OR static-
   trace back from the renderer found in step 2 to the array it iterates.
4. Candidate billboard primitives to expect at the bottom of the chain:
   `FUN_0045a56f` (sprite billboard → DrawPrimitiveUP) and/or the mesh helper
   `FUN_00455191` — but confirm via the call-graph diff, do not assume.

## What IS solid (the load arc this unblocks)
- Working arena (`save_work.c`) loads a chosen save into live slot 0 (W1).
- Continue picker (`title_continue_picker.c`) drives the load (M1).
- Post-fade branches new vs continue (`scene.c`, W2).
Once the real display renderer + its source array are identified, the loaded
working bank (or the runtime display array it feeds) is the data source — that's
the visible payoff. See [[save-working-arena.md]] (docs/findings/).

## Smaller, safer "missing for this game state" wins (no blind RE)
If the display renderer RE stalls, these read known working-bank fields and are
lower-risk than the unmapped display path:
- **Top HUD** (clock / day / money): money = working bank dword 3, day =
  `0xb0fe`, week = `0xb0fa`. STATUS.md already flags the top HUD as the next
  target (overlay-registrar unported). Wire it to `save_work_dwords_at(0)`.
