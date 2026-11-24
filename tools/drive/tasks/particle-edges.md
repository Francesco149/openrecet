---
name: particle-edges
start: save:824053720e01
steps:
  - on a dungeon floor with control, Caillou leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and loading == 0 and s32(0x438b7d8) == 2
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **three live particle branches no recording reaches** (`FUN_0040fb3a`, table A at `0x69b2f80`, stride 0x94, type
at +0x30, age at +0x34; `docs/findings/retail-coverage.md` "Dead in retail: the particle tick" lists the live ones):
1. **0x11 water drops** — the splash (`FUN_0048b850`: walking into water; also Caillou's swing / Tielle's skill 5 in water)
   spawns 0x91 then, when the stage's field +0x1b38 (`smallwater:`) is 0 — every dungeon stage — the four drops 0x11.
   `wet` = `s32(0x56daed8)` turns 1 in water (`charge-attacks`: Hall of Trials 1F has none). Find a dungeon floor with
   water you can walk into (Jade Way / Amber Garden / Lapis — try the floors whose pictures show water; the stage label
   1-8 "underground lake" is one) and walk in, then swing there.
2. **0x98 Caillou's hit spark killed at age 64** — after a Caillou swing hits, a spark 0x98 steers to him and dies within
   3 units; if he walks straight away it reaches age 64 and is killed there (block 0x4102b5). Land a swing, then run away
   for 70+ ticks.
3. **0x75 a returning spark whose target died** — a type-0x13 enemy record that hits the player sends a spark 0x75 back to
   its owner; if the owner dies (or the crown slime's 0x93) before it arrives it is killed at age 64 (0x4103dd). Take a
   sting from a flier (`FUN_0042a74c` fliers / stingers — find which enemy fires record 0x13), then kill it at once.

**Crafted start** `824053720e01` (hire lineage; route: `docs/findings/probes/hire-caillou.md`, `charge-attacks.md`). Read the
table A layout above; watch it with `rd eval` over a few slots or a small script. Record per item: frame, floor, the
particle types / ages seen. Any order; partial success is fine.
