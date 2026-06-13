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
- **THE PORTING LOOP (default workflow — do this yourself, DON'T guess, DON'T ask for
  what a trace can show). User directive 2026-06-13:**
  1. **SYNTHESIZE a trace** that reproduces the exact behaviour (record/edit a scenario;
     `tests/scenarios/`, F2/F3 recorder, or hand-edit the input TAS — e.g. press ESC
     *later* so the 3D scene is actually rendered when the menu opens). The behaviour you
     want to port must be ON SCREEN in the capture.
  2. **ANALYZE** with the v3 tools to pinpoint the EXACT retail behaviour: the **d3d
     program** (`orv3_window … --launch` viewer, `orv3_draws` per-draw tex/state/RT,
     `orv3_shot` headless frame/draw render), the **call graph + game-state flow**
     (`--state`, `flow_diff --verdict`, `call_trace.jsonl`), cross-checked against the
     decompile/objdump. Ground every claim in a probe, not a guess.
  3. **PORT it 1:1 — no compromises, no PORT-DEBT** on the behaviour you're actively
     porting; iterate until bit/structurally identical (draw-program + state + pixels).
  4. **Only escalate to the human** for COMPLEX GAMEPLAY behaviours that can't be easily
     synthesized in a trace — never for "what does retail render/do here", which a probe
     answers.
  **If the tools CAN'T show you something, IMPROVE the tools until they can** (then it's
  one command next time) — e.g. the v3 proxy/replayer not capturing
  `SetRenderTarget`/`CopyRects` ⇒ RT-based effects (pause backdrop, radial-blur
  transitions, post-processing) replay empty ⇒ extend the capture, don't guess the effect.
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
  if down at session start. **But if it's inspectable in the open Trace Studio v3 viewer,
  remind the user it's there (or to re-`--launch` it) instead of pushing a feed montage that
  duplicates what they can already scrub (see the Trace Studio bullet).**
- **The parity loop is Trace Studio v3 — iterate on the SAME window, never one-off captures
  or `/tmp` pixel-diffs (standing workflow, like the llm-feed). v2 is RETIRED 2026-06-13**
  (archived under `tools/trace_studio/`, not deleted — see `plans/trace-studio-v3.md`; only
  fall back on a real blocker). Tooling lives in **`tools/trace_studio_v3/`**; deep how-to is
  that plan. **One command:** `nix develop --command python3 tools/trace_studio_v3/orv3_window.py
  <scenario> --window OFFSET:COUNT --launch` — drives ONLY what's missing/stale (retail is
  captured ONCE, content-addressed-cached, then SLICED for any sub-window — zero re-drive; the
  port is re-driven only when `build/openrecet.exe` is newer), JOINs port↔retail by **stored
  identity** `(anchor, offset)` (no hand frame-matching, load-stretch-immune — the v2 sync
  whack-a-mole is gone), and opens the **native viewer** (`viewer/viewer.exe` — port|retail|diff
  replayed live from the captured d3d command stream + scrub + diff ribbon + per-frame d3d state).
  After a port fix, re-run the SAME `orv3_window … --launch` (slices cached retail, re-drives only
  the port); the user refreshes/re-opens the viewer — you both inspect the same identity-aligned
  frames. **Verify via the viewer's OWN replayed/identity-synced panels + `pairs.json`, never a
  `/tmp` diff you pair yourself.** **The user flags divergences as NOTES in the viewer** (note
  mode `m` → drag a box / "note frame" → type; stored Windows-local, identity-labelled).
  **ALWAYS read them FIRST** via `orv3_notes.py <scenario> --render [--feed]` (replays the flagged
  frame port|retail|diff, crops to the box, → feed so you SEE it) — they're the authoritative
  per-scenario gap list, often sharper than the docs. For anything inspectable in the viewer just
  remind the user it's open (or to `--launch`); reserve the llm-feed for one-off non-viewer visuals.
  Caveat: the port-exe singleton mutex stalls *parallel* drives (one retail at a time).
- **CHASE render-program divergences — v3 sees what v2 (pixels-only) couldn't.** The viewer's
  **draw-program panel** flags when the PIXELS are 1:1 but the RENDER PROGRAM differs — draw
  ORDER, batching splits, an extra/doubled draw (per-texture triangle totals; verdict
  ALIGNED/BATCHING/DIVERGENT; click a pixel → the draw that painted it; `s` solo a draw). **A
  rendering-order divergence on bit-exact pixels is worth chasing: it makes the port more
  faithful AND is a lead that our LOGIC may not match retail's order — a latent REAL divergence
  later even when invisible now.** (Already found: a retail-only 0-px overlay quad, a port
  double-drawn bg.) **Game-state panel:** `orv3_window … --state` (opt-in, negligible cost — the
  4 once-per-frame VAs are window-gated) caches each side's `call_trace.jsonl` and the viewer
  shows engine fields (rng/rngcalls, player+companion px/py/anim, menu, dialogue) port-vs-retail,
  diff-highlighted (f32-normalised, so a red row is a REAL gap), filter + diffs-only. Same data
  drives `flow_diff --verdict` (next bullet). **Make every hard-to-see divergence a NEW v3
  feature** (the state panel, draw-program diff, notes, pixel-pick all came from this) — don't
  work around a blind spot ad-hoc; close it in the tool so the next divergence is one command away.
- **ALWAYS phase+RNG-pin every trace we work on** (user policy 2026-06-09) — pin up front so
  the diff shows REAL gaps. The working scenario traces carry the canonical `{phasepin}` +
  `{rngseed 19937}` (+`{tutloadpin}`); one `{phasepin}` zeros db054 + anim + b154 + rmb shake +
  the bg-NPC warmup (re-seeded 19937) + the sparkle %8 phase. A pin moves BOTH sides → re-drive
  both. Rules + template: `tools/trace_studio/edits/lint.py` docstring +
  `tests/scenarios/house-loaded-display-pinned/trace.jsonl`. **The RNG/phase verdict is
  UNCHANGED in v3 — same `flow_diff`, now on the v3 cache:** capture with `orv3_window … --state`
  (caches each side's `call_trace.jsonl`), then `tools/flow_diff.py --verdict --align-field db054
  --retail runs/studio-v3-cache/<scen>-<key>/retail/call_trace.jsonl --port …/port/call_trace.jsonl`
  → ALIGNED / CONST-OFFSET (= phase, accept) / DRIFT (= real logic divergence), with raw-rng
  bit-exactness + per-frame rngcalls match (the whole `flow_diff` suite — `--field-timeline`,
  `--rng-drill` — reads the v3 traces unchanged). The 4 once-per-frame state VAs are window-gated;
  a v3 drive does NOT auto-load the heavy full call-graph (lean by default). `docs/flow-trace-cheatsheet.md`.
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
- **There is NO bare `python3`/`pip` on this NixOS box.** Every Python/host-tool
  invocation needs the prefix: `nix develop --command python3 tools/<tool>.py …`
  (run from the repo; first call may take seconds to evaluate the devshell). Outside
  the repo (e.g. llm-feed) use `nix run nixpkgs#python3 -- <script>` instead.
- **Build:** `nix develop --command make -C src` (mingw32; **don't** override `CC`) →
  `build/openrecet.exe` (+ `-debug.exe`, console). **Tests:** `make -C tests run` (~3000,
  ASan/UBSan; run before committing C).
- **Run the exe ONLY via `tools/run-openrecet.sh`** (or the supervisor) — bare-exe launches
  are forbidden (Job-Object reap + singleton mutex). Defaults to `--turbo --silent-audio`.
- **Disassembly/hex:** use `vendor/unpacked/` (not `vendor/original/`, still SteamStub-
  encrypted). Decompiled C (gitignored): `docs/decompiled/all.c` + `functions.csv`.
- **Retail introspection (Frida):** `--remote cutestation.soy:27042`. **NEVER ask the user
  whether the Frida host / frida-server is up and NEVER gate progress on it — assume it's
  available and just run the capture.** If frida-server isn't reachable the harness
  (`frida_capture.py` `ensure_frida_server`) auto-spawns it via an **elevated Start-Process,
  and the UAC prompt is auto-approved on this host** — you can launch it yourself, no human
  step. A `process-terminated`/launch failure has a REAL cause (investigate per
  `feedback_frida_server_leak`), it is never "the host is down".
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
- **Render/parity debugging tools:** START at the **Trace Studio v3 viewer** —
  `tools/trace_studio_v3/orv3_window.py <scenario> --window OFFSET:COUNT --launch` (port|retail|diff
  replayed live + identity-synced + diff ribbon + the draw-program panel; add `--state` for the
  game-state panel; flag gaps as notes → `orv3_notes.py <scenario> --render`). Deep how-to:
  `docs/plans/trace-studio-v3.md`. `tools/flow_diff.py` (`--verdict --align-field db054` RNG/phase
  determinism — ALIGNED/CONST-OFFSET/DRIFT — on the v3-cached `call_trace.jsonl`, + `--field-timeline`
  + `--rng-drill`). `tools/d3d_state_at_draw.py` (**the reliable device-state-at-draw inspector** —
  replays a d3d trace carrying the FULL device state FORWARD across frames, since device state is
  persistent; prints the complete COLOROP/COLORARG/ALPHA/filter/blend pipeline at any draw. **Use
  this whenever a draw looks wrong but its per-frame state "looks identical" — per-frame state
  tracking misses INHERITED state** (it cracked the white-UI COLORARG leak: retail sets zero
  COLORARG, the port's 3D renderers leak it into the 2D UI)). `tools/pixel_diff.py` /
  `tools/compose_comparison.py` (visual one-offs). *Legacy v2 d3d-trace tools (`d3d_state_diff.py`/
  `render_diff.py`) + `trace_studio triage` are superseded by the v3 viewer's draw-program panel.*
  Playbooks: `docs/flow-trace-cheatsheet.md`, `docs/render-depth-debugging.md`.
- **Decompile/probe traps (read before porting a chip):**
  `docs/reference/decompile-gotchas.md` — the 17 burned-us-once gotchas (Ghidra FPU drops,
  enum value-vs-name, bit-pattern literals, probe timing, diff-before-theories).
- **Orchestration / when to spawn sub-agents:** `docs/AGENT-WORKFLOW.md`.
- **Active plan:** `docs/plans/`. **Strategic frame / tooling phases:** `docs/PLAN.md`,
  `docs/harness-roadmap.md`. **Methodology/tooling audit (settled strategy verdicts +
  ranked tooling roadmap T1–T12):** `docs/audits/2026-06-09-methodology-audit.md`.
