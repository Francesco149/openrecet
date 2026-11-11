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
| search | `explore` inside the process (cells, goal, avoid, optional score); `-j N` sessions | ~700–5,000 ticks/s per session; Windows-native ~3× wine (below) |

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
(the live process: a cache — host, pid, port, rundir), `view/*.png`. Live sessions' rhost dirs:
`C:\orc-rhost\runs\drive\` on the Windows box, `/mnt/ephemeral/openrecet/rhost-wine/runs/drive/` under wine.
A session exits after 30 min without a command; the next command replays the movie (800–5,000 fps).

## Where sessions run (`RD_HOST`, 2026-11-11)

`RD_HOST=auto` (default): **natively on the Windows box** (`ssh $RHOST_WIN`; `export RHOST_WIN=win-wifi` when the
box is on wifi) when it answers within 3 s and admits the session within `RD_WIN_ADMIT_WAIT` (60 s), else on
**bunker** (the temporary NixOS box, `tools/bunker/README.md`) when it answers within 3 s and admits within
`RD_BUNKER_ADMIT_WAIT` (30 s), else under wine here. `RD_HOST=win` / `bunker` / `wine` force one. A session keeps its
host (`rd ls` shows it); a rebuilt one takes the current choice. `-j N` workers start on the main session's host; under
`auto` a worker that host does not admit goes to the next host of the order (win → bunker → wine; each worker replays
the movie, its savestates are its own anyway). The search prints which host each worker ran on, why a host took no more,
and the searching rate per host.

| host | what | admission | per session |
|---|---|---|---|
| `win` | Ryzen 9 5900X, Windows native (retail-faithful floats) | `PS_ADMIT` (shared with gates) + ≤ `RD_WIN_MAX_SESSIONS` (8) | 650–780 MB working set |
| `bunker` | i5-10600K, NixOS, wine 11.19 wow64 in one bwrap per session, all in the 6 G / 8-CPU pool | ≤ `RD_BUNKER_MAX_SESSIONS` (6) + `RD_BUNKER_MEM_FLOOR_MB` (1024) free in the bound (+ 700 MB per session still replaying) + host MemAvailable ≥ 1 GB | ~0.3 GB idle, 0.6–0.8 GB after a dungeon search |
| `wine` | maximus i9-10900K, wine 11.20 | `admit_local`: ≤ `RHOST_MAX_PROCS` (10) rhost processes, ≥ 8 GB available | ~650 MB RSS |

How a bunker session works (`drive.py` `spawn_live` → `bunker.py spawn_session` → `pool.py spawn` on the box): the
session dir travels as a tar on the ssh command's stdin to `/var/lib/orc-bunker/root/runs/drive/<name>` (=
`C:\orc-rhost\runs\drive\<name>`); `pool.py` admits it under a lock shared with batches, then starts `sh → bwrap →
wine rhost.exe … --live 0 --live-idle N` (+ the render skip) detached, joined to the pool's cgroup; it prints the host
PID (the `sh`: killing it takes bwrap's pid namespace — wine, wineserver — down) and the live port. rd reaches the port
through a local forward added to the ONE multiplexed ssh connection (`ssh -O forward`; ControlPersist 2 h; bunker's sshd
refuses past `MaxStartups 10:30:100`, so nothing opens a connection per command). `rd stop` / a rebuild cancel the
forward, then `pool.py kill PID NAME` (kills only when the PID's argv names that session's run dir) and remove the dir.
Liveness = the forward listening + `pool.py alive` (one round trip on the master, ~0.14 s). A command round trip over
the forward: 0.7 ms (LAN).

How a Windows session works (`drive.py` `win_spawn` / `win_forward`): the session dir is copied to
`C:\orc-rhost\runs\drive\<tag>-<pid>-<ms>`; rhost starts in sshd's session 0 (no desktop: fine for `--headless`)
through WMI `Win32_Process.Create`, which is outside the job sshd kills on disconnect; admission = the queue runner's
gate (`rhost.py` `PS_ADMIT`: ≤ `queue\procs.txt` rhost / port processes, ≥ 1.5 GB free, one `Global\orc-rhost-admit`
mutex — gates and sessions share one budget) + ≤ `RD_WIN_MAX_SESSIONS` (8) rd sessions; `queue\priority.txt` = low →
BelowNormal. rd talks to it through `ssh -L localport:127.0.0.1:port … "Wait-Process -Id PID"`: the forward ends when
the process does, so a dead forward = a dead session. `rd stop` / a rebuild kill the forward, then the Windows process
by PID after checking its command line is that session's, and remove its dir (`rhost.py` prune: crashed sessions' dirs
after 12 h, never a running one's). A command round trip over the forward: ~2 ms (wifi; local wine 0.1 ms).

Measured 2026-11-11 (rhost bin `38e396f9ea5b`; maximus at load 30–38 from other work, the Windows box running another
agent's gates):

| | Windows (Ryzen 9 5900X, native) | wine (maximus, i9-10900K) |
|---|---|---|
| `rd new --from griff-30f` (replay 39,676 frames, dungeon) | 15.5 s | 79 s |
| `rd new --from shop-session-week3` (8,990 frames) | 6.0 s | 17.3 s |
| `rd search … -j 1`, 30 s, Obsidian 30F → town | 1,582–1,912 ticks/s | 532 ticks/s |
| `rd search … -j 4` (wine: `-j 3`, the local cap), 30 s | 4,729 ticks/s total (~1,180 each); 3.4 s to start 3 workers + 16 s replay | 1,067 total (~356 each); ~75 s worker replay |
| spawn (stage, copy, admit, start, forward) | ~2 s | ~1 s |
| memory per session (dungeon, after a search) | 650–780 MB working set (private 700–840 MB) | ~650 MB RSS |

Bunker, measured 2026-11-11 (rhost bin `d418228b2e88`, the box otherwise idle; same movies):

| | bunker (i5-10600K, wine 11.19 in the pool) |
|---|---|
| `rd new --from griff-30f` (replay 39,676 frames) | 16.3 s (16.5 s for 39,767) |
| `rd new --from shop-session-week3` (8,990 frames) | 5.3 s |
| `rd search … -j 1`, 30 s, griff-30f + 91 ticks (town map) | 1,775 ticks/s |
| `rd search … -j 4`, 30 s, same state | 6,203 ticks/s total (1,551 each); 1.6 s to start 3 workers + ~17 s replay |
| spawn (stage check, tar → admit → start → forward) | ~0.5 s + the replay |
| memory per session (dungeon, after a `-j 4` search) | 762 MB RSS (rhost.exe) |
| command round trip over the forward | 0.7 ms |

`auto` search spread (bk-auto, shop-session-week3 head, `RD_WIN_MAX_SESSIONS=4` with another agent's 2–3 sessions on
the box, `RHOST_MAX_PROCS=3`, `-j 12`, 30 s): run 1 — win main w1 w5, bunker w3 w4 w8 w9 w11, wine w2 w6 w7 w10
(before the `admit_local` fix below); run 2 — win main (the box's cap reached), bunker 5 (the 6th slot held by another
session of mine), wine 3, 3 not admitted; searching rates win 2,208/s (one session), bunker 9,152/s (1,830 each),
wine 2,327/s (776 each; maximus loaded). Pool `memory.peak` over the whole test: 4.75 GB of 6 G.
`admit_local` fix (2026-11-11): a wine loader still starting rhost (comm `wine*`, argv `…rhost.exe`) now counts — 11
parallel worker admissions all saw 0 rhost processes and 4 started under `RHOST_MAX_PROCS=3`.

Equality (same movie, both hosts; each `rd verify` MATCH on its own host):

| movie | frames | frame / RNG win = wine | `state_sem` win = wine |
|---|---|---|---|
| hire-griff | 4,276 | yes (`19da2b9b`) | **no** |
| shop-session-week3 | 8,990 | yes (`efdbe670`) | **no** |
| griff-30f | 39,676 | yes (`04dbfdce`) | **no** |

The `state_sem` difference is float rounding: `hire-griff` replayed to its end headless on both hosts differs in 36 dwords,
all floats 1–2 ulp apart (actor positions `0x76e0ac..`, matrices `0x438ce38`, `0x6a470d0`, camera blocks); x87 CW is
`0x027F` on both. The Windows headless replay has the Windows real-device replay's values in every one of them
(their remaining differences are device pointers / vertex data): **Windows-native is the retail-faithful host**;
wine rounds differently somewhere — not root-caused; lead: retail imports `IsProcessorFeaturePresent`
(D3DX8's CPU dispatch) and the two boxes are different CPUs. **Bunker = maximus wine in game state, not in `state_sem`:** hire-griff / shop-session-week3 / obsidian-21f-30f-griff
headless to the end — frame + RNG equal on all three hosts; `state_sem` bunker `9d91e7ee…` / `0f243bd8…` / `4905a78d…`,
maximus wine `93589096…` / `0a4f1dd2…` / `dddaf52f…`, Windows `28c92241…` / `507bfc35…` / `c64fa605…` (all deterministic:
bunker repeated ×3 at -j1 / -j3). State dumps bunker vs maximus wine (hire-griff f4500, shop-session-week3 f9000,
obsidian f9000 + f39900): 1 differing dword each, `0x964c448` = an `ext-ptr(both)` (a wine DLL address: 11.19 vs 11.20
builds), plus once a `volatile-addr` — no float differences. So bunker rounds like maximus wine (same CPU family,
Comet Lake), and its hash differs only by a DLL-pointer slot not in `volatile.txt` (adding `0964c448` would make bunker
and maximus-wine hashes equal; not done while other agents' sessions hash with the current list).
Hence `rd verify` replays on the session's host
(hashes compare like for like) and prints it; `--real` still compares frame / RNG / scene with the real device.
Gates on Windows: `rhost.py snap-check --target win -j 6` **159/159 PASS** (one row re-run after a lost
`rhost.log` fetch — now retried; 1 with write-tracking residue, restore exact: `decor-variants`, as under wine);
`skip-check 414ee2 --mask 76b750:76b7b0,64c514:64da38 --target win` on 7 rows (hire-griff, day2-shop, house-door,
dungeon1-charme, dungeon2-21f-25f, dungeon2-25f-crown-kill, obsidian-21f-30f-griff): 7/7 equal; `rd check` /
`rd endsave` on a Windows session: PASS / the same save as wine.

## Reading earlier frames (`rd scan`, `rd eval --at-frame`, 2026-11-11)

Four drivers on 2026-11-11 (probes obsidian-31f-40f, obsidian-21f-30f, amber-26f-30f, obsidian-6f-10f) needed state at
frames their run had already passed; one spent 48 side runs / 16 min sampling one fight. `rd scan R --from F --to T
--watch ..` replays R's movie with its own ops (masks, ESC posts, rngseed ops: `Movie.run_args`, the live `run`'s `esc=`
/ `rng=`; hand-converting a movie to `{mask}` tokens diverged after ~100 frames because the `{"esc": F}` lines were not
ESC on that tick) and samples the columns after every tick (`run … s=P,.. e=1`). Two ways (`scan_frames`):

- **R's live session holds a checkpoint savestate ≤ F** (only checkpoints still in `meta.json` at the same frame): a
  savestate at the head, restore the nearest one, check the session's movie there equals R's movie prefix, play R's
  inputs on, restore the head, free. R's lock is held for the scan; its movie, checkpoints, undo stack and
  `session.json` are not written. (`rd verify` after scans in the session: MATCH.)
- **else a fresh side session** on the best host (`spawn_live`, the `rd new` choice; `RD_HOST` forces one) replays
  the movie from boot to F (the trace dialect, `--frames F`), then the window; stopped in a `finally`. Without a live
  session no lock is taken: the driver can keep working on R. `--fresh` forces this way; `--at-run OTHER` always
  uses it (OTHER is only read).

`rd eval R EXPR.. --at-frame F` is a one-row scan (any number of expressions, batched); `--at CK` is `--at-frame` at
the checkpoint's frame, so it falls back to the replay when the savestate is gone instead of failing. Windows are cut
into `run` commands that fit the server (`Movie.chunks`: ≤ 16 ESC and ≤ 16 rngseed ops each — `rh_live.c` drops the
rest silently — and < 60 KB per line; `rd check` / `lint` use it too).

Measured 2026-11-11 (rhost bin of that day; runs `scan-a` = obsidian-31f-40f-ohms@40500 + 2,115 played fight ticks,
`scan-g` = griff-30f@32800):

| | win (native) | bunker |
|---|---|---|
| fresh side session to f37,802 (spawn + replay) | 23.8–24.5 s | 25.8 s |
| sampling 3,001 frames (13 columns, a 40F fight) | 1.83 s = 1,640 rows/s | 1.36 s = 2,200 rows/s |
| from a checkpoint in R's session, 1,801 frames | 1.0 s (≈ 1,800 rows/s) | — |
| fresh to f31,850 + 951 frames (griff-30f, two ESCs) | — | 14.0 s + 0.50 s (1,900 rows/s; pause menu) |

Equality: scan rows = the live run's own `play --watch` rows at every frame (scan-a: 302 frames f40501..40802 with
an ESC / pause menu, `{dpad}` input, a charge hit; 1,800 frames f40816..42615) — 0 differ, both ways (fresh, and from
checkpoints); `rd eval --at c0..c3` (the live savestates) = the fresh scan's rows at 17 digits → 9 digits; `--at-frame
40650` and `--at c2` after `rd stop` (replay fallback) = the savestate values bit for bit. win = bunker on all 3,001
rows of the 13 columns (floats included). scan-g (bunker): the scan across ESC ops f31886 / f32706 = the run's head
(replayed through the trace) at f32800 and an independent `--at-frame 32750` replay.

## Measured: the first campaign (2026-11-10)

35 probes accepted in one session (task files in `tasks/`, reports in `docs/findings/probes/`), each: `rd check`
PASS, `rd verify --real` MATCH on Windows Direct3D 8, a retail golden, the port triaged. Drivers: `agent.sh`
(claude-opus-5-5, effort high) for 34, a subscription subagent for the dungeon.

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
| week5-gameover | 25,281 | 53 | $0.88 | 12 min | identical + save |
| order-deadline-missed | 10,716 | 144 | $2.55 | 22 min | identical + save |
| title-extras | 2,568 | 56 | $0.87 | 9 min | identical |
| guild-talk-topics | 3,881 | 52 | $0.77 | 8 min | identical + save |

What the numbers say:
- A menu / page probe is ~$0.5 and a few minutes; a flow with dialogue-heavy play $1–3. The driver's turns are the
  cost; the game side is free (a probe uses seconds of CPU). 34 API probes: ~$43.
- **9 port divergences in 35 probes**, each with a one-command repro; 5 ported the same day by subagents from the
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
```sh
./rd play fus "{dpad(ex - px, ez - pz)}*200" --until "portal > 0"       # an input that reads state: walk at the exit
./rd search fus --goal "a1hp <= 0" --score "-a1hp" --avoid "hp < 40" \
    --actions "{dpad(a1x - px, a1z - pz)}*8" "{dpad(a1x - px, a1z - pz) | A} .*9" "{dpad(px - a1x, pz - a1z)}*8" ".*8"
```
`{EXPR}*N` (rh_live `pID:N` segments, 2026-11-11): the mask is a program's value per tick; `pad` / `dpad` turn a
world vector into the d-pad mask. Measured on dungeon 2's 25F: an aimed walk closes 12 → 7 units in 30 ticks, the
aimed swing lands; the movie verifies (`rd verify` MATCH).

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
- **Memory is admitted, not assumed** (2026-11-11: 7 API drivers × `search -j 4` + a wine batch + a build ran maximus
  out of memory; the kernel killed desktop apps). Every local rhost process — a live session, a search worker, a wine
  batch run — starts through `rhost.py` `admit_local`: at most `RHOST_MAX_PROCS` (10) on the box and ≥
  `RHOST_MEM_FLOOR_GB` (8) available. A session waits up to 300 s for a slot; a search worker that gets none is
  skipped (`-j 6` may run as 2). Campaign rule: **≤ 3 drivers at once, `-j 2` in task files**, no wine gates beside them.
  Sessions on the Windows box (the default when it is up: "Where sessions run") go through that box's gate instead
  (shared with the queue runner's batches, ≤ 8 rd sessions) and cost maximus nothing but an ssh forward each.
- A session is ~0.65 GB resident. `./rd ls` shows the live ones;
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
- Windows sessions share the box with gates (one admission budget): during a gates batch, `-j N` may get fewer workers
  ("not admitted") and an `auto` session falls back to wine after 60 s. Idle sessions hold a slot for 30 min: `rd stop`.
- Retail's render code runs every tick (it writes state the sim reads): ~70% of a 3D-scene tick. Live
  sessions skip the one function proven pure, the overlay particle dispatcher `FUN_00414ee2` (48% of a house
  tick): house 741 → 1,330 ticks/s. Proof = `rhost.py skip-check 414ee2 --mask 76b750:76b7b0,64c514:64da38`:
  121/121 scenarios end in the same frame / RNG / game state / save.dat with and without it (the two masked
  blocks are the vertex scratch it writes; `64d408..64da38`, the arc strip of draw kind 7 = Griff's skill, added
  2026-11-11 — a session keeps the mask it started with in `session.json`, `rd verify` uses that one). `rd verify` replays WITHOUT the skip, so no accepted movie rests on
  it; `RD_SKIP_FN=""` turns it off. Next candidates (`rhost --profile F`): `FUN_00459847` (mesh walker, 9%) —
  gate each with `skip-check` first.
- Write tracking (on from a session's first savestate) is not perfectly transparent: a write fault's exception
  frame lands on the game stack, and retail code reading an uninitialised local there sees it
  (`docs/findings/headless-savestates.md` §3; one heap dword in one scenario so far). `rd verify` tells the case
  apart (same frame + RNG, other state hash → re-compared on a clean session); `rd check` always judges a fresh
  replay. Fix designs are in the findings doc.
- `rd text` does not mark the highlighted row of a list (cursor variables: mark / diff).

## Live protocol

`rh_live.c`'s header lists every command. One line in, one line out, on `127.0.0.1:<port>` (the port
is in `<session dir>/live.port`). Limits the client respects (`drive.py` top of "expressions"): a command line is split
into ≤ 600 tokens, so a program has ≤ 598 (longer ones were CUT silently — the "49 summed `and` terms evaluate to 0" of
probe obsidian-21f-30f; `Live.prog` now refuses them), a VM stack of 64, 256 program ids (the client wraps at 250),
`eval` ≤ 64 ids (`rd eval` batches 48 — the "undefined program" past ~40–600 expressions), `run s=` ≤ 32 (`play
--watch` columns first, the `event` names get what is left — ~26 columns failed before), `esc=` / `rng=` ≤ 16 each.
`compile_parts` splits an over-long top-level sum (`+`, `sum()`, `actors()`, or a name defined as one) into programs
that fit; read contexts sum their values, a stop condition must be one program. `drive.py`'s `Live` class is 40 lines; a search or probe script can
talk to a session directly.
