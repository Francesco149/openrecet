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
| L1 step | `rhost --live PORT`: step N ticks, read memory, eval predicates per tick, screen text | ~0.1 ms per command; 800 ticks/s in a 3D scene, ~5,000 in dialogue / menus |
| L2 snapshot | in-process savestates (`rh_snap.c`) | restore 0.2 ms, take 0.1–3 ms; a savestate is the pages changed since the anchor (17 pages / 5 ticks idle in the house) |
| search | `explore` inside the process; `-j N` sessions | ~700–5,000 ticks/s per session; maximus runs 14+ |

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
| `agent.py` / `agent.sh` | the API-billed driver (Messages API tool loop, budget, log) |
| `../rhost/rh_dev.c` | `--headless`: fake D3D8 / DirectMusic / DirectShow / DirectInput in the DEV region |
| `../rhost/rh_snap.c` | savestates: anchor (base copy + write tracking), take / restore, diff |
| `../rhost/rh_live.c` | `--live`: the command server, programs (postfix), movie, screen text, `explore` |

State on disk: `/mnt/ephemeral/openrecet/drive/runs/<run>/` — `movie.jsonl` (a flat rhost trace: the
run), `meta.json` (start, checkpoints as frames), `vars.json` (the run's own names), `session.json`
(the live process: a cache), `view/*.png`. Live sessions' rhost dirs:
`/mnt/ephemeral/openrecet/rhost-wine/runs/drive/`. A session exits after 30 min without a command;
the next command replays the movie (800–5,000 fps).

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
`./rd port R` does the last three in one go under a scratch name (`_rd-<run>`).

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

## Limits and known gaps

- Pictures need the real device: `look` / `strip` replay the movie on Windows (`ssh $RHOST_WIN`, the
  queue runner) or under wine + Xvfb (slow: ~115 fps). ~10 s for a short movie; minutes for a
  200k-frame one — chain by saves.
- Sprite-drawn menus (title, pause main page, HUD digits) are invisible to `rd text`.
- A savestate lives in its process: `-j N` search sessions each replay the movie to the head first
  and explore independently (island model; the shortest path wins).
- Search is wall-time bounded (not reproducible) and has no notion of progress beyond new cells: one
  hit per call for fights, as in the Kirby recipe.
- `--live` sessions run under wine on maximus only; the Windows box does goldens, triage, pictures
  and `verify --real`.
- Retail's render code runs every tick (it writes state the sim reads): ~70% of a 3D-scene tick.
  Skipping the pure-draw part is the next speed lever (profile: `rhost --profile F`).

## Live protocol

`rh_live.c`'s header lists every command. One line in, one line out, on `127.0.0.1:<port>` (the port
is in `<session dir>/live.port`). `drive.py`'s `Live` class is 40 lines; a search or probe script can
talk to a session directly.
