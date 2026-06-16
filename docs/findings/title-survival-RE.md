# Title Survival difficulty selector — code 6 overlay — RE + port

The title menu's **"Survival"** row (item code 6) opens a small two-option
difficulty selector — **"Survival Hell"** / **"Normal Survival"** — that slides
IN over the still-visible main menu. Unlike every other title submenu it is
**NOT a submenu_state**: it lives in its own counters (engine
`DAT_096435{50,54,58,5c}`) while `submenu_state` stays 0, so the main menu
renders behind it the whole time. Picking a difficulty + A hands off to the save
picker (start-survival slot pick); B slides it back.

## Engine functions / port map

| engine | role | port |
|--------|------|------|
| `FUN_0049a324` | save-bank scan → `uVar1` unlock bitmask (Survival = `uVar1==3`) | `save_io.c` `save_io_scan_for_title_menu` |
| `FUN_0049a59e` code-6 arm (`LAB` @ 0x49b124) | dispatch: `survival_state=1`, `survival_option=0`, snap cursor (170,272) | `scene_title.c` `scene_title_sim` code-6 branch |
| `FUN_0049a59e` selector SM (0x49b138-0x49b195) | the `DAT_09643550>=1` arm: ramp/toggle/B-cancel/A-confirm | `scene_title.c` `scene_title_survival_selector_tick` |
| `FUN_0049c644` @ 0x49cbe8 (`if 0<DAT_09643550`) | the selector render (backdrop + 2 labels) | `scene_title.c` `scene_title_render` survival block |
| `FUN_0049b4f4` | survival-eligible bank filter for the picker | **PORT-DEBT(survival-picker)** |

## Menu unlock (code 6) — and the `save_io` bug it exposed

`FUN_0049a324` returns `uVar1==3` (both bits) only when SOME save bank satisfies:

- **bit 1**: `bank[2]` (OCCUPIED / play-time) > 0 **AND** `bank[0xb759]`
  (GAME_MODE) == 3.  (The decompile labels these "adventure-2 cleared"; in the
  port's bank layout — `save_bank.h` — it is the GAME_MODE tag == 3.)
- **bit 2**: one of `bank[6 + j]` for `j` in `[0, bank[0xaec6])` (ITEM_COUNT)
  has `(entry >> 6)` in `[0xd49 .. 0xd50]` (an "adventure-8" item id).

`scene_title_menu_init` adds code 6 when `uVar1 == 3` (a literal `== 3`, not a
bit test).

**Bug found + fixed (the porting loop caught it):** `save_io_scan_for_title_menu`
read the item count from `bank[0]` instead of `bank[0xaec6]` (ITEM_COUNT, engine
`local_c = *piVar3` where `piVar3 = bank + 0xaec6`; the item base is `piVar3 -
0xaec0 = bank + 6`). `bank[0]` is a zero field, so the item loop was a no-op and
`has_any_adv8_cleared` never set ⇒ Survival could never unlock. The bug was
latent because no prior save had GAME_MODE==3, so the earlier `bank[0xb759]!=3`
test short-circuited before the loop. The crafted survival save is the first to
reach it: **retail unlocked Survival, the port did not** — the divergence pinned
the fix. (`bank[0]` → `bank[SAVE_BANK_FIELD_ITEM_COUNT]`.)

## Sim state machine (FUN_0049a59e, the DAT_09643550 arm)

Four counters (memset-0 in `scene_title_anim_init_fresh`):

| field | engine | meaning |
|-------|--------|---------|
| `survival_state`    | `DAT_09643550` | 0 closed; 1..8 slide-in ramp, pinned 8 at rest; 8→0 on B-cancel |
| `survival_option`   | `DAT_09643558` | 0 = Survival Hell (top), 1 = Normal Survival (bottom) |
| `survival_anim`     | `DAT_0964355c` | 0 idle; A-confirm ramps 1→0xf (closing → picker) + drives the sin pulse |
| `survival_slideout` | `DAT_09643554` | 0 normal; 1 once a B-cancel begins |

Flow: code-6 dispatch → `survival_state=1` (selector engages **next** frame,
matching the engine's single top-of-frame `DAT_09643550<1` test). The selector
SM (run while open, replacing the main-menu input) ramps `survival_state` 1→8;
at rest (state 8): **pressed-edge** B (0x20) → slide-out → main menu, A (0x10) →
closing ramp → picker, up/down (0xc) → toggle option + ease the hand cursor to
y = `option·0x24 + 272` (272 / 308). The A-confirm at `survival_anim==0xf` opens
the picker (`submenu_state=1`, reuses `title_continue_picker_open` which already
models code 6); `survival_state` is left at 8 — the render's `basex` slides the
panel off-screen as the picker folds in, and the engine never clears it (so
cancelling the picker returns to the selector).

## Render (FUN_0049c644 @ 0x49cbe8 — objdump 0x49cbe8-0x49cde6)

Drawn over the still-visible main menu (gated on `survival_state>0`, NOT
submenu_state). The decompile drops the FPU; consts read from `.rdata`. With
`t = survival_state/8`, `basex = 320 − cursor_anim·64` (= `slide` + 320; the
cursor_anim term slides it off-screen as the picker folds in):

- **backdrop**: `savewindow.tga` (`g_sysassets.savewindow_tga`, `&DAT_073d8dc0`,
  512×128 — the SAME banner the choice box uses), COLOROP **ADDSIGNED**, diffuse
  grey `0xff7f7f7f`, src `(0,0,512,128)` → dst `(basex − t·256, 288 − t·64,
  t·512, t·128)`, alpha `(int)(t·255)`.
- **two labels** (centered at `basex`, scale 1.0): **"Survival Hell"** y=264,
  **"Normal Survival"** y=296. Greyscale ARGB `(alpha<<24)|(c<<16)|(c<<8)|c`.
  The SELECTED row (`survival_option`) gets the sin pulse
  `c = (int)(127 + sin(survival_anim·π/15)·64)` — a bright 127 at rest, flashing
  during the A-confirm close ramp; the OTHER row is a flat grey `0x60`.
- **restore** COLOROP MODULATE at the tail.

(The labels draw under the backdrop's ADDSIGNED, like the choice box — the
engine resets to MODULATE only after both labels.)

## Verification

`tests/scenarios/title-survival` on a crafted save that unlocks Survival
(`tools/craft_survival_save.py` pokes GAME_MODE==3 + an adv-8 item onto a bank +
restamps the bank checksum, else `save_bank_init_all` resets the tampered bank
on load). Nav UP→A; the selector slides in (1→8) and pins. New
**`TITLE_SURVIVAL_READY`** anchor (scene 0 / submenu_state 0 / cursor_anim 0 /
survival_state 8 — no async load ⇒ a +0-stretch join).

Vs the retail v3 cache (`title-survival-f75dbf74`, join 119/119 @ +0 stretch):
the selector at rest is **PIXEL-BIT-EXACT — 0/786432 px differ** at every
sampled offset across the window (start/mid/end + a dense sweep), draw program
**0 draw-divergent**, each side self-verifies bit-exact (120/120 retail, 119/119
port). The 1-frame anchor offset (port reaches rest at present 42, retail 41) is
the same benign title boot-phase seam the Records screen has (retail 120 / port
119 there too); the identity join pairs by absolute present so the at-rest
frames compare 1:1. Host tests: `survival_opens_on_code6`, `survival_ramps_and_
pins`, `survival_toggle_option`, `survival_b_cancels`,
`survival_a_confirms_opens_picker`.

## PORT-DEBT

**`PORT-DEBT(survival-picker)`** — the A-confirm opens the picker but with the
all-banks list (the `FUN_0049b4f4` survival-eligible-bank FILTER is deferred),
and the actual survival game LAUNCH (picker-confirm → load survival mode) is
unported. The selector render + nav + B-cancel + the picker handoff are 1:1; the
survival-specific picker filtering + launch close with the survival gameplay arc.
