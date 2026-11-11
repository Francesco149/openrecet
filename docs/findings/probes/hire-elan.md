# Probe: hire-elan

- **Run:** `hire-elan` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/hire-elan.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 101 turns, $1.81, 10 min
- **Scenario:** `tests/scenarios/hire-elan/` (2517 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/hire-elan.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     hire-elan                            f1250 FUN_0049a1b9 < FUN_00494ca4 < FUN_0048cdcc ⇄ sim_step_a (+17)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I ran out of budget before finishing. Steps 1–4 are reached in the movie, step 5 should hold but was never checked, and rd check and rd verify were never run. I never looked at strips, and 2F was not reached.

Run: hire-elan, movie ends at f2517. Newest checkpoint is near7 (f2394); earlier ones are floor1 (f922), near0, near1, near2, sk0done and near8.

Steps:
1. In the Adventurers' Guild (scene == 11): f375. On the town map, D D D R gave wm_cur 5 → 2 → 3, then stayed on 3 and went to Market (1). The cursor moves by map position, not list order. Fixed with R R U → 6, then A.
2. On 1F with control, Elan leading: f922 (lock == -1). On the floor s32(0x438b7d8) reads 4, so the predicate is right. Elan's internal character id is 40 (chr[0]).
3. Normal attack: f1243 (p_act == 2).
4. Skill: f1688 (p_act == 4; this try missed). A skill that hit came at f1716 on another branch, after a checkpoint load.
5. Enemy hit: a slime's HP went 15 → 0 at f1251, and kills continue after the skill (e.g. f2461). Should satisfy the step, but unchecked. In that predicate, +0x430 is the HP bar, which eases toward HP; max HP is +0x434. a0hp < bar still catches the tick of a hit, but a0hp < f32(0x76b970+0x434) is the clearer test.

Guild: the list reads Leave, then Louie, Charme, Caillou, Tielle, Elan, all "Level:40"; Elan is D×4 from Louie. Elan's fee is 4,400 (Louie's 3,950); gold went 470,000 → 465,600 at Depart. The packing screen has 5 slots, and A on a slot opens a picker. The Clothes picker offered "Tough Clothes" with "Number possessed- 0", which is odd for this crafted save. The dungeon map opens on Jade Way (the last one used), so Hall of Trials needs an L. Hall of Trials offers only "Floor 1~5". "Equip items in bag?" (Yes / No) appeared at f911 after the intro, and an A answered it with the default. 1F has 13 slimes of type 0, 15 HP each.

Elan: ATK 54, MATK 25, HP 247, SP 68. He has 3 skills (s32(0x438b7dc+16) == 3, unlocked at levels 0 / 9 / 19), so keys 4 and 5 do nothing. SP costs are 5 / 20 / 5.

Table (p_act / length / hit record / SP / damage):
- Punch (A): p_act 2; 25 ticks (actt 0..24), +5 frozen per hit; record 0x5b spawns at actt 7, hits at actt 8; 0 SP; 15-HP slime one-shot, 34 seen (side, buffed).
- Follow-up punch (A again): p_act 3; 30 ticks; entered at actt 16 of state 2, only if the punch connected; record 0x5c at actt 14; 0 SP; damage not seen (no target left).
- Skill 0 (B / 1): p_act 4; 60 ticks; record 0x5f at actt 24, live to ~47; a forward lunge ~0.35/tick from actt 22; p_inv = 1 during it; 5 SP; 25 damage (side + counter words).
- Skill 1 (2): p_act 5; dash at 0.18/tick; record 0x85 at actt 3; 20 SP; 15 → 13 on contact.
- Skill 1's contact flurry: p_act 7; ~120 ticks; repeated 0x86 records; popups 11, 11, 26, 12, 11, 10, 11, 11.
- Skill 2 (3): p_act 6; 40 ticks; no hit record; 5 SP.

Notes on the table:
- State 7 is not only Tielle's: the comment in src/dungeon_party.c about it is wrong. Skill 1 on contact turns into the state-7 flurry.
- Skill 2 is a buff: it sets p_inv 30 and writes 600 to s32(0x56dae9c) (code sub_49594E). That counter blocks every skill until it runs out. What the buff does to damage is unmeasured.
- Comment fixes: combat-core.md calls p_act 3 "chara 40 only", which is Elan's (id 40) follow-up punch; src/dungeon_party.c lists Elan's state-6 skill as FUN_0049594e.

Not confirmed:
- Dash: L . L*20 gave about 0.2775/tick against the 0.175 walk. Not confirmed to be a dash state.
- Hit taken: at f2499 p_act became 1 with a push back in +x. Looks like a hit taken (hp, p_hurt and the popup kind were not read), or possibly the dash ending against a wall.

Not done: the exit, 2F, the strips, check and verify.

Tool gaps:
- search --apply-best alone does not apply a path that reaches the goal; it needs --apply as well.
- -j 2 usually got only 1 worker (machine-wide session cap).
- --watch does not accept commas inside a call, so every distance needs a define first.
- No aN* names past slot 5.
- No helper to read the popup table or find the newest hit record.

Names worth sharing: d0 d1 d2 d7 d8, e7x/e7z/e7hp, e8x/e8z/e8hp (actor slots 7 and 8), and s32(0x56dae9c) = Elan's buff timer.

To continue: rd new X --from hire-elan:near7.
