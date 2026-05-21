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

- **Phase A — our-exe integration loop.** No Frida, no retail
  instrumentation. Adds `--input-trace <file.jsonl>` recorder + player
  to `build/openrecet.exe`, deterministic frame capture by frame index,
  and a `tests/scenarios/<name>/` directory layout with `trace.jsonl`
  + golden frame PNGs + golden audio-event JSON. Test runner replays
  the trace into our exe, pixel-diffs against goldens, asserts audio
  events. Starter scenarios cover the regression we just hit: "boot 60
  frames idle" + "press Z at frame 30, expect dispatch within 16
  frames and the cursor brightness pulse." Self-hosted goldens —
  regenerated on intentional behavior changes via `--bless` flag.
  Catches regressions between *our* commits.

- **Phase B — retail capture via Frida (DLL inject).** Same JSON/PNG
  format as Phase A so the two pipelines share schemas. Hooks D3D8
  `IDirect3DDevice8::Present` for back-buffer dumps, the engine's
  `audio_play_track` / SE call sites for audio events, and
  `DAT_073dddd0` for the per-frame button bits. Ground truth for new
  scenes as they port; also a debugging probe for "what is the running
  retail engine actually doing right now" questions that no amount of
  static decomp answers. Hook addresses largely already identified —
  scene dispatcher `FUN_004547ab`, save-load, audio entry points — so
  the RE work is "wire up known anchors", not new discovery. The
  state-forcing role (save-injection → "drop me into shop day 5") is
  the same hook surface and lands in the same session.

- **Phase C — PCM diff (deferred until needed).** Once both pipelines
  capture matching JSON event traces, the next class of bug (subtle
  audio glitches — wrong attenuation curve, mistimed SE, sample-rate
  artifacts) won't show up in event-log diffs. The cure is the Tier 3
  #6 PCM-capture hook: record what each engine actually pushed to the
  audio buffer, render as waveform + spectrogram PNG, diff visually.
  Implement when the first such bug actually bites — don't pre-build.

Open question (deferred to start of Phase A): pixel-diff strictness.
Options under consideration:
- **Bit-exact.** Cleanest pass/fail. Brittle to driver / GPU /
  compositor diffs across hosts.
- **SSIM > threshold.** Standard image-similarity. Tunable but
  threshold tuning hides subtle regressions.
- **Per-pixel red-tint diff overlay, eyeball-driven.** Visual review,
  no auto fail. Lowest authority, but the existing `tools/smoke-test.py
  --diff` already produces this.
- Likely answer: bit-exact for *deterministic* scenarios (idle title
  menu, controlled input) + tinted overlay for the rest. Same harness
  emits both.

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
