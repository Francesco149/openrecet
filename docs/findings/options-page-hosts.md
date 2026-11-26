# Options pages: title vs pause hosts, shared panel, header art (mod-loader S5)

Retail facts the mod loader's native UI kit (`../recettear-mod-loader/core/lua/ui.lua`, `mod.ui.native`) builds on.
Evidence: IDA (`decompiled-ida/all_ida.c`), port `src/settings_panel.c`, live probes under the loader (wine,
`tools/rml`, 2026-11-26). Title-side state machine: [title-settings-submenu.md](title-settings-submenu.md).

## One panel render, two hosts

`0x49c050(float slide_x, float base_y, int row, int saving)` cdecl, callers `0x48150c` (pause) and `0x49c644`
(title render). Draws `bmp/dungeonbord.tga` src (0,0)–(320,360) at (slide_x+160, base_y+32) from the slot
`0x73a9b08` (both hosts), then the rows at (slide_x+208, base_y+112+40·i) under COLOROP MODULATE2X with diffuse
`0xff7f7f00` (selected) / `0xff7f7f7f`; values at slide_x+400. Rows: 6 when scene `0x438b1c0` == 0 (title adds
"Clear Save Data"), else 5. base_y is always 48. **`row` = −1 lights no row** (probe: the loader passes −1 while
the hand is on its header row). [doc + probe]

| | title | pause |
|---|---|---|
| logic | `0x49a59e` (whole title logic, from sim_a `0x4536cb`), sub-page `0x9643524` == 2 | `0x47fc44`, sole caller the pause update `0x47fa76` (from sim_a) |
| open | ramp `0x9643520` == 10, modal `0x9643548` == 0, exit state `0x9643560` == 0 | called only when open; slide `0x74b2880` ≥ 10 |
| cursor | `0x9643530` (0..5, wraps) | dword `[0x74b2834 + 4·level 0x74b288c]` (0..4, wraps) |
| directions | pressed word `0x73dddd4` (R 1 L 2 U 4 D 8) | the repeat word `0x73dddd6` (dword `0x73dddd4` bits 16+) |
| quirk | L/R on row 5 (Clear Save Data) step row 4's Unread Text Skip — the dispatch's `else` arm [probe] | (5 rows; row 4 = skip) |
| exit | A/B → `0x9643560` = 2 (dirty: save `0x4905a8(−1)`) / 3; sub-page stays 2 while `0x9643528` = 1 slides out | B → dirty `0x74b2890` |
| panel | `0x49c644` → `0x49c050(slide, 48, row, …)` | `0x48150c`: slide = `0x74b2880`·64 − 640 (≤ 0) |
| header | `0x49c644` draws `bmp/item_win.tga` plain oval at (200,48)–(440,128) + `title_fuki.tga` "OPTIONS" (240,68)–(400,100) **after** the panel | none over the panel (the "Options" book tab top-left is the pause menu's) |

Hand cursor (shared by every menu): `0x435710(x, y)` 6-frame tween, `0x435693(x, y)` teleport; both Options pages
use (168, 168 + 40·row). The hand quad is drawn at (x−7, y−20, 40, 40) (`nowloading.tga`). SE `0x499519(id)`:
0x146 move / value change, 0x143 confirm. [doc + probe]

Glyph text: `0x47ca05(x, y, str, argb, scale)` → width (double), centred `0x47d14c`, right-aligned `0x47d2db`,
measure `0x47d0ea(str, scale)` — all cdecl, inherit COLOROP (retail sets MODULATE2X around its rows). 2D quads:
`0x404efc(dst xywh, src ltrb, slot, argb)` + flush `0x405354`; the caller binds the texture (SetTexture stage 0). [doc]

## Header art in `bmp/item_win.tga` (1024²) — texel rects

| piece | rect | note |
|---|---|---|
| arrow oval (◀ ▶, green) | (448,736)–(688,816) | alpha bbox (467,747)–(669,804); arrows (488,766)–(508,782) and (631,766)–(652,782) |
| plain oval (retail's OPTIONS plate) | (448,816)–(688,896) | alpha bbox (467,824)–(669,881) |
| green up / down arrows | (465,908)–(496,938) / (530,908)–(561,938) | |

The 3-slice used for wider plates: caps (448..520) / (616..688), middle (520..616) stretched.

## Live display changes through retail's own Reset

Changing `0x5cbc04/08` + the present params inside the D3D8 `Reset` that retail's lost-device path issues
(TestCooperativeLevel → `0x474e7a` free RTs → Reset → reload) is enough for retail to rebuild every size-dependent
resource at the new size: the scene RT `0x73de648` is recreated from `0x5cbc04/08` after the Reset, and the 2D scale
is read per quad (`0x404efc`). Probed: 640×480 → 854×480 (wide) → 1708×960 (2× scale) → 800×600 4:3 on the title,
no stale sizes; `rml det` with the loader's live-apply code in is IDENTICAL to retail. [probe]

LuaJIT note for mods (not an engine fact, but it bit here): `ffi.cast('<declaration>', p)` mints a new ctype per call
— in a per-frame path it fills LuaJIT's ctype table ("table overflow") within a minute. Make ctypes once with
`ffi.typeof`.
