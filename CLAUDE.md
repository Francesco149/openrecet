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
- **Parity is multi-pillar — attribute every divergence to a pillar BEFORE suspecting
  logic.** An observed difference comes from one of: (1) **logic / data→output** — the
  pure-function contract we actually port (same inputs ⇒ same output); (2) **phase** —
  load-dependent counter/anim-cycle ORIGIN (retail freezes `db054` through the intro video
  the port skips ⇒ a *constant* offset, not a bug; normalized by `{phasepin}`); (3) **RNG**
  — same LCG consumption order/count ⇒ same values for a seed, but the seed/phase origin may
  differ (and may even be non-deterministic in retail); normalized by `{rngseed}`; (4)
  **upstream inputs** — if a frame's inputs already diverge, don't blame that frame's code
  (fix the path in order, frame 0 forward). **Normalize phase + RNG + inputs, THEN compare:
  if output is bit-identical under pinning, the logic is "confirmed 1:1 given same data"
  even when the raw un-pinned output differs.** Record that distinction (data-1:1 vs
  observed-1:1, with which pillars are off-but-accepted) so a known phase/seed-origin offset
  is never re-suspected as wrong logic. **`tools/flow_diff.py --verdict --align-field
  db054`** gives the verdict: ALIGNED / CONST-OFFSET (= phase, accept) / DRIFT (= real
  logic divergence), over a `scenario-test --target both --call-trace` capture. Playbook:
  `docs/flow-trace-cheatsheet.md`.
- **Verify before pruning; archive, don't delete** (`docs/archive/`, `memory/archive/`).
- **Full port, not MVP.** Tag MVP/synthetic shortcuts with `PORT-DEBT(tag, ...)`
  (registry: `docs/port-debt.md`). Retire them; don't let them silently cap parity.
- **Log engine quirks as you find them** in `docs/findings/engine-quirks.md` — retail
  ground-truth behavior ONLY (not port/tooling notes).
- **Show visuals on the llm-feed** (`/opt/src/llm-feed/feed.py`, localhost:8777) — push
  images/montages/comparisons with the diff, never eog/explorer. Healthz-check + start it
  if down at session start. **But if it's inspectable in a live Trace Studio session, point
  the user at the session URL instead of pushing it to the feed (see the Trace Studio bullet).**
- **Iterate ON the user's open Trace Studio session — recapture IT, don't spawn a parallel
  capture (standing workflow, automatic like the llm-feed).** The core parity loop runs on
  **studio traces**: the user keeps a `trace_studio.py serve --session <name>` open (check
  `ps`/the serve logs for the live session + port) and watches it. When you tweak a trace
  (edit its `edit.trace.jsonl` — caprange/pins/inputs) or land a port fix, **re-capture THAT
  SAME session** (`trace_studio.py recapture <name>` / `capture --only port` for the fast
  port-fix loop) so the user can refresh and immediately check the result frame-by-frame as
  you iterate — you both inspect the *same* frames. Do **not** run your own one-off
  `--session <other>` capture for trace work, and don't pixel-diff in `/tmp` as a substitute
  for updating the session the user is looking at. **To VERIFY a fix, recapture the session and
  inspect the studio's OWN aligned output** — the served session at the URL, or its
  `diff/frames/frame_<ordinal>.png` (already retail−port, studio-aligned) — **never a
  hand-rolled `/tmp` diff that pairs frames yourself.** Pitfall that burned us (2026-06-09,
  C3a): `port/frames/` is named by **absolute** frame (`base_abs + ordinal`, e.g. ord 107 =
  `frame_00537.png` at base_abs 430) while `retail/frames/` is named by **ordinal**
  (`frame_00107.png`); naively diffing `port/frame_00537` vs `retail/frame_00107` LOOKS
  aligned but the load-stretch means they're different sim moments → a huge false divergence.
  The studio's alignment maps the two; trust `diff/frames/` (or the served session), not your
  own pairing. (Zooming a single studio frame to eyeball it is fine; pairing two yourself is not.)
  **For anything inspectable IN the session,
  just remind the user of the session URL — `http://localhost:8778/?session=<name>` (default
  serve port 8778; confirm the live port via `ps`/serve logs) — rather than composing+pushing
  a feed montage of it: the studio already shows retail|port|diff + frame scrub, so a push only
  duplicates what they can already open. Reserve the llm-feed for visuals that are NOT in a
  studio session** (one-off crops, montages of non-session frames, external images). Caveats: the port-exe singleton mutex stalls *parallel* captures (one at a
  time); a window/caprange change forces a retail re-capture even under `--only port`
  (`626949c`); back up `edit.trace.jsonl` before re-windowing. Memory pointer:
  `recapture-shared-session`.
- **ALWAYS phase+RNG-pin AND keep a call-trace on every trace we work on (whenever the
  scene supports it) — pin up front so the diff shows REAL gaps (not bg-NPC / sparkle / anim
  phase noise), and keep the flow-trace so we can always probe retail ground truth** (user
  policy 2026-06-09). **How (so it's automatic):** in the session's working trace
  (`edit.trace.jsonl`), in the segment that OPENS the `{caprange}` (right after the FIRST
  free-roam `{wait LOADING_END}`/HOUSE_FREEROAM — NOT the last wait), insert
  `{"phasepin": F}` then `{"rngseed": [F, 19937]}` immediately before `{"caprange": [F,…]}`.
  Use `F` = the caprange start (pin a few frames earlier if you can spare a settle margin —
  the companion spring-lerp re-converges over ~48f; bg-NPCs/sparkle snap immediately). One
  `{phasepin}` (`segtrace_phasepin_cb`, `main.c`) zeros db054 + player/companion anim +
  cursor-bob b154 + rmb screen-shake + the **bg-window-NPC 180× warmup** (re-seeds to the
  canonical **19937** = `SCENE1_BG_NPC_PHASEPIN_SEED`) + the sim-frame (目玉 sparkle %8
  phase); the `{rngseed:[F,19937]}` sets the shared LCG to that same seed so post-warmup RNG
  (sparkle/dust) is 1:1. Replace any RECORDED `{rngseed:[F,…]}` at that anchor with the
  canonical 19937 (don't stack two at one frame). Then **`recapture` BOTH sides** (a pin
  moves both). `apply --auto-pin` only auto-inserts off a STORED verdict (CONST-OFFSET→
  phasepin, DESYNC→rngseed) and inserts after the LAST wait — so for a no-verdict or
  multi-segment trace, hand-edit as above. Template: `tests/scenarios/house-loaded-display-
  pinned/trace.jsonl`. Verify: `flow_diff --verdict --align-field db054` = ALIGNED, or the
  NPCs/sparkle visibly stop drifting in the diff. **Call-trace: always add
  `{"calltrace": [F, count]}` spanning the `{caprange}`** (then `recapture` auto-detects it /
  `capture --call-trace`) — we need the flow-trace to probe retail ground truth and to compute
  the `--verdict`. A long-window retail call-trace is heavy + slow to analyze, so for DEEP
  dives `drill` specific moments at dense cadence; but the base capture KEEPS the call-trace —
  don't drop it to save time (user policy 2026-06-09).
- **Commits:** **commit in logical units as you go, without waiting to be asked** (user
  policy 2026-06-05); co-author trailer is auto-injected (don't type it); the pre-commit
  hook regenerates the port ledger + runs host tests on C changes. **Push** only when asked.
  **No branches** — commit to master (until nightly users matter).
- **Session hygiene — suggest `/clear` at milestone breakpoints + orient fast.** When a
  self-contained arc lands (a foundational tool, a sweep phase, a chip + its verification)
  and the next arc is a fresh effort, proactively offer the user a `/clear` to reset context
  — long contexts get summarized and slow. Make first-session orientation as fast as
  possible: this file auto-loads, `docs/FRONT.md` is the 60-second current-front read, and
  the "Where to read next" map below routes by need. **Durable knowledge — process,
  conventions, harness facts — lives HERE (or `docs/`), not in `~/.claude` auto-memory**
  (which stays thin + pointer-only). If you catch yourself writing a process fact to
  auto-memory, put it in this file instead and leave only a pointer in memory.

## Run / build (host tools need `nix develop --command` prefix)
- **Build:** `nix develop --command make -C src` (mingw32; **don't** override `CC`) →
  `build/openrecet.exe` (+ `-debug.exe`, console). **Tests:** `make -C tests run` (~3000,
  ASan/UBSan; run before committing C).
- **Run the exe ONLY via `tools/run-openrecet.sh`** (or the supervisor) — bare-exe launches
  are forbidden (Job-Object reap + singleton mutex). Defaults to `--turbo --silent-audio`.
- **Disassembly/hex:** use `vendor/unpacked/` (not `vendor/original/`, still SteamStub-
  encrypted). Decompiled C (gitignored): `docs/decompiled/all.c` + `functions.csv`.
- **Retail introspection (Frida):** `--remote cutestation.soy:27042` (assume the host is up;
  don't gate progress on it).
- **THE unified harness — one command for driving port/retail tests.**
  `tools/scenario-test.py <scenario> --target {openrecet|retail|both}` is the single
  standard entry point. It owns the whole TAS stack — input replay / anchor segtraces,
  **save virtualization** (`{savefile}` → sandboxed, never touches the real save), frame
  **alignment**, resolution pinning, and `--turbo` (the **forced fixed 17ms/frame** virtual
  clock — bit-identical on both sides → 1:1 frame mapping, lag-immune). Add trace capture in
  the SAME command: `--call-trace` (→ `flow_diff.py`, the execution+dataflow drill-in),
  `--d3d-trace [--d3d-trace-verts]` (→ `render_diff.py [--explain]`). Don't hand-wire
  `run-openrecet.sh` + `frida_capture.py` for synced captures — that's the old path
  scenario-test supersedes (those remain the low-level primitives it calls). Scenarios live
  in `tests/scenarios/`; record new ones with the in-engine F2/F3 recorder (see
  `docs/trace-workflow.md`).

## Where to read next (by need)
- **Is FUN_x ported / coverage:** `docs/STATUS.md` + `docs/port-ledger.{md,json}` (derived).
- **Changelog:** `docs/PROGRESS.md`. **RE writeups:** `docs/findings/INDEX.md`.
- **Tracing port↔retail (TAS traces, anchors, d3d-trace, call-trace, save override):**
  `docs/trace-workflow.md`. **Flow-trace cheatsheet (THE state-comparison tool):**
  `docs/flow-trace-cheatsheet.md`.
- **Render/parity debugging tools:** `tools/flow_diff.py` (`--verdict` RNG/phase
  determinism + `--field-timeline` + `--rng-drill`; the modern phase_probe replacement),
  `tools/d3d_state_diff.py` + `tools/render_diff.py` (per-draw command-stream diff),
  `tools/d3d_state_at_draw.py` (**the reliable device-state-at-draw inspector** —
  replays a d3d trace carrying the FULL device state FORWARD across frames, since
  device state is persistent; prints the complete COLOROP/COLORARG/ALPHA/filter/
  blend pipeline at any draw. **Use this whenever a draw looks wrong but its
  per-frame state "looks identical" — per-frame state tracking misses INHERITED
  state** (it cracked the white-UI COLORARG leak: retail sets zero COLORARG, the
  port's 3D renderers leak it into the 2D UI)),
  `tools/pixel_diff.py` / `tools/compose_comparison.py` (visual). Playbooks:
  `docs/flow-trace-cheatsheet.md`, `docs/render-depth-debugging.md`.
- **Orchestration / when to spawn sub-agents:** `docs/AGENT-WORKFLOW.md`.
- **Active plan:** `docs/plans/`. **Strategic frame / tooling phases:** `docs/PLAN.md`,
  `docs/harness-roadmap.md`. **Methodology/tooling audit (settled strategy verdicts +
  ranked tooling roadmap T1–T12):** `docs/audits/2026-06-09-methodology-audit.md`.
