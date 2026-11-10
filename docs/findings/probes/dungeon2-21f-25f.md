# Probe: dungeon2-21f-25f

- **Run:** `dungeon2-21f` · **start:** end:dungeon3-deep · **task:** `tools/drive/tasks/dungeon2-21f.md` (the adjusted thresholds)
- **Driver:** subscription subagent (`tools/drive/DRIVER.md`), `rd search -j 6` for every floor
- **Scenario:** `tests/scenarios/dungeon2-21f-25f/` (12513 frames)
- **Verdicts (2026-11-10):** check PASS (the adjusted task); verify --real MATCH; retail golden made (Windows, real device);
  triage vs port 4bcff114e236: **identical ticks 1..12813** (floors 21F–25F of dungeon 2 + the first crown slime: the port's
  dungeon sim holds on floors no recording had reached)
- **Tool gaps → fixed the same day:** durable `rd undo` (1), `rd search --score / --apply-best` (2, 3), `sqrt sin cos atan2 hypot`
  in expressions (4), `rd ram dwords` (5), task labels with ":" and `--event` collapsing (8). Open: action specs that read
  state (4), the highlighted row in `rd text` (6).

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Result:** 25F of dungeon 2 reached with the party alive; one of the two crown slimes is dead, the other is at 311/380. The boss is not beaten. The movie ends at frame 12513 (Louie hp 89/142, sp 22/32) and the live session is stopped.

## Verdicts

Original task file (`tools/drive/tasks/dungeon2-21f.md`):
```
step 1 at frame 3704: loaded into dungeon 2 from the 20F save
step 2 at frame 6155: one floor below the start
step 3 at frame 6500: two floors below the start
step 4 NOT REACHED: floor 25 reached: s32(0x438b4c8) == 2 and s32(0x438b4cc) >= 25
FAIL
```
Step 4 fails because `0x438b4cc` is 0-based: 21F reads 20 and 25F reads 24 (checked against the HUD on 21F, 22F, 23F and 25F).

Adjusted copy (now the committed task file: step 4 threshold `>= 24`, plus a step 5 for the first slime's death):
```
step 4 at frame 9836: floor 25F reached (0x438b4cc is 0-based, 21F reads 20 and 25F reads 24)
step 5 at frame 12403: one of the two 25F crown slimes (actor 1, type 23) dead
PASS
```
`rd verify`:
```
fresh headless replay: frame 12513 rng 32a657ee sem 069d36785f9afcd0 dev 3386575a0bdb8946
live session:          frame 12513 rng 32a657ee sem 069d36785f9afcd0 dev 3386575a0bdb8946
MATCH
```
Strip looked at: `view/strip_12300_12510.png` (slime 1 dying, then "Counter Attack! Side Attack! 22" on slime 2).

## The task's start premise is wrong

`end:dungeon3-deep` → Continue → slot 021 loads the **shop on day 23 at night**, not a dungeon. The Adventurer's Guild is disabled at night, so the movie spends f0–3704 getting there:

- **Burn the period:** shop door → map → Merchant's Guild → `B` → home → night exchange → day-end summary → day 24 (f880).
- **Guild:** door → map dest 6 → Louie (L26, the cursor's default) → bag → dungeon 2 → floor row 4 ("21~25") → Depart at f3704.
- **Bag:** Steel Sword, Parade Armor, Parade Helm, Big Shield (auto-equipped by the "Equip items in bag?" prompt), 3 Walnut Bread, 1 Taiyaki, 6 Candy.

## State named (run `vars.json`)

| name | address / expression | meaning | validated by |
|---|---|---|---|
| `hp`, `sp` | f32 `0x56db0bc`, `0x56db0c0` | leader HP / SP | HUD 142/142, 128/142, 89/142, 32/32 |
| `floor` | s32 `0x438b4cc` | floor, 0-based | HUD 21F=20 … 25F=24 |
| `portal` | s32 `0x438be9c` | portal prompt (1.. = "Go to the next floor?") | text on screen |
| `equipq` | s32 `0x438bea0` | "Equip items in bag?" prompt | text |
| `lock` | s32 `0x438b8b0` | −1 = player has control | boss intro holds it at 0 |
| `ticker` | s32 `0x438b92c` | floor-event ticker; **play is frozen while it is non-zero** | `0x438b8d0` stuck at 1, movement dead until it reached 0 |
| `ex`, `ez` | f32 `0x695ef78`, `0x695ef80` | exit portal (object 0, kind 8 at `+0x8c`) | walking there raised the prompt on 4 floors |
| `gate` | s32 `0x695ef78 + 0x268` | exit object state; 3 = hidden on the boss floor | read 3 on 25F only |
| `b0`, `b1` | f32 `0x76bd9c`, `0x76c940` | HP of actors 0 / 1 | matches the boss HUD bar |
| actor fields | `0x76b970 + i*0xba4`: type `+0x424`, in use `+0x428`, hp `+0x42c`, x/z `+0x3f0/+0x3f8`, yaw `+0x420` | actor table | from `src/actor_table.h`, matches pictures |
| guild floor cursor | s32 `0x6a49b54` | floor-list row | mark/diff on one `D` |
| world map | sel `0x9643684`, enable `0x9643588[7]`, timer f32 `0x9643628` > 10 | dest 6 read 0 at night, 1 in the morning | picture + behaviour |

`prog` was redefined per floor as a hand-written route-progress expression (see below).

## Per floor

| floor | frames | enemies (type × count) | HP in → out | what it took |
|---|---|---|---|---|
| 21F | 3704 → 6155 | 44 at L12: 45×8, 6×7, 3×8, 7×7, 19×4, 37×3, 15×3, 20×3, 44×3 (plus two type 2 spawned later) | 142 → 128 | 3 failed searches, then 1 found (1188 frames) |
| 22F | 6155 → 6500 | not listed | 128 → 128 | exit one cell up; plain `U*252` |
| 23F | 6500 → 9094 | 68 at L13: 3×7, 6×7, 7×4, 15×8, 19×10, 20×4, 37×11, 44×7, 45×10 | 128 → 95 | 2 failed full-route searches, then 4 stepping stones |
| 24F | 9094 → 9836 | 57 at L13: 3×7, 6×8, 7×7, 15×6, 19×5, 20×8, 37×5, 44×5, 45×6 | 95 → 95 | 1 search, 648 frames |
| 25F | 9836 → 12513 | type 23 ×2 (crown slimes, 380 HP, L14); a type 2 (57 HP) spawns when one dies | 95 → 108 (bread) → 89 | 9 searches; blocked |

## Search settings

Throughput was 1,000–4,700 ticks/s summed over `-j 6`, roughly 400–800 per session in 3D.

**Worked**
- **Navigation:** `--cell "int(prog/2)"` (later `(int(prog/2), int(hp/12))`), where `prog` is route progress written from the 40-unit cell grid. Enemy positions binned by 40 show which cells exist. 12-tick walks in 8 directions plus `A .*7`, goals at most ~60 units apart: 26–56 s each on 23F, 97 s for 24F.
- **Boss, first slime:** `--cell "(int(bhp/5), int((px-b1x)/1.5), int((pz-b1z)/1.5), int(b1y/2))"` with directed attacks `"DIR*2 A .*8"` for 8 directions. 43 damage in 33 s, 135 in 30 s, the kill in 115 s.

**Failed**
- Default cell with a stuck player (ticker up): 4 cells in 448k ticks.
- `(int(px/5), int(pz/5))` with default 8-tick actions: 106 cells in 465k ticks. One action is 1.4 units and never crosses a 5-unit cell.
- `(int(px/3), int(pz/3))` with 20-tick actions: explores uniformly, 581 cells in 156 s, no direction.
- Full-route `prog` goals on 21F and 23F: three ran out of time, and a failed search keeps nothing.
- Boss without directed attacks: 60 damage in 100 s, found by 1 of 6 sessions.
- Second slime after ~f12550: 4 searches, ~1.4M ticks, never more than 6 damage.

## Game facts

- Dungeon controls are flipped against the shop: `R` = −x, `L` = +x, `U` = +z, `D` = −z. Walk speed is 0.175/tick; floor cells are 40 units.
- `A` is the sword; `B` is a skill costing 5–10 SP (it one-shot a 47 HP knight).
- Every floor entry can show a ticker that freezes play; `A` after 30 ticks speeds it up.
- Normal enemies die in 1–3 hits and hit for about 11.
- Crown slimes: a plain hit does 1–3, and so did one skill use. Hits labelled "Side Attack!" or "Counter Attack!" do 20–33. The landing after a big jump costs 18 HP at 3.3 units and nothing at 10.
- What blocks the boss: after about f12550 I could not produce a single labelled hit on the survivor. Position around it, its yaw, the hop state, the skill and Louie's stats (Attack still 54) made no difference. The small slime that spawned at the first death vanished at ~f12555 with nobody attacking it, right when the damage dropped; I suspect the survivor absorbs it, but did not confirm this.
- Items in the pause menu: Walnut Bread gave +13 HP, the Taiyaki took SP from 2 to 32, and Candy did nothing at full SP.
- The guild item picker keeps its tab and row between picks and drops empty tabs, so scripted picks drift (I got Candy and Taiyaki in place of Apples).

## Tool gaps, most painful first

1. **`rd ck load` of an earlier checkpoint lost work.** It printed "`rd undo` brings it all back", then after one `rd ram mark` undo said "nothing to undo in this session". I lost f12403–12818 and replayed the known path to get back to 12513.
2. **A failed search keeps nothing.** A `--apply-best` on some progress expression would have saved about 10 minutes.
3. **No direction in the search.** Each floor needed a hand-written `prog`; a "minimise EXPR" mode or a built-in cell-graph distance would remove that.
4. **Expressions have no trig, and action specs cannot reference state**, so "face the enemy and attack" or "which side of its facing am I on" cannot be written.
5. **No bulk memory read.** `rd eval` with 512 expressions fails with "undefined program"; I batched 64 at a time and looped over actors (~70 calls per enemy list).
6. **`rd text` does not mark the highlighted row,** so menu cursors need mark/diff or pictures.
7. **Rebuild and session noise:** one `rd search` printed "built …/rhost.exe" twice (I did not ask for a build) and took 21 s to start; later "the live session did not start" appeared once and an eval printed "[session rebuilt by …". The run recovered each time.
8. Smaller ones:
   - The run lock gives "busy" to a second `rd` inside a shell pipeline.
   - A task-step label containing ":" breaks `rd check`.
   - `--event` on a counter prints a line per tick.
   - A `stime` status variable appeared mid-run from somewhere else.
