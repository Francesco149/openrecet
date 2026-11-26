# House camera: pose, Button-4 cycle, yaw ease, glyph cache (mod-loader S6)

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
