# Probe: hire-tielle

- **Run:** `hire-tielle` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/hire-tielle.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 112 turns, $1.78, 13 min
- **Scenario:** `tests/scenarios/hire-tielle/` (4466 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/hire-tielle.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     hire-tielle                          f1759 FUN_00447f4f < FUN_0048a833 < FUN_005038ff ⇄ init_type_1f_100 (+1)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `hire-tielle`: unfinished.** The movie is 4466 frames. `rd check` passed at f2127, before the later steps were added; I have not re-run it since. `rd verify` was never run, so there is no MATCH. 2F was reached but nothing was done there yet. The budget ran out at f4466, with Tielle on Hall of Trials 2F within 8 of enemy slot 0 (`d0 < 8`) and no 2F attack made.

**When each step held (from `rd check`)**
1. f384, `scene == 11` (the Adventurers' Guild).
2. Reported at **f747, which is too early.** The predicate already holds during the load into the dungeon, before the "Equip items in bag?" prompt. Real control (`lock` goes from 0 to −1) starts at **f1382**, after accepting that prompt with A. The chara variable `s32(0x438b7d8)` reads **3** on both 1F and 2F, so it is right for Tielle. Suggested fix: add `not loading`, plus `frame` > the prompt or `cc08 == 1`, or require `lock == -1` for several ticks.
3. f1758, a normal attack (`p_act == 2`).
4. f1826, a skill (`p_act == 4`).
5. f1827, "an enemy hit". This counts damage *dealt*: slot 0 was killed at f1775 (hp 15 → 0). `f32(0x76b970+0x430)` reads 15 = slot 0's max HP (equal to its hp at spawn), so the guess is right on 1F.

**Checkpoints:** floor1 f1382, near1 f1748, hit1 f1794, sk0 f1887, near2 f2252, floor2 f3288.

**Guild, fee and prompts**
- The guild list reads: Leave, then Louie, Charme, Caillou and Tielle, all Level 40. The cursor started on Louie, so it took D×3 to reach Tielle.
- Tielle's fee is **4200** (Louie's is 3950). There is no fee prompt: the gold went 470000 → 465800 only when Depart was confirmed.
- Next comes the bag screen (Depart! and slots 1–5). Its first U press was swallowed while the page was locked.
- The dungeon map opened with the cursor on Jade Way, not Hall of Trials; L moves it to Hall of Trials. The floor row was "Floor 1~5".
- "Equip items in bag?" (Yes/No) appears on 1F after the load; A = Yes.
- "Go to the next floor?" (Yes/No) appears at the portal; a single A went down to 2F.

**What Tielle's actions do** (in the decompile her internal model id is 30: `s32(0x56da1cc) == 30`)

| Action | `p_act` | Length | SP | Hit record / timing | Damage |
|---|---|---|---|---|---|
| Normal attack (A) | 2 | 35 ticks (0..34), plus 5 frozen ticks on a hit | 0 | An arrow record (type 0x4d) spawns in the first free slot (slot 9 here) at `p_actt` 12. It flies 0.3/tick along the facing direction (one of 8), reach 0.5, power 0.7, and becomes type 6 on impact. It hit at `p_actt` 17, 3.9 units out. | 16 and 17 (both with the "counter" word), one-shot 15-HP slimes |
| Skill 0 (B / key 1) | 4 | 25 ticks | 4 | No hit record; at tick 20 the code calls `sub_44376A(a1, 99, -1)`, which seems to launch a separate object. It hit a slime 15.4 away 41 ticks after launch. | 35 (counter) |
| Skill 1 (key 2) | 5 | 45 ticks | 5 | Object 100 at tick 20. In the strip a purple glow stays on Tielle after the skill ends. The next normal attack did not do more damage (17). | none |
| Skill 2 (key 3) | 6 | 55 ticks | 8 | Object 105 at tick 16. | none on the target |
| Skill 3 (key 4) | 7 | 15 ticks (0..14); 20 when nothing was in range | 10 | Area attack (`sub_4967C1`): at tick 2 it sets `+3304` to 360 and loops over the actor table. | 33 (back) and 32 (side), killing both slimes in reach, the farther one 13 units away; a slime 20 away was untouched |
| Key 5 | — | — | — | Nothing happened. | — |

- **Dash:** "D . D" did not dash; speed stayed 0.175/tick.
- **Taking a hit:** a slime contact did 1 damage (f2634, and again at f2668). Tielle did not stagger (`p_act` stayed 0, `p_hurt` 0).

**Strips:** I looked at the skill-0 strip (f1826–1850) and the skill-1 strip (f2254–2318). There is no strip of a normal attack, skill 2 or skill 3.

**Game facts**
- On 2F Tielle is at (−80, −80) with the exit at (−80, −160); the 2F slimes have 18 HP.
- On 1F (start (−40, −120), exit at (−80, −120)), walking straight at the exit gets stuck on a wall. A search scored by `-exitd` over 12-tick walks found the way.
- Odd things about the crafted save: Tielle is level 40 but has only 117 HP and 147 SP. The HUD shows Day 16 while `day` reads 15.

**Tool gaps**
- `--watch` splits on commas, so `hypot(a, b)` cannot be used inline.
- On success, `--apply-best` does not apply the path; you need `--apply`, so I had to search a second time.
- `-j 2` was often refused because the machine-wide session cap was reached.
- `rd ram actors` shows no projectile or object table, so skill objects 99/100/105 are invisible to it.
- There is no expression for "newest popup".

**Still to do:** make a 2F attack, re-run `rd check` and `rd verify`, and take strips of the normal attack and skills 2–3.
