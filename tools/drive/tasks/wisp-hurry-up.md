---
name: wisp-hurry-up
start: hire-elan@2400
steps:
  - on Jade Way 1F with control, the hurry-up timer running: scene == 1 and s32(0x438b4dc) >= 4 and lock == -1 and s32(0x438c85c) > 0
  - the timer runs out, the first wisp (type 34) spawns: actors(T == 34) > 0
  - more wisps (FUN_0048b850 adds one every 240 ticks, up to 5): actors(T == 34) >= 3
  - the party leader goes down: hp <= 0
  - the results screen: scene == 9
---
Probe: **the hurry-up wisps** — `FUN_0048b850` spawns type 34 (FUN_0042ac16's AI) around the player once the floor timer
DAT_0438c85c (rooms × 3600 / 2400 / 2100 / 1800 / 1200 / 900 by dungeon) runs out, one per 240 ticks up to 5; event 19/3
(Recette / Tear, "incorporeal balls of fire") on the first sighting. Idle on 1F, let them catch Elan, down, results.
