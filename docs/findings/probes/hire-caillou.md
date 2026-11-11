# Probe: hire-caillou

- **Run:** `hire-caillou` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/hire-caillou.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 103 turns, $1.68, 9 min
- **Scenario:** `tests/scenarios/hire-caillou/` (5387 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/hire-caillou.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     hire-caillou                         f1583 FUN_00442cef < FUN_004536cb < FUN_0047be92 ⇄ - (+1)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run: **hire-caillou**, from save:824053720e01. Movie is 5387 frames. `rd check` gives PASS and `rd verify` gives MATCH (fresh headless replay at frame 5387, rng d2b43bc9). I looked at three strips: the normal attack (1582–1660), skill 0 (1680–1740) and skills 1–4 (2342–2702).

**Step frames (`rd check`)**
1. scene == 11 (Adventurers' Guild): f492.
2. Caillou on the floor with control: f746 by the check, but that is too early. At f746 the transition into the dungeon has just happened and `lock` already reads −1. The game then shows an "Equip items in bag?" Yes/No box (lock 0). Real control starts at **f1371**, after `A` on Yes.
3. Normal attack: f1582.
4. Skill: f1679.
5. Enemy hit: judged at f1680. It was true through `a0use != 1`, because slime a0 had been killed at about f1628, and the check only judges step 5 after step 4.

**Checkpoints:** floor1 1371, near0 1580, atk1 1661, sk0 1743, near3 2338, sk1 2460, sk4 2705, near11 2877, floor2 4908. I also defined `d0`, `d1`, `d3` (distance to actor 0 / 1 / 3) and `bx`, `bz`, `bhp` (actor 11's x, z and HP).

**Route facts**
- Town map: `D .*7 D .*7 D .*7 R .*7` did not give `wm_cur == 6`. The first `D` was ignored, then the cursor went 0→5→2→3. `U` from 3 (Merchant's Guild) gives 6 (Adventurers' Guild).
- Guild list: the cursor opens on Louie (row 1). `D D A` picks Caillou. No confirm or fee prompt; it goes straight to the bag screen ("Pick the items you wish to bring with you"). The first `A` only closes the help scroll; `U A` then hits Depart!.
- Dungeon picker: the cursor starts on Jade Way. `B L A` reaches Hall of Trials, then `A` departs.
- **Fee: 4,100 G**, taken on Depart (470000 → 465900 at f746).
- The guild screen showed "3950" next to Louie's info, probably his fee.
- **Equip prompt:** "Equip items in bag?" (Yes/No) appears on arrival on 1F even with an empty bag; `A` = Yes.
- Exit portal: "Go to the next floor?" Yes/No. `A` pressed on the arrival tick is too early; `.*20 A` works.
- 2F control at f4908.

**Predicate fixes**
- `s32(0x438b7d8) == 2` is correct for Caillou.
- Step 2 should also require that the bag prompt is gone. `lock == -1` alone fires at the transition tick (f746).
- In step 5, `f32(0x76b970 + 0x430)` is the eased HP **bar**. Max HP is +0x434.
- Step 5 also passes through `a0use != 1` once a0 dies: a killed slime's slot shows use 2, then 0 about 11 ticks later. Better: `a0hp < f32(0x76b970 + 0x434)`, or check the hit-stop flag `s32(0x438bed8) > 0`.

**Caillou, level 40 (crafted save):** HP 80/80, SP 303/303, ATK 25, DEF 39, MATK 56, MDEF 49. Walks 0.155 per tick (Louie 0.175). The party seen in the strips includes Recette following him. SP +1 per kill (popup kind 6, shown as "SP +1"). 1F has only type 0 slimes (HP 15); 2F has types 0 (18 HP) and 1 (24 HP).

**Action table.** `p_actt` counts from the press tick. Hit-stop adds 4 frozen ticks per hit, 8 on a crit.

| key | p_act | length | record, spawn | hit landed | SP | damage / effect |
|---|---|---|---|---|---|---|
| `A` (normal) | 2 | 35 ticks (0..34) | type 4, 1.0 ahead, spawns at p_actt 12 (age 1) | p_actt 16–17 | 0 | 28 to a type 0 slime (crit + side, f1628); 10 to a type 1 slime (side, f5299) |
| `B` / `1` (skill 0, explosion) | 4 | 60 ticks (0..59) | slot not found; fire burst | p_actt 24 | 5 | 32 "Counter Attack" to a type 0 slime; killed a type 1 slime on 2F with 14 HP left; `p_inv` = 1 for ticks 1..~31 |
| `2` (skill 1, ice crystal) | 5 | 60 ticks | type 97 (0x61) at p_actt ~28, 4.3 ahead, reach 3, power 0.5 | no hit | 5 | sliding ice block that stays for 100+ ticks (age jumped 71→100); its y read −193..−333 |
| `3` (skill 2, homing bolts) | 6 | 60 ticks | several type 98 records from p_actt ~15 | p_actt 40 and ~47 | 5 | 13 then 2, on a slime about 15 units away |
| `4` (skill 3, orb ring) | 7 | 60 ticks | type 106 (0x6a) in 3–4 slots from p_actt ~32 | p_actt 43–44 | **25** | kill (15 HP); orbs orbit the player and kept killing slimes later (actors 10 and 11 near f2878–2920) |
| `5` (skill 4, teleport) | 8 | 110 ticks | type 11 records at ~p_actt 55 | — | 5 | warps the player across the floor: (−65, −77) → (−115, −160) |

More on the attacks:
- **Crit:** a crit (`s32(0x56db064)` = 1 at tick 1) freezes the world for **30 ticks at swing tick 4** (`s32(0x438bed8)` = 30). The crit swing took 75 ticks in total.
- **No combo:** `A` mashed during a swing does nothing; the next swing starts 2 ticks after `p_act` returns to 0.
- **Skill select:** `C` cycles the selected skill (`s32(0x56db0d8)`), but the first `C` press was ignored (0 → 0, then 1, then 2). `B` with slot 2 fired p_act 6.
- **Dash:** none. Double-taps (`U . U`, `D .*2 D`) only walk at 0.155 per tick.
- **Hit taken:** about f2444, a slime did 1 damage (HP 80 → 79), set `p_inv` to 30 and knocked the player about 4.5 units.

**Crafted save oddities:**
- The HUD says Day 16 while the day dword reads 15.
- Everything else behaved normally.

**Tool gaps:**
- There is no way to find "the hit record of type T" without guessing the slot; slot 0/1 type 11 records from the floor-entry effect sit there. A `rd ram R records` table like `actors` would help.
- The `aN` names only cover actor slots 0–5; for slot 11 I had to define my own expressions.
- A `--apply-best` search without `--apply` kept nothing when it found the goal, so I had to run it again.
- Walking straight at a target (`{dpad(...)}*400`) gets stuck on walls with no warning; I had to undo 400 frames.
