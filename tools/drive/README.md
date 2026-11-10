# tools/drive — agents drive retail (`rd`)

An agent authors verified retail traces on its own: it steps the game live, reads state by name,
searches for inputs on in-process savestates, and files the result as a scenario the port is then
compared against. Human free-play traces (`docs/plans/free-play-capture.md`) cover what a player
does; this covers what nobody played — a menu page, a failure arm, an order missed on purpose.

Method = the Kirby playbook (`/opt/src/kirbys-adventure/tas/PLAYBOOK.md`): a trace is a program; a
goal is ordered state predicates; the model names state and designs the search, a local search finds
the inputs; done = checked on a fresh replay + looked at. This directory is that method on rhost.
The agent-facing page is **`DRIVER.md`**; this file is the reference and the campaign recipe.

## What the target provides (measured 2026-11-10)

| level | rhost | figure |
|---|---|---|
| L0 replay | any trace, any device mode | real D3D on Windows ~1,000 fps; headless 800–5,000 fps per process |
| L1 step | `rhost --live PORT`: step N ticks, read memory, eval predicates per tick, screen text | ~0.1 ms per command; ~1,300 ticks/s in a 3D scene (render skip on; 740 without), ~5,000 in dialogue / menus |
| L2 snapshot | in-process savestates (`rh_snap.c`) | restore 0.2 ms, take 0.1–3 ms; a savestate is the pages changed since the anchor (17 pages / 5 ticks idle in the house) |
| search | `explore` inside the process (cells, goal, avoid, optional score); `-j N` sessions | ~700–5,000 ticks/s per session; maximus runs 14+ |

Why it is fast: retail runs on fake devices (`rh_dev.c`, `--headless`) — no GPU, no window, no audio
device — under wine without an X server, so a session is one cheap process and all its state is
plain memory. Why it is right: `rhost.py headless-check` — headless replays equal the Windows
real-device goldens tick for tick (RNG draws + LCG state, anchors, resolved inputs, save.dat bytes):
**100/100 scenarios, 1,670,506 frames** (wine, 2026-11-10). `rhost.py snap-check`: restore + replay
reproduces state, device memory and RNG hashes. Evidence: `docs/findings/headless-savestates.md`.

## Pieces

| file | what |
|---|---|
| `drive.py` (`./rd`) | the stateless CLI: runs, movies, sessions, expressions, search, pictures, check / verify, filing |
| `DRIVER.md` | the one-page card a driving agent reads |
| `vars.json` | named retail state shared by all runs (validated entries only) |
| `tasks/*.md` | task files: start, ordered predicates, forbids, prose |
| `SAVES.md` | the probes' start saves: lineage (played → crafted → made by probes) and how to rebuild each |
| `agent.py` / `agent.sh` | the API-billed driver (Messages API tool loop, budget, log) |
| `../rhost/rh_dev.c` | `--headless`: fake D3D8 / DirectMusic / DirectShow / DirectInput in the DEV region |
| `../rhost/rh_snap.c` | savestates: anchor (base copy + write tracking), take / restore, diff |
| `../rhost/rh_live.c` | `--live`: the command server, programs (postfix), movie, screen text, `explore` |

State on disk: `/mnt/ephemeral/openrecet/drive/runs/<run>/` — `movie.jsonl` (a flat rhost trace: the
run), `meta.json` (start, checkpoints as frames), `vars.json` (the run's own names), `session.json`
(the live process: a cache), `view/*.png`. Live sessions' rhost dirs:
`/mnt/ephemeral/openrecet/rhost-wine/runs/drive/`. A session exits after 30 min without a command;
the next command replays the movie (800–5,000 fps).

## Measured: the first campaign (2026-11-10)

31 probes accepted in one session (task files in `tasks/`, reports in `docs/findings/probes/`), each: `rd check`
PASS, `rd verify --real` MATCH on Windows Direct3D 8, a retail golden, the port triaged. Drivers: `agent.sh`
(claude-opus-5-5, effort high) for 30, a subscription subagent for the dungeon.

| probe | frames | turns | cost | wall | port (final) |
|---|---|---|---|---|---|
| tear-news-summary | 277 | 32 | $0.51 | 2 min | identical |
| tear-outstanding-orders | 1,682 | 44 | $0.50 | 4 min | identical |
| counter-move-tables | 749 | 35 | $0.61 | 4 min | **split f618 → ported (749a4e37), identical** |
| guild-fusion-confirm | 783 | 93 | $2.03 | 11 min | identical |
| decor-defaults / decor-variants | 535 / 770 | 32 / 43 | $0.36 / $0.53 | 2 / 4 min | identical |
| pause-options-saved | 419 | 30 | $0.40 | 3 min | **save differed (5 bytes) → ported (dfa39b94), identical** |
| payday-week2-paid | 6,383 | 68 | $0.82 | 8 min | identical + save |
| guild-sell-expansion | 1,100 | 64 | $0.80 | 7 min | **split f746 → ported (dfa39b94), identical** |
| tear-menu-pages | 13,825 | 87 | $1.19 | 12 min | identical |
| town-rounds-week3 | 3,930 | 55 | $0.93 | 7 min | identical |
| market-buy-display | 1,647 | 87 | $1.42 | 10 min | identical |
| gameover-loop-dungeon0 | 9,541 | 99 | $1.74 | 13 min | identical |
| pause-status-items | 566 | 42 | $0.77 | 5 min | identical |
| shop-session-week3 | 8,990 | 159 | $2.90 | 25 min | identical + save |
| pub-evening-week3 | 3,928 | 67 | $0.70 | 8 min | identical |
| order-customer-week3 | 10,139 | 185 | $3.28 | 23 min | identical |
| calendar-week3 | 38,835 | 158 | $2.72 | 14 min | identical + save |
| vending-level16 | 5,622 | 105 | $1.49 | 27 min | **split f5500 → vending machines ported (10e36b61), identical** |
| dungeon2-21f-25f | 12,513 | subagent | subscription | ~2 h | identical |
| calendar-week4 | 36,468 | 166 | $2.75 | 21 min | identical + save |
| pause-encyclopedia | 2,038 | 41 | $0.88 | 5 min | identical |
| change-tables-l16 | 2,149 | 92 | $1.46 | 10 min | identical + save |
| vender-sales-l16 | 9,296 | 88 | $1.39 | 10 min | **save differed (5 bytes) → ported (10e36b61), identical + save** |
| vender-revert-sell | 1,569 | 54 | $0.71 | 5 min | identical + save |
| ending-week5-paid | 57,480 | 137 | $2.27 | 25 min | **split f30885** (the money roll in the pause scene; then the ending) |
| newgame-plus | 1,372 | 47 | $0.63 | 5 min | **split f436** (New Game + start unported) |
| item-storage-l17 | 2,207 | 74 | $1.03 | 8 min | identical + save |
| survival-start | 6,918 | 64 | $0.95 | 9 min | **split f243** (the picker start, as New Game +) |
| endless-first-days | 10,659 | 69 | $1.07 | 11 min | identical + save |
| survival-hell-start | 20,163 | 81 | $1.34 | 14 min | **split f212** (the picker start) |

What the numbers say:
- A menu / page probe is ~$0.5 and a few minutes; a flow with dialogue-heavy play $1–3. The driver's turns are the
  cost; the game side is free (a probe uses seconds of CPU). 30 API probes: ~$38.
- **9 port divergences in 31 probes**, each with a one-command repro; 5 ported the same day by subagents from the
  probe's report + IDA (Move Tables, the Options save, the guild's Sell + Expansion, vending machines, machine
  sales), 4 open (the ending; New Game + and both Survival starts, one cause). Probes that come back identical verify "ported ahead of a recording" code (Fusion's
  confirm / result, the paid payment night, Tear's pages, a week-3 shop session with haggling, customer orders,
  dungeon 2's floors 21–25).
- Drivers correct the task: three predicates of mine were wrong (a mode that does not exist, a 0-based floor, a
  counter cleared a tick early) and the reports said so with the fix. Lint first; fix the task, not the run.
- Two first probes stopped at "this save lacks the precondition" within 2 minutes — cheap, but avoidable: check it.
- Probes make the next probes' start saves: the calendar walks left a save per day for days 15–29 (weeks 3–5); the
  crafted level-10 / level-16 saves opened Tear's pages, Move Tables, vending machines.
- Action play works by search but is the expensive kind: 5 dungeon floors + one boss kill took a subagent ~2 h,
  most of it failed searches without direction — the reason for `--score` / `--apply-best` (below).
- What did not work: nothing was grinded. A driver that needs many shop days plays them at ~$1.5 per session;
  craft the precondition instead and say so.

## A run, end to end

```sh
./rd new fus --from end:dungeon3-run2-save          # boot with the save that scenario ends with
./rd play fus ".*100 A .*40 A .*200"                # title → Continue → slot → in the house
./rd status fus ; ./rd text fus                     # frame, scene, gold, day … / strings on screen
./rd ck fus save home
./rd search fus --goal "scene == 6" --time-limit 90 --apply -j 8     # e.g. walk out to the town map
./rd strip fus --from 300 --to 900                  # contact sheet (real-device replay): Read the PNG
./rd verify fus --real                              # fresh headless replay = live hashes; real D3D = same frame / RNG
./rd file fus fusion-confirm                        # → tests/scenarios/fusion-confirm/trace.jsonl
python3 tools/rhost/rhost.py golden fusion-confirm  # retail golden on the real device (Windows)
python3 tools/rhost/rhost.py triage fusion-confirm  # the port vs that golden: IDENTICAL or its first split
```
More, per sub-goal:
```sh
./rd tap fus A --n 60 --gap 30 --text               # a dialogue transcript: the typed-out rows before each press
./rd search fus --goal "portal > 0" --score "-exitd" --apply-best --time-limit 60 -j 6
                                                    # direction + progress kept when the goal is not reached in time
./rd eval fus "atan2(a1z - pz, a1x - px) * 180 / pi" "hypot(px - ex, pz - ez)"
./rd ck fus load home ; ./rd undo fus               # a step back, and back again (8 deep; survives a session rebuild)
```
`./rd accept R NAME --task tasks/T.md` does the acceptance in one go: check → verify --real → file → golden →
triage → the driver's report (if `agent.sh` made one) into `docs/findings/probes/NAME.md`. `--no-triage` while
someone is editing the port (triage builds the working tree). `./rd port R` = file + golden + triage under a
scratch name (`_rd-<run>`).

## Campaign recipe (an orchestrating session)

1. **Pick targets from the gaps**, not from what is easy: FRONT's "no recording reaches it" items,
   `PORT-DEBT` rows marked unverified, triage clusters, retail functions with no coverage
   (`rhost.py run X --coverage`). One behaviour per task.
2. **Write the task** (`tasks/NAME.md`): `start:` the latest save that makes the behaviour reachable
   (`end:SCENARIO`, `save:SHA`), ordered `steps:` as predicates over named state, `forbid:` lines,
   prose with the findings docs / IDA functions that describe the mechanic. If the state is not named
   yet, say which function writes it — finding and naming it is part of the task.
   **Check the precondition before spending a driver on it** (a merchant level, a day, an item): both
   first probes of 2026-11-10 stopped at "this save is level 6, the entry needs 7 / 10". When no
   played save has it, craft one — `tools/craft_pix_save.py --base SAVE --set 0xb100=9` (any bank
   dword, checksum restamped) → `start: save:SHA` — and say in the task that the state is crafted.
3. **Lint** predicates on runs whose behaviour you know (`rd lint TASK --on RUN`): a step that never
   holds on a run that does the thing is a wrong predicate; one that holds inside the start prefix
   proves nothing (steps are judged after the prefix).
4. **Hand each task to a driver** with `DRIVER.md` + the task file. Two kinds, same card:
   - **a subagent of the Claude Code session** (subscription — the default): brief = "read
     `tools/drive/DRIVER.md` and `tools/drive/tasks/NAME.md`; your run is NAME; report per the card";
   - **`tools/drive/agent.sh tasks/NAME.md --run NAME --budget N`** (`agent.py`: the Messages API with
     five tools — `rd`, `read`, `grep`, `view`, `finish` — no shell; billed to the API key in
     `ANTHROPIC_API_KEY` / `~/claude-platform.txt`; hard dollar budget; log + report under
     `/mnt/ephemeral/openrecet/drive/agents/NAME/`). Use it when there is API credit to burn or more
     drivers are wanted than a session should spawn. Exit 4 = the account has no credit left.
   Drivers are independent: one run each, any number side by side (a session is ~0.6 GB and one
   core while it works).
   Measured 2026-11-10 (claude-opus-5-5, effort high): News Summary probe 32 turns / $0.51 / 2 min;
   counter-menu probe 20 turns / $0.31 / 2 min — a menu probe costs well under a dollar.
5. **Accept:** `rd check` PASS, `rd verify --real` MATCH, a strip of the key moment looked at. Then
   `rd file` + golden + triage. A split is the payoff: fix the port by the porting loop (CLAUDE.md),
   re-run `triage`, add the scenario to `gates` when it is IDENTICAL.
6. **Keep what was learned:** names → `vars.json` (after you validated them), game facts →
   `docs/findings/game-recipes.md` and `DRIVER.md`'s facts section, tool gaps → fix the tool.
7. **Chain by saves.** Every replay (look, verify, check, a rebuilt session) starts at boot, so keep
   movies short: save in game, `rd endsave R`, start the next run `--from save:SHA`.

## Several agents at once (what bit, what is in place)

- One run per driver; a run is locked while a command works on it. Never read or step another driver's run.
- rhost rebuilds: `rd` / `rhost.py` rebuild rhost.exe when a source it links is newer. Builds are locked, atomic
  and deterministic (same sources → same `bin\<sha>`); only `tools/rhost/*` and the port files rhost links
  (`rhost.py` `PORT_LINKED`, any `src/*.h`) count — an agent editing the port does not restart everyone's sessions.
  When rhost itself changes, each live session is rebuilt by replay on its next command (savestates of that
  session are gone: `ck load` falls back to replay).
- Headless wine runs have their own prefix (`prefix-headless`); the Xvfb prefix stays usable for real-device runs
  on maximus while sessions are alive.
- A session is ~0.65 GB resident; maximus (31 GB) holds ~20 next to a build. `./rd ls` shows the live ones;
  `./rd stop R` when a probe is accepted (accept does it).
- `triage` / `portrng` build the port from the working tree: while a port-fix agent is at work, accept with
  `--no-triage` and triage after its change is committed.

## Limits and known gaps

- Pictures need the real device: `look` / `strip` replay the movie on Windows (`ssh $RHOST_WIN`, the
  queue runner) or under wine + Xvfb (slow: ~115 fps). ~10 s for a short movie; minutes for a
  200k-frame one — chain by saves.
- Sprite-drawn menus (title, pause main page, HUD digits) are invisible to `rd text`.
- A savestate lives in its process: `-j N` search sessions each replay the movie to the head first
  and explore independently (island model; the shortest path wins).
- Search is wall-time bounded (not reproducible). Without `--score` it has no notion of progress beyond new
  cells, and a failed search keeps nothing; with it, cells are expanded by score and `--apply-best` keeps the
  best cell's path. Measured (shop, 13 units to the door, the goal needs the right side + facing; 3 seeds, 40 s):
  unscored found 2/3 (5.4 s, 11.6 s), scored 3/3 (3.1 s, 10.3 s, 27.6 s) — the score is not a faster search on a
  short goal, it is a search that always returns ground gained (4 s: unscored nothing, scored at the door).
  Fights: one hit per call, HP in the cell, as in the Kirby recipe.
- `--live` sessions run under wine on maximus only; the Windows box does goldens, triage, pictures
  and `verify --real`.
- Retail's render code runs every tick (it writes state the sim reads): ~70% of a 3D-scene tick. Live
  sessions skip the one function proven pure, the overlay particle dispatcher `FUN_00414ee2` (48% of a house
  tick): house 741 → 1,330 ticks/s. Proof = `rhost.py skip-check 414ee2 --mask 76b750:76b7b0,64c514:64d408`:
  121/121 scenarios end in the same frame / RNG / game state / save.dat with and without it (the two masked
  blocks are the vertex scratch it writes). `rd verify` replays WITHOUT the skip, so no accepted movie rests on
  it; `RD_SKIP_FN=""` turns it off. Next candidates (`rhost --profile F`): `FUN_00459847` (mesh walker, 9%) —
  gate each with `skip-check` first.
- Write tracking (on from a session's first savestate) is not perfectly transparent: a write fault's exception
  frame lands on the game stack, and retail code reading an uninitialised local there sees it
  (`docs/findings/headless-savestates.md` §3; one heap dword in one scenario so far). `rd verify` tells the case
  apart (same frame + RNG, other state hash → re-compared on a clean session); `rd check` always judges a fresh
  replay. Fix designs are in the findings doc.
- Action specs cannot read state ("face the enemy, then attack"): write one directed action per direction.
  `rd text` does not mark the highlighted row of a list (cursor variables: mark / diff).

## Live protocol

`rh_live.c`'s header lists every command. One line in, one line out, on `127.0.0.1:<port>` (the port
is in `<session dir>/live.port`). `drive.py`'s `Live` class is 40 lines; a search or probe script can
talk to a session directly.
