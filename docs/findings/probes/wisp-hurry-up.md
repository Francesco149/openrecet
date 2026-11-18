# Probe: wisp-hurry-up

- **Run:** `wisp` · **start:** `hire-elan@2400` (crafted `824053720e01` lineage; Jade Way 1F, Elan L40, HP 247) · **task:** `tools/drive/tasks/wisp-hurry-up.md`
- **Driver:** the orchestrator session, by hand (2026-11-18): idle, tap through 19/3, idle, tap through the down dialogue
- **Scenario:** `tests/scenarios/wisp-hurry-up/` (33406 frames)
- **Verdicts (2026-11-18):** check PASS; verify --real MATCH; retail golden (Windows, real device); port triage identical
  ticks 1..33706 (every v2 block every tick) after the wisp AI FUN_0042ac16 and the 19/3 dialogue call (below)

Retail facts (observed live):
- The hurry-up timer `DAT_0438c85c` is set at floor load to rooms × 3600 on Jade Way (27200 left at f2400),
  falls 1 per tick while HP > 0 and hits −1 at f29641: the first wisp (type 34) spawns 12 from the player
  (FUN_0048b850), `DAT_0438cbe0` starts counting; at 60 (f29701) event **19/3** opens (Recette / Tear: "a flaming ball
  arcing with electricity … We can not fight incorporeal balls of fire and hate"; bank `+0x2bd16` = 1, once per save).
- One more wisp each 240 ticks while fewer than 5 live (3 by f31674, 4 by f31931). They home on the player and hit for
  ~20 each through Elan's L40 gear: HP 231 → 0 in ~860 ticks of standing still (f32094), then the down dialogue
  "Oh no! Wh-what do we do now?!", results screen (scene 9) at f33106.
- **Not reached:** a wisp's own despawn (> 30 from the player, > 900 ticks, > 35 from its spawn point), d5's
  900-per-room timer, item 25 (the double decrement).

Port defects it found: FUN_0042ac16 was a PORT-DEBT stub in FUN_0042f28c (the wisps never moved), FUN_0044ba2c(19, 3, 0)
a PORT-DEBT stub in the party tick (f29701: retail froze for the dialogue, the port ran on).
