# OpenRecet — Claude entry point

C reimplementation of **Recettear** (EasyGameStation, 2007) as a drop-in Win32+DirectX8
replacement. Goal: **full structural parity with the retail engine, matched 1:1 frame by
frame** — not an MVP. Legal line: **never redistribute game assets** (the user owns the
game; the port reads the user's own data files / extracts at runtime). Not byte-identical;
correctness is proven behaviorally against the original exe.

This file is dense on purpose and auto-loads every session. It holds orientation +
conventions; deeper detail lives in `docs/` (pointers below). **The repo is the source of
truth, not the uncommitted auto-memory.**

## Current front
→ `docs/FRONT.md` (the ONE hand-edited "current front") which is injected into
`docs/STATUS.md` (derived). Active multi-session plan: `docs/plans/`. Do **not** trust
point-in-time memory snapshots (archived under `memory/archive/`) for current state.

## How we work here (conventions)
- **Knowledge in the repo; status is derived.** Durable knowledge (conventions, playbooks,
  RE findings, per-function notes) → `docs/`. Keep the `~/.claude` auto-memory **thin and
  pointer-only**. Live status is derived: `docs/FRONT.md`→`STATUS.md`, and the
  `port-ledger`. Don't hand-track status in prose.
- **Confirmed-1:1 is authoritative.** `docs/findings/confirmed-parity-ledger.md` records
  parity a human verified vs retail. A tool/decompile "divergence" on a confirmed-1:1 item
  is a **lead to investigate**, NOT an assumed regression (may be benign-structural, a
  flawed test, or later-game variation). Don't lightly overturn well-evidenced conclusions.
- **Verify before pruning; archive, don't delete** (`docs/archive/`, `memory/archive/`).
- **Full port, not MVP.** Tag MVP/synthetic shortcuts with `PORT-DEBT(tag, ...)`
  (registry: `docs/port-debt.md`). Retire them; don't let them silently cap parity.
- **Log engine quirks as you find them** in `docs/findings/engine-quirks.md` — retail
  ground-truth behavior ONLY (not port/tooling notes).
- **Show visuals on the llm-feed** (`/opt/src/llm-feed/feed.py`, localhost:8777) — push
  images/montages/comparisons with the diff, never eog/explorer. Healthz-check + start it
  if down at session start.
- **Commits:** logical units as you go; co-author trailer is auto-injected (don't type it);
  the pre-commit hook regenerates the port ledger + runs host tests on C changes. Commit/
  push only when asked. **No branches** — commit to master (until nightly users matter).

## Run / build (host tools need `nix develop --command` prefix)
- **Build:** `nix develop --command make -C src` (mingw32; **don't** override `CC`) →
  `build/openrecet.exe` (+ `-debug.exe`, console). **Tests:** `make -C tests run` (~3000,
  ASan/UBSan; run before committing C).
- **Run the exe ONLY via `tools/run-openrecet.sh`** (or the supervisor) — bare-exe launches
  are forbidden (Job-Object reap + singleton mutex). Defaults to `--turbo --silent-audio`.
- **Disassembly/hex:** use `vendor/unpacked/` (not `vendor/original/`, still SteamStub-
  encrypted). Decompiled C (gitignored): `docs/decompiled/all.c` + `functions.csv`.
- **Retail introspection (Frida):** `--remote cutestation.soy:27042` (assume the host is up;
  don't gate progress on it). Drive port↔retail together with
  `tools/scenario-test.py --target both`.

## Where to read next (by need)
- **Is FUN_x ported / coverage:** `docs/STATUS.md` + `docs/port-ledger.{md,json}` (derived).
- **Changelog:** `docs/PROGRESS.md`. **RE writeups:** `docs/findings/INDEX.md`.
- **Tracing port↔retail (TAS traces, anchors, d3d-trace, call-trace, save override):**
  `docs/trace-workflow.md`.
- **Render/parity debugging tools:** `tools/phase_probe.py` (RNG/phase determinism verdict),
  `tools/d3d_state_diff.py` + `tools/render_diff.py` (per-draw command-stream diff),
  `tools/pixel_diff.py` / `tools/compose_comparison.py` (visual). Playbooks:
  `docs/phase-debugging.md`, `docs/render-depth-debugging.md`.
- **Orchestration / when to spawn sub-agents:** `docs/AGENT-WORKFLOW.md`.
- **Active plan:** `docs/plans/`. **Strategic frame / tooling phases:** `docs/PLAN.md`,
  `docs/harness-roadmap.md`.
