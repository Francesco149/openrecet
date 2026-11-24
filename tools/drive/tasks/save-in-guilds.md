---
name: save-in-guilds
start: save:64d43650865f
steps:
  - loaded with control: scene == 1 and dlg == 0
  - saved in the Market (resume mode 4): u32(bank + 0xb381 * 4) == 4
  - back from the title / reload into the Market (scene 6 variant 1 after a Continue): scene == 6 and s32(0x963c5f0) == 1 and frame > 700
  - saved in the Merchants' Guild (resume mode 3): u32(bank + 0xb381 * 4) == 3
  - reload into the Merchants' Guild (scene 6 variant 0 after a Continue): scene == 6 and s32(0x963c5f0) == 0 and frame > 1400
  - saved in the Adventurers' Guild (bag copied into the preview dwords): scene == 9 and s32(0x6a499a8) == 11 and u32(bank + 0xb75a * 4) == 0xfac0 and u32(bank + 0xb381 * 4) == 1
  - reload onto the town map: scene == 8 and frame > 2300
forbid:
  - game over: scene == 2
---
Driver copy of tools/drive/tasks/save-in-guilds.md (start + step 3 changed, steps 5-7 added).
