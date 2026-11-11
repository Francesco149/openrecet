# Probe: hire-nagi

- **Run:** `hire-nagi` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/hire-nagi.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 111 turns, $1.78, 26 min
- **Scenario:** `tests/scenarios/hire-nagi/` (5024 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/hire-nagi.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     hire-nagi                            f1435 FUN_00447f4f < FUN_0048a833 < FUN_005038ff ⇄ init_type_1f_100 (+1)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**hire-nagi: stopped at frame 5024 on Hall of Trials 2F, Nagi in control. The budget ran out, so I did not run `rd check`, `rd verify` or any strips.** So: no PASS, no MATCH, and none of the requested pictures of the attack or the skills. I haven't seen the trace on screen except at the guild and the "Equip items in bag?" box. 2F is unfinished: only the normal attack was done there.

**Run:** `hire-nagi`, from `save:824053720e01`, 5024 frames. Checkpoints: floor1 1190, near0 1432, near1 1513, near3 2049, sk1 2140, near2 2242, sk2 2283, near6 2731, sk3 2767, near7 2926, hit1 3270, floor2 4616, f2near6 4988.

**Step frames:**
1. In the guild (scene 11): f450.
2. On the floor with control and Nagi leading: f1190. `s32(0x438b7d8)` reads 5 for Nagi, so that predicate is right.
3. Normal attack (`p_act == 2`): f1434.
4. Skill (`p_act` 4 or more): f1515.
5. Enemy hit: f1455. The predicate is written wrong, though. `+0x430` is the HP bar, which eases toward HP; max HP is `+0x434` (combat-core.md). Also, a0 died at f1455, so `a0use != 1` stays true from then on. Suggested fix: `a0hp < f32(0x76b970 + 0x434)`.
6. Forbidden state (title after frame 400): never seen.

**Route facts:**
- The route timings were right: shop control at f212, door found by search at f401, `wm_cur` = 6, guild at f450.
- The first two of the four D presses on the town map are dropped while the map animates; the cursor still ends on 6.
- The guild cursor starts on Louie; Nagi is 5 rows down ("Level:40 Lancer Nagi").
- **Fee:** 4400, with no confirmation box. It is taken on Depart (gold 470000 → 465600).
- **Bag screen:** a help box opens first and swallows the first press. After that: `A`, `U` (to "Depart!"), `A`.
- The dungeon map remembers the last dungeon (it opened on Jade Way); `L` moves to Hall of Trials. Then `A`, and `A` on "Floor 1~5".
- **Equip prompt:** "Equip items in bag?" (Yes / No) appears on the floor about 300 ticks after arrival, and `lock` stays 0 until it is answered. An `A` pressed at f837 was ignored; `A` at f1179 on "Yes" gave control (`lock == -1`) at f1190.
- **Odd things about the crafted save:**
  - The HUD shows Day 16 while `day` reads 15.
  - The day-14 saves' level-up event did not play.
  - Nagi starts with HP 157 and SP 124 at level 40.
  - Hall of Trials enemies: 1F slimes have 15 HP; 2F has type 0 (18 HP), type 1 (24 HP) and type 29.

**Nagi's actions (1F, Lv40 against 15-HP slimes, so every hit killed):**

| Action | `p_act` | Length | Record (in table B) | Hit lands | SP | Damage |
|---|---|---|---|---|---|---|
| Normal attack (`A`) | 2 | 30 ticks; 34 with a hit (4-tick hit-stop) | type 88 (0x58), appears at `p_actt` 16 (age 1) | `p_actt` 21, from 4.2 away | 0 | 34, side + counter words |
| Skill 0 (`B`) | 4 | 45 ticks + hit-stop | type 62 (0x3e) at `p_actt` 5 | `p_actt` 5 | 5 | 23, side word |
| Skill 1 | 5 | 70 ticks + 3 hit-stops | type 102 (0x66), respawned every 14 ticks | 5 per hit at `p_actt` 6, 8, 10 | 8 | 5 per hit |
| Skill 2 | 6 | 35 ticks + hit-stop | type 103 (0x67) in slot 0, reach 5.5, power 1.2, age = `p_actt` − 4 | `p_actt` 10 | 10 | 30, front |
| Skill 3 | 7 | 30 ticks + hit-stop | type 81 (0x51), power 1.5, appears at `p_actt` 12 | `p_actt` 12, from 1.8 away | 5 | 37 |

- **Normal attack:** repeated presses do not chain. A press during the swing is ignored, and the next swing starts 2 ticks after the state ends.
- **Skill 0:** a charge that carries her about 15 units.
- **Skill 1:** a flurry. The hits came 2 ticks apart, each followed by 4 ticks of hit-stop.
- **Skill selection:**
  - `C` cycles the skill slot `s32(0x56db0d8)` through 0, 1, 2, 3, 0: Nagi has 4 skills.
  - Key `2` did nothing — tool gap or game, unknown.
- **Dash (double-tap):** 0.2325 per tick against a walk of 0.155 (×1.5). `p_act` stays 0 while dashing.
- **Taking a hit:** a slime hit for 1 HP (157 → 156), giving `p_act` 1 and `p_hurt` 30. The damage popup has kind 5. I didn't identify the enemy's hit record.
- **2F:** normal attack on the type-1 enemy (24 HP) killed it, with the same timing (record 88 at `p_actt` 16, hit at 21). The popup read 24, plus words of kind 13 that may be left over from the earlier hit — not certain. I didn't do skills, a hit taken or the dash on 2F.
- **Slot 0 of the hit-record table** usually holds a long-lived type-11 record. Watching only slot 0 misses the sword record (that one was in slot 2).

**Names I defined in this run:** `d0`–`d7` and `d6b` (distances to actors), `h0` / `hr` / `hs` (hit-record slots 0, 1 and 2 as type×1000 + age).

**Tool gaps:**
- The actor names only cover slots a0..a5; for slots 6 and up I had to compute raw addresses by hand. I misread HP at `+0x424` once that way.
- Nothing reads Yes/No prompt boxes or the dungeon-map cursor; I needed pictures.
- The search's default actions include `A` and `B`, which spend SP and kill enemies during navigation.
- Helper search sessions were refused all run because the machine was at its session cap (10/10).

**Still to do:** `rd check hire-nagi tools/drive/tasks/hire-nagi.md`, `rd verify hire-nagi`, strips around the normal attack (f1434–1468) and skills 0 (f1515–1564), 1 (f2058–2140), 2 (f2244–2283) and 3 (f2733–2767), and 2F skills from checkpoint f2near6.
