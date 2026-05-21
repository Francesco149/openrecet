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
  input poll reading `DAT_073dddd0`. Frame numbers come from
  `DAT_073dfcfc` (engine global frame counter) so capture filenames
  match the scenario `capture_frames:` list.
  `boot-idle/golden-retail/` blessed; 3/3 bit-exact on re-run.
  Auto-start helper for `frida-server.exe` via elevated
  Start-Process if the port isn't reachable.

  **Save-inject + scene-dispatch state-forcing still deferred** —
  separate scope from the Phase B+ pure-fn diff that landed below.

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
