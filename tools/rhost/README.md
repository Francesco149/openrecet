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
python3 tools/rhost/rhost.py run X --hook shadow:haggle_decide # port vs retail on identical state
python3 tools/rhost/rhost.py portshots X --at 800,848       # retail | port screenshots, side by side
python3 tools/rhost/rhost.py portrng X --window 651:653     # LCG steps per tick + callers, port vs retail
python3 tools/rhost/rhost.py portrng X --window 1:end --seq  # + the first differing tick draw by draw
python3 tools/rhost/rhost.py portstate X --va 438bef4,438bf24 --changes   # every frame a VA changes, both sides
python3 tools/rhost/rhost.py portblock X --at 29806          # every dword of the layout blocks + work bank
python3 tools/rhost/rhost.py run X --extra "--watch-write 69324d8 --ww-frames 40000:50000"   # name every writer of a dword
```
`record --save` takes a `_saves` sha (a unique prefix will do), a path, or a scenario name
(the save that scenario starts from). `record --from X` replays X in rhost to its end and
starts from the save.dat X commits, stored content-addressed in `tests/scenarios/_saves/`
(user data, gitignored). The new trace's `savefile` points at that blob, with a
`# save: the one X ends with` comment line under it. This is how saves chain: dungeon 0
starts from buy-from-seller-guild-intro's end save, dungeon 1 from dungeon0-full's.

`--watch-write VA[,VA..]` (≤ 4 dwords) arms x86 debug registers (write traps, set on every
thread through a helper thread's SetThreadContext) and logs each write to `watchwrite.log`:
`ww fN VA=newvalue eip=<after the store> < return addresses` (the EBP chain); `--ww-frames A:B`
limits the log. The traps don't touch retail memory; `run` fetches the log.

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
- `C:\orc-rhost\{bin,game,runs,queue}`; `game\` is a pristine copy of the game data.
- Sibling projects run the same architecture in parallel (OpenSummoners `C:\oss-rhost`,
  DioramaEngine `C:\de-rhost`). `stage` kills only `C:\orc-rhost\bin\rhost.exe`, matched
  by full path; never kill by process name.
- `--d3d-proxy` + `--cap FROM:COUNT` records the D3D8 stream with the v3 proxy
  (loaded in-process; armed at an exact host tick via `OrV3PresentCount`).

## Not yet

wine/Linux target (works for the prototype; driver support TODO), podman container,
port-side determinism knobs (same timeline as rhost ⇒ port↔retail by frame number),
heap objects in RET-hook snapshots, D3D/file effect recording during shadow runs.
