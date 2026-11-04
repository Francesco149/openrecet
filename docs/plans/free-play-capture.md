# Free-play capture — a whole playthrough as the divergence corpus

> **Status:** ACTIVE PLATFORM PLAN (owner-approved 2026-11-04). Live step in `../FRONT.md`.
> Depends on rhost (`../../tools/rhost/README.md`). Supersedes the per-interaction
> `rhost.py record` workflow for new human recordings (record stays for targeted asks).

## Goal

The owner plays the whole game (~1 week, no care about segmenting or "clean" interactions);
rhost builds a corpus that yields port divergences, coverage and context without a human
recording per interaction, in < 1 GB of persistent disk. After the playthrough the
Windows box (cutestation: 5900X 12C/24T, 16 GB, RTX 5060) processes it natively, in
parallel. Wine only when Windows is down or a wine-specific check is wanted.

## Principle: inputs are the database

Retail under rhost is a deterministic function of (save, input trace). Store only inputs +
the save each piece starts from; regenerate every divergence detail on demand by replay.
No live port↔retail diff: the port is not retail-layout, so it cannot run in lockstep in
rhost or be resynced after a split; offline replay gives the same data at machine time.

## Owner decisions (2026-11-04)

- Cut a new piece only at a save that crosses a day boundary (saved bank day > the piece's
  start day). Same-day saves (dungeon saves) don't cut.
- A save **load** after the process's first game start (fail → reload, quit to title →
  Load) cuts too: an in-process load carries earlier in-memory state (RNG, timers, loaded
  assets), so the next piece restarts from boot to be reproducible on both sides.
- Processing after the playthrough: the Windows box is the agent's; use it to the full.

## Work packages

### FPC-1 — Windows parallel processing (first; speeds everything up today)
- `win/runner.ps1`: N job slots (`queue\slots.txt`, default 8) instead of one FIFO job.
- Content-addressed binaries: `bin\<sha12>\rhost.exe`, `port\<sha12>\openrecet.exe`; `stage`
  never overwrites or kills a running binary (today it kills `bin\rhost.exe`, and port runs
  share one `port\openrecet.exe` upload).
- Concurrent port runs: `OPENRECET_NO_SINGLETON=1` (as under wine).
- `gates -j N` on Windows: rows as parallel child commands, the parent builds/stages once.
- Acceptance: `gates` (Windows) 50/50 ok, wall time measured vs the ~10 h serial wine run.

### FPC-2 — retail goldens (retail runs once per piece, ever)
- Per (trace sha256, rhost determinism version): end save, flat trace, anchors, coverage
  (first-hit over IDA's 3,550 entries), per-tick rand count + LCG state, `--watch` env byte.
  Compact, gzip; full `--log-rand` caller logs only for a split's window, on demand.
- portrng / portsave / portanchors read the golden instead of re-running retail (today
  every port command re-runs retail first, `rhost.py` cmd_portrng).
- One port run per piece yields rng + save + anchors verdicts (gates runs portrng and
  portsave as two port runs on 16 scenarios today).

### FPC-3 — port early exit + divergence DB
- Port `--rng-golden FILE`: compare per-tick draw count / LCG at each tick end; on the first
  mismatch run `--margin` frames, write `split.json`, exit. Port time ∝ frames to first split.
- `rhost.py triage [--only RE]`: per piece × port build → row {first split frame, kind
  (rng/save/anchor), signature, scene context from the anchors timeline}. Signature = the
  first differing draw's retail caller chain (narrow `--log-rand` window) vs port caller.
  Cluster by signature → ranked queue (`sig: N pieces, earliest P fF`, one-command repro).
  Results cached by (port exe sha, trace sha): a fix re-runs its cluster + a periodic full pass.
- `rhost.py where FUN_x|anchor` → pieces/frames that first reach it (coverage index): answers
  the FRONT's "no recording reaches it" items by lookup.
- Validate on the existing 102 scenarios before the playthrough.

### FPC-4 — capture session (`rhost.py session`, Windows, while the owner plays)
- Launched from a desktop shortcut (`C:\orc-rhost\play\play.cmd`), not via ssh / the queue;
  frozen binary copy per session (agent builds can't touch it); writes
  `C:\orc-rhost\play\<session>\<NNN>\{trace.jsonl,start.sav,meta.json}`.
- rhost `--record` + cut detection:
  - save commit: the written save-arena bank's day (bank dword `0xb0fb`, `save_bank.h`
    SHOP_DAY) > the piece's start day → finish the piece, exit code CUT;
  - load: a call to `FUN_00490259(bank)` (the only caller is the Continue picker,
    `findings/save-working-arena.md`) or a new-game commit after the process's first →
    exit code CUT (the user's load intent: the bank index).
- The session loop relaunches rhost from the current `save.dat` with a generated preamble
  (title → Continue → cursor to the bank; the picker opens at `DAT_056e578c`, last-used
  slot) driven in turbo against retail state, recorded into the new piece's trace, then
  realtime + real input. Owner sees a ~1 s reload.
- Window close → session ends; crash → the piece keeps every input (each edge is flushed),
  the next launch starts from the newest save; no end save for that piece (portrng only).
- Per-piece self-check in the background at low priority (replay → state_sem vs the
  recording's; keep the recording dump only on a mismatch).
- `rhost.py session-sync`: pull pieces to `tests/playthrough/<session>/NNN/` (traces
  committed like scenarios; saves to `_saves/`, gitignored), chain `savefile`s.
- RE so far (2026-11-04, IDA `all_ida.c`):
  - save commit `FUN_004905a8(slot)` (cdecl, 1 arg): slot ≥ 0 from the pause / dungeon save
    pickers (callers 0x41ee24, 0x47f5bc); −1 from 0x47fc44 / title / game over / ending (header
    writes). The saved bank = save arena `0x56e6280 + slot·0x2dfc8`; its day = dword `0xb0fb`.
  - load `FUN_00490259(bank)`: one caller, the Continue picker (FUN_0049a59e ~L112107), then
    `DAT_056e578c = bank` (last-used slot, header +0x1c).
  - title main menu: items `dword_9643358[i]` (count `dword_9643510`), cursor `dword_9643540`;
    codes 1 / 4 open the picker (1 = Continue, the default cursor when any bank has a score);
    input edges / repeats in `dword_73DDDD4` (0x10 A; 0x40000 / 0x80000 = held 0x4 / 0x8 repeat →
    cursor ∓1). After 7110 title frames (`dword_9643518`) the attract movie starts.
  - picker: cursor `dword_9643530` opens at `DAT_056e578c`; 0x40000 / 0x80000 ∓1, 0x20000 /
    0x10000 ∓3; A on a filled bank (`bank dword 2 != 0`) loads.
  - a human recording (buy-45pct-accept): A f1170 (menu → Continue), TITLE_PICKER_READY f1194,
    A f1224 (picker), load f1269. rhost's anchor traces can carry the preamble (resolved in
    turbo, then hand over to `--record`): wait menu → A, wait TITLE_PICKER_READY → moves → A.
  - still open: an in-process New Game commit (FUN_0049a59e L100601 `DAT_0438b1e0 = 0`, inline:
    no call to hook) — v1 lets it run on (the next day-boundary save cuts).

### FPC-5 — overnight processing
- `rhost.py playpass SESSION`: retail goldens for new pieces (Windows, parallel slots),
  then triage; report `docs/findings/`-ready tables. Steady state: Windows-native.

## Budgets

- Disk: traces ≈ 2.7 B/frame (dungeon3-deep: 796 KB / 294k frames) → 40 h play ≈ 8.6M
  frames ≈ 23 MB; saves ≈ 60 KB gz × ~150 pieces; goldens + coverage + anchors ≈ tens of
  MB. Large artifacts (`.rhs` dumps ≥ 150 MB, D3D captures) only on demand, pruned.
  Windows `runs\` reached 30 GB by 2026-11-04 with no prune since 10-25: prune per batch.
- RAM: all processes 32-bit (≤ 4 GB each); play = one retail process. 16 GB fits play, or
  ~8–12 parallel processing jobs after it.
- Compute: one full both-sides pass over 8.6M frames ≈ 1.5 days single-process under wine
  (extrapolated from gates); Windows-native retail ~1000 fps; parallel slots + goldens +
  early exit cut it further. Measure after FPC-1..3.

## Limits

- One actionable port split per piece per port build (downstream of a split is noise until
  fixed); pieces + clustering amortize it. Several divergences per pass need the hybrid
  (retail-layout port code shadowing on retail state, methodology §3 Phase 2).
- Wine: a new game draws one rand fewer under headless wine (engine quirk §163, handled by
  the DirectMusic byte handoff); any wine replay is checkable against the Windows end state.
- Pieces sit on cutestation until synced.

## Build log

- 2026-11-04: plan approved. FPC-1 landed: N-slot runner, content-addressed binaries, admission,
  parallel batches, `gates -j`. Found on the way: rhost's per-line log flush made 6 parallel
  `--log-rand` replays ~40× slower (buy-45pct-accept 4.5 s alone → 160 s each); buffered → 4.1 s.
  `gates -j 8` on Windows: 50/50 ok, 1000 s wall.
- 2026-11-04: FPC-2/3 landed: `golden`, `triage`, port `--rng-golden`. Corpus on Windows: 97 goldens
  528 s, 97 port runs 394 s; 98 identical, 1 split (guild-ui-flow f3425, FUN_00406584's draw the port
  lacks), 1 unreplayable legacy probe. Forced divergence (`RHOST_PORT_EXTRA="--retail-dmusic-ptr 00"`,
  house-idle) → split f71 `FUN_00490e56 < … ⇄ guild_stock_daily (+1)`: the signature names the right
  function. Boot (frame 0) is never compared: retail draws from seed 1 then reseeds, the port seeds
  first; both enter tick 1 at 19937. Golden of dungeon0-full (44.7k frames): 2.2 MB.
