# Title Records / high-score screen — submenu_state 4 (code 8) — RE + port

The title-menu **"Survival Score"** row (the port author's `HIDDEN_CHAR`
name is a **misnomer**, exactly like `RANKING` was for the encyclopedia — the
in-game menu tile literally reads "SURVIVAL SCORE"). Selecting it opens a
display-only personal-best **Records** panel that slides in from the right like
the settings/encyclopedia submenus and closes on A or B.

## Engine functions / port map

| engine | role | port |
|--------|------|------|
| `FUN_0049a59e` code-8 arm (`LAB` @ 0x49b091) | dispatch: `submenu_state=4`, cursor=0, **no** hand cursor (goto tail, skips `FUN_0043561a`) | `scene_title.c` `scene_title_sim` code-8 branch |
| `FUN_0049a59e` state-4 close (@ 0x49b019 → `LAB_0049aaff`) | A\|B → SE 0x143, `select_phase=0`, `menu_folding_out=1` | `scene_title.c` `scene_title_sim` state-4 block |
| `FUN_0049c439` | the records panel render | `scene_title.c` `scene_title_records_render` |
| `FUN_0049c644` state-4 arm (@ 0x49c896) | render dispatch + item_win/fuki header chrome | `scene_title.c` `scene_title_render` state-4 block |

Dispatch path: title menu code 8 → `submenu_state=4` + slide-in (no cursor) →
the fold ramp climbs `cursor_anim` 0→10 → render gated on `cursor_anim>0 &&
submenu_state==4` → A/B folds out (the generic fold-out clause clears
`submenu_state` once `cursor_anim` hits 0).

## Render (FUN_0049c439)

`FUN_0049c439(slide_x, base_y=48.0, ·)` — the 3rd call arg (`DAT_09643530`,
the submenu cursor) is **ignored** (no navigable rows). `slide_x = 640 −
cursor_anim·64`.

- **board**: `dungeonbord.tga` (`DAT_073a9b08`, the SAME sheet the settings
  panel uses) src `(0,0,320,360)` → dst `(slide_x+160, 80, 320, 360)`, COLOROP
  MODULATE.
- **text**: COLOROP ADDSIGNED→MODULATE2X back-to-back (the second wins, like
  the settings panel), grey `0xff7f7f7f`, scale **0.8** (`0x3f4ccccd`), centered
  at `x = slide_x + 320`. 4 rows; label at `y = row·76 + 128`, value at
  `label_y + 32`:

  | row | label (`PTR_0x5d1cc4[i]`) | value (non-zero / zero) | source |
  |-----|--------------------------|-------------------------|--------|
  | 0 | `Record End-game Score`  | `%d pt`  / `-- pt`  | `DAT_056e60f4` |
  | 1 | `Record End-game Money`  | `%d pix` / `-- pix` | `DAT_056e60f8` |
  | 2 | `Survival Hell Record`   | `Day %d` / `Day --` | `DAT_056e60fc` |
  | 3 | `Normal Survival Record` | `Day %d` / `Day --` | `DAT_056e60f0` |

- **restore** COLOROP MODULATE at the tail.
- **header chrome** (drawn by `FUN_0049c644` after the panel, under the restored
  MODULATE): the `item_win.tga` gold tab src `(448,816,688,896)` → dst
  `(slide_x+200, 48, 240, 80)` + the fuki code-8 menu-item tile src
  `(224, 256, 384, 288)` → dst `(slide_x+240, 68, 160, 32)`. Identical to the
  settings chrome except the fuki source y = **code·32 = 256** (settings = code
  2 → 64).

## The record values are persistent SAVE-HEADER fields (no separate loader)

The four record values are NOT runtime-only globals — they live in the **save
header**. `FUN_004905a8` (the save writer, called `(0xffffffff)` from the
end-of-game record producers) writes the **whole arena from `&DAT_056e5770`**, so
`DAT_056e5770` is the save-arena base and the records sit at arena offsets:

| global | arena offset | header dword |
|--------|-------------|--------------|
| `DAT_056e60f0` normal-survival days | `0x980` | `[0x260]` |
| `DAT_056e60f4` end-game score | `0x984` | `[0x261]` |
| `DAT_056e60f8` end-game money | `0x988` | `[0x262]` |
| `DAT_056e60fc` survival-hell days | `0x98c` | `[0x263]` |

All four are inside the 0xb10-byte header, so they round-trip through `save.dat`
and are already in the port's `g_arena` at the title — the port render reads
them straight from `save_arena_base()` (same memory the engine reads). The
end-of-game producers that WRITE them (`FUN_0049d8a4`/`FUN_0049db8a`) stay
unported until the game-completion arc (they only matter mid game-over).

## Menu unlock (code 8)

`scene_title_menu_init`: code 8 is added when `hidden_char_unlocked ||
has_any_adv_cleared`. It sits right after `RANKING` (code 7). On a save with a
score (→ `ContinueAny` default cursor), `DOWN×2` lands on it.

## Verification

`tests/scenarios/title-records` (crafted save: `hidden_char` set + four DISTINCT
record values written into the header — `123456 pt / 654321 pix / Day 88 / Day
33` — so the **populated `%d` path** is exercised, not just `--`). New
**`TITLE_RECORDS_READY`** anchor (scene 0 / submenu_state 4 / cursor_anim 10, no
async load ⇒ +0-stretch join). Vs the retail v3 cache (`orv3_shot`): the records
screen is **PIXEL-BIT-EXACT — 0/786432 px differ** across the whole window
(ramp + settled), draw program **0 draw-divergent**, each side self-verifies
bit-exact (120/120 retail, 119/119 port). Host tests: `records_opens_on_code8`,
`records_closes_on_ab`.

## PORT-DEBT

The end-of-game record **producers** (`FUN_0049d8a4`/`FUN_0049db8a` — write the
high-watermarks at game-over + save) stay unported (game-completion arc; the
title render is fully 1:1 given the header values, which is all the title path
ever reads).
