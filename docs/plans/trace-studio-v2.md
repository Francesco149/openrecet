# Trace Studio v2 — a maintainable, fast TAS trace workhorse

**Status (2026-06-07):** Phase 0 ✅ landed (`de3909c`). Phase 1 D1 (load-
suppression) ✅ landed + verified port-side; retail agent mirrored (both-run
verify pending); D2 + harness kept-count guard next. Phases 2–5 pending.
Multi-session rebuild; `/clear` at each phase boundary. This is the canonical
plan — the `~/.claude` plan file is a session-local mirror.

### Phase-1 finding — the port load-pump is non-deterministic (and that's OK)
The port's nowloading load-pump count is **non-deterministic**: it's a wall-clock
worker-thread race, NOT on the turbo virtual clock. The SAME `fa7c82` Continue/Load
pumped **2 → 2587** frames depending only on main-loop speed (capture I/O paces the
loop slow ⇒ short load; suppressed/fast ⇒ long load — can even outrun the worker, so
give a from-boot overview generous `max_frames`). **This does NOT leak into
gameplay:** two `--capture-suppress-loads` replays with load lengths 2276 vs 2848
produced **bit-identical** gameplay (`0/119` differ, ordinal-paired at the
LOADING_END window) — the load-pump work (NPC RNG warmup etc.) is convergent. So
**D1 + LOADING_END-relative anchoring = deterministic captures for free**; the load
variance is purely cosmetic (absolute frame numbers). **Foundation rule (enforce):
anchor capture windows at LOADING_END or later, NEVER at absolute / NEW_GAME-relative
frames across a load.** Port↔port determinism needs no engine change; port↔retail
warmup convergence stays the `{rngseed:19937}`/`{phasepin}` story (→ auto-apply at
anchors, planned). On the PORT, D1 buys determinism, not speed (its load is short
when capture-paced); the 6 min→s SPEED win is retail-side (its load is a long,
deterministic, pure-waste capture span).

## Why

`tools/trace_studio.py` is meant to be the workhorse for validating the port vs
retail: record a trace from the main menu → auto-sync at load screens → capture +
scrub both sides fast → flag divergences / add anchors until 1:1, so we can set up
specific scenarios and visually confirm parity without llm-feed round-trips. It
never got good enough to rely on: a 30 s trace took ~6 min to process, and a
New-Game recording froze retail. Rebuilding it carefully (not throwaway) so it
becomes the thing we actually use to push the port forward.

### Root causes (traced from the failing `towntest3-20260607-001358` session)
1. **~5 fps capture**: every frame is a **3 MB uncompressed BMP** written by the
   Windows exe over the `\\wsl.localhost` **9p mount** (both sides), × the **whole
   trace span**, × every **turbo-stretched load frame** (a ~6-frame live load →
   ~2700 turbo frames; we captured them all). Then serial BMP→PNG + a 54 MB
   all-intra encode.
2. **Retail froze in the New-Game prologue**: `recet_op.wmv` is wall-clock
   DirectShow media that ignores the turbo clock, so absolute-frame `{esc}` skips
   miss it. (New-Game only — Continue/Load never plays it.)
3. **Cross-target prologue desync** (documented): a New-Game recording can't replay
   1:1 on BOTH sides (the port auto-completes the prologue with a different anchor
   sequence). **Continue/Load traces reproduce LOADING_START/END/HOUSE_FREEROAM
   identically on both sides → they are the 1:1 happy path** and the first target.

### Sim-freeze question, answered
"Freeze the sim during loads and pretend it was a couple frames" — we do the
**capture** half (suppress capture during loads → collapse to zero-width seams),
NOT the **sim** half. The ~2700 load ticks run real deterministic work
(`sim_loading_pump`: nowloading alpha, font aging, NPC RNG warmup); two replays
(and port↔retail) stay bit-identical only because both pump the same tick count
under the same 17 ms clock. Sim-skip would make tick counts wall-clock-dependent →
non-deterministic → it breaks the exact property the tool verifies. Capture-suppress
gets the whole speed win with zero parity risk.

### Decisions locked (from the user)
- **Careful rewrite** into a maintainable package (borrow the good UI ideas, fix the
  jank). Registries + one shared model + versioned schema → restructurable later.
- **Continue/Load first.** New-Game support (video force-skip + prologue cross-replay
  RE) is the last phase.

---

## Architecture

### Engine/agent/harness (the speed core)

- **D1 — load-aware capture suppression.** `anchor_world.loading_active` is already
  computed at `src/main.c:2628-2634`; promote it to file scope and mask the capture
  decision at `src/main.c:3078`:
  `if (should_capture && !suppress) { capture_backbuffer(); g_capture_count++; }`
  (suppress = `g_frame_loading_active || g_capture_suppress_cutscene`). Bump the
  counter only on a real write. Parity-safe (engine still runs the load; capture is a
  pure READONLY readback). Mirror the gate in the retail agent
  (`tools/frida/openrecet-agent.js` capture gate ~1287, read `loading_active` once per
  frame like `anchorTick` already does ~2672). **No new engine record** — the
  suppressed span `[LOADING_START, LOADING_END)` is reconstructable from the anchor
  stream both sides already emit. **Anchor emission stays OUTSIDE the gate.**
- **D2 — local-disk capture + copyback.** Write frames to a fast Windows-local dir
  (`%LOCALAPPDATA%\openrecet\cap\<run>\`, cloning `src/se_pack.c:164-181`'s
  `LOCALAPPDATA` pattern) instead of the 9p UNC; one bulk copy/convert back at the
  end. New `--capture-local <dir>` on the exe (copyback in `run-openrecet.sh`'s
  post-run slot ~237); point `frida_capture.py:1111` `capture_dir` at the local dir
  (agent unchanged; convert locally, copy back the ~15× smaller PNGs). **Keep BMP/raw
  locally, convert to PNG only on copyback — do NOT add an in-exe encoder** (off 9p a
  local write is sub-ms; encoding would only add latency). Local disk suffices for v1.
- **D3 — two-tier cadence.** New `{capstride:N}` segtrace op (port) for a coarse
  OVERVIEW (every Nth gameplay frame); retail already has `g_capture_stride` (agent
  ~445). A dense `{caprange}` window stays a superset (drill = every frame).
- **Ordinal-pairing invariant (highest risk).** The comparator pairs Nth-left vs
  Nth-right, not by absolute frame — so we never reconcile port (~475) vs retail
  (~14285). But both sides MUST keep the **same kept-count in the same order**.
  Load-suppression gives this for free (same deterministic `loading_active` edges);
  the harness **asserts equal kept-count per inter-anchor segment** as a guard.
- **D4 — New-Game retail video force-skip (last, isolated, flag-gated).** Detect the
  DirectShow graph (engine video poll `FUN_0040cea6`, `src/sim.c:216`) in the agent →
  `IMediaControl::Stop`; fallbacks: short-circuit the play branch at
  `src/scene_title.c:489`, or a widened ESC-pump stopgap. Off the Continue/Load path.
- **D5 — investigated, rejected.** Turbo sim-skip of load spans breaks bit-identical
  determinism (see "sim-freeze question" above). Capture-suppress only.

### Python package (`tools/trace_studio/`) — replace the 3 flat modules + monolith
```
cli.py            thin argparse → orchestrator
model/  ops.py segments.py timeline.py session.py     # PURE, unit-testable
drive/  port.py retail.py runner.py caps.py           # wraps export_trace / frida_capture
transport/ sink.py convert.py encode.py               # local-disk fast path + copyback
analysis/ registry.py pixeldiff.py verdict.py state.py
edits/  marks.py apply.py                             # mark/analyzer/anchor REGISTRIES
record/ controller.py recover.py                      # lift verbatim from _serve.py
server/ app.py routes.py ranged.py jobs.py            # dispatch table, not a regex ladder
web/    components/*.mjs  app.mjs(composition only)   # Preact+htm, no build step
```
Maintainability backbone:
- **One alignment core**: port `tools/trace_studio_web/align.mjs` → `model/segments.py`
  as the single source of the viewer-index ↔ segment-frame ↔ absolute-frame contract
  (today duplicated 4×: align.mjs / apply.py / export renumber / timeline). Keep
  `align.mjs` as the JS twin guarded by a golden cross-check test on a shared fixture.
- **Segment-centric, versioned schema** (`session.json` `schema_version:2`): the
  timeline is an ordered list of `gameplay` segments + zero-frame `load_seam` entries;
  capture cadence / per-segment videos / per-segment verdict attach to each entry, not
  one global window. v1 sessions migrate (synthesize one gameplay entry). Analyzers +
  marks are open maps.
- **`EngineCaps` probe** (`drive/caps.py`): detect whether the running exe supports
  `--capture-local` / load-skip / `{capstride}`; degrade gracefully so the Python
  rewrite ships and works before/independently of the engine changes.
- **Registries** (mark-types, analyzers, routes) → adding an anchor, a mark kind, or
  an analysis view touches one data entry, not unrelated code.

### Web UX
- **Filmstrip segmented timeline**: gameplay blocks (width ∝ frames, fill ∝ verdict:
  green=aligned/black, amber=const-offset, red=drift, gray=unreached) separated by
  **load-seam chevrons** you scrub across instantly (hover shows `port 6t / retail
  2711t` stretch). The filmstrip IS the scrubber.
- **Per-segment lazy video** (small all-intra mp4s, swap `<video src>` at seams — never
  one 54 MB whole-trace mp4) + a **per-frame diff ribbon** (click-to-seek to first
  non-black frame) + the existing port-vs-retail state field-diff overlay.
- **Unified mark-at-cursor** (`phasepin/rngpin/anchor/feature/note`, one persistence
  path) + keep the good box-select→crop-ref. **One job tray** (replaces 3 pollers).
- **Drill**: click a segment → recapture just it at cadence 1 → re-encode only its
  subtree → reload only that entry.

---

## Phased plan (`/clear` at each boundary)

Value front-loaded: the speed win lands in Phase 1, before the full UI rewrite.

### Phase 0 — Stabilize + verify old flow ✅ DONE (`de3909c`)
Fixed `export_trace.py` raw path (load_raw 7→9 unpack + raw boot-save embed) +
`tools/test_export_trace.py`. Verified: host tests **3192/0**, distill CLI + the
Continue/Load scenario traces unaffected. The distill/pin/scenario flow is intact;
only the raw→export shortcut was broken (Trace Studio dodged it by feeding distilled
traces).

### Phase 1 — Engine speed core (the headline fix: 6 min → seconds) ← NEXT
- D1 load-suppression (port `main.c` + retail agent) + D2 local-disk capture +
  copyback + the harness seam/kept-count machinery, behind flags. Drive through the
  existing export_trace / frida_capture for now (clean minimal glue the package
  absorbs in Phase 2 — not throwaway). `make -C src` + `make -C tests run`.
- **Benchmark vehicle**: a Continue/Load trace that captures a span INCLUDING the load
  + a gameplay tail (so suppression + local-disk both show). Record a house/town walk
  off a save, or extend `house-loaded-display-pinned` (the validated small Continue/Load
  trace: `savefile` fa7c82 → `wait LOADING_END` → `caprange [120,48]`) to a from-boot
  span. Measure the BEFORE time first.
- **Acceptance**: that capture drops from ~minutes to **< ~45 s**, both sides, loads
  collapsed to zero-frame seams, per-segment kept-counts equal port↔retail, verdict
  still PHASE-CLEAN (`flow_diff --verdict`). Two replays bit-identical per ordinal idx.
- **Commit per logical unit. `/clear` after.**

### Phase 2 — Maintainable package foundation
Create `tools/trace_studio/`; port `align.mjs` → `model/segments.py` (golden-checked);
versioned segment-centric `session.json` v2 + v1 migration; `EngineCaps`; decompose
the `cmd_capture` monolith into `drive/transport/analysis/model`; wire the Phase-1
core; lift `RecordController`/`_recover_raw`/HTTP-Range verbatim. **Acceptance**: a
capture produces a v2 segmented session (loads as seams); old v1 sessions still open.
**`/clear` after.**

### Phase 3 — Two-tier capture + per-segment media
D3 `{capstride}` (port) + overview/drill wiring; per-segment encode + lazy reload;
drill recaptures one segment subtree. **`/clear` after.**

### Phase 4 — New SPA
Build `web/components/*` on the v2 model (Filmstrip, per-segment VideoStage, DiffRibbon,
StatePanel, unified MarkBar, JobTray); serve mark/analyzer registries as JSON; retire
`app.mjs`/`timeline.mjs`. **Acceptance**: the full in-browser loop (record → overview →
drill → mark → apply → recapture-segment → re-view) runs without the CLI. **`/clear`.**

### Phase 5 — New-Game support (hardest, last)
D4 retail intro-video force-skip (flag-gated) so New-Game recordings replay on retail
without freezing; then the deferred port prologue cross-replay RE (the FRONT.md
conversation-pose / mid-load actor-spawn gap) so main-menu→prologue→town cross-replays
1:1. Until done, New-Game segments render as `unreached` / "retail-faithful,
port-divergent" — flagged, not silently wrong.

---

## Reuse vs rewrite
- **Reuse as-is** (wrap): `export_trace.py`, `frida_capture.run_capture`, `flow_diff.py`
  (`--verdict`/`load_trace`), `pixel_diff.amplified_diff`, `frame_io`, `trace_save`,
  `distill_trace.py`.
- **Lift verbatim**: `RecordController`, `_recover_raw`, HTTP Range serving,
  `ffmpeg_encode`, and the good JS (VideoStage, box-select crop, StatePanel, per-button
  dense bars / pin-drag).
- **Rewrite**: the `cmd_capture` god-function, the `do_POST` regex ladder, the
  `app.mjs`/`timeline.mjs` monoliths, the flat untyped manifest.

## Verification
- **Old flow** (Phase 0 ✓): host tests; `export_trace.py <raw>` no longer crashes;
  Continue/Load scenario stays 1:1.
- **Speed** (Phase 1): time a Continue/Load capture before/after; seams carry zero
  captured frames; per-segment kept-counts match port↔retail.
- **Parity** (every engine phase): two replays bit-identical (`cmp`/`md5` per ordinal
  index); a known-1:1 Continue/Load trace stays PHASE-CLEAN.
- **Studio loop** (Phases 2–4): record → capture → serve → scrub filmstrip → mark pin →
  apply → drill-recapture a segment → re-view, all in-browser.
- **Maintainability** (Phase 4): add a throwaway mark type + analyzer via registry only,
  confirm it surfaces end-to-end with no other edits.

## Critical files
`src/main.c` (capture gate 3070-3082, `loading_active` 2628-2634, caprange cb ~3601),
`tools/frida/openrecet-agent.js` (capture gate ~1287, loading read ~2672),
`tools/run-openrecet.sh` (copyback ~237), `tools/frida_capture.py` (`capture_dir` 1111),
`tools/export_trace.py` (Phase 0 fix, done), `tools/trace_studio_web/align.mjs`
(→ `model/segments.py`), `src/se_pack.c:164-181` (LOCALAPPDATA), `src/anchor_trace.c`
(anchors), `tools/trace_studio.py` (the monolith to decompose, esp. `cmd_capture`).
