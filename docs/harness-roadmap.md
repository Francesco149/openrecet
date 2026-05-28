# Harness roadmap — graphics & audio smoke tooling

> Living document. Sub-plan of `PLAN.md`; cross-linked from its §6.

The Linux-side unit suite (`tests/`) covers portable decoders (494 tests
under ASan/UBSan as of 2026-05-21). What it can't reach: render-path
correctness, audio sequencing, full-pipeline behavior vs. retail, and
the integration-level "did the latest commit silently break something
the unit tests don't observe" question. `tools/smoke-test.py` +
`tools/contact-sheet.py` already capture frames and tile them, but the
loop has gaps that show up the moment a render or audio commit lands
and an assistant has to verify it without the user manually eyeballing
files. This doc lists the gaps and a ranked plan.

## Active plan (next two sessions)

Decided 2026-05-21 after the title-menu input regression (see
`PROGRESS.md` "Build-system header dep tracking") demonstrated that
mid-port regressions can hide between commits when no integration test
exercises the live-pipeline path. The plan is a two-session split,
intentionally separated to keep each session focused:

- **Phase A — our-exe integration loop.** ✅ **Landed 2026-05-21**
  (commits `2ef4e2b` input_trace module + tests, `4df9292` main.c
  flag wiring + replay loop, follow-up commit for the scenario runner
  + first two scenarios). No Frida, no retail instrumentation.
  Pipeline:
  - `src/input_trace.{c,h}` sparse-JSONL parser + writer + lookup.
    Schema: `{"frame":N,"buttons":"0xNNNN"}` — one line per change.
  - `--input-trace-record <file>` snapshots `g_input_state[0].buttons`
    each frame; sparse-emit only on transitions.
  - `--input-trace-replay <file>` skips DirectInput entirely, drives
    a 20 ms virtual clock so the tick scheduler never delays, pins
    `g_paused=FALSE`, and writes the recorded mask directly into
    `g_input_state[0].buttons` each frame.
  - `--rng-seed <n>` / `--max-frames <n>` / `--capture-frames i,j,k`
    pin determinism + cap budget + sample only at the listed sim
    frames (filename `frame_<sim_frame>.bmp`).
  - `tools/scenario-test.py` discovers `tests/scenarios/<name>/`,
    runs the exe with the right flags, **bit-exact** diffs captured
    BMPs against `golden/`. Mismatches emit a red-tint overlay PNG
    so the regression is multimodally inspectable. `--bless` mode
    regenerates goldens from a fresh run.
  - Starter scenarios: `boot-idle` (3 captures, 60 frames idle) and
    `title-z-press` (5 captures, Z at frame 30 → dispatch at
    frame 44). Both bit-exact deterministic across re-runs.
  - Goldens are gitignored (vendor-texture redistribution risk); the
    `scenario.yaml` + `trace.jsonl` ship, golden/ is `--bless`-local.
    See `tests/scenarios/README.md`.

  Pixel-diff strictness decision (was the open question): **bit-exact
  + red-tint overlay on mismatch**. Determinism holds across two
  back-to-back replay runs in the smoke validation (3/3 frames
  bit-exact for boot-idle, 5/5 for title-z-press). Cross-host /
  cross-GPU portability is open — re-bless after switching hosts.

- **Phase B — retail capture via Frida.** ✅ **Landed 2026-05-22**
  (capture half only; state-forcing deferred). `tools/frida_capture.py`
  + `tools/frida/openrecet-agent.js` + `tools/scenario-test.py --target
  retail`. Same JSON/PNG schemas as Phase A; per-target golden dir
  `tests/scenarios/<name>/golden-retail/`. Hooks:
  `IDirect3DDevice8::Present` (vtable[15], frame capture via sysmem
  bounce because retail's back-buffer is non-lockable),
  `FUN_00499200` BGM swap, `FUN_00499c63` SE play, `FUN_0047b73c`
  input poll reading `DAT_073dddd0`. Frame numbers come from the
  agent-side `g_manual_frame_counter`, bumped once per `Present`
  onEnter — `DAT_073dfcfc` was originally used here but turned out
  to be a title-scene-local BG-scroll tick that freezes the moment
  the title scene dispatches into a sub-scene; the manual counter is
  the scene-agnostic replacement (see engine-quirks §"Frame counter
  pauses on scene transition" for diagnosis). Capture filenames
  match the scenario `capture_frames:` list.
  `boot-idle/golden-retail/` blessed; 3/3 bit-exact on re-run.
  Auto-start helper for `frida-server.exe` via elevated
  Start-Process if the port isn't reachable.

  **Save-inject + scene-dispatch state-forcing still deferred** —
  separate scope from the Phase B+ pure-fn diff that landed below.

- **Phase B input injection.** ✅ Landed 2026-05-22. The agent now
  overwrites `DAT_073dddd0` (`var_input_mask`) on every `FUN_0047b73c`
  LEAVE with the sticky-trace mask for the current engine frame; the
  driver passes the Phase A `trace.jsonl` straight through to the
  agent's `init({input_trace, force_input})` RPC. Sparse semantics
  match `src/input_trace.c`: the most-recent `entries[i].frame <=
  current_frame` is the mask in effect, held until the next entry.
  `tools/scenario-test.py --target retail` enables injection by
  default so retail walks the same key sequence as openrecet. Audio
  + visual confirmation on `title-z-press` (SE slot 7 fires at
  frame 30, NEW GAME button brightens through select_phase) and
  `title-down-press` (cursor steps NEW GAME → MINIGAME, tooltip
  swaps). RNG / clock pinning still deferred — the engine drives
  itself at real time during a capture.

- **Phase B+ — state-forcing for differential tests.** ✅ MVP landed
  2026-05-22. Distinct from the save-inject / scene-jump effort above:
  this branch targets *pure or near-pure* functions we've already
  ported, calling them directly via `NativeFunction` and diffing the
  output against an in-process oracle that links the matching `src/*.c`.
  Pipeline:
  - `tools/frida/openrecet-agent.js` gains 5 camelCase RPCs:
    `readMemory` / `writeMemory` / `readU32` / `writeU32` /
    `callU32NoArgs`, plus a purpose-built `captureFadeCentibel(slider)`
    that plants a fake `IDirectMusicAudioPath` (vtable[5] =
    `NativeCallback`) and forces the BGM slider to record what
    `FUN_00499583` would have sent to `SetVolume`.
  - `init({install_hooks: false})` skips the Phase B capture hooks
    when we're only state-forcing — no main-thread resume needed.
  - `tools/state_diff/oracle.c` links `src/rng.c` + `src/audio_fade.c`,
    stdin protocol `rng_seq <seed_hex> <n>` + `fade_compute <slider>`.
  - `tools/state_diff/lcg_fade.py` spawns retail `CREATE_SUSPENDED`
    via Frida, **never resumes the main thread** (Frida's helper
    thread runs the agent independently — `NativeFunction` calls
    + memory ops work without the engine executing), forces seed /
    slider, captures, diffs.
  - Results: 6 seeds × 256 LCG steps (1536 u32 comparisons) + 10
    fade slider values, all **bit-exact** to retail. cf. PROGRESS
    2026-05-22 entry for the full log.

  Same agent surface generalises to LZSS / LZW / lnkdatas_hash CRC /
  input mask decoder / tick scheduler — one driver per subsystem,
  reusing the same oracle pattern. See PROGRESS for the ranked
  follow-up list.

  **Deferred to a follow-up session** (the original save-inject /
  scene-jump effort): same hook surface but a separate scope; landing
  the bigger version now would have bounced the
  capture-pipeline session between two unfinished pieces.

- **Phase B++ — cross-target visual probe suite.** ✅ Landed 2026-05-22.
  `tools/scenario-test.py --target both` runs the openrecet and retail
  pipelines back-to-back for one scenario, diffs each against its own
  per-target golden, and drops a per-frame ours|retail PNG at
  `runs/scenarios/<run>/sidebyside.png`. `tools/regen-comparisons.py`
  fans out across every scenario under `tests/scenarios/`, copies each
  latest sidebyside.png into `runs/comparisons/<scenario>/`, and emits
  a static HTML index at `runs/comparisons/index.html`. The index is
  dark-mode, has a TOC, shows each capture's absolute timestamp + JS-
  rendered "X ago" age (stale at >1h, very-stale at >24h), and
  cache-busts each `<img src>` with `?v=<mtime>` so a plain Ctrl-R in
  the browser always pulls the latest. Output dir is gitignored.
  Workflow:
  - regen after each shipped change: `tools/regen-comparisons.py`
  - view: `file:///opt/src/openrecet/runs/comparisons/index.html`
  - Ctrl-R to refresh; eyeball-scroll for obvious regressions; click
    the scenario heading anchor to share/cite a specific test
  Determinism was explicitly NOT pursued — retail runs organic, so
  some color delta and animation-phase drift between the two columns
  is expected (different capture path: our exe writes the back-buffer
  via Win32 BMP, retail bounces through CopyRects → sysmem surface).
  The signal is **structural** divergence (missing menu items, wrong
  positioning, scene-state drift), not per-pixel delta. State-forcing
  (input/RNG/clock pin on retail) was scoped and rejected as not worth
  the cost for visual eyeball-regression checks; revisit if a real bug
  needs it.

- **Phase C — PCM diff (deferred until needed).** Once both pipelines
  capture matching JSON event traces, the next class of bug (subtle
  audio glitches — wrong attenuation curve, mistimed SE, sample-rate
  artifacts) won't show up in event-log diffs. The cure is the Tier 3
  #6 PCM-capture hook: record what each engine actually pushed to the
  audio buffer, render as waveform + spectrogram PNG, diff visually.
  Implement when the first such bug actually bites — don't pre-build.

Open question (resolved at start of Phase A, 2026-05-21): pixel-diff
strictness → **bit-exact + red-tint overlay on mismatch**. The
overlay is auto-generated per mismatch frame at
`runs/scenarios/<run>/diff/frame_NNNNN.png`. SSIM was rejected
because threshold tuning hides single-pixel offset bugs (e.g. the
`render_quad_add` scaling miss from 2026-05-21 was a 1-2 px shift).

## Why this exists

When an assistant (Opus or a Sonnet subagent) ports a graphics or audio
subsystem, the verification step today is:

1. Build, run the exe, ask the user to look at the window.
2. Or run `smoke-test.py --capture`, read the SSIM number, hope it's
   close to the golden run.

Both fail in the same way: the assistant can't independently form a
picture of what the engine did. Goal of this roadmap is to make every
graphics/audio verification step **multimodally inspectable from inside
the conversation** — PNG contact sheets the assistant can `Read` as
images, JSON audio traces it can diff line-by-line, waveform/spectrogram
PNGs for SE work — so iteration doesn't bounce off the human in the loop.

## Ranked plan

### Tier 1 — small, immediate wins

1. **Auto contact-sheet on smoke runs.** ✅ landing now.
   `smoke-test.py --capture` already drops BMPs into `runs/<...>/frames/`.
   After the run, call `contact-sheet.py --src <frames>` to write
   `runs/<...>/contact.png`. Single tile grid by default. Side-by-side
   variant via `--diff-against <prev-run>` already exists for SSIM —
   reuse the same flag to also emit `diff-contact.png` (L|R rows).

2. **Per-pixel absolute-diff overlay alongside SSIM.** Extends
   `smoke-test.py::diff_runs`. For each (golden, new) pair, write
   `runs/<...>/diff/frame_NNNNN.png` with changed pixels tinted red over
   the new frame; tile via `contact-sheet.py`. SSIM gives a number, this
   tells you *where*. The `render_quad_add` scaling bug found 2026-05-21
   was a textbook case for this.

3. **Linux unit test for FUN_00499583's sin-curve volume fade.** Pure
   math; lives in `tests/test_audio_fade.c`. Render the curve to a PNG
   via a small `tools/plot/` helper so the test can spit a visual when
   it's tweaked. Lands alongside the fade port.

### Tier 2 — needed for SE backend port

4. **`--audio-trace <file>` JSON event log.** One JSON line per BGM
   swap / SE trigger / fade start: `{t_ms, kind, name, vol, fade_phase}`.
   Wire at the play call (`audio_play_track`, future
   `audio_play_se`), not inside DirectMusic. Cheap to add, gives a
   diffable transcript. Pairs with item 7 (retail-side instrumentation)
   to enable real A/B comparison.

5. **Curated render scenes (`--render-scene <name>`).** Extends the
   existing `--show-sprite <name>` pattern. Pre-composed test poses:
   e.g. `--render-scene title-menu-all` lays out all 8 menu items at
   known positions; `--render-scene layer-stack-bg2` exercises a
   specific layer composition. Lets a render-side change be regression-
   tested without driving through the title state machine. Worth doing
   when render-state-overlay work (the "washed out non-selected menu
   items" open item) lands.

### Tier 3 — heavier, defer until pain demands it

6. **PCM-capture hook on the DirectSound/DirectMusic buffer.** Records
   what the engine actually played to a WAV. Render the WAV as
   waveform + spectrogram PNG so the assistant can compare to retail
   captures visually (not aurally). Heavier than #4; do it only if SE
   timing diffs against retail get ambiguous from event-log alone.

7. **Retail-exe instrumentation via Frida (DLL inject).** Two roles:
   - **Capture**: emit the same `--audio-trace` JSON + matching frame
     captures from the unmodified `recettear.unpacked.exe`. Without
     this there's no ground truth for SE/BGM timing or for non-title
     frames.
   - **State forcing.** Hook the retail save-load + scene-dispatch
     functions to **inject specific game states**: "shop with customer
     Recette, day 5, 3 items on shelf at prices X/Y/Z", "dungeon level
     5-7 entry, party Recette+Charme, full HP". With this in place, an
     assistant can produce a deterministic golden frame for an
     arbitrary scene **without the user interactively playing**. Huge
     unblock for Phase 4 (subsystem fill-in): every scene the assistant
     reimplements gets a reproducible reference image without a
     play-through.

   Significant up-front cost (one Frida bootstrap + per-hook RE work),
   compounding downstream value. The hook addresses are mostly already
   identified — scene dispatcher `FUN_004547ab`, save-load, the table
   resolvers — so the RE work is "wire up known anchors", not new
   discovery.

8. **Headless render diff.** If a portable render path emerges (Wine,
   or a software D3D8 shim), run render tests on the Linux side without
   WSLInterop. Faster iteration; less coupling to host Windows. Not
   blocking anything today.

## Notes on multimodality

- The assistant can `Read` PNG/JPEG/BMP as images — contact sheets,
  diff overlays, waveform plots all land in conversation context that
  way.
- It can't listen to audio. Every audio verification has to terminate
  in a JSON event log, a waveform PNG, or a spectrogram PNG.
- Don't generate giant images. Default tile size 320×240, grid
  4-wide; full PNG output stays under ~2 MB which Read handles
  cleanly.

## Cross-references

- `PLAN.md` §6.2 (Frame capture under WSLInterop), §6.4 (Visual
  verification protocol), §6.5 (Tooling layers diagram).
- `tools/smoke-test.py` — existing capture + SSIM-diff harness.
- `tools/contact-sheet.py` — existing grid composer (single +
  side-by-side + zoom).
- `tools/analyze/pe.py` — handy when retail-hook addresses need
  resolving from VA → file offset.

---

## Phase D — Generic differential testing (multi-session plan, 2026-05-26)

### Motivation

Phase B+ proved the state-forcing pattern works (RNG + audio_fade bit-
exact across thousands of vectors via `tools/state_diff/`).  But it's
bespoke per-target — every new function gets its own ~200-line driver,
no shared scaffolding.  And the render layer has zero coverage: the
Cf.minimal landing (2026-05-26) ships visible HOUSE shop_table furniture
pixels, but with three diagnosed-but-unresolved bugs (translucent
rendering, mesh-on-its-side orientation, 2-3x scale).  Each bug class
is asm-archaeology territory today; the bug-class signature ("retail's
D3D state at the walker draw is some specific combination, ours is
different") demands a state-trace diff, not more decompile reading.

`../OpenLords2/docs/harness-roadmap.md` Phase 4 has the right shape —
generic orchestrator (`tools/diff_test.py`) + per-target `runRetail*`
RPCs on the agent + a single host shared library
(`tests/build/libengine_diff.so`) loaded via `ctypes` — and lands
9 pure-function targets at 1800/1800 vectors.  Phase D ports that
pattern + extends it with D3D state diffing.

### Scope decisions

- **Replaces** the bespoke `tools/state_diff/` pattern with a generic
  orchestrator.  Migrates RNG + audio_fade to the new harness as the
  first two targets (proves the migration, validates parity).
- **Adds** D3D state-trace diff for render-path verification.  The
  pure-function harness and the D3D-trace harness share the agent
  but live in separate orchestrator scripts.
- **Adds** memory-access watch for finding unported writers (PHC
  entries marked "no writer in decompile" — these have writers in
  retail; Frida's `MemoryAccessMonitor` finds them).

### What we keep / migrate / add

| Capability                                  | Today                                        | Phase D state                          |
|---------------------------------------------|----------------------------------------------|----------------------------------------|
| Bespoke pure-fn oracle (`tools/state_diff/`) | RNG + audio_fade, 2 targets                  | Migrate to `tools/diff_test.py`        |
| Frida agent capture hooks                   | `openrecet-agent.js` ~1900 LOC, in place     | Reused                                 |
| Cross-target visual probe                   | `regen-comparisons.py` + `scenario-test --target both` | Reused                                 |
| Generic pure-fn diff orchestrator           | —                                            | New `tools/diff_test.py`               |
| Host-side ctypes-loadable `libengine_diff.so` | `tools/state_diff/build/oracle` (one-off)  | New `tests/build/libengine_diff.so`    |
| Engine-tick freeze + race-retry             | —                                            | New (Frida side)                       |
| D3D state-trace emitter (Frida side)        | —                                            | New `installD3dTraceHooks()`           |
| D3D state-trace emitter (port side)         | —                                            | New `src/d3d_trace.c`                  |
| D3D trace diff orchestrator                 | —                                            | New `tools/render_diff.py`             |
| Memory-access watch                         | —                                            | New (Frida side, ad-hoc tool)          |
| Walker-behavior diff (controlled-state)     | —                                            | New (built on D3D trace + state inject) |

### Sub-phases (suggested session order)

#### Phase D.1 — Pure-function diff scaffolding

**Goal**: orchestrator + first target end-to-end working.

- New `tests/Makefile` target `diff`: compiles selected `src/*.c` into
  `tests/build/libengine_diff.so` with host gcc, no sanitizers (the
  diff path doesn't need ASan; the regular unit tests still cover that).
  Initial sources: `rng.c`, `audio_fade.c`.
- New `tools/diff_test.py` (~400 LOC, OL2 pattern):
  - Function registry: name → (port_symbol, retail_rpc, vector_gen).
  - Vector generator: 7 fixed edge cases + N random (default 200),
    deterministic from `--seed`.
  - RPC fan-out: spawn retail via Frida, init with `diff_test: true`,
    fire all vectors per target.
  - Result diff: byte-exact compare; first mismatch wins, dumps the
    full input vector + both outputs + a context window.
  - `--functions <list>` / `--vectors N` / `--seed N` / `--warmup-s N`
    flags.
- Agent: new init flag `diff_test: bool`.  When set, skip the capture
  hooks (Phase A/B path), install only the diff scaffolding.
- First target: **rng_next15** (`FUN_00471089`).
  - Globals: LCG state at `DAT_006023a0` (single u32).
  - RPC: `runRetailRngNext15(seed_u32) → {out: u15, post_state: u32}`.
  - Vector: random u32 seed, expected output = our port's
    `rng_next15` from `libengine_diff.so`.
- Lands `docs/findings/pure-function-diff.md` with the engine-thread
  race + retry pattern (we WILL hit this — retail's main thread is
  alive during diffs unless frozen).
- Migrate existing `tools/state_diff/lcg_fade.py` test plan into the
  new harness as a regression gate.

**Deliverables**: `tools/diff_test.py`, `tests/build/libengine_diff.so`,
1 RPC export, 200/200 vectors pass for rng_next15.

#### Phase D.2 — Two more pure-function targets

> **STATUS (audited 2026-05-29): NOT as described below.**  Only
> **rng_next15 has landed** in the generic harness — `tools/diff_test.py`
> defines exactly one `Target` (`rng_next15`) and `tests/Makefile`
> `DIFF_SRCS` = `diff_entry.c + rng.c` only.  **audio_fade migration is
> in progress** (a separate agent may be wiring it as you read this —
> re-verify the `TARGETS` dict before quoting a number).  **tick math is
> NOT yet built.**  The "600/600 vectors / 3 targets" figure below is a
> plan target, not a shipped result.

- **audio_fade_compute** (`FUN_00501a48` or similar — verify decomp).
  Reuses the existing oracle logic.
- **tick math** — `tick.c::tick_should_advance` or
  `tick_compute_step`.  Verify against retail's per-frame tick driver.
- Validates the orchestrator handles multi-target dispatch.
- 600/600 vectors total (3 targets × 200).

#### Phase D.3 — Engine-tick freeze + race-retry

**Goal**: support stateful diff targets (read/write shared globals).

- Survey Recettear's per-tick driver function (analogous to OL2's
  `FUN_004b99c0`).  Candidates: trace the call from WndProc /
  main loop → per-frame entry.  Document as
  `docs/findings/per-tick-driver.md`.
- Implement `Interceptor.replace(per_tick_driver, no_op)` install +
  uninstall.  Engine sits frozen on a known tick boundary; diff RPC
  fires safely; resume.
- Implement race-detect retry: snapshot pre-call; post-call, checksum
  the inputs to verify they weren't perturbed; retry up to N if so;
  fail with `raced` if budget exceeded.
- Smoke target: a small ported function with non-trivial reads — e.g.,
  `scene1_records_b_tick` per-slot dispatch (one slot, BSS-zero
  globals).  Proves the pattern handles state.

**Cross-cutting**: pre-emptively design for the OL2-discovered Frida
quirk — installing→uninstalling→re-installing the freeze on the same
target triggers a Frida-internal `TypeError`.  Install once at run
start, uninstall once at end.

**Estimated**: 1 session.

#### Phase D.4 — D3D state-trace emitter (Frida side)

**Status**: ✅ landed 2026-05-26.  See
`docs/findings/d3d-trace.md` for the schema + smoke results.
12 vtable slots hooked (SetTransform / SetMaterial / SetRenderState
/ SetTexture / SetTextureStageState / DrawPrimitive[UP] /
DrawIndexedPrimitive[UP] / SetVertexShader / SetStreamSource /
SetIndices).  Per-frame batching via Present.onEnter flush; one
`d3d_trace_batch` send per Present cycle.  `ret_va` annotation via
`this.returnAddress` (free, no `Thread.backtrace()` cost — module-
relative, add 0x00400000 for Ghidra VA).  `d3d_trace_frames` filter
gates buffering so a non-title scenario doesn't push megabytes per
frame.  Driver writes `<run_dir>/d3d_trace.jsonl`, one row per call.

GetRenderState deferred (low diff value — state-changing methods
already cover the divergence class).  `caller` field replaced
with `ret_va` (cheaper to compute, equivalent expressiveness; the
driver / D.6 orchestrator resolves to FUN_-name client-side).

#### Phase D.5 — D3D state-trace emitter (port side)

**Status**: ✅ landed 2026-05-26.  See
`docs/findings/d3d-trace.md` "Port side (D.5)" §.  Approach (b) —
vtable hot-patch — was tried first and produced a reliable
ACCESS_VIOLATION whenever `dev->lpVtbl` was reassigned to a verbatim
copy of the engine's vtable.  Falling back to approach (a), the
`-include d3d_trace_macros.h` compile-flag transparently rewrites
every `IDirect3DDevice8_Foo(dev, …)` call site to
`d3d_trace_Foo(dev, …)` across all ~30 TUs with zero source-tree
churn.  CLI: `--d3d-trace <path>` + optional
`--d3d-trace-frames i,j,k`.  Wrappers emit JSONL rows matching the
Frida agent's schema verbatim (`op` + `args` + `ret_va` (module-
relative caller offset via `__builtin_return_address(0)`) + `frame`);
floats serialized with `%.9g` to round-trip IEEE-754 single
precision.  Frame filter is per-frame in `d3d_trace_begin_frame`;
zero-emit cost when the frame isn't selected.

Smoke validated against boot-idle frames 0,1,2 — 270 events total
(SetRenderState / SetVertexShader / SetTextureStageState /
SetTexture / DrawPrimitiveUP, distribution matches title-BG-scroll
shape).  Canaries bit-exact (boot-idle 3/3 + title-down-press 4/4 +
title-options 2/4 + title-z-press 14/14, same as baseline).  Host
suite 2701/2701 untouched.

#### Phase D.6 — render_diff orchestrator + first diagnosis

**Goal**: compare two D3D trace JSONLs, surface first divergence.

- New `tools/render_diff.py`:
  - Reads both traces, walks them in lockstep frame-by-frame.
  - Per-call diff: matches `op` + `args`; if mismatch, reports both
    + a context window of 5 calls before + after.
  - Special handling for state-coalescing: if retail sets the same
    state to the same value twice (engine quirk), suppress in our
    output to match.
  - Filter mode `--scope walker-pass-init` to narrow to events
    inside a specific call range.
- First use: capture title-z-press frames 90, 100, 108, 115 in both
  targets, run `render_diff.py --scope walker-pass-init`.  Find:
  - ALPHABLENDENABLE state at walker-draw time.
  - Texture stage state for shop_table mat draws.
  - Whether retail sets a SRC/DEST blend pair we don't.
- Cf.minimal alpha bug becomes a known concrete fix from this
  diagnosis.

**Estimated**: 1-2 sessions (incl. fixing Cf.minimal alpha based on
findings).

#### Phase D.7 — Memory-access watch

> **Detailed executable plan: `docs/plans/d7-mem-watch.md`** (2026-05-29).
> Prioritised ahead of E.4 — it unblocks the active HOUSE shop_table render gap.

**Goal**: identify writers of "no decompile writer" memory regions
(several PHC entries) via Frida's `MemoryAccessMonitor`.

- New ad-hoc tool `tools/mem_watch.py` + agent-side
  `installMemoryWatch(regions[])` RPC.
- Sets up `MemoryAccessMonitor.enable([{base, size}, ...], {onAccess:
  emit_event})`.
- Filter: write-only by default, optional read-trace too.
- First use: trace `stage_record + 0x2c750 .. +0x2c77c` (40 bytes,
  10 slot flags) during HOUSE-INGAME boot in retail.  Find the
  writer(s).  Cross-reference against the decompile to identify
  the unported chip.  Resolves Cf.minimal orientation bug by
  identifying the missing writer chip.
- Reusable for any PHC entry of the "no writer in decompile" class
  (PHC #19, #22, #26, etc.).

**Estimated**: 0.5 session.

#### Phase D.8 — Walker-behavior diff (consumer of D.6)

**Goal**: given synthetic stage_state, diff walker output between
targets at the matrix/draw-call level.

- Agent-side RPC `runRetailWalker(state) → trace`: inject state
  globals, freeze tick, call `FUN_00457714`, capture D3D trace,
  restore, return.
- Port-side `--inject-walker-state <json>` flag: set the same
  globals, fire `scene1_walker_pass_render_house`, capture trace.
- Diff via `render_diff.py`.
- Targets:
  - **PII.3a matrix builder** — per-mesh world matrix bit-exact?
  - **PII.3b draw loop B** — same SetTexture/SetMaterial/Draw
    sequence?
  - **Future**: shop_walker (C8c), wide_followup (C8f), etc.

**Estimated**: 1-2 sessions.

#### Phase D.9 — TAS-bot / continuous diff (long-term)

OpenLords2's Phase 5 — drive a whole game session through both
targets at inhuman speed, full-frame + state diff.  Out of scope
for the next 7 sessions; revisit once D.1-D.8 are in place.

### Suggested session order (high-leverage path)

1. **D.1** — scaffolding + rng_next15 target (1-2 sessions).
2. **D.4 + D.5** — D3D trace emitters both sides (parallel-feasible
   but cleaner sequentially; 1 session each = 2).
3. **D.6 + Cf.minimal fixes** — first render diagnosis (1-2 sessions).
4. **D.7** — memory-access watch + flip-chain writer trace (0.5).
5. **D.2 + D.3** — fill in more pure-function targets + engine-tick
   freeze (1-2 sessions, lower priority once visual bugs are fixed).
6. **D.8** — walker-behavior diff (1-2 sessions).

Total: ~5-9 sessions to reach Cf.minimal-visually-correct + a
durable diff harness for future render chips.

### Cross-cutting design notes

- **Race handling** (mandatory from D.3 onward): snapshot pre-call,
  checksum after, retry on perturbation, fail after N retries.
  OL2 has 1800/1800 vectors landing clean — pattern is proven.
- **Snapshot/restore in `finally`**: every RPC that mutates retail
  globals restores in a `finally` block.  Exception → engine NOT
  left perturbed.
- **Engine-tick freeze cycling quirk**: install once per run,
  uninstall once at end.  Cycling triggers a Frida-internal
  TypeError.  OL2 burned several debugging sessions on this; we
  pre-emptively design around it.
- **Frame budget**: ~1-3 ms per Frida RPC.  Pure-function diffs are
  fine (200 vectors × few ms = ~1 s).  D3D-trace events MUST be
  batched per-frame; per-call `send()` would saturate.
- **Schema versioning**: every JSONL emitter writes a `version: 1`
  header line.  Lets us evolve the schema without breaking older
  captures.
- **No mandatory retail dependency**: the diff harness is opt-in
  (run `make -C tests diff` + `tools/diff_test.py` only when
  diagnosing).  Default CI = the host-test suite alone.

### Cross-references (D.*)

- `../OpenLords2/docs/harness-roadmap.md` Phase 4 — origin pattern.
- `../OpenLords2/tools/diff_test.py` — orchestrator reference.
- `../OpenLords2/tools/frida/openlords2-agent.js` (lines 3838+) —
  RPC export structure to mirror.
- `../OpenLords2/docs/findings/pure-function-diff.md` — engine-
  thread race + retry details.
- `tools/state_diff/oracle.c` — the bespoke pattern Phase D.1
  replaces / migrates from.
- `docs/findings/scene1-walker-pass-init.md` "Cf.survey landing"
  — the bug class Phase D.6 is designed to resolve.

## Phase E — Leaf-first execution parity (post-D.6 strategic shift)

> **IMPLEMENTATION NOTE (audited 2026-05-29): E.1–E.3 were realised via
> the annotation-driven `CALL_TRACE_ENTER(0xVA)` scheme
> (`src/call_trace.h`, `tools/call_trace_diff.py`), NOT the
> `cyg_profile`/`call_graph_diff` design sketched below.  The latter is
> retained as rejected-alternative rationale.**  The
> `-finstrument-functions` / `__cyg_profile_func_enter` build flag,
> `tools/call_graph_diff.py`, and `tools/call_graph_diff_map.json`
> described in E.2/E.3 were never implemented — grep confirms none of
> them exist.  The annotation scheme was chosen because it is lossless
> and explicit (each traced site names its engine VA at the source, so
> there is no fragile auto-derived VA-equivalence map to maintain), it
> doubles as port↔engine documentation, and its `CALL_TRACE_ENTER_STUB`
> variant surfaces stubbed-but-wired bodies that a pure call-count
> diff would silently treat as matching.

### Motivation

Phase D's diff harness surfaces divergent behaviour after the fact
(a translucent shop_table, a swapped-axis mesh, a missing draw call).
That's useful for triaging known-broken render chips, but it's not
enough for a faithful drop-in reimplementation: each visible bug
points at one or more leaf-function defects, and the harness gives
no signal on leaves that are wrong but happen not to show up
visually yet.  The right shape of the work is to walk the execution
tree bottom-up: for every leaf retail calls during a frame, verify
the port calls the same leaf with the same inputs and that it
returns the same outputs.  Then iterate inward: once all the leaves
match, internal nodes match by construction.

This is a multi-session commitment (months, realistically) and a
different methodology from Phase D's bug-driven cadence — the user
explicitly chose it over chasing the Cf.minimal alpha/orientation/
scale bugs piecemeal.

### Tooling pivot: TTD + cdb instead of Frida-only

Frida is great for live RPC and state-watch but every analysis pass
re-runs the target — non-deterministic.  **Time Travel Debugging**
records once and replays infinitely; the trace IS the data, and any
query can be re-run against the same recording with no re-execution.
For the "iterate per leaf, look at the same call site dozens of
times" loop, that's the right primitive.

Frida stays for: live state-forcing RPCs (`runRetailRngNext15`-style),
the D.7 memory-access watch when it lands, and the D.4 / D.5 D3D
state-trace emitters that already shipped.  TTD owns: every-function
call enumeration, per-leaf I/O capture, repeatable forensic replay.

#### Phase E.0 — TTD record/query harness scaffold ✓

**Status**: landed (commits `59b521e` → `76f7da2`, 2026-05-26).

> **STATUS (audited 2026-05-29): TTD harness is currently forensic-only
> / unconsumed by the live diff loop — see audit.**  It was built on the
> premise of replacing Frida for leaf enumeration, but
> `tools/call_trace_diff.py` (the scheme that actually ships for E.1–E.3)
> consumes a Frida-produced retail trace, not a TTD `.run`.

  - `tools/ttd/ttd_paths.py` — binary discovery (`TTD.exe` +
    `cdbX86.exe`).  WindowsApps shims first, classic SDK paths
    next; env-var overrides for outliers.
  - `tools/ttd/ttd_capture.py` — record-cycle driver.  Outer
    PowerShell → `Start-Process -Verb RunAs` → inner PowerShell
    runs `_run_elevated.ps1` which spawns TTD, sleeps wall-time,
    `Stop-Process` the target, waits for finalize, writes a status
    JSON across the elevation boundary.  Capture flow elevated
    because TTD rejects non-admin callers with `0x80070005`.
  - `tools/ttd/ttd_query.py` — load a `.run` trace into cdb, run a
    JS via `.scriptrun` (NOT `.scriptload`; the latter only fires
    `initializeScript()`, not `invokeScript()`).  `--extra-global
    K=JSON_VALUE` and `--extra-global-file K=PATH` pin per-call JS
    globals.
  - `tools/ttd/scripts/batch_calls.js` — the leaf-first analysis
    primitive.  Takes `TARGET_VAS: [int, …]` and returns per-VA
    `{n_calls, callers: [{ret_va, count, first_time_seq}],
    truncated}`.  Address-keyed queries — works without PDBs (retail
    has none).
  - `tools/ttd/data/engine_function_vas.json` — 2103 static call
    targets extracted via objdump `-d --section=.text`.  Misses
    indirectly-called function-pointer/vtable targets (those'll
    surface as "TTD saw a call we don't have in the list" findings
    during enumeration).
  - **Classifier-clean output design**: every subprocess stdout/
    stderr → log file the harness never reads back.  Harness's own
    stdout = a single JSON line with abstract failure stages
    (`paths_discover`, `record_spawn`, `no_output_file`, etc.).
    Anthropic's API-level Usage Policy classifier trips on dense
    debugger help text regardless of model reasoning about
    legitimacy — design forces all that text into log files only.

Validated end-to-end against a 352 MB boot-smoke trace
(`runs/ttd-boot-smoke-*/trace.run`): rng_next15@0x5041f6 → 600
calls, single caller; rng_unit@0x471089 → 600 calls, 6 sites at 100
each (unrolled loop); first-100 engine VAs → 2 called (low-VA init
paths dormant in the first 400 ms).

Per-VA cost ~2.5 s after a 10 s cdb startup; full 2103-VA
enumeration extrapolates ~90 min.

#### Phase E.1 — Per-frame bracketing

**Goal**: partition a trace's calls into per-frame buckets.

Without symbols, `TTD.Calls("d3d8!*Present*")` returns empty.
Options:

  - Address-keyed `TTD.Calls(va)` against the engine call site for
    Present (we know its address from D.4's vtable hook).  Each
    Present-call's `TimeStart.Sequence` is a frame boundary.
  - Or `TTD.Memory(d3d8_vtable_present_slot, +4, "R")` for vtable
    reads — slower but exhaustive.

Once frame boundaries are known, `batch_calls.js` widens to take
`(target_va, time_lo, time_hi)` and filter the iterator by time
position — produces per-frame call lists per VA.

**Estimated**: 1 session.

#### Phase E.2 — Port-side call enumeration

**Goal**: emit the same `{target_va, n_calls, callers}` shape on
the openrecet side.

Build flag: `-finstrument-functions` on every TU.  Implement
`__cyg_profile_func_enter(this_fn, call_site)` + `_func_exit` to
append rows to a JSONL emitter, gated by per-frame `g_emit_this_frame`
the same way `src/d3d_trace.c` is gated.  Output: per-frame call
list matching the TTD `batch_calls.json` schema.

`scene1_emit_frame_begin()` resets the per-frame call buckets the
same way D.5's `d3d_trace_begin_frame()` does.

**Estimated**: 1 session.

#### Phase E.3 — Call-graph diff orchestrator

**Goal**: align retail vs port call lists per frame, surface
divergences leaf-first.

`tools/call_graph_diff.py` — same shape as `render_diff.py`:

  - Load both JSONLs.
  - Per frame, align by `target_va` (engine ↔ port VA equivalence
    table needed — see below).
  - For each leaf: compare `n_calls`, `callers[]` distribution,
    `first_time_seq` ordering.
  - Output: deepest-diverging leaf first.

**Engine↔port VA equivalence**: maintain a manual mapping
`engine_va → port_function_name` in `tools/call_graph_diff_map.json`,
derived from our per-chip port commits (most port functions name
the engine FUN_ they correspond to in their docstring already).
Port-side VAs come from `nm openrecet.exe` post-build.

**Estimated**: 1-2 sessions.

#### Phase E.4 — Per-call I/O capture

> **Detailed executable plan: `docs/plans/e4-per-call-io-capture.md`** (2026-05-29).
> Deferred: build when count-parity stops being enough (first invisible
> behavioral divergence). Start with Tier 1 (small, reuses the diff_test.py oracle).

**Goal**: at every call boundary, capture {stack args, register
state, memory writes} on both sides.

TTD side: extend `batch_calls.js` to dump per-call `Registers` and
`MemoryWrites` model objects.  Per-call cost rises significantly;
gate via `MAX_IO_CAPTURES_PER_VA`.

Port side: wrap `_func_enter`/`_func_exit` with stack-arg snapshot
+ a shadow-page hash of post-call writable memory regions.

Diff orchestrator gains I/O comparison: same args → expect same ret
+ same memory delta.  First mismatch = the leaf's behavior diverges.

**Estimated**: 2-3 sessions.  Heaviest tooling chip in Phase E.

#### Phase E.5 — Iterative porting loop

**Goal**: pick the deepest diverging leaf, fix or port it, re-run.

This is the actual leaf-first work; the prior chips are
infrastructure.  Each iteration:

  1. Run capture on both sides for the same scenario.
  2. Run diff orchestrator.
  3. Identify the deepest leaf that diverges (or is called in retail
     and unimplemented in port).
  4. Port / fix that leaf.
  5. Verify the leaf no longer diverges.
  6. Goto 1.

Stop conditions: every called retail leaf has a matching port
implementation, every leaf's per-call I/O matches retail's verbatim
on the recorded scenario.  Stub leaves (intentionally unimplemented
because their effect is provably isolated) are documented in a
per-leaf annotation file.

**Estimated**: ongoing for as long as the project runs.  Each leaf
is a self-contained chip.

### Suggested session order (Phase E)

1. **E.1** — per-frame bracketing (1 session).
2. **E.2** — port-side `-finstrument-functions` emitter (1 session).
3. **E.3** — call-graph diff orchestrator (1-2 sessions).
4. **E.4** — per-call I/O capture (2-3 sessions).
5. **E.5** — iterative porting (ongoing).

Total to "first leaf comparison": ~5-7 sessions from E.0 landing.

### Capture friction notes

  - TTD's first run on a host shows a one-time "Start Trace" consent
    dialog (already cleared on this host).
  - Recording requires admin; PowerShell `Start-Process -Verb RunAs`
    fires UAC.  At lowest UAC setting it auto-approves silently.
  - retail's CWD must point at `vendor/original/` (the Steam-install
    symlink) for asset bundles to load.  Already wired through the
    harness `--cwd` default.
  - Native NTFS trace output (`~/openrecet-traces/`) is ~3× faster
    than UNC writes through `\\wsl.localhost\NixOS\...`.  Use
    `--run-dir /mnt/c/Users/<user>/openrecet-traces/<scenario>` for
    captures that matter.

### Cross-references (E.*)

  - `tools/ttd/README.md` — harness layout + working primitive +
    leaf-first workflow sketch.
  - `tools/ttd/data/engine_function_vas.json` — input for
    `batch_calls.js`.
  - `memory/feedback_classifier_clean_output.md` — why the harness
    looks the way it does (Anthropic API classifier mitigations).
  - `memory/reference_ttd_harness.md` — quick command reference.
