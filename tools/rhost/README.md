# tools/rhost — in-process retail host

Runs the user's own `recettear.unpacked.exe` **inside our process**, deterministically,
so the retail side of every comparison is a reproducible function of (save, input
script). Phase 0 of `docs/audits/2026-10-13-methodology-reset.md`; prototype history in
`tools/oracle/` and `docs/findings/in-process-retail-host.md`.

## Quick use (Windows native, from maximus)

```sh
python3 tools/rhost/rhost.py run house-idle            # replay a scenario (off-screen window)
python3 tools/rhost/rhost.py run house-idle --show     # visible window
python3 tools/rhost/rhost.py det house-idle -n 3       # determinism gate (3 runs, end dumps, diff)
python3 tools/rhost/rhost.py corpus                    # all tests/scenarios, summary.tsv
python3 tools/rhost/rhost.py diff A.rhs B.rhs          # classify two state dumps
python3 tools/rhost/rhost.py record my-scenario --save <sha>   # HUMAN PLAY → flat trace
python3 tools/rhost/rhost.py record my-scenario --from X      # … from the save scenario X ends with
python3 tools/rhost/rhost.py endsave X                      # the save.dat X commits → _saves/<sha>.sav.gz (`run` doesn't fetch it)
python3 tools/craft_pix_save.py --base _saves/<sha>.sav.gz   # that save's newest slot with 9,999,999 pix (checksum restamped)
python3 tools/rhost/rhost.py run X --hook shadow:haggle_decide # port vs retail on identical state
python3 tools/rhost/rhost.py portshots X --at 800,848       # retail | port screenshots, side by side
python3 tools/rhost/rhost.py portshots X --at 0:30000:3000 --rank   # a sweep, worst frames first
python3 tools/rhost/rhost.py portcensus X                  # per table-A / B type, the frames it goes live (which frames a render arm draws in)
python3 tools/rhost/rhost.py portframes X --at F             # every loaded model's frame tree, retail heap vs port --model-dump
python3 tools/trace_studio_v3/orv3_draws.py CAP 0 --worlds   # every draw's WORLD (indexed too) — line a capture up against the port's d3d.jsonl
python3 tools/trace_studio_v3/up_diff.py PORT_D3D F CAP      # the UP draws (sprites, glyphs, 2D) value for value: port d3d.jsonl (portshots --port-d3d) vs retail (run --d3d --cap F-1:1)
python3 tools/rhost/rhost.py gates --target wine           # the whole pipeline on this host (retail AND port under wine + Xvfb; no Windows box)
python3 tools/rhost/rhost.py prune --hours 12             # drop Windows run dirs older than 12 h (record runs kept; every batch does this once, RHOST_PRUNE_HOURS=0 disables)
python3 tools/rhost/rhost.py portrng X --window 651:653     # LCG steps per tick + callers, port vs retail
python3 tools/rhost/rhost.py portrng X --window 1:end --seq  # + the first differing tick draw by draw
python3 tools/rhost/rhost.py portstate X --va 438bef4,438bf24 --changes   # every frame a VA changes, both sides
python3 tools/rhost/rhost.py portblock X --at 29806          # every dword of the layout blocks + work bank (--at F,F,.. several in one run)
python3 tools/rhost/rhost.py run X --extra "--watch-write 69324d8 --ww-frames 40000:50000"   # name every writer of a dword
```
`record --save` takes a `_saves` sha (a unique prefix will do), a path, or a scenario name
(the save that scenario starts from). `record --from X` replays X in rhost to its end and
starts from the save.dat X commits, stored content-addressed in `tests/scenarios/_saves/`
(user data, gitignored). The new trace's `savefile` points at that blob, with a
`# save: the one X ends with` comment line under it. This is how saves chain: dungeon 0
starts from buy-from-seller-guild-intro's end save, dungeon 1 from dungeon0-full's.
End saves are cached in `_saves/endsaves.json`, keyed by the trace file's sha256 (its
`savefile` line included, so the start save too; replays are deterministic): a second
`--from X` / `endsave X` skips the replay (`endsave X --refresh` forces one). `record`'s
self-check replay stores the new recording's own end save there, so `record --from NEW`
starts right away.

### Headless, savestates, live sessions (`docs/findings/headless-savestates.md`)
```sh
python3 tools/rhost/rhost.py run X --headless --target wine        # fake devices: no GPU / desktop / audio; wine without Xvfb
python3 tools/rhost/rhost.py headless-check [SCEN..|--only RE] --target wine -j 14   # headless replays vs the Windows real-device goldens
python3 tools/rhost/rhost.py headless-check --real --target win -j 10               # this rhost build on REAL devices vs the goldens
python3 tools/rhost/rhost.py snap-check [SCEN..|--only RE] --target wine -j 12      # savestate self-test: restore + replay reproduces state
python3 tools/rhost/rhost.py skip-check 414ee2 [SCEN..] --target wine -j 8           # is skipping this render function invisible to game state?
python3 tools/rhost/rhost.py run house-idle --frames 200 --extra=" --d3d-probe"     # re-record the real adapter's answers → gen_dev_ref.py
./rd …                                                              # the driver on top of --live: tools/drive/README.md
```
- `--headless` (`rh_dev.c`): retail gets fake Direct3D 8 / DirectMusic / DirectShow / DirectInput objects living in the
  DEV region (`0x40000000..`); adapter answers come from `rh_dev_ref.h` (the reference machine's, by `--d3d-probe`);
  the window is message-only. Gate: `headless-check` — **100/100 IDENTICAL to the real-device goldens** (per-tick
  RNG, anchors, inputs, save bytes; 2026-11-10). After a change to `rh_dev.c`: `headless-check`; after a change to
  rhost's core (tick hook, heap, files, handles): `headless-check --real` too.
- Under wine, headless runs use their own prefix (`RHOST_WINEPREFIX_HL`, default
  `/mnt/ephemeral/openrecet/prefix-headless`, made on first use): they start without an X display, and a wineserver's
  first process fixes the desktop driver for all — a display-less desktop made every Xvfb run in the shared prefix
  fail at `CreateWindowExA` (err 1400) while a headless session was alive.
- `--skip-fn VA[,VA]`: calls of these retail functions become no-ops — for render functions proven pure by
  `rhost.py skip-check VA` (plain vs skipped headless replays: same frame, RNG, `state_sem`, save.dat over the corpus).
- `--live PORT` (`rh_live.c`, implies `--headless`): a command server on 127.0.0.1 (`<run>\live.port` = port + pid; 0 picks a
  port). Commands: `run`, `r` / `w`, `prog` / `eval`, `anchor` / `snap` / `restore` / `free` / `diff`, `text`, `movie`,
  `hash`, `explore`, `quit` — the file's header is the reference. With `--trace T [--frames N]` it replays first. A
  session exits after `--live-idle` seconds (1800) without a command.
- Savestates (`rh_snap.c`): `anchor` copies the tracked state (retail .data / .bss, heap arena, DEV: ~210 MB) and
  write-protects it; a savestate is the pages written since, shared with its parent where equal. Gate: `snap-check`
  (`--snap-test A:B[:K]` inside rhost) — **99/99 PASS**. An import through which the SYSTEM writes retail memory must
  call `rh_snap_touch()` first (kernel-mode writes do not fault into the tracker): `rh_snap.c`'s wrapper list,
  `rh_handles.c`'s ReadFile.
- The tick hook (`rhost.c`): host work runs as a coroutine on its own stack (`rh_step()` = one tick, boundary to
  boundary); retail's tick runs at its native stack depth with retail's registers.
- `--profile F`: EIP samples of the main thread from tick F → `<run>\profile.txt` (bucket them with
  `functions_ida.tsv`).
- The game's `MessageBoxA` is logged and answered (never blocks a batch); `PostQuitMessage` / `ExitProcess` log
  their caller; an access violation logs registers + the retail return addresses on the stack.

### Goldens and triage (`docs/plans/free-play-capture.md` FPC-2 / FPC-3)
```sh
python3 tools/rhost/rhost.py golden [SCEN..|--only RE] [--refresh]   # retail's side of the port commands, once
python3 tools/rhost/rhost.py triage [SCEN..|--only RE] [--refresh]   # every scenario: first port split, clustered
```
- A **golden** = one retail replay with `--log-rand 0:end --log-anchors --flatten` (+ the DirectMusic
  `--watch`), stored gzipped in `/mnt/ephemeral/openrecet/goldens/<target>/v<GOLDEN_VERSION>/<scen>-<trace
  sha>-<save sha>/` with `rng.golden` (per tick: draw count + LCG state after the last draw) and
  `meta.json` (RESULT). `portrng` / `portsave` / `portanchors` use it whenever the run is a plain replay
  (no `--extra` / `--hook` / `--frames` / …; `--no-golden` forces a fresh retail run). Bump
  `GOLDEN_VERSION` when rhost's deterministic core changes what retail does.
- **triage**: per scenario the port runs `--rng-golden rng.golden split.txt` (`src/rng_golden.c`): every
  tick from 1 is checked (count + end LCG state — stricter than portrng's counts); on the first split it
  runs 2 more ticks and stops, `split.txt` carrying the port's draws of the split tick. A clean run's
  save is compared to the golden's. Rows are cached in `/mnt/ephemeral/openrecet/triage/<port sha>.jsonl`;
  the report clusters splits by signature = the split tick's first consumer run whose length differs,
  `retail FUN chain ⇄ port function (retail−port)`, with scene context and a `portrng --seq` repro.
  Each staged port exe is kept under `triage/port-exes/` to symbolize its callers later.
- `RHOST_PORT_EXTRA="…"` appends raw port args (experiments: `--retail-dmusic-ptr 00` forces the
  new-game split at f71 → `FUN_00490e56 < FUN_0049a59e < FUN_004536cb ⇄ guild_stock_daily (+1)`).

### Free-play capture (`docs/plans/free-play-capture.md` FPC-4)
```sh
python3 tools/rhost/rhost.py session-install [--save S]   # freeze this rhost build + the Desktop shortcut "Recettear capture"
python3 tools/rhost/rhost.py session-status               # the pieces on Windows: cut, frames, self-check
python3 tools/rhost/rhost.py session-sync                 # finished pieces → tests/playthrough/<session>/<NNN>/ (name pt-<session>-<NNN>)
python3 tools/rhost/rhost.py session-test SCEN            # the loop end to end, headless (C:\orc-rhost\playtest)
```
- The owner double-clicks the shortcut (`C:\orc-rhost\play\play.cmd` → `win/play.ps1`): retail runs in rhost with
  real input, one **piece** per process, from `play\latest.sav` (the newest save.dat; none → New Game). rhost
  `--record trace.jsonl --cut --dump-at end` ends a piece at the tick boundary after
  - a save commit `FUN_004905a8(slot ≥ 0)` whose saved bank's day (dword `0xb0fb`) is past the day the piece
    started on (exit 10, `cut-save`; dungeon / same-day saves don't cut), or
  - a game start after the process's first: the title's start counter `dword_964351C` rising 0 → 1 (the title
    init zeroes it) — Continue / New Game / New Game+ / survival (exit 11, `cut-start`).

  The cut point is the same boundary as a plain `--frames N` replay, so the piece's trace replays to the
  identical state. `cut.json` carries the reason + the next piece's `resume` (`CODE:BANK:TOGGLE`).
- The next piece boots from that save.dat with `--preamble RESUME`: rh_play.c drives the title menu (cursor →
  the item code, A; picker cursor → the bank, A), reading retail's menu state each tick; one tap per two ticks so
  every tap is an edge; turbo until the game start, then realtime + real input. The taps are recorded like any
  input. Window close / Exit at the title ends the session; a failed preamble hands the title to the owner.
- Per piece: `play\sessions\<session>\<NNN>\{trace.jsonl,start.sav,save.dat,cut.json,meta.json,rhost.log}`; a
  `selfcheck` job (queue runner, BelowNormal; `queue\priority.txt` = low while a session runs) replays the trace
  to the recorded frame and compares frame / rng / save.dat bytes / the two state dumps (rhs.py classes, pointer
  slots aside: a recording's `state_sem` always differs from its replay's), then deletes the dumps on PASS and
  gzips the saves → `check.json`.
- `session-sync` writes `tests/playthrough/<session>/<NNN>/trace.jsonl` (savefile → `_saves`, like scenarios) +
  `meta.json`; a PASSed piece's end save goes into the end-save cache. `golden` / `triage` include the pieces
  (`--only '^pt-'`); `corpus` / `gates` stay on `tests/scenarios`.

`--watch-write VA[,VA..]` (≤ 4 dwords) arms x86 debug registers (write traps, set on every
thread through a helper thread's SetThreadContext) and logs each write to `watchwrite.log`:
`ww fN VA=newvalue eip=<after the store> < return addresses` (the EBP chain); `--ww-frames A:B`
limits the log. `--watch-exec VA[,..]` shares the 4 registers: an execute trap logs
`wx fN VA eax= ecx= edx= esi= edi= < callers` each time the instruction runs (a function's
path: trap its branch targets). The traps don't touch retail memory; `run` fetches the log.

`portblock` compares, at one frame, retail's `--dump-at` memory against the port's
`--block-dump` (src/state_va.c): the party block, the actor table, the stage objects,
the popups, records B / C, the drop-weight rows, the map objects, the floor-flow scalars
and the active work bank — differing dwords listed per block with record index + offset.
`--log-rand` logs each draw's exact call site first (the hook's return address), then
the text addresses scanned from just above it (heuristic: stale slots can still appear).
`--shot-at F` reads retail's backbuffer through its D3D device (`DAT_073dfcbc`) at the
tick-F boundary — the frame tick F presented, deterministic (PrintWindow, the fallback,
lags up to ~1000 frames at rhost speed). Port runs get rhost's canonical `recet.ini`
next to the exe, so both sides render 1024×768 with the same settings.
`portrng` labels: tick N's draws are `fN` on both sides; `--log-rand` tags draws
made by a deferred loader worker (the boundary before tick N, logged `fN-1 … worker`)
and portrng counts them in tick N, as the port does. Caveat: a port caller reached
through a sibling-call (tail) jump is reported as its caller's caller (e.g. `sim_step_a`).
`--hook shadow:floor_layout` / `shadow:actor_spawn_reset` compare FUN_0047df4d /
FUN_0041f252 table by table (both in `verify`).
Every port run (portrng / portsave / portstate / portblock / portshots) also fetches the
port's stderr as `port/port.err` and prints its failure lines (`port stderr: …`): failed asset
loads, det-loads thread failures, and the `mem:` probe (src/mem_probe.c, every 5000 frames and
per stage load: used address space by kind, threads, each heap, the live textures with the most
duplicated names) — a leak shows up there long before the 32-bit port runs out of address space.
A trace replay runs to its last op + `--tail` (the frame cap is a 2,000,000-frame backstop).
Results land in `/mnt/ephemeral/openrecet/rhost-runs/<batch>/<run>/` (`rhost.log`, last
line `RESULT key=value…`). The driver builds + stages automatically, rebuilding
rhost.exe when tools/rhost or src sources change; `run --dump-at F,..` fetches the
`state_f*.rhs` dumps.

## What makes a run deterministic

| source of run-to-run difference | rhost layer |
|---|---|
| wall clock (QPC/timeGetTime/GetLocalTime/…) | virtual time: `1000000 + 17·frame` ms |
| loader `CreateThread` race (load length in frames) | DEFER: workers start suspended, run to completion at the next tick — every load = 1 frame |
| LFH heap placement (≈280 pointers/run) | `rh_heap.c`: retail's Heap*/VirtualAlloc(NULL) → fixed-address arena 0x20000000.. |
| kernel handle numbers (CRT `__pioinfo`, loader thread handle) | `rh_handles.c`: sequence-numbered fake handles, translated in every consuming import |
| stack addresses in globals (CRT `__NLG_Notify` → 0x602374) | fixed stacks: main 0x58000000, workers 0x59000000; TEB bounds set |
| command line / environment / startup info / std handles copied by the CRT | fixed values |
| temp-file names, run-dir names leaking into retail memory | `GetTempPathA`/`GetTempFileNameA` virtual + deterministic |
| user's save / recet.ini / any game-dir write | `rh_files.c` overlay: game dir read-only, writes copy-on-write into the run dir; `save.dat` absent unless supplied |
| real devices (keyboard, pads incl. player-2 slot) | the DInput poll is not called; masks come from the script; pads hidden from enumeration |
| fake `hInstance` 0x400000 rejected by Windows dinput8 | `rh_dinput.c` hands DirectInput8Create the host module (before 2026-10-13 no Windows run had DInput, so `--record` read no keys) |
| focus / minimise pausing the main loop | `WaitMessage` → gate up, return (not in `--record`) |
| RNG seed | `DAT_006023a0` = 19937 at frame 0 (trace `{rngseed}` ops still apply) |

Residual run-to-run differences after all of the above (Win11 native, `house-idle`
4418 frames, 3 runs): **only** slots holding system-DLL/COM pointers (≈282) and the
HWND. Those addresses are listed in `volatile.txt` (calibrated by
`rhost.py det SCEN --calibrate`) and masked in `state_sem`; `state_full` hashes every byte.
`det` fails on any other class of difference.

## Determinism gate and dumps

`--dump-at F,…|end` writes `state_*.rhs` (retail .data/.bss + committed heap arena + a
per-page "external mapping" bitmap). `rhs.py` classifies each differing dword:
`ext-ptr(both)` / `ext-ptr(stale)` (DLL pointers), `volatile-addr`, `det-ptr`, `value`.
Masking is by ADDRESS only — masking by value was tried and is flaky (heap pixel data
is random bits; ~10% falls inside whatever ranges DLLs occupy in that run).

## Hooks (`--hook shadow:NAME,replace:NAME`)

`rh_hooks.c` registry; port code is linked in, compiled with the port's own `-std`.
RET hooks (`haggle_decide`, `rand`) compare return value + listed state words;
STATE hooks (`sim_a`, `sim_b`) use `rh_shadow()` (snapshot .data + arena + allocator
metadata → retail → restore → port/retail → compare; retail stays authoritative).
Add a port function = one wrapper + one table row.

## Windows plumbing

- `ssh win` = Windows OpenSSH on cutestation:2222 (port 22 there is the WSL portproxy).
  sshd sessions have no desktop (session 0 ⇒ no D3D device), so jobs go through
  **`win/runner.ps1`**, a queue runner in the interactive logon session (scheduled task
  `orc-rhost-runner`, at logon; install: `win/install-runner.ps1`). WSL is not needed.
- `C:\orc-rhost\{bin,port,game,runs,queue}`; `game\` is a pristine copy of the game data.
- **Parallel** (2026-11-04, `docs/plans/free-play-capture.md` FPC-1): the runner starts up to
  `queue\slots.txt` jobs at once (default 8; `rhost.py restart-runner --slots N` installs a staged
  runner.ps1 and sets it); `queue\priority.txt` = `low` starts new jobs BelowNormal (the owner
  playing). Every rhost / port process a job starts is admitted machine-wide: at most
  `queue\procs.txt` of ours (default 12) and only with ≥ 1.5 GB RAM free. A batch runs its runs `-j`
  at a time; `gates -j N` runs N rows at once (one build + stage, children pinned to it).
  Measured: a run is ~250–330 MB working set; 6 × buy-45pct-accept side by side = 4.0 s each, like one.
- Binaries are content-addressed: `bin\<sha12>\{rhost.exe,v3proxy_d3d8.dll}`,
  `port\<sha12>\{openrecet.exe,recet.ini}`. A stage never overwrites or kills a running image;
  `prune` drops dirs unused for 3 days. Concurrent port runs set `OPENRECET_NO_SINGLETON=1`.
- rhost.log is buffered (flushed every 1000 frames, on exceptions, at exit); stdout carries only
  RESULT / FATAL / DIVERGE / hook-result. A flush per line ran 6 parallel `--log-rand` replays ~40×
  slower than one.
- Sibling projects run the same architecture in parallel (OpenSummoners `C:\oss-rhost`,
  DioramaEngine `C:\de-rhost`, OpenLords2 `C:\ol2-rhost`, OpenMare `C:\om-rhost`), each with its
  own runner; never kill by process name.
- `state_sem` / `state_full` hash retail memory that holds a few rhost-image pointers (e.g.
  `0x964d330`): compare hashes within one rhost build only; across builds compare behaviour
  (RNG, saves, anchors).
- `--d3d-proxy` + `--cap FROM:COUNT` records the D3D8 stream with the v3 proxy
  (loaded in-process; armed at an exact host tick via `OrV3PresentCount`).

## Not yet

podman container,
port-side determinism knobs (same timeline as rhost ⇒ port↔retail by frame number),
heap objects in RET-hook snapshots, D3D/file effect recording during shadow runs,
`--live` on the Windows box, savestates across processes, pictures from the headless draw stream
(`docs/findings/headless-savestates.md` §Open).
