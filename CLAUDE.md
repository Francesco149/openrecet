# OpenRecet — agent entry point

C reimplementation of **Recettear** (EasyGameStation, 2007) as a drop-in Win32+DirectX8
replacement. Goal: **full structural parity with the retail engine**, proven behaviorally
against the user's own exe — not an MVP, not byte-identical. Legal line: **never
redistribute game assets** (the user owns the game; we read their files at runtime; never
commit game data, decompiled output or save blobs).

Dense on purpose; auto-loads every session. **The repo is the source of truth**; the
`~/.claude` auto-memory holds pointers only.

## Session start
1. `git log -n 5 --oneline`
2. `docs/FRONT.md` — the active front, ★ next micro-task, human-verification queue.
3. At session end: report pending human items (FRONT §Human queue), ranked by what they unblock.

**Hosts.**
- **maximus** — this Arch box, primary agent host. Native `python3`/`gcc`/
  `i686-w64-mingw32-gcc`/`wine`. `nix develop --command …` = the pinned devshell (build,
  tests, hooks).
- **`ssh win`** — Windows desktop, native OpenSSH on cutestation:2222, PowerShell shell.
  sshd runs in session 0 (no desktop ⇒ no D3D), so GUI/D3D jobs go through the rhost
  queue runner (`tools/rhost/README.md`). IDA Pro 9.4 lives here (`C:\orc-ida\`).
- **`ssh wslop`** — WSL NixOS on the same box. Optional now. Port 22 there is a portproxy
  to WSL.
- **`cold`** — backups.

## Methodology (2026-10-13 reset — `docs/audits/2026-10-13-methodology-reset.md`)
**Verification foundation = rhost** (`tools/rhost/`): retail runs INSIDE our process,
deterministically. The same save + input script gives byte-identical retail state, apart
from calibrated system-DLL pointer slots. No Frida, no cross-process sync, no pins. ~1000
fps on Windows native.

**Porting loop (per function):**
1. **Read:** IDA pseudocode + disasm: `/mnt/ephemeral/openrecet/decompiled-ida/all_ida.c`,
   `functions_ida.tsv` (callers/callees, FLIRT lib flag). The Ghidra export
   `docs/decompiled/all.c` is secondary: it drops x87 operands (`__ftol()` with no args)
   and garbles some float math.
2. **Write** the C port with retail semantics. Read `docs/reference/decompile-gotchas.md` first.
3. **Verify vs retail on identical state:**
   - in-game: `rhost.py corpus --hook shadow:NAME` (add a wrapper + row in
     `tools/rhost/rh_hooks.c`);
   - offline: pure functions via `tools/oracle/` P1 (call retail directly).
   - A port fn must never call its own retail VA.
4. **Commit with an evidence line:** `rhost shadow: N calls / K scenarios, 0 divergences`
   (or the divergence + why).

**Scenarios.**
- `tests/scenarios/<s>/trace.jsonl` + `{savefile}` (`@fresh` = no save).
- New ones: `rhost.py record NAME --save SHA` (the user plays on the Windows desktop) →
  a flat frame-absolute trace.
- Legacy anchor-segmented traces replay in rhost unchanged; `rhost.py flatten` freezes
  their resolved frames.

**Gates.**
- `rhost.py det SCEN -n 3`: only `volatile.txt` slots (DLL/COM pointers) may differ.
- `rhost.py corpus`: every scenario completes or reports `stalled`.

**Visual checks.** Trace Studio v3 viewer (`docs/plans/trace-studio-v3.md`): port|retail|diff
replayed from captured D3D8 streams. rhost records retail's stream with
`--d3d --cap FROM:COUNT`. Human eyes only for visual-equivalence, feel and audio judgement.

**Open Phase-0 items** (see FRONT):
- port-side determinism, so port↔retail align by frame number;
- `-std=gnu11` switch after the float sweep;
- wine/container target;
- re-verify every earlier "1:1" claim under rhost.

**Legacy — superseded, keep for reference, don't extend:**
- the Frida retail drive (`tools/frida/`, `scenario-test.py --target retail`, `orv3_window.py` retail capture);
- the pins `{phasepin}` `{bgnpcpin}` `{*loadpin}`;
- `flow_diff.py`;
- the live-probe MCP (retired; `.mcp.json` removed).

## Parity rules
- **Attribute a divergence before suspecting logic:** upstream inputs → RNG → phase →
  logic. rhost pins all of these for retail; for port↔retail, check the port's input and
  timeline first.
- **x87:**
  - In-game CW = 0x027F (53-bit), measured in rhost.
  - MSVC6 multiplies by FLOAT constants and `__ftol`s the unrounded x87 result.
  - The port's `-std=c11` (`-fexcess-precision=standard`) evaluates `f` literals as long
    double. That is a proven `haggle_decide` mismatch; `gnu11` matches.
  - Mind literal types (`0.9` ≠ `0.9f`).
- **Earlier "confirmed 1:1" records are UNVERIFIED** until re-checked under rhost
  (`docs/findings/confirmed-parity-ledger.md`). The old haggle checks covered only the
  first customer.
- **Full port, not MVP:** shortcuts are tagged `PORT-DEBT(tag, …)`; registry
  `docs/port-debt.md` (derived). Retire them; don't let them silently cap parity.
- Engine quirks (retail ground truth only) → `docs/findings/engine-quirks.md`.

## Build / run / test
- **Port:** `nix develop --command make -C src -j16` → `build/openrecet.exe`. Don't
  override `CC` (the devshell's `CC` is native gcc).
- **Host tests:** `nix develop --command make -C tests run -j16` — ~3400, ASan/UBSan; 17
  skip without `vendor/`.
- **Python tool tests:** `python3 tools/run_python_tests.py`.
- **rhost:** `python3 tools/rhost/rhost.py run|det|corpus|record|flatten|diff` (auto
  build + stage). Results go to `/mnt/ephemeral/openrecet/rhost-runs/`.
- **Game data:** maximus `/mnt/ephemeral/openrecet/staged_retail/` (unpacked exe + data);
  Windows `C:\orc-rhost\game\` (pristine; rhost writes go to the run dir). Use
  `recettear.unpacked.exe` (Steamless); `vendor/original` is SteamStub-encrypted.
- **Save blobs** (`**/_saves/*.sav.gz`) are user data: gitignored, never pushed.
- **Port exe on Windows:** only via `tools/run-openrecet.sh` (job object + singleton
  mutex). Port under wine/rhost-style determinism is Phase-0 work.
- **Backups:** `wslop-backup` (on wslop) → cold `gigavault/wslop-backup`.
  **Archives:** cold `/gigavault/archive/`.

## Conventions
- **Terse prose** (user policy 2026-06-21, revertible): telegraphic responses, docs and
  commits; code, hex, identifiers, paths, numbers and gate expressions verbatim.
  Reasoning depth stays.
- **Batch** independent tool calls in one turn. Delegate mechanical/search work to
  subagents; keep RE interpretation, parity adjudication and cross-subsystem decisions
  (`docs/AGENT-WORKFLOW.md`, R1/R2/R3).
- **Commits:** in logical units, without being asked. Master only, no branches. Push
  only when asked. The pre-commit hook (`core.hooksPath=tools/git-hooks`) regenerates
  STATUS / port-ledger / port-debt and runs the Python / C suites when those change.
- **Knowledge in `docs/`.** `FRONT.md` holds only open work; resolved arcs move to
  `PROGRESS.md` or findings in the same edit. Archive, don't delete prose
  (`docs/archive/`); git history is the archive for code.
- **If a tool can't show you something, improve the tool** (then it's one command next
  time). Don't hand-wave a divergence as phase/RNG on work in progress.
- **Human verification:** never ask the human to reconstruct state. Give a
  one-command setup and say exactly what to look for.
- **Suggest `/clear`** when a self-contained arc lands.

## Where to read next
- **Status/coverage:** `docs/STATUS.md`, `docs/port-ledger.{md,json}` (derived; runtime
  axis = rhost evidence, being rebuilt).
- **History:** `docs/PROGRESS.md`.
- **RE writeups:** `docs/findings/INDEX.md`. rhost internals and measurements:
  `docs/findings/in-process-retail-host.md`.
- **Tools:**
  - rhost: `tools/rhost/README.md`
  - oracle prototypes: `tools/oracle/README.md`
  - save.dat compare: `tools/save_compare.py`
  - viewer: `docs/plans/trace-studio-v3.md`
  - legacy trace workflow: `docs/trace-workflow.md`, `docs/flow-trace-cheatsheet.md`
- **Docs ownership:** `docs/DOCUMENTATION.md`.
- **Agent-era cleanup record:** `docs/archive/2026-10-13-agent-era-cleanup.md`.
