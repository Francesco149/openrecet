# Save → working arena: the "load a game" data path

**Date:** 2026-06-04  ·  **Status:** W1 landed (working arena + per-slot load);
W2/M1 (continue picker + post-fade wiring) in progress.

This is the architecture behind "Continue / load a save". It corrects a
long-standing simplification: the port had **no working arena** at all — it
faked the per-stage record with four standalone selector globals
(`g_scene_*_selector`, see `stage_state.h`). Real save loading + everything that
reads live gameplay state (money, day, **items on display**, inventory) needs
the actual working arena.

## Two arenas, not one

The engine keeps **two** ~18 MB arenas, both 100 banks × `0x2dfc8` bytes
(`0xb7f2` dwords) behind a `0xb10`-byte shared header:

| arena | base (header) | banks base | role | port owner |
|---|---|---|---|---|
| **Save arena** | `DAT_056e5770` | `DAT_056e6280` | disk mirror of `save.dat`; what the loader fills | `save_bank.c` (`save_arena_base`) |
| **Working arena** | `DAT_044e2c88` | `DAT_044e3798` | the **live game state** gameplay reads/writes | `save_work.c` (`save_work_base`) **[NEW]** |

`bank_base = header_base + 0x0b10`; `bank N = banks_base + N*0x2dfc8`.
`(banks_base + 0x18)` (dword 6) is the start of the 20000-entry item-slot table.

The active working slot is `DAT_0438b1e0` ("current stage index"). Its **only
writer in the whole engine is `= 0`** (FUN_0049a59e L100601, new-game commit),
and BSS-init is 0 — so in practice **the live game always lives in working slot
0**; the 100 save banks are pure storage. Gameplay reads working slot
`DAT_0438b1e0` (≡ 0).

## The load primitives

### `FUN_00490259(src_save_bank)` → `save_work_load_slot`
The literal "load this save into the live game". Copies **save**-arena bank
`src_save_bank` → **working** slot `DAT_0438b1e0`, all `0xb7f2` dwords, then
recomputes the live item count:

```
piVar3 = work_bank + 6;             // item-slot table (dword 6)
copy 0xb7f2 dwords: save[src] -> work[active]
for (i=0; i<20000; i++)             // first empty (-1) slot = item count
  if (item_table[i] == -1) { work_bank[0xaec6] = i; break; }
```

`work_bank[0xaec6]` (`DAT_0450f2b0[slot*0xb7f2]`) is the **live inventory
count**. Called once from the continue slot-picker confirm (FUN_0049a59e
L100906).

### `FUN_004902aa` → `save_work_sync_from_save` (secondary)
"Clear all + persist": zero save magic, `FUN_004901c2` reinit (fresh), copy the
**whole** save arena → working arena (`0x47dd4c` = all 4,709,708 dwords), then
write the save arena back to `save.dat`. Used by the special/new-from-picker
paths (FUN_0049a59e L101012), not the normal continue.

## Title → load → game flow (FUN_0049a59e)

1. **Menu dispatch** (select countdown hits `0xf`, L101066):
   - codes **{0,5}** → NEW: `DAT_0438bed4 = 1`, bump `DAT_0964351c` (fade trigger).
   - codes **{1,4}** (and 6 survival) → CONTINUE picker: `FUN_0049b537()`,
     `DAT_09643524 = 1`, `DAT_0438bed4 = 0`, cursor = `DAT_056e578c` (last-used
     slot). ⚠️ Note the port's `SCENE_TITLE_MENU_*` names read backwards vs this
     dispatch (code 5 `CONT_HAS_SAVE` is the NEW arm); the **numeric code →
     behavior** above is authoritative.
2. **Slot picker** (`DAT_09643524 == 1`, L100795): 3-column grid cursor over
   `DAT_005d1bbc` (=100) slots. `DAT_09643530` cursor, `DAT_09643534` scroll,
   `DAT_096435{38,3c}` h/v scroll-anim. A/`0x10` confirm:
   - empty slot (`save_bank[sel] dword 2 == 0`) → error SE `0x16a`.
   - else `FUN_00490259(sel)` (load), `DAT_0964351c++` (fade), `DAT_056e578c=sel`.
   B/`0x20` cancel → SE `0x13d`, back to main menu.
3. **Post-fade commit** (`scene_post_fade_init`, FUN_0049a59e L100620+):
   - `DAT_0438bed4 == 0` (CONTINUE): read resume fields from **working** slot
     `DAT_0438b1e0` — substate `DAT_0438b1c0` (= `work[0xb378+...]`), etc.
   - `!= 0` (NEW): the existing `save_bank_init_one(0)` + `stage_post_load_init`
     path; must also seed working slot 0 from the fresh bank.

## `FUN_0049b537` → continue-picker slot-index init (trivial)
Fills `DAT_09643380 .. DAT_09643510` with identity `0,1,2,…` (a 100-entry slot
index array the picker indexes through) and sets `DAT_005d1bbc = 100`.

## Bank "occupied" marker
The picker's empty test reads **save**-bank dword 2 (`DAT_056e6288[sel*0xb7f2]`).
A played save writes nonzero there; a never-used bank is 0. (Our
`save_bank_init_one` "fresh" state must match whatever the retail new-game writes
to dword 2 — verify when wiring the picker; currently fresh banks leave it 0,
which would read as "empty", so a fresh-but-unsaved slot is correctly
non-continuable.)

## What this unlocks
Once a real save populates working slot 0, the **items-on-display** renderer and
the top HUD (money/day) can read live state from `save_work_*` instead of the
MVP hardcodes — that's the visible payoff (task D1+).
