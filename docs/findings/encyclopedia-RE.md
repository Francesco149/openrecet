# Encyclopedia (図鑑) — pause submenu type 6 — RE + port

The in-game item catalog, reached from the PAUSE menu (`ESC` → Encyclopedia).
A horizontal carousel of item CATEGORIES; each category is a 3-column grid of
discovered-item cells (icon + name) with a 3-row visible window + vertical
scroll, a per-screen completion-rate panel, a bottom description panel for the
cursor's item, and an A-press item-detail overlay.

Engine functions / port map (`src/encyclopedia.{c,h}`):

| engine | bytes | port | role |
|--------|-------|------|------|
| `FUN_0049f012` | 851 | `encyclopedia_setup` | build the catalog (discovery store + item DB) |
| `FUN_0049efb8` | 90  | `encyclopedia_cursor_recompute` | slide the hand cursor to the cell |
| `FUN_0049f365` | 1363 | `encyclopedia_update` | per-frame nav state machine (returns 1 = close) |
| `FUN_0049f8b8` | 2033 | `encyclopedia_render` | the catalog draw |
| `FUN_0046a336` | 2722 | `encyclopedia_detail_render` | the A-press item-detail overlay (**Phase 2**) |
| `FUN_0045526a` | 102 | `enc_sort` (static) | parallel bubble sort (keys asc, co-permute idx) |

Dispatched from `scene_pause.c`: `pause_menu_setup` → `encyclopedia_setup(0)`
(engine L81616); nav-commit type 6 → cursor snap (72,112) + open submenu;
`pause_menu_update` @ sub_anim==10 → `encyclopedia_update`; `pause_menu_render`
@ sub_anim>0 → `encyclopedia_render(d, 640 − sub_anim·64, 0)`.

## Data model (the non-obvious part)

Three per-category tables, 100 categories × 60 cells (0x14 rows × 3 cols):

- **slot table** `g_enc_slot[100][60]` (`int16`, `DAT_09643698`) — **PACKED with
  the DISCOVERED items only.**  The engine's build loop advances the slot write
  pointer ONLY on a discovered item, so undiscovered catalog entries are
  transient (`-2` written then overwritten next iteration).  A category with
  catalog items but ZERO discoveries keeps a single `-2` placeholder at slot 0
  (one empty "?" frame).  `-1` = no slot.
- **index table** `g_enc_index[100][60]` (`int`, `DAT_09646650`) — per-category
  display order, sorted by `(item_id/100·100 + 10 + rank)·0xc80 + item_id`
  (rank then id).  Maps a display cell → its packed slot.
- **cat-key array** `g_enc_cat_key[100]` (`int16`, `DAT_09646578`) — per-category
  header item_id (resolves the category name).

`cat_count` (`DAT_0964c42c`) = number of distinct DB categories (every DB
category appears; undiscovered ones show one `-2` placeholder + the "？？？"
name).  Completion% = `comp_num / comp_den` = discovered / total-catalog.

**Discovery store** (per working bank, byte `0x279d8` = dword `0x9e76`, 100
records × 0x12 dwords): `+0` category key, `+4` catalog count, `+8+subindex`
discovered flag (set by `FUN_0049126b` on first acquisition — the only
persistent part; key/count are recomputed from the item DB each setup).
(`save_bank.h` previously mislabeled this region "RANKING summary".)

## Render (`FUN_0049f8b8`)

`(px, py)` = the submenu slide offset (`px = 640 − sub_anim·64`, rests at 0).
Carousel `p=0..2` (prev/cur/next category, x = `p·640 + anim·64 − 640`); the
guard skips p=0 when anim<0 / p=2 when anim>0.  Per current panel (p==1):

- board strip — item_win src(448,736,688,813) dst(200,4,240,77) **absolute**
- completion panel — pause.tga src(880,0,1008,256) dst(px+504,py+88,128,256)
- "Completion"/"Rate"/"%3d％" centered @ px+568 (y 220/240/264) scale 0.8
- category name centered @ (320,34) **absolute**, "？？？" if 0 discovered
- 3×3 grid: slot frame pause.tga src(880,256,944,320) 64×64; item name centered
  @(gx+32,gy+48) scale 0.692; icon item_icons[category] cell=subindex 32×32
  @(gx+16,gy+16); grid x = `col·149.33334 + panel_x + px + 88`, y = `row·88+80`.

Bottom: desc board item_win src(0,320,640,480) dst(px,py+332,**640,160**);
cursor item's desc_line1/desc_line2/"Price- N" left @ px+80 (y 368/396/424).
Scroll arrows `FUN_0048edee`/`ee77` (item_win, gated scroll>0 / scroll<rows−3).
Category L/R selector: data_win, **2 quads batched into ONE draw** (bind once,
add 200×32 @ y16 + @ y48, flush once).

**Decompile-decimal traps caught vs objdump:** category-arrow h = **32**
(`0x42000000`, not 64); desc board h = **160** (`0x43200000`, not 200).
All diffuses `0xffffffff`, colorop inherited MODULATE (no state changes).

## Update / nav (`FUN_0049f365`)

Input: `pressed`=`g_sim_buttons[0].pressed` (DAT_073dddd4), `held`=`.held`
(DAT_073dddd6).  **A/confirm = `pressed&0x40`** (the examine button — NOT the
menu-select `0x10`); B = `pressed&0x20`; d-pad held `0x04/0x08/0x02/0x01`
(up/down/left/right); shoulders held `0x10/0x80` (L/R = page category).

- Per-frame precompute writes `rows_prev/cur/alt` (`DAT_0964368c/90/94`) =
  ceil(populated/3) over prev/cur/next category.
- Category slide anim `DAT_0964c424` (±1/frame, commit ±10 → category change).
- A on a discovered cell → detail open (SE 0x2c6, hide cursor); A again closes.
- U/D scroll the 3-row window (SE 0x146; up plays 0x146 **twice** — once as the
  `&&` side effect, once on the move); column fixup pulls col off empty cells.
- L/R move the column, or page category at the edge; L/R-shoulder always page.
- B → SE 0x13d, **return 1** (close submenu).
- Tail debug text (`FUN_00451874` rows 0x17/0x18) is INVISIBLE in retail
  (`DAT_06a49938` BSS-zero) — omitted.

## Verification (PHASE 1 — grid render + nav + setup, usual house-pause save)

Scenario `house-pause-encyclopedia` (ESC → 1×down → Z opens type 6).  Re-drove
the port over PAUSE_READY+0:300, compared vs the retail v3 cache:

- **Pixel: BIT-EXACT.** `orv3_shot` port#150 vs retail#152/#156 (bob-aligned) =
  **gt8 0.0000%**.  At the same offset (port#150 vs retail#150) gt8 = 0.099%,
  the ONLY diff being the **hand-cursor bob phase** (`|sin(b154·0.1)|·8`, the
  load-seam phase pillar — same accepted residual as the save picker / option
  list; an `ENCYCLOPEDIA_READY` rebase would zero it, as `SAVE_PICKER_READY`
  did for the save nav).
- **Draw program: 1:1** — 133/133 draws, 123 content-matched; the two "replace"
  blocks (the item-name glyphs draws 30-38, and draw 132) are **pixel-identical**
  (same paint bbox/px-count) geo_hash artifacts, the benign sub-pixel font-
  centering class.  The category-selector batching (port split → retail's single
  4-prim draw) was fixed (bind-once/flush-once).

## Verification (PHASE 2 — maxed-save stress: every item + full-grid nav)

Scenario `house-pause-encyclopedia-max` (the hacked maxed save f693fbd6, every
item discovered) + the `ENCYCLOPEDIA_READY` anchor (clean per-side pause-load
rebase, join 559/559).  Pages the whole 33-category carousel with full 3-column
grids + vertical scroll + column moves.

- **Items render BIT-EXACT.** After the cursor fix below, the resting full grids
  are **gt8 0.0000%** vs retail across the carousel — every item's icon + name +
  the completion % + the description match.  The only non-zero frames are the
  mid-category-SLIDE transients (~3.5%, the moving full-grid carousel offset by
  the 1-frame async-pause render seam — the accepted load-seam pillar; the
  resting frames before/after each slide are bit-exact, proving the slide
  starts/ends on the same cells).

### THE BUG the maxed save caught: the pause cursor was never ticked (mode 9)

On full grids the hand cursor MOVES (Down/Right within the grid); on the sparse
usual save it never left cell (0,0), so the slide path was never exercised.  The
maxed grids exposed it: the port cursor LAGGED its (correct) logical state
because the **6-frame cursor slide (`title_save_dialog_anim_tick` = FUN_004356cd)
was never ticked during mode 9.**  The engine runs it at the END of every
`FUN_0047fa76` (pause_menu_update) frame; the port had assumed the integration
layer (sim.c) did it "like the other menus" — but sim.c ticks the cursor only
for modes 1/8/6, and during mode 9 the per-mode dispatch is SKIPPED (the
counter_998 freeze path).  The Save submenu hides the hand cursor (its card-
breathe uses a separate counter), so this never surfaced before the encyclopedia.
Fix: call `title_save_dialog_anim_tick()` at the tail of `pause_menu_update`
(scene_pause.c), matching FUN_0047fa76 L82104.  Verified no regression — the
maxed grids go to **bit-exact**, house-pause (M2/M3) stays **bit-exact**, the
usual-save nav is unchanged (slightly better at the slides), 3281 host tests pass.

**PORT-DEBT(encyclopedia-detail):** the A-press item-detail overlay
`FUN_0046a336` (the big stat/combine popup) is stubbed — the next milestone.
