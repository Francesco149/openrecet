---
name: finishers-boss-web
start: save:824053720e01
steps:
  - on a dungeon floor with control, Caillou leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 2
  - a Caillou swing record (type 4 or 82) dealt a finishing blow under 10 (the hit value DAT_06a46f94): s32(0x6a46f94) >= 1 and s32(0x6a46f94) <= 9
  - on a dungeon floor with control, Charme leading: scene == 1 and s32(0x438b4dc) >= 4 and dlg == 0 and lock == -1 and s32(0x438b7d8) == 1
forbid:
  - back at the title: scene == 0 and frame > 400
  - game over: scene == 2
---
Probe: **small record branches** (records-B tick `FUN_0043ae20`, `findings/retail-coverage.md` "Reachable, unrecorded"):
- record 0x4 (Caillou's short swing) / 0x52 (`FUN_00497e3a`): when the hit value `DAT_06a46f94` = min(damage, the enemy's
  HP left) divided by 10 is below 1 it is clamped to 1 (blocks 0x43c454 / 0x4405c6) — a **finishing blow on an enemy with
  < 10 HP left** (or a weak hit);
- record 0x53 (Charme's skill 6 web, `FUN_0049508c`): lifetime 120 instead of 600 when FLAG_A == 0 (the party's) and
  `FUN_004319d6` (a boss floor) holds — **use Charme's skill 6 on a boss floor** (Jade Way 5F crown slime / 10F, Hall of
  Trials 5F, …) (block 0x43e5f4).

**Crafted start** `824053720e01` (hire lineage, everyone L40). Read `docs/findings/probes/charge-attacks.md` (Caillou's
swings, the hire route; the hire predicates fire during the load — wait for real control) and `charme-skills.md` (Charme's
keys: `3` = skill 2 = the web; check which key is skill id 6 / state 6 there — the task's "skill 6" is `FUN_0049508c`,
state 6). Do: Caillou leading, Hall of Trials 1F: wear an enemy down, then finish it with a short swing, and another with a
charged one (watch `s32(0x6a46f94)` on the hit). Then Charme leading, a boss floor (5F of a dungeon whose 5F is a boss
floor): cast the web there and watch its record's lifetime (`rd ram R records`: type 83 = 0x53, age to death).
Record: frames, hit values, record ages.
