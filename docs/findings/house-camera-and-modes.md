# House camera: pose, Button-4 cycle, yaw ease, glyph cache, party / first person (mod-loader S6–S7)

Retail facts the mod loader's camera binding (`../recettear-mod-loader/core/lua/game.lua` `B.camera`,
`mod.game.camera`) and the free-camera mod build on. Evidence: IDA (`decompiled-ida/all_ida.c`), objdump of the
unpacked exe, live probes under the loader (wine, `tools/rml`, `tests/lua/camera.lua`, `rml det`, 2026-11-27).
Pose block-by-block: [scene1-camera-helpers.md](scene1-camera-helpers.md) (its Cc.1 erratum: `0x73de31c` = eye,
`0x73de328` = target).

## Render step

`0x45bbf9` (render, from `0x4547ab`'s game branch): `if (!0x6a49998 && !0x6a46fa4) call 0x441c3e` (site **`0x45bc0b`**)
→ `call 0x4424e7` (`0x45bc11`, billboard basis from eye/target) → SetTransform VIEW `0x73de29c` → projection
(`0x73de3a0`° , 4/3, 1, 350) → `0x459dfd`. The pose `0x441c3e` has a second caller, sim-side `0x48dbfb`.

- The pose consumes `rand()` in its shake block (`0x438cc14 > 0`): a mod must let it run and edit its output, not
  replace it. Post-call edit = write eye `0x73de31c..24` / target `0x73de328..30`, then call the view build
  `0x40120c` (pure: LookAtRH × RotZ(`0x6051c4`/2), BSS 0) → `0x73de29c`; `0x4424e7` then follows. [probe: billboards
  face the moved eye; det identical when unchanged]
- Pose output: eye = target + (sin yaw·R, H, −cos yaw·R) with R = `0x438b774 + 0x695ef70`, H = `0x438b778 +
  0x44e2c70` (+ the cinematic boost), then 0.2 smoothing in `0x438cc38..58`. Measured in the shop: mode 0 R 14 H 21,
  mode 2 R 18 H 11.8; target y 1.2. [probe]

## Button-4 cycle (house tick `0x48670f`)

```
486c42  call 0x46f621
486c47  cmp  [0x73dddb4], edi(0)   ; ≠ 0 → 0x41375a, out
486c59  cmp  [0x438b924], edi      ; == 0 → call 0x48960d (site 486c61)
486c66  test byte [0x73dddd4], 0x80 ; pressed Button 4 (loader PAD.X)
486c6f  [esi+0x2ce0c] = ([esi+0x2ce0c] + 2) % 4   ; esi = the working bank
```

No scene-mode gate: reached whenever that branch of the house tick runs (customer / free-roam modes alike). The other
`0x46f621` call (`0x486c2e`) is the `v0[179440]` branch, which never cycles. Clearing bit 0x80 of the pressed byte
after `0x486c42` (or after `0x486c61` when it runs) suppresses the flip; the pressed word is rebuilt from the held
mask every sim_a. [probe: 0 → 2 → (loader mode) → 0, bank dword only ever 0 / 2]

## Yaw

`0x73de39c` (rad) is sim state: `0x48b850` (from the house tick) steps it toward the target `0x56db060` with
`0x482ae7(cur, target, 0.0628)` — both wrapped to (−π, π], **fixed step** 0.0628 rad/tick along the shorter way (not
an ease). Retail writes the target in cinematics (`0x48670f`: 2.827, 2.985, π; stage init `0x436f97`: 0 / π) and,
leaving a cinematic, sets yaw = target = π directly. Movement is camera-relative (facing `0x56db05c` = pad angle −
yaw), so a target write steers walking like retail's own cinematic turns. In the shop's free roam the target is π.
[doc + probe]

## Glyph cache + debug text screen (render scratch a mod's text fills)

- `0x47cbcb(char*)` (the text draws `0x47ca05 / 0x47d14c / 0x47d2db`): 200 entries × 28 B at `0x73de658..0x73dfc48`
  {…, char(s) +0x10, in-use +0x14, age +0x18}, textures `0x73dde44[200]`; a miss builds the glyph texture, eviction by
  age. Text a mod draws takes entries retail's own text would have used → those dwords differ, nothing reads them in
  the sim. [doc + probe: `rml det` diff with the free-camera help box]
- `0x6a47aac` = an 80 × 60 debug text screen (`0x451874(col, row, s)` from ~40 sites, cleared by `0x451863`), never
  shown in the release; `0x47c29d` (from sim_a) prints the glyph cache's fill there, `0x441c3e` its HEIT / DIST /
  CANG / MIPMAP lines. [doc + probe]
- `bmp/dungeonbord.tga` (the Options panel, slot `0x73a9b08`) is not loaded in the shop. [probe]

Both regions are masked in the loader's det when the sides run different mods (`tests/lockstep/env-draw.txt`).

## Party, interaction, first person (mod-loader S7)

Evidence: IDA + objdump of `0x4552d0` / `0x45aa36`, live probes under the loader (wine, `tests/lua/first_person.lua`,
`fp_display.lua`, 2026-11-27).

- **Party slots** (`0x56da1d8 + 12·k` pos; char index `0x56da1cc[k]`, −1 = empty): 0 Recette, 1 the hired adventurer
  (empty in the shop), **2 Tear** (`0x56da1f0..f8`, y ≈ 3: she hovers). [probe]
- **Facing** `0x56db05c` (rad): she walks along (sin f, cos f). Camera-relative movement = pad angle − yaw with pad
  Up = 0, Right = −π/2; the camera's forward is (−sin yaw, cos yaw) (eye = target + (sin yaw·R, H, −cos yaw·R)), so
  "face the view" = facing −yaw. The octant `0x56dab00` follows the facing every tick (a facing write alone turns her;
  measured: f 0 → oct 4, −π → 0, π/2 → 6 at yaw π). A press of A uses the facing: writing it while standing turns what
  A reaches. [probe]
- **Her billboard**: walker `0x4552d0`'s party loop calls the leaf `0x45a56f` at **`0x4564d4`** with arg0 =
  `0x56daae8 + 0x2c·slot` (the anim record); skipping that call for slot 0 hides only her. World scale
  0.03 × `0x56dae18` (xz) / `0x56dae24` (y); sprite top = `0x438cedc[char·0x5058]` px × `0x438ced8[…]`/100 × 0.03
  (Recette 114 px → **3.42** units, Tear 64 → 1.92). [doc + probe]
- **Her shadow**: `0x45aa36`'s party loop builds each slot's quad at **`0x45ae17`** (`0x40d11b(0x64bd88, colour)`,
  ebx = slot, edi = the buffer) and draws it with `DrawPrimitiveUP(TRIANGLESTRIP, 2, buf, 24)` (`0x45ae44`);
  zeroing the 4 positions after `0x45ae17` for ebx 0 hides it. Skipped by retail when `0x56daf94[slot]` = −100. [doc + probe]
- **In-scene mode `0x438cc08`** in the house: **1** = free roam (closed shop), **4** = shop open; **0** = the counter's
  choice list (A at the counter: 1 → 0, counter-move-tables f581; also the opening prologue) — not free roam;
  10 counter menu, 15..18 / 30 Tear's pages (A next to Tear opens 15 or 17), 2 / 3 / 0x17 table + decor editors.
  **`0x438cc04` = 1**: a table's display menu is open while cc08 stays 1. Tear wins A over the counter when she
  hovers closer. [probe]
- **Door**: right wall by the octagonal stone mat (walk +x at z ≈ −3.8 → x 3.0): the "go!" tooltip is
  `0x56db004` = 7; A there → scene 8 (leaving for the town map). [probe]
- **Keyboard**: retail's default bindings (`recet.ini` pad00..08 = 1,2,3,4,39,37,16,35,36; table `0x438cce8`, shorts)
  put **W on button E (0x100) = the pause menu**, Z / X / C / V on A / B / C / D; A, S, D are unbound. [doc + probe]
- **Projection**: the house 3D pass projects into `0x73de2dc` from the pose step (`0x45bc5b`) and the passes under
  `0x459dfd` (far 350 / 2000 / 1100-or-`2200 − (H−11)·75`), all with zn 1.0 and fovy = `0x73de3a0`° — an override
  must cover every one of them (the first person mod rewrites fovy / zn at each call while armed between the pose
  post `0x45bc0b` and the 3D pass return `0x45bc77`). [doc + probe]
