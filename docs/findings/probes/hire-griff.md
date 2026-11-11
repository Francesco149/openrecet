The `hire-griff` run passes the task check (`rd check` PASS, 5/5 steps, forbid never true; movie is 4276 frames). I never ran `rd verify`, so a fresh replay has not been shown to MATCH. Two of the passing steps are weaker than they look: steps 2 and 5 pass on frames where their predicates are loose (below).

**Step frames**
- Step 1 (guild): f450.
- Step 2 (on the floor with Griff leading): f686.
  - It passes on the first floor tick, but at that point `lock` reads 0, not −1. The predicate likely only passed because `lock` briefly read −1 for that tick; I did not check.
  - Real control comes at f997, after the "Equip items in bag?" box (Yes/No) is answered with `A`.
  - Fix: add `frame > 990`, or require the box to be gone.
- Step 3 (normal attack): f1151.
- Step 4 (skill): f1622.
- Step 5 (enemy hit): f1623.
  - It passes only because a0 was already dead: the A-mash after the first swing killed it at f1200 (the f1151 swing killed a1).
  - `f32(0x76b970+0x430)` is not max HP: the actor table defines +0x430 as the HP bar (eases 2%/tick) and +0x434 as max HP.
  - Fix: `a0hp < f32(0x76b970 + 0x434)`, or better, a damage popup (`0x44e28f0` entries, kind 0) newer than the skill.

**Checkpoints:** floor1 f997, near1 f1150, near4 f1620, sk0done f1691, ambush f1715, pre_exit f2106, floor2 f3839. Named expression: `d4` = distance to actor 4.

**Route and prompts**
- Load and leave the shop exactly as the card says; the door search took 189 frames, reaching the town map at f401.
- On the town map the first presses were ignored: `wm_cur` reached 6 by f426.
- Guild list: the cursor starts on Louie; 6 × `D` reaches "Level:40 Assassin Griff". The fee is shown as **4900** (Louie 3950).
- `A` goes straight to the bag screen, with no fee confirmation. The bag shows a help scroll that the first input dismisses, then "Depart!" above slots 1–5. `U A` departs.
- The dungeon picker's cursor starts on **Jade Way**, not Hall of Trials. `L` moves it to Hall of Trials, then `A`.
- The fee is charged at departure: gold 470000 → 465100 at f686.
- With an empty bag, "Equip items in bag?" still appears on the floor; `A` (Yes) gives control.
- The exit asks "Go to the next floor?" and needs a second `A` once the box is up.

**Griff's state**
- `s32(0x438b7d8)` = 6, which confirms Griff; the per-character code `s32(0x56da1cc)` = **41**.
- Totals: ATK 53, MATK 42 (my DEF read, 44, came while skill 3 was active).
- HP 127, SP 207 at level 40.
- Code 41 runs these game functions: swing `sub_4984BF`, skill states 4 / 5 / 6 = `sub_49708E` / `sub_49733D` / `sub_497408`. He has no state 7 or 8.

**Action table** (1F; 2F repeated the same values)

| Action | Input → `p_act` | Length | Hit record and when it lands | SP | Damage |
|---|---|---|---|---|---|
| Claw swing | `A` → 2 | 30 ticks + 4 hit-stop | 3 records of type 109 (0x6d), reach 2.5, power 1.0, anchored 1.0 ahead; spawn at `p_actt` 8, kill lands at age 6 (`p_actt` 12→13) | 0 | 30 (counter word), 33 on a second slime; one-shots 15-HP 1F and 18-HP 2F slimes |
| Skill 1: lightning charge, then a dash strike | `1` or `B` → 4 | 65 ticks + 4 hit-stop | Type 130 (0x82) centred on the player, reach 2.0, power 2.0; spawns at `p_actt` ≈30, lands at `p_actt` 48→49 | 10 | 61 (1F, side), 66 (2F, counter + side) |
| Skill 2: dark aura / buff | `2` → 6 | 40 ticks | None; sets `s32(0x56daea4)` = 480, counting down 1/tick | 20 | None; ATK/DEF totals unchanged |
| Skill 3: summon shadow familiar | `3` → 5 | 80 ticks | Type 131 in slot 0 at y + 4.7, outlives the skill (age > 295) and drifts | 30 | Familiar kills nearby enemies on its own for ~200 ticks: 20–24 per hit; 2 "HP +7" heals during it |

- In state 4 / 6, `p_inv` is held at 1; state 6 also sets it to 30 at the start, and state 5 sets it to 30.
- Mashing `A` does not chain: each swing simply restarts after `p_actt` 29.
- `C` cycles the skill slot (`s32(0x56db0d8)`) 0 → 1 → 2 → 0.
- Keys `4` and `5` do nothing.
- Key `2` maps to slot 1 but enters `p_act` 6; key `3` maps to slot 2 and enters `p_act` 5. The slot-to-state mapping is per character.
- Dash (`L . L`): `p_act` stays 0, `p_inv` is set to 40, speed 0.2625/tick (1.5× walk).
- Taking a hit (~f1800–1880, slimes during the summon): hits for 2 and 1 (popup kind 5), HP 127 → 126 → 124, healed back by the familiar. I did not record `p_act` during the hit.
- Enemy hit-record type 11 (side 1) sits in slots 0, 1, 3 and 5 around enemies.

**Strips viewed**
- Swing f1151–1184: hooded Griff, claw slashes, "CounterAttack 30".
- Summon f1720–1940: a monster-house ambush; the shadow familiar shreds slimes.
- 2F f4084–4276: skill 1's lightning dash, 66 damage; the summon orb; skill 2's dark burst.

**Save oddities:** gold reads 470000; the HUD says Day 16 while the day dword reads 15. Nothing else looked wrong.

**Route on the floors**
- 1F: a search walked to the exit portal (1405 frames, an indirect route), arriving at f3511.
- 2F reached at f3839. Swing kills a2 at f4006; skill 1 at f4079 kills a1 at f4132; summon at f4151; buff at f4234. Stopped at f4276.

**Tool gaps**
- No list comprehension or range in expressions, so I could not scan all 512 hit records or 32 popups at once; I had to use `ram diff` and `dwords`.
- A step predicate cannot tie "an enemy hit" to the action just taken.
- `--watch` fails on commas inside an expression (needs `define`).
- `search -j 2` was refused for lack of a session slot (machine at its cap), so it ran on one session.

---
model claude-opus-5-5, effort high, 108 turns, $1.80, 28 min
