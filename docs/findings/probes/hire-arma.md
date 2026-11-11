# Probe: hire-arma

- **Run:** `hire-arma` · **start:** save:824053720e01 · **task:** `tools/drive/tasks/hire-arma.md`
- **Driver:** API harness (`tools/drive/agent.py`), claude-opus-5-5, effort high, 101 turns, $1.44, 12 min
- **Scenario:** `tests/scenarios/hire-arma/` (3335 frames)
- **Verdicts (2026-11-11):** check PASS (tools/drive/tasks/hire-arma.md); verify --real MATCH; retail golden made (Windows, real device); port triage: split     hire-arma                            f1149 FUN_0049a2a9 < FUN_004986ee < FUN_0048cdcc ⇄ sim_step_a (+23)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run **hire-arma** is done. The movie is 3,335 frames. `rd check` gives PASS and `rd verify` gives MATCH (fresh headless replay: rng 84800466, sem 2112447a8fdf8e7c). Two things fell short of the card: no skill could be used on 2F (no SP left), and no dash was seen.

**Frame of each step (from `rd check`)**
1. Guild (scene 11): f450.
2. On the floor with Arma leading: f727. lock reads −1 for a moment here, before the "Equip items in bag?" box takes control again. Real control starts at f1147.
3. Normal attack: f1148.
4. Skill: f1643.
5. Enemy hit: the check puts it at f1644, but the first real hit is the punch kill at f1610 (a0hp 15→0). See the predicate fixes below.

Checkpoints: floor1 f1147, punched f1637, preskill f1641, skilled f2007, floor2 f3057. Named expressions in this run: d0 = hypot(a0x−px, a0z−pz), d1 = the same for a1.

**Route and prompts**
- The given route works up to the town map (search to the door took 189 frames; the guild loads at f450).
- **The guild list:** Leave, then the adventurers, all level 40. The cursor starts on Louie; Arma is 8 presses of D down. Fees: Louie 3950, Charme 4000, Caillou 4100, Tielle 4200, Elan 4400, Griff 4900, **Arma 5900**. Nagi's fee did not show up in the text rows.
- **Fee:** gold went 470000 → 464100 when the floor row was picked, not when Arma was picked.
- **Arma's gear:** Punch Unit, Scrap Plate, Wool Hat.
- **Bag screen:** a "Pick the items you wish to bring" notice; `A` closes it, `U` goes to Depart!, `A`.
- **Dungeon map:** the cursor remembers the last dungeon (it was on Jade Way); `L` moves it to Hall of Trials, then `A` and `A` on the "Floor 1~5" row.
- **"Equip items in bag?" Yes/No** shows on the floor about f790–1136, even with an empty bag. `A` (Yes) closes it.

**Odd things about the crafted save**
- The HUD shows Day 16 while the day dword reads 15.
- Arma starts with HP 210 and SP 404. The other adventurers and the dungeon map looked normal.

**Predicate checks**
- `s32(0x438b7d8)` reads 7 for Arma, so step 2's predicate is correct. Note that the port's `g_party.chr[0]` uses a different numbering (Charme is 27).
- `f32(0x76b970+0x430)` is the enemy's max HP: 15 on 1F where a0hp is 15, and 24 on 2F where a0hp is 24.
- **Fix for step 5:** steps are judged in order, so `a0use != 1` is already true when step 4 arrives (a0 died at f1610). Use a step that does not depend on a0, for example `any aNhp < aNmax and aNuse == 1`, or have step 5 check the damage tick before step 4.

**What each action did**

| action | p_act | length | hit record | SP | damage |
|---|---|---|---|---|---|
| Normal attack (`A`) | 2 | 40 ticks (p_actt 0..39, free on the 41st), +4 per hit (pause on impact) | type **113** (0x71) appears at p_actt 10 with age 1, hits at p_actt 13 (age 4), slot freed at age 30 | 0 | 42 shown, from behind ("BackAttack"); one-shot a 15 HP enemy (1F) and a 24 HP enemy (2F) |
| Skill 0 (`B`) | 4 | 365 ticks (f1642..2006), including the pause on impact | type **157**, appears about p_actt 199 (f1842), hits at p_actt 279 (record age 80) | the `sp` variable drops 404 → 0 at once; the HUD counts down about 4/tick | 67 shown ("CounterAttack! BackAttack!"); killed a1 (15 HP) |

- **Punch details:** the punch moves Arma about 0.46 forward. Pressing `A` repeatedly gives no combo: each punch is 40 ticks with a 1-tick gap. The picture is a fist that flies out. Missed punches: one from 3.36 away, and one interrupted at d0 1.7 when the enemy hit first; the punch that hit was pressed at d0 2.04.
- **Skill details:** a large cannon builds up in front of Arma. "CHARGE" shows from about p_actt 250 and a vertical beam fires. After the hit, Arma slides about 7 units forward (p_actt 280..305).
- **Other skills:** keys `2`–`5`, `C` and `X` did nothing, and the skill slot `0x56db0d8` stayed 0. With the Punch Unit, Arma seems to have only one skill. `Y` opened the pause menu (scene 9).
- **SP:** there is no regeneration, so after the one skill SP stayed 0 and no skill was possible on 2F. To test a skill on 2F, the skill would have to wait until 2F.
- **Dash:** `D . D*20` gave a plain walk at 0.175/tick; no dash was seen.
- **Taking a hit:** HP 210→208 at f1518 (1F) and 208→206 at f3335 (2F). The hurt state (p_act 1) lasts 34 ticks with a knockback of about 0.29/tick.

**2F:** reached through the portal (`A`, then `A` on "Go to the next floor? Yes"); control at f3057. A punch killed the 24 HP enemy a0 (search found it, f3242), and Arma took a hit at f3335. No skill (SP 0). I stopped there.

**Pictures I looked at:** the punch at f1597–1641, the skill's charge and hit at f1842–1930, and an earlier skill try at f1642–2002 that I later cut from the movie.

**Tool gaps**
- `--watch` splits on commas, so `hypot(a, b)` has to be defined as a name first. The error message did not say why.
- `--apply-best` does not apply the path when the goal *is* found; I replayed the printed path by hand.
- The `sp` variable does not match the HUD's gradual drain during the skill.
- No name for the damage number or for skill availability, and `rd text` shows nothing for the dungeon-map cursor (it took a picture).
