# Screen-space geometry: what assumes 4:3 (widescreen inventory)

Source: recettear-mod-loader S4 (2026-11-26) — the retail exe with the loader's D3D8 wrapper counting / remapping
every draw, over the scenarios day2-shop, dungeon0-wipe, pause-encyclopedia, pause-equip-dungeon, town-rounds-week3,
tear-menu-pages, title-options (lockstep replays, wine). **[probe]** = observed there; **[doc]** = IDA only.

## The 4:3 assumption lives in three places

1. **Back-buffer size globals `0x5cbc04/08`** (ini `screen` → 640×480 … 1280×960; recet-ini.md). Readers [doc]:
   `0x404e98` / `0x404efc` (2D quad scale `w/640`, x **and** y), `0x4054c0` (D3DX font path: x by `w/640`, y by
   `h/480`), `0x406241`, `0x4063c7`, `0x4072f5` (EXP gauge fans), `0x407cac`, `0x47ca05` / `0x47d14c` / `0x47d2db`
   (glyph text), `0x47aa8b` (window rect), `0x47ac6a` (present params `0x73de268/6c`), `0x47ae65` (scene RT texture
   `0x73de648` at this size), `0x47b2e7` (WndProc resize menu). Nothing else reads them.
2. **Projection aspect**: all 18 direct callers of `D3DXMatrixPerspectiveFovRH 0x4a3ee8` pass the literal 4/3
   (`0x403c1e`, `0x4161c7` ×2, `0x4176ff`, `0x451790`, `0x452f58`, `0x4552d0`, `0x459dfd` ×8, `0x45bbf9`, `0x46c9a2`,
   `0x48ef00`). [doc; all 18 call sites redirected by the loader, probe]
3. **3D → 2D markers**: `0x490d29` (sole caller `0x490c78`, which has 10 callers: `0x405d70`, `0x407cac`, `0x409925`,
   `0x40a765`, `0x484e97`, `0x485016`, `0x4855e2`, `0x485712`, `0x485861`, `0x4897c6`) projects a world point to the
   640×480 virtual screen: `x = 320 − sx·v.x / w`, `y = sy·v.y / w + 240`, with `sx, sy` (`0x95d37c8/cc`) = the
   projection matrix applied to (1,1,1) × 640/2, 480/2 (`0x490cc6`, called every time). With a wider projection
   aspect the x scale shrinks by 4/3 ÷ aspect, so markers (damage numbers, `!` bubbles) need `x' = 320 + (x − 320)·k`
   (k = target width ÷ 4:3 width) to stay on their actor. [doc + probe: dungeon damage numbers land on the hit enemy]

## How the 2D reaches the device [probe]

- **Every** 2D draw in the scenarios above is `DrawPrimitiveUP(TRIANGLELIST)` with FVF `0x1c4` (XYZRHW | DIFFUSE |
  SPECULAR | TEX1, stride 32) — the quad array `0x605208` flushed by `0x405354`. No pre-transformed vertex-buffer
  draws, no D3DX sprite / font draws (`byte 0x438b784 & 1` = 0: the glyph path).
- **Glyph text**: one `SetTexture` (`0x73dde44[glyph]`) + one quad + one flush **per glyph** (`0x47ca05`); the glyph
  textures are generated (`0x47cf22`), not loaded from a file.
- **HUD widgets are atlas pieces**, not text: the money / day dial and the merchant-level bar are quads of
  `bmp/item_win.tga` (plates at virtual (0,0)–(230.4,102.4) and (0,424)–(192,464)), digits from the same atlas;
  the dungeon HP/SP/EXP plate is `bmp/hpmp_base.tga` (0,376)–(256,478.4) with digits + bars from it and
  `bmp/hpmp_exp.tga`; the floor-name plate is `item_win.tga` (460,436)–(640,476); the minimap is all of
  `bmp/map.bmp` (see "Minimap" below — the S4 "marker at x 572" was just one 16-px room tile); button hints ("Button 4: Change Camera", "ESC Key: Event Skip") are plates of `bmp/data_win.tga`
  (440,440)–(640,472) bottom-right, (472,16)–(672,80) top-right. Widgets slide in vertically (the dial from y −128,
  the party plate from y 504).
- **Full-screen fades** are `bmp/system.bmp` atlas pieces stretched over (0,0)–(640,480) (a few texels across), not a
  solid-colour draw. Full-screen pictures (title `bmp/title_bg2.bmp` + frame `title_waku.tga`, event backdrops
  `bmp/ivent/bg_*.bmp`, the town map, guild) are one quad each over (0,0)–(640,480).
- **Off-screen layout**: the load-game list keeps its neighbour pages at x < 0 and x > 640; slide-ins start outside
  the 640×480 rect — a wider view shows them unless clipped.
- **Clear colour** behind full-screen pictures is scene-specific (cyan in town / events) — retail never shows it at 4:3.

## The scene render target

`0x47ae65` creates `0x73de648` (texture) / `0x73de630` (surface) at `0x5cbc04 × 0x5cbc08`, usage RT, plus the fixed
1280×256 blur RT `0x73de64c` / `0x73de634`. `0x4547ab` renders the 3D scene into `0x73de630` when a transition /
blur is active (`0x6a49994 > 0`, `0x6a49990 == 2`, …) and `0x454191` composites it with full-screen quads
(0,0)–(640,480) virtual (UV 0..1), the radial blur via 12 inset quads of the blur RT, and the pause background.
So that texture must follow the real target size and its composite quads must cover the real width.

## The 640 / 480 / 320 / 240 float literals

~430 of them in `.text` [doc]: all are positions / sizes in the virtual 640×480 space handed to the quad path
(`0x404efc` family) or to `0x490d29`'s output — they need no per-site treatment once the quad path and the marker
projection are handled (the S4 remap covered every draw seen; no site found that turns them into device coordinates
by another route).

## Viewport / scissor

No `SetViewport` / clip-plane use; the device default viewport (= the current render target) is all retail relies on.

## Minimap, event standees, title backdrop (recettear-mod-loader S4.1, 2026-11-26)

- **Minimap** = the dungeon block of the scene HUD (`FUN_00407cac`, called from `FUN_0040a765` when
  `*DAT_068dd2f0 >= 1`), texture `bmp/map.bmp` (slot `0x73d8768`, loaded 256×512; drawn nowhere else). Grid
  `W = DAT_005cc2e8`, `H = DAT_005cc2ec` (skipped when both are 3); origin x = `620 − 16·W`, y = `432 − 16·H +
  256·DAT_0438b1dc − 16` (the HUD slide); one 16×16 cell per explored room (src 48-px tiles), then the player dot
  (src (0,384) 4×4), monsters (src (24,384), colour by kind 29/30/31) and the exit / item markers (src (8,384) /
  (16,384)), all at `39 − (world + 40)·0.4 − 1` from the origin. ⇒ right/bottom-anchored, grows left/up. [IDA]
- **Event standees**: `bmp/ivent/NNname_NN.tga` (e.g. `01recette_01`, `02tear_00`, `03lui_04`, `14eurya_04`,
  `26machi_01`), 512×512 virtual quads hanging off the 4:3 column (left speaker −140..372, right 256..768); emote marks
  `bmp/ivent/giku.tga`, `tameiki.tga` beside them; the box `ive_window.tga` and name plate `chrname.tga` centred. [probe]
- **Title backdrop** `bmp/title_bg2.bmp`: a 640×840 picture in a 1024×1024 bitmap (the rest is green); the title
  render `FUN_0049c644` draws src (0, 360 − t·360/7140) 640×480 → (0,0)–(640,480) (`t = DAT_09643518`), so it
  scrolls up the town over 7140 ticks; nothing exists past x 640. The white soft frame `bmp/title_waku.tga`
  (1024×512, 640×480 used) goes over it. [IDA + extracted asset]
- **Enemy HP** (dungeon): `bmp/ene_hp2.tga` plate (360,0)–(640,64) top-right (slides in from y < 0), the enemy's
  `bmp/en/<name>_mini.tga` icon (364,−4)–(428,60), bar pieces, HP digits from `hpmp_base.tga` at (456,40). [probe]
- **Chain counter** (`FUN_00407cac`, while `DAT_0438b90c > 0` — consecutive kills of the same enemy kind,
  `FUN_0042e1a6`): `"%d"` digits 32×40 of `item_win.tga` (src (32·d, 960)) at x 16 + 24·i, y 160, then the
  "Chain!" label src (320,960)–(416,1000) at (96,160) 96×40. [IDA + probe]
- **Pause art** `bmp/pause_bg_rete.tga` (1024×512, 640×480 used): Recette + Tear drawn as one full-screen picture over
  the stretched scene RT. [asset]
- **World map** (`FUN_0049e3a3`): picture `worldmap_{nomal,yugata,night}.bmp` (640×480; dusk/night drawn as two
  layers); one `bmp/mappoint.tga` quad per place button (labels baked in); the pointing hand is a
  `bmp/nowloading.tga` quad; the help bubble ("If you return now no time will pass", "Going to a dungeon will take
  2 periods of time") is one `item_win.tga` quad (28,80)–(148,160) under the day dial. [probe]
- **Item pickup notice** (dungeon): `item_win.tga` plate 168×21.6 at (420,93), the item's `bmp/item/itemNN.bmp`
  icon at (424,93) and generated glyph text from x 452. [probe]
- **Screen shake** moves the scene-RT composite and other full-screen quads by a few virtual px (seen −2..638,
  ±1 / ±2 in y). [probe]
