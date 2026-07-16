# Trace Studio v2 — archived build record

> **ARCHIVED 2026-07-16.** v2 retired as the parity front end on 2026-06-13.
> Current operational guide: `../../trace-workflow.md`. Current renderer build
> record: `../trace-studio-v3.md`.

**Historical status (2026-06-07):** Phase 0 ✅ (`de3909c`). **Phase 1 ✅ landed + verified**
(`7d22255` D1, `5d59499` kept-count guard, `3335fca` D2). **Phase 2 ✅ landed +
verified** (`b2e01c6` model, `ed76707` export_trace D1, `51fa340` decompose +
v2 capture, `0a0cf8c` lift record/server/edits). **Phase 3 ✅ landed + verified
(Core + CLI drill)** — `5c0de5c` D3 `{capstride}` engine, `a580e8f` D3 agent,
`b8660ca` D2 through export_trace + caps fix, `1ecff91` overview wiring, `a1e2ea1`
CLI drill, `324debe` retail-harness `{capstride}` forward. Per-segment video split
+ in-browser drill deferred to Phase 4 (the SPA consumes them). **Phase 4 COMPLETE
(S1–S10 ✅ 2026-06-07: the whole server backend + the core scrub viewer + the side panels
+ in-browser drill + the re-homed trace editor (`7b39d59`) + the default-entry flip /
monolith retirement / maintainability check (`3f06717`), user-confirmed).** Phase 5 pending. Multi-session rebuild; `/clear` at each phase boundary. This
is the canonical plan — the `~/.claude` plan file is a session-local mirror.

**Phase 3 results (all acceptance criteria met):**
- **D3 `{capstride:N}`** trace-global two-tier cadence on BOTH targets: thins a
  `{caprange}` to every Nth frame from its start (anchor-relative). Port:
  `g_capture_stride` + stride-aware `capture_in_range`. Retail: the agent strides
  its `g_capture_pending` fill identically. +2 host tests (3194/0).
- **Ordinal-pairing invariant HOLDS**: `house-loaded-display-pinned --target both
  --caprange 120,240 --capstride 8` → **port == retail == 30 kept** (anchor-rel
  120,128…352), guarded + logged. Each strided frame diffs only ~3820px@mean0.002
  (the known scene residual) ⇒ retail's strided frames land on the SAME instants.
- **Determinism**: two port overviews **bit-identical 0/30** per ordinal.
- **Load seam** collapsed to 0 frames (port 1920t / retail 2346t).
- **D2 through export_trace**: `--capture-local` (local NTFS staging + parallel
  copyback, via run-openrecet `--no-frame-convert`) — **content-neutral 0/30** vs
  the 9p path; default-on in the studio, EngineCaps-gated.
- **CLI drill**: `trace_studio drill ov-both --at 10 --span 24 --call-trace` →
  anchor-rel caprange **[200,24]** (= 120+10·8) dense both targets, **port ==
  retail == 24**, **verdict exit 0 (PHASE-CLEAN)** — dense path no-regression.

### Phase-3 finding — three segtrace-op parsers, not two
A new segtrace op must be taught to **THREE** parsers, not two: (1) the port engine
`src/input_segtrace.c`, (2) the Frida agent `tools/frida/openrecet-agent.js`
(`segtraceBuildSegments`), AND (3) the **retail Python harness**
`tools/frida_capture.py` (~L983 if/elif chain that lowers ops to the agent — its
`else` branch assumes an input entry and does `rec["buttons"]`, so an unrecognized
op `KeyError`s on `--target both`). Also: **D2 `--capture-local` is NOT an exe flag**
— it's pure Python staging (`--capture-to` a Windows-local dir + `copyback_convert`);
the capability is "a local stage root resolves" (host/env), now probed correctly in
`drive/caps.py`. And `{capstride}` is **trace-global** (mirrors retail's existing
`g_capture_stride`), applied anchor-relative so both sides keep the identical
kept-set — the overview MUST still obey the Foundation rule (anchor at LOADING_END
or later) so no load falls inside the strided window.

**Phase 1 results (all acceptance criteria met):**
- **D1** load-suppression, BOTH targets, opt-in (`capture_suppress_loads`), default
  off so the ~30 validated scenarios are untouched. Verified on `bench-load-overview`
  (`--target both`): the load span collapses to a zero-frame seam — retail's
  **~2796-frame** turbo load yields **0** captured frames (lowest kept == LOADING_END).
- **Kept-count parity** port == retail (119 == 119), guarded in scenario-test.
- **Determinism**: two suppress replays bit-identical (0/119) DESPITE load lengths
  2276 vs 2848 — see the finding below.
- **D2** local-disk capture + parallel copyback (port, `--capture-local`):
  47.5 s → **22.4 s** (2.1×), PNG out (3× smaller), content-neutral (0/119 px).
- **Speed**: minutes → ~20 s both sides. Retail's ~2796-frame load (the 6-min waste)
  → suppressed; the port's 9p write cost → halved by D2.

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

### Phase 2 — Maintainable package foundation ✅ DONE
Created `tools/trace_studio/` (the 805-line monolith + 2 flat helpers → a package;
`tools/trace_studio.py` is now a thin launcher that coexists with the package —
a dir shadows a same-named `.py` for imports, so the documented command + the
server's capture spawn work verbatim). Landed:
- **`model/`** — `ops` (trace/anchor parsing), `segments` (Python port of
  `align.mjs`, **golden cross-checked** via a shared fixture + JS dumper:
  `test_trace_studio_segments.py`), `timeline` (build load_seams from the
  anchor spans + a gameplay entry), `session` (**v2 = v1 superset +
  `schema_version:2` + `timeline`**; v1 sessions migrate in memory → one
  gameplay segment; `test_trace_studio_session.py`).
- **`drive/`** — `EngineCaps` (probes the built exe for the D1/D2/D3 flag tokens,
  side-effect-free; degrades on a pre-D1 exe), `port`/`retail`/`runner`. **Wired
  the Phase-1 core**: D1 `--capture-suppress-loads` forwarded through `export_trace`
  (new passthrough) + retail `suppress_loads=`; default on for the studio. (D2
  exe-side `--capture-local` through `export_trace` is deferred to Phase 3 — caps
  reports it unsupported and the studio degrades with a one-line log.)
- **`transport/`** convert·encode, **`analysis/`** pixeldiff·verdict·state,
  `trace_build`, `capture` (the decomposed `cmd_capture`), `cli`.
- Lifted **`record/`** (`RecordController`/`CaptureController`/`recover_raw`),
  **`server/`** (`ranged` HTTP-Range + `app`; the do_POST ladder kept as-is —
  dispatch-table refactor moved to Phase 4 with the SPA), **`edits/apply`**.
  Deleted the flat `trace_studio_serve.py`/`trace_studio_apply.py`.

**Acceptance met**: a port-only capture of `house-loaded-display-pinned` writes a
v2 session (`schema_version 2`, **1 load_seam** port 261→2505 = 2244 turbo ticks
collapsed to zero frames, 1 gameplay/48 frames); the server lists it alongside the
old v1 sessions, serves the v2 manifest, HTTP-Range 206 works; all 7 tools tests
green (5 existing + 2 new). The old web UI is untouched (v2 is a v1 superset).
**`/clear` after.**

### Phase 3 — Two-tier capture + CLI drill ✅ DONE (Core + CLI drill)
D3 `{capstride}` (engine + agent + retail harness) + overview wiring + D2 through
export_trace + the CLI `drill` subcommand. See the "Phase 3 results" block at the
top. **User-confirmed 1:1** on the overview both-run diff ("basically 1:1 besides a
few faint dots" — the known deferred ambient-particle gap, not a pairing bug).
**Deferred to Phase 4 (the SPA consumes them):** per-segment video splitting at
seams + in-browser click-to-drill (the timeline entry already carries `cadence`, and
the CLI drill localizer `frames[0] + v*cadence` is the canonical math the SPA reuses).
**`/clear` after.**

### Phase 4 — New SPA  ← IN PROGRESS (S1–S9 landed 2026-06-07)
Build `web/components/*` on the v2 model (Filmstrip, segment-aware VideoStage, DiffRibbon,
StatePanel, unified MarkBar, JobTray); serve mark/analyzer registries as JSON; re-home the
trace editor; retire the `app.mjs`/`timeline.mjs` monolith. **Acceptance**: the full
in-browser loop (record → overview → drill → mark → apply → recapture-segment → re-view)
runs without the CLI, + the maintainability check (add a throwaway mark kind via a registry
entry only → it surfaces end-to-end with no JS edit).

**Decisions locked (user, 2026-06-07):** (1) **PRESERVE + robustify the trace editor** —
re-home `timeline.mjs` → `web/components/TraceEditor.mjs` (keep `align.mjs` as the shared
pure core) as a collapsible *advanced* panel under the Filmstrip; the **tweak-inputs /
`extend`-a-trace / recapture** loop is a primary workflow (iterate + extend a trace without
re-recording a whole gameplay segment). (2) **DEFER per-segment video** (single-segment
traces + per-area savefiles suffice now) but keep the model open: `VideoStage` reads
`segment.videos`, falls back to whole-session `manifest.videos`. Option-B recipe (encode
subranges) noted at the bottom of this doc.

**THE coordinate contract (get this right — `web/model.mjs`):** captured PNGs are named by
their anchor-relative **strided label** but `transport/encode.py` encodes them positionally
→ in the mp4 they are dense frames `0..n-1`. Three deliberately-separate spaces: global
ordinal `g` (the scrub position) → `(seg,k) = view.locate(g)` → label `L = frames[0] +
k*cadence`. **Video time `(k+0.5)/fps`** (ordinal, same formula every segment); **state** by
ordinal `state[seg.offsetGlobal+k]`; **diff** by label `diffByLabel.get(L)` (the
`manifest.diff.per_frame[].frame` is the strided label); **marks** persist the LOCAL ordinal
`k` and `apply.py` does its own `caprange.start + k` — the SPA NEVER pre-multiplies by cadence
(apply runs only on dense sessions; drill-before-pin sidesteps the latent `apply.py`
strided-overview off-by-stride, which we did NOT change). 1-segment ⇒ every formula collapses
to the old whole-session UI (headless-verified vs ov-both: totalFrames===n_frames).

**Build at a PARALLEL entry** (`studio.html` → `web/`) so the old UI keeps working until S10
flips the default + deletes the monolith. No build step (Preact+htm via the vendored `.mjs`);
the static-asset GET branch serves the new `web/` subtree with no server change.

| Stage | What | Status |
|---|---|---|
| S1 | `server/routes.py` dispatch table (wire-stable: the do_POST/do_GET ladder → a `ROUTES` table; state on the Server instance) | ✅ `c492281` |
| S2 | `server/jobs.py` `JobsRegistry` + `GET /api/jobs` (unify record + capture/recapture/drill; `kind` field on `CaptureController`) | ✅ `c08cdb9` |
| S3 | `edits/marks.py` + `analysis/registry.py` + `GET /api/registries`; `apply.py` imports `APPLY_KINDS`/`WORKLIST_KINDS` | ✅ `0f3d6b8` |
| S4 | `model/drill.py` `drill_window()` (CLI + route share it) + `POST /s/<sess>/drill` + `CaptureController.start(extra_args=)` | ✅ `d5bbdf0` |
| S5 | `studio.html` + `web/model.mjs` (segmented view + `classify` + `useStudioModel`) + composition-only `web/app.mjs` + `SessionPicker` | ✅ `550dfc2` |
| S6 | `web/components/` Filmstrip + VideoStage (+attachBox box-select) + DiffRibbon + ScrubBar + `web/util.mjs` (toast/copy/fmt) | ✅ `b817f40` — user-confirmed UX (“much better, like the diff scrub”) |
| S7 | StatePanel + unified MarkBar (from `/api/registries`) + JobTray (one poller on `/api/jobs`) + lift Record/Iterate/Verdict | ✅ `908c03b` (+ `e8cf44e` polish: fold anchor/feature→note, mark crop-preview thumb, readable link) — user-confirmed |
| S8 | in-browser drill UI (`DrillBar`) → `POST /drill` (S4 `drill_window`) → JobTray (kind=drill) → open child; **+ responsive panel layout** (1280px matchMedia: wide = videos\|State-sidebar w/ Verdict left-bottom + folded tools; narrow = videos→fold→Verdict\|State) | ✅ `d92fa81` — user-confirmed |
| S9 | re-home `timeline.mjs` → `web/components/TraceEditor.mjs` (collapsible advanced panel, wired to the global cursor) + **robustify** the extend/edit/recapture loop; re-run the segments golden | ✅ `7b39d59` — user-confirmed |
| S10 | flip the default entry to the SPA; delete legacy `app.mjs`/`timeline.mjs` (+ legacy `/record/status`+`/capture/status` if unreferenced); maintainability check | ✅ `3f06717` — verified headless (3 tools tests + 22 import-path 200s + 5×404 + redirect Content-Length) |

**S7 resume notes — lift from the OLD `tools/trace_studio_web/app.mjs`:** `StatePanel`
(reads `view.state[seg.offsetGlobal+k]` by ordinal; needs a `--call-trace` session for data —
ov-both has none → shows the empty message), `VerdictPanel` (`view.manifest.verdict.text`),
`RecordPanel`, `IteratePanel` (apply/recapture/clone/divergent — those POST routes already
exist), and `MarksPanel` → a registry-driven **`MarkBar`**: a `useRegistries()` hook in
`model.mjs` fetches `/api/registries` once, render one button per `marks[]` entry; persist via
the existing `POST /s/<sess>/edits/set` writing the LOCAL ordinal `k` as the mark `frame`.
**`JobTray`** polls `GET /api/jobs` through the existing `store.useStatus` hook (it keys its
interval on `.running`) and replaces the 3 separate pollers; surface a “a capture is already
running” rejection as a toast. Wire the lifted panels into `web/app.mjs`’s `.panels` grid.

**S8 landed (`d92fa81`) — DrillBar + the responsive layout.** `DrillBar` (under the
Filmstrip, gated on active-segment `cadence>1`) POSTs `/s/<sess>/drill {at:cur|offsetGlobal,
span}` — `at` is the 0-based VIEWER INDEX (= the global cursor on a 1-gameplay-segment
overview); the backend adds `caprange.start + at*stride`. Watches the unified `/api/jobs`
(jobStatus action-loop) → opens the child on rc 0. **Layout is JS-gated** (`useWide(1280)` /
`matchMedia`) by a `layout(p)` helper in `app.mjs` that returns an element array: the
collapsible **`.tools-fold`** ALWAYS holds **mark · record · iterate** (the occasional
actions); **State + Verdict are the always-on reference**. WIDE = `.workarea` flex row [left
`.scrub-col` (videos → tools fold → Verdict in `.below-panels`, a flex stack so Verdict fills
the bottom) | right `.ref-col` 340px, `align-items:stretch` so it's full-height, State `.panel`
sticky inside]. NARROW = videos → tools fold → `.two-col` (Verdict | State, grid-stretch equal
height; the `.verdict` 260px cap is lifted in `.two-col` so its body fills). **S9's TraceEditor
slots into `layout()`** — decide wide (left-column, under the tools fold) vs its own collapsible.

**S9 landed (`7b39d59`, user-confirmed) — TraceEditor re-homed + wired to the global cursor.**
Resolved the S8 open question by NOT putting the editor inside `layout()`: it's its OWN
full-width lazy-mounted collapsible **`<details class="trace-editor-fold">` directly under the
Filmstrip/DrillBar** (a horizontally-scrolling timeline wants full width; tucking it in the
left scrub-column would cramp it). `web/components/TraceEditor.mjs` is the legacy
`/timeline.mjs` re-homed verbatim in capability; `/align.mjs` stays the shared pure core
(both UIs import it). **The cursor bridge is the crux:** the editor x-axis is frames-from-sync
but the SPA scrub position is the global ordinal `cur`, so the editor cursor is now a DERIVED
projection — `{seg,k}=view.locate(cur)`, `absPort = manifest.port.base_abs + k*seg.cadence`,
`editorRel = absPort − port.syncFrame` — and `setCursorRel(rel)` maps back
(`k = round((syncFrame+rel − base_abs)/cadence)`, clamped, `cur = offsetGlobal+k`,
snap-to-captured-frame). Bidirectional, no second cursor state (the old monolith kept a
separate `tlCursor` one-way-coupled via `onSeekWindow`). For 1 dense gameplay segment it
collapses to the legacy `base_abs+k−syncFrame`. **Robustification** (extend/edit/recapture is
a primary workflow): `web/actions.mjs` holds the ONE shared `recapture(sess,{only,pollJobs,
onDone})` flow (IteratePanel + the editor's in-toolbar accent `⟳ re-capture` both call it);
`app.mjs` owns the trace-edit working state (`editTrace`/`notesState`/`localStale` + debounced
`POST /trace` + `POST /notes` + a **flush-before-recapture** so the last keystroke isn't lost);
`extend()` toasts instead of `alert()`; the editor **scrolls to the cursor on mount** (the
captured window sits ~`base_abs` frames right of boot-sync, so a fresh open would otherwise
land on empty pre-load space). Gutter label `emitted in`→`inputs` (no 2-line wrap).
**S10 landed (`3f06717`).** `index.html` now loads the SPA (`/web/app.mjs`) so `/` serves it
directly; the legacy `app.mjs`/`timeline.mjs` monolith + the redundant `studio.html` are
**deleted**, and the now-dead `/record/status`+`/capture/status` GET routes pruned. **Audited
KEPT (still in use):** `store.mjs`'s `useStatus` (the SPA polls `/api/jobs` through it via
`useJobs`), `align.mjs` (the SPA's `TraceEditor` imports it), and `recorder/capturer.status()`
(`JobsRegistry` builds `/api/jobs` from them — only the HTTP routes were dead). Also hardened
the `/`→`/?session=` redirect with **`Content-Length:0`** — without it an HTTP/1.1 keep-alive
client (curl, probes) blocks on the empty body (browsers tolerate it; that's why it lay
latent). **Maintainability check PASSED:** a throwaway mark kind added to `edits/marks.py`
AND a throwaway analyzer to `analysis/registry.py`, both data-only, surfaced at
`/api/registries` (→ a MarkBar button, since it `.map`s the registry) with **zero JS/route
edits**, then reverted. Verified headless (no jsdom oracle): the 3 tools tests pass; `/`
302→SPA; **all 22 SPA import paths 200**; `studio.html`/`app.mjs`/`timeline.mjs`/`record-status`/
`capture-status` all 404; `api/sessions|jobs|registries` 200.

**Dev/test harness (so the next session doesn’t rediscover it):**
- **Serve:** `nix develop --command python3 tools/trace_studio.py serve --session ov-both
  --port 8779` — background via the Bash tool’s `run_in_background`, NOT an inline `&`
  (the inline `&` + wrapper-exit reaps the server). The SPA is now the DEFAULT entry:
  open `/?session=<name>` (bare `/` 302-redirects to the `--session` default). The old
  `/studio.html` is gone (S10). The strongest white-screen guard remains: curl every
  `/web/...` import path → 200.
- **Test sessions:** `runs/trace-studio/ov-both` (both-target overview, 30 strided frames @stride
  8, 1 load seam, no call-trace → verdict gray + no state) and `ov-both-drill-200-24` (dense
  stride-1 child → the 1-seg/dense reduction).
- **ESM check:** `nix develop --command node --check <file.mjs>` via EXIT CODE (catches syntax,
  NOT htm runtime errors). **No headless browser/jsdom in the devshell ⇒ the USER is the visual
  oracle**; the strongest headless guard against a white-screen is to curl every `/web/...`
  import path → 200 (a mistyped import is the usual culprit).
- **Tools tests:** `nix develop --command python3 tools/test_trace_studio_segments.py` +
  `test_trace_studio_session.py` + `test_export_trace.py` (run after backend changes).
- **Bash-tool env gotchas:** foreground `sleep` is BLOCKED (exit 144) — use `curl
  --retry-connrefused --retry N --retry-delay 1` for readiness; `pkill -f "trace_studio.py
  serve"` SELF-MATCHES the wrapper shell’s argv (kill by port via `ss -ltnp` → PID, don’t
  pkill the pattern); `python3`/`node` need the `nix develop --command` prefix.

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
