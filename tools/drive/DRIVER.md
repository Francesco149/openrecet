# Driver card: producing a verified retail trace with `rd`

For a coding agent with a shell (a Claude Code session or subagent). You turn a task — ordered state
predicates, or "reach / exercise X" — into an input movie that a fresh retail process replays to the
same state. Everything runs locally and is free; your turns are the cost, so work in big strides: one
`play`, `tap` or `search` per sub-goal, not one per button.

`rd` = `/opt/src/openrecet/rd` (run it from the repo root). Retail runs headless (no window): state is
numbers and text; pictures cost a replay on the real device (~10 s), so ask for them when they answer
something.

## The loop

1. **Read the task and what it points to** (findings docs, IDA names). Say in state terms what each
   step means before touching the controls.
2. **Create your run:** `rd new <run> --from <start>` (the task's `start:`). Never write into another
   run. `--force` replaces yours.
3. **Per sub-goal: act, then read state.** `rd play` / `rd tap` with `--until EXPR` stops on the frame
   the predicate turns true. For walking / positioning / timing, **search instead of steering**:
   `rd search <run> --goal EXPR --avoid EXPR --apply`.
4. **When something does not happen, find out why before repeating it:** `rd status`, `rd text` (the
   strings on screen), `rd eval`, `rd strip` (a contact sheet), `rd ram mark` → act → `rd ram diff`.
   Never run the same failing thing twice.
5. **Checkpoint before anything you might redo:** `rd ck <run> save NAME`; `load NAME` returns in
   0.2 ms. **A load is a step back in time: the movie is cut there and later checkpoints are dropped.**
   `rd undo` reverts the last state-changing command — a `ck load` or `rewind` included, 8 deep, and
   it survives a session rebuild (then it replays instead of restoring). To only read an earlier
   state: `rd eval <run> EXPR --at NAME` (the run does not move).
6. **Finish:** `rd check <run> TASK.md` prints PASS (if there is a task file), `rd verify <run>`
   prints MATCH, and you have LOOKED at a strip of the moment the trace is about. Then report: run
   name, frames, what each step's frame was, state you named, game facts you learned, what the tool
   lacked.

One command at a time per run (a run is locked while a command works). Give shell calls a timeout
above the search's `--time-limit` (default 60 s + 6 s refine; 300 s is safe).

## Commands

```
rd new R --from fresh | SCENARIO[@F] | RUN[:CK|@F] | end:SCENARIO | save:SHA    [--force]
rd play R "SPEC" [--until EXPR] [--avoid EXPR] [--watch EXPR,EXPR] [--every N] [--rows N]
rd tap R "A" [--n 40] [--gap 8] [--until EXPR] [--text]   # (A .*gap) x n: advance dialogue, mash a menu;
                                                          # --text = a transcript (the new rows before each press)
rd status R        rd text R        rd eval R EXPR [EXPR..] [--at CHECKPOINT]        rd ram R actors   # dungeon actor table
rd ram R list | read VA [NBYTES] | dwords VA [N] | str VA | define NAME --addr HEX --type f32 | --expr "..." [--status] [--event]
rd ram R mark ; (act) ; rd ram R diff [--lo VA --hi VA] [--save NAME] [--minus NAME] [--show N]
rd ck R save NAME | load NAME | list        rd undo R        rd rewind R --to F
rd search R --goal EXPR [--avoid EXPR] [--cell "(a, b)"] [--actions SPEC ..] [--time-limit S] [--apply] [-j N]
            [--score EXPR [--apply-best] [--greed G]]
rd look R [--at F,F]        rd strip R --from F --to F [--every N]      # PNGs → Read them
rd check R TASK.md        rd verify R [--real]        rd lint TASK.md --on RUN
rd endsave R        rd file R NAME        rd port R        rd stop R        rd ls
```

**Input spec:** tokens separated by spaces. `U D L R` d-pad, `A` confirm / talk (keyboard Z),
`B` cancel (X), `C` (C: camera; opens the shop from the counter), `X Y` buttons D / E, `1`..`5` skills,
`.` nothing, `~` the ESC key (pause menu; "Event Skip" in cutscenes). Combine letters (`UR`), repeat
with `*N`, group with `( … )xN`: `A . (D .*5)x3 A*2 .*30 ~ .*60`. A press is an edge: release (`.`)
between two presses of the same button.

**Inputs that read state:** `{EXPR}*N` holds the mask EXPR evaluates to, re-read before every tick (the movie
records the masks it gave, so replays do not depend on it). In EXPR, button letters are masks (`A`, `UR`),
`pad(dx, dz)` is the d-pad mask (8 directions) that walks along the world vector (dx, dz) in the shop / house,
`dpad(dx, dz)` the same in dungeons (flipped axes). Walk toward a point: `{dpad(ex - px, ez - pz)}*40`; face an
enemy and swing: `{dpad(a1x - px, a1z - pz) | A} .*9`; back off: `{dpad(px - a1x, pz - a1z)}*8`; strafe around it:
`{dpad(a1z - pz, px - a1x)}*8`. They work in `play`, in groups, and as `search --actions` — a search over
"approach / attack / retreat / strafe left / strafe right / wait" needs far fewer cells than one over raw
directions. `--watch` splits on commas: name an expression first (`rd ram R define d1 --expr "hypot(..)"`).

**Expressions** (goals, `--until`, `--watch`, `eval`, task steps): Python syntax over named state
(`rd ram R list`), `frame`, `rng`, raw reads `u8(va) s8 u16 s16 u32 s32 f32 f64` (nestable:
`s32(u32(0x73dfcbc) + 4)`), `+ - * / // % ** & | ^ << >> ~`, comparisons, `and or not`,
`x in (1, 2)`, `a if c else b`, `min max abs int sqrt sin cos atan2(y, x) hypot(a, b) pow`, `pi`.

## Search: how to think

`rd search` is the score-free exploration of the Kirby playbook, run inside the game process on
savestates. It keeps one savestate per **cell**, expands rarely-visited cells with random
macro-actions until the **goal** holds, then spends `--refine` seconds shortening the path.

- **Goal:** the step's predicate, or a stepping stone ("a cell I have not been in":
  `scene != 1`, `cc08 not in (0, 1)`).
- **Cell:** what counts as a different situation. Default `(scene, cc08, int(px), int(pz))`.
  **Put the mechanic's state in the cell** — a menu cursor, a dialogue line index, an enemy's HP, a
  timer `frame // 20` when nothing moves. Finer position (`int(px*2)`) when a step matters.
- **Avoid:** every constraint that must survive (`gold < 9000`, `scene == 2`).
- **Actions:** ~8-tick macro-actions. Default: the 8 walking directions ×8 and ×2, `A .*7`, `B .*7`,
  `.*8`. Give what the sub-goal needs (`--actions "A .*3" "D .*3" "U .*3" ".*4"` for a menu).
  ESC (`~`) cannot be in an action.
- Nothing is kept unless `--apply` (or `--save FILE`, then `rd play R "$(cat FILE)"`).
- `found: false` lists the newest cells = how far it got. One cell = your actions change nothing the
  cell sees. Split the goal there, add the action, or change the cell.
- **Score (`--score EXPR`, higher = closer):** gives the search a direction — cells are expanded in
  proportion to their score (`--greed 3`: the best e³ ≈ 20× as often as the worst), and a cell keeps
  its best-scored arrival. `--score "-exitd"` (minus the distance to the exit), `--score "-a1hp"`
  (damage dealt). The goal stays the stop condition. **`--apply-best`: when the goal is not reached
  in time, the path to the best-scored cell is kept** — search again from there instead of losing
  the minute. Without a score a failed search keeps nothing.
- `-j N` runs N independent explorations (N sessions) and applies the shortest path. Use it for
  hard goals; a single session does ~800 ticks/s in a 3D scene, ~5,000 in menus / dialogue.
- Wall-time bounded ⇒ not reproducible: apply or save what you got.

## Finding state

- `rd ram R mark`, do the thing, `rd ram R diff`: every dword that changed (as hex, int, float).
  Drop noise: mark, wait (`rd play R ".*30"`), `diff --save noise`; then mark, act,
  `diff --minus noise`. Narrow with `--lo/--hi` (retail .data is 0x528000..0x9650000; the working
  save bank starts at `bank`, layout in `tools/save_arena_map.json`).
- Name what you find: `rd ram R define NAME --addr HEX --type s32 --event` (this run's
  `vars.json`). Report names worth sharing; do not edit `tools/drive/vars.json` yourself.
- Trust state for progress, pictures for "does this look like the thing". Validate a predicate where
  you know its answer before relying on it.
- The game's own names: IDA `/mnt/ephemeral/openrecet/decompiled-ida/all_ida.c` (`dword_438CC08` =
  VA 0x438cc08), findings in `docs/findings/INDEX.md`, the port's mirror table `src/state_va.c`.

## Recettear facts (this workbench)

- Seeded state: `scene` (0 title, 1 in game, 2 game over, 6 market / guild, 9 pause menu, 11
  adventurers' guild), `dlg` (1 = a dialogue / event owns the game), `cc08` (in-scene mode: 0/1 free
  roam, 4 customer service, 10 counter menu, 15..18/30 Tear's menu, 0x17/3 decor editor), `px pz`
  (player; left/right = −/+x, up/down = −/+z), `gold`, `day`, `items`, `loading`, title vars `t_*`,
  pause-menu vars `pm_*`. `rd ram R list` has the notes.
- Title with no save: cursor on New Game (`t_item == 0`); with a save: on Continue (`t_item == 1`),
  `A` opens the slot picker with the cursor on the last-used slot, `A` again loads it (in the house
  ~190 ticks after boot). Selecting EXIT ends the game and the session (the run stays).
- The day-14 saves (`end:dungeon3-run2-save` and the crafted ones made from it): `.*60 A .*30 A` loads the slot
  (in the house ~f137; the day dword reads 14, the HUD says Day 15). The played save then gives control at once.
  The CRAFTED ones (merchant level raised) first play a level-up event (`dlg == 1`, "Recette. May I have a
  moment?", ~1,100 ticks of A taps): `~`, wait ~40 ticks for the Yes / No box, `A` on "Yes" skips it — check
  `dlg == 0`. Recette then stands at
  (−0.3, 9.35) in front of the counter, Tear at (0.6, 9.35) (`tx`, `tz`). `.*30 DL*16 U*8 UR*16 L*6 A .*7` walks to
  the counter and opens its menu; the shop door is at (3.0, −3.4): within 1.8 of it AND facing +x leaves to the
  town map (`scene == 8`, cursor `wm_cur`).
- Talking to Tear needs `reach == 20` (within 2 of her and facing her), then `A` → `cc08 == 15`. In any list menu
  `A` on an entry dispatches 15 ticks later (`cc08 == 16` / `10` meanwhile); pages lock input for ~10 ticks.
- `--from end:SCENARIO` boots with the save that scenario ends with; `--from fresh` is a new game
  (the prologue is ~2,900 ticks of dialogue: `rd tap R A --n 400 --gap 4 --until "dlg == 0"`, or ESC
  = Event Skip where the game offers it).
- Dialogue: `A` advances; a line types out first (`reveal`), a press during it completes the line.
  `rd text` shows the rows drawn through the text functions (dialogue, help boxes, lists, the save
  picker); sprite menus (title, pause main page, HUD digits) show nothing there — use state + a picture.
- ESC opens the pause menu (`scene == 9`): Items, Save, Options, Encyclopedia, Exit. `B` closes.
  An in-game Save writes save.dat: `rd endsave R` files it so a new run can start from it
  (`--from save:SHA`) — keep movies short by chaining saves.
- Loads take one tick here (deferred loader threads run at the tick boundary); never wait for a
  load by wall time, wait for state (`--until "not loading and scene == 1"`).
- A run that was poked (`w` on the live socket) is flagged POKED and cannot be filed.
- **Dungeons** (probe dungeon2-21f-25f): the controls are flipped against the shop — `R` = −x, `L` = +x,
  `U` = +z, `D` = −z; walk 0.175/tick, floor cells are 40 units. `A` sword, `B` skill (5–10 SP). `floor`
  is 0-based (21F reads 20); "in a dungeon" = `stage >= 4`. Named: `hp sp` (leader), `ex ez exitd`
  (exit portal; walking onto it raises `portal`, `A` confirms), `lock` (−1 = control), `ticker` (≠ 0 =
  play frozen; `A` after 30 ticks speeds it up), `gate` (3 = exit hidden: boss floor), `a0hp a1hp`;
  the actor table is `0x76b970 + i*0xba4` (type +0x424, in use +0x428, hp +0x42c f32, x / z
  +0x3f0 / +0x3f8 f32, yaw +0x420): `rd ram R dwords` reads a block of it in one call. What worked:
  navigation = `--score "-exitd"` (or a route-progress expression in the cell) with 12-tick walks,
  goals ≤ ~60 units apart; a boss = HP and the relative position in the cell, directed attacks
  (`"U*2 A .*8"` per direction) as actions. `end:dungeon3-deep` loads the SHOP on day 23 at night:
  burn the period, sleep, then Adventurers' Guild → Louie → dungeon 2 → row "21~25".
- **Combat** (`docs/findings/bosses/combat-core.md`, cards per boss in `docs/findings/bosses/`): sword = `p_act == 2`,
  30 ticks, the hit lands at `p_actt == 9`; damage `ATK/2 − DF·pow²/4` (`aNpow`), ×1.2 from the side, ×1.5 from behind
  (`aNrel` > pi/4 / > 3pi/4), ×1.2 while the enemy attacks (`aNst` 1..6); `aNunhit != 0` = cannot be hit. Names per
  actor slot N = 0..5: `aNx aNz aNhp aNst aNt aNsub aNpow aNd aNrel aNtype aNuse`. Read the boss's card before a
  boss floor: most have a damage gate (a power that must shed, a flip, a stun, a hit counter).
- **Floors by route, not by wandering** (probes amber-26f-30f, obsidian-3f-5f: 31 legs, every one found first try, no
  damage): the maze grid is in memory — W / H `s32(0x5cc2e8)` / `s32(0x5cc2ec)`, open cells
  `s32(0x73dfd08 + 4*(y*W + x)) == 1`, start / exit cells at `0x438b434..440`, cell (x, y) = world (−40x, −40y). BFS it
  (read the grid with `rd ram R dwords 0x73dfd08 N`), then one scored search per 40-unit leg: `--score "-dist"` to the
  next cell centre, `--cell "(int(px/2), int(pz/2))"`, actions = an aimed 12-tick walk + the 8 plain ones,
  `--avoid "hp < H"` (H = the HP now), `-j 2`. Wait for `ticker == 0` first (the floor banner freezes play although
  `lock == -1`). Boss floors: the exit is not object 0 — use `estage == 1`, then the portal / door (`portal > 0`; a door
  answers from one side only); the pad is dead ~120 ticks after a clear.
- Enemy stats live at `0x5c23f0 + type*0x68` (+0x2c HP, +0x34 AT, +0x38 DF, +0x3c MA, +0x40 MD): read them, the cards'
  code-read stats were wrong more than once. Hit records: `0x69324b0 + i*0x124` (type +0, side +4, age +0x98).
- `rd` reads state AFTER the tick (a state entered this tick shows tick 0); wrap `--expr` defines in parentheses.
- Sessions are capped machine-wide (memory): use `-j 2` at most; "not admitted" = wait or `rd stop` an idle run.
- Sessions run on the Windows box when it is up (`RD_HOST=auto`; `rd ls` shows `win` / `wine`), ~3× faster than wine;
  `rd verify` replays on the session's host. Nothing else changes for you.
- Level-15+ saves (`save:2afe7fb42617`): vending machines are bought at the Market ("Venders") and
  placed from a table's display menu (`cc04 == 1`, `L`/`R` to "Venders"); the machine's own menu is
  `cc04 == 2` (probe vending-level16).

## What not to do

- Do not read other runs' movies or `tests/scenarios/*/trace.jsonl` for the inputs of your task:
  the trace must be your own. Reading findings docs and the decompile is the point.
- Do not kill processes by name; `rd stop R` ends your session. Do not touch `C:\orc-rhost` or
  other projects' runners.
- Do not commit; report instead.
