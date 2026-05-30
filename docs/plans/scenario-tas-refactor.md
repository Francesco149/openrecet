# Refactor scenarios onto the TAS tools + interactive comparison page

## Context

This session built the TAS **anchor-segmented input forcing** (retail Frida side +
port C mirror `src/input_segtrace.{c,h}`, 7 host tests, validated end-to-end) with
`{wait}`/`{capture}`/`{calltrace}` ops, `--watch`, and `tools/montage_frames.py`. It
obsoletes `--auto-z-spam` and made HOUSE free-roam reachable deterministically — which
identified the movement controller (`FUN_0048b850`, engine-quirks §60).

The scenario harness (`tools/scenario-test.py` + `tests/scenarios/<name>/`) still uses
the **old** primitives: absolute `--input-trace-replay` traces and absolute
`capture_frames`, with goldens keyed by absolute engine frame
(`golden/frame_NNNNN.bmp`). Absolute frames are exactly the fragility this session
proved wrong (load jitter ±250 frames). **Goal: refactor scenarios onto segtrace +
anchor-relative captures, add a HOUSE-movement scenario, auto-regenerate the comparison
gallery on every run, and make the gallery's images interactive (click-to-reveal an
amplified pixel diff; copy-image yields a 3-up [left|right|diff] atlas).**

This is a planning deliverable for the next session. Prior-session work is committed-
ready but uncommitted; commit before starting if desired.

## Phase 1 — Scenario format onto segtrace + anchor-relative captures

**Format.** `tests/scenarios/<name>/trace.jsonl` may now be a **segtrace** (contains
`{"wait":"ANCHOR"}` and/or `{"capture":N}` ops; see `src/input_segtrace.h`). Captures
come from the trace's `{capture}` ops (anchor-relative), so `scenario.yaml`'s
`capture_frames` becomes **optional/deprecated** for segtrace scenarios. Plain
`{frame,buttons}` traces keep working unchanged (backward compat).

**`tools/scenario-test.py` changes:**
- Detect segtrace: if any trace line has `"wait"` or `"capture"` → segtrace mode.
- Port drive (`run_scenario_capture`, ~L247): swap `--input-trace-replay` →
  `--input-segtrace` (already wired in `main.c` this session); **drop**
  `--capture-frames` (the port schedules captures from `{capture}` ops via
  `segtrace_capture_cb`). Keep `--anchor-trace-record` so anchor frames are logged.
- Retail drive (`run_scenario_capture_retail`, ~L236): pass `input_segtrace_path`
  instead of `input_trace_path` to `frida_capture.run_capture` (add the kwarg there —
  the agent + driver already support `--input-segtrace`).
- **Capture-index goldens.** Anchor-relative absolute frames jitter run-to-run, so key
  goldens by **capture order**, not frame number: store `golden/cap_00.bmp`,
  `cap_01.bmp`, … (Nth `{capture}` op = Nth golden). Both sides emit captures in trace
  order; collect the run's `captured_frames` (sorted) and zip to indices. `--bless`
  writes `cap_NN.bmp`; `diff_against_golden` compares by index. Keep the absolute
  `frame_NNNNN.bmp` path for non-segtrace (legacy) scenarios — branch on mode.
- `Scenario` dataclass (~L116): add `is_segtrace` (derived) + make `capture_frames`
  optional; add nothing to the YAML schema (the trace carries capture timing now).

**Reuse:** `_red_tint_overlay` / `diff_against_golden` (SHA-exact + red overlay) stay;
`contact-sheet.py grid()`/`thumb()` stay.

## Phase 2 — HOUSE-movement scenario + port double-HF "dialogue" stub

**Scenario `tests/scenarios/house-movement/`:** `trace.jsonl` = the validated
`traces/house_walk.jsonl` shape — title commit → `wait HOUSE_FREEROAM` → Z-spam →
`wait HOUSE_FREEROAM` → Z-spam 3D dialogue → release → `{capture}` (idle) → hold LEFT
(`0x0002`) → `{capture}` (walking). `scenario.yaml`: description + `max_frames` headroom.

**Port double-HF stub (the blocker, per user "stub the dialogue so the same trace works
on both"):** the port reaches HOUSE via ONE load (HF#1@~101) — it skips the two intro
events, so the trace's 2nd `wait HOUSE_FREEROAM` never resolves and the port stalls.
Add a minimal **intro-event stub** so the port fires `HOUSE_FREEROAM` twice with the
same structure as retail (`NEW_GAME → HF#1 → [load] → HF#2 → controllable`):
- Entry point: the new-game→INGAME transition (`src/scene.c:63`,
  `g_scene_state = SCENE_STATE_INGAME`). Drive a tiny state machine that runs two brief
  phases, each toggling `nowloading_set_active(1)` then `(0)` (`src/nowloading.h`) so the
  `anchor_trace` loading-gate edges fire a 2nd `LOADING_START/END → HOUSE_FREEROAM`.
  Events render nothing (stub) — Recette already stands in the 3D shop after HF#1.
- Mark clearly as a stub to be replaced when the dialogue subsystem is ported; behavior
  then "fills in" without changing the trace (the user's stated principle).
- Host-test the stub's anchor sequence via `anchor_trace` (extend `test_anchor_trace.c`:
  a new-game world snapshot sequence should emit HF twice).

**Note:** the port player won't *walk* until `FUN_0048b850` is ported — so the movement
scenario's port frames show the frozen-but-correct shop vs retail's walking Recette.
That's the intended parity baseline; it converges as the controller lands. State this in
the scenario description.

## Phase 3 — Auto-regenerate the comparison page on every run

- After `scenario-test.py --target both` finishes (`~L676`/`~L682`), invoke the gallery
  regen for that scenario (import `regen-comparisons` as a module, or factor its
  `render_html`/`collect_artifacts` into a shared `tools/comparison_page.py`). Add
  `--no-regen` to opt out.
- Auto-open `runs/comparisons/index.html` in the Windows viewer (reuse
  `montage_frames._open_windows` → factor it into a shared helper).
- `regen-comparisons.py` keeps its standalone `--html-only` path.

## Phase 4 — Interactive gallery: click-to-reveal amplified diff + copy-as-3-up atlas

The clever requirement: **on the page each comparison shows only [left | right]; click it
to reveal the amplified diff below; right-click "Copy Image" yields a single montage of
all three.**

**Per-capture atlas (replaces the one-big-`sidebyside.png`).** For each capture index,
build ONE atlas PNG laid out as:
```
row 0:  [ left (port) | right (retail) ]      ← always visible
row 1:  [ amplified pixel-diff, full width ]  ← hidden until click
```
- Diff math: reuse `tools/pixel_diff.py`'s amplified white-diff —
  `perpx = |a-b|.sum(axis=2); d = clip(perpx*amp, 0, 255)` — **pure black = bit-identical**,
  subtle diffs amplified (`amp`≈6, configurable). Factor that into a reusable
  `amplified_diff(a_rgb, b_rgb, amp) -> rgb` (lift from pixel_diff.py L77–84).
- Build the atlas with PIL (the row0 2-up via `contact-sheet.grid`, row1 the diff).

**The "atlas magic" (copy = 3-up, view = 2-up):** the `<img src>` is the full atlas;
a wrapper `<div style="overflow:hidden">` is sized to **row 0's height** so only
[left|right] shows. Clicking toggles a class that expands the wrapper to full height,
revealing the diff row below. Browser "Copy Image" copies the full source bitmap (all
three panels) regardless of the CSS clip — so a pasted screenshot is the complete
[left|right|diff] montage while the page stays clean. (Verify in the user's browser;
fallback: `clip-path` on the img, same copy behavior.)
- Gallery HTML (`render_html`, regen-comparisons.py L266): emit one clickable
  atlas-wrapper per capture index per scenario; keep the dark-theme card + staleness JS.
  Add a tiny `onclick` toggle + a per-image "differing px / mean-abs" caption from the
  diff stats (pixel_diff.py already computes these).
- Keep the zoom-text companion as an additional atlas row when `zoom_text:` is set.

## Critical files

- `tools/scenario-test.py` — segtrace detection, both drive paths, capture-index
  goldens, auto-regen hook (Phases 1, 3).
- `tools/frida_capture.py` — add `input_segtrace_path` kwarg to `run_capture` (Phase 1).
- `tools/regen-comparisons.py` (+ maybe new `tools/comparison_page.py`) — per-capture
  atlas, interactive HTML, diff embedding (Phases 3, 4).
- `tools/pixel_diff.py` — factor out `amplified_diff()` for reuse (Phase 4).
- `tools/montage_frames.py` — factor out `_open_windows()` (Phase 3).
- `src/scene.c`, `src/nowloading.{c,h}`, new `src/scene1_intro_events.c` (stub) +
  `tests/test_anchor_trace.c` — port double-HF stub (Phase 2).
- `tests/scenarios/house-movement/{scenario.yaml,trace.jsonl}` (new); migrate existing
  scenarios' traces to segtrace where it adds determinism (optional, non-breaking).

## Verification

- **Phase 1:** `nix develop --command make -C tests run` green; run an existing scenario
  (`scenario-test.py title-z-press --target openrecet`) — still passes with legacy
  absolute goldens; a converted segtrace scenario blesses + diffs by `cap_NN`.
- **Phase 2:** drive the port with `traces/house_walk.jsonl` via
  `tools/run-openrecet.sh --input-segtrace … --anchor-trace-record` → anchors show
  `HOUSE_FREEROAM` **twice**; the trace runs to completion (reaches seg2) instead of
  stalling. New anchor host test green.
- **Phase 3:** `scenario-test.py house-movement --target both` regenerates
  `runs/comparisons/index.html` and opens it; rerun updates it.
- **Phase 4:** in the browser, each comparison shows 2-up; clicking reveals the amplified
  diff (black where identical); right-click Copy Image pastes a 3-up montage. Confirm
  with the user via the opened page + `eog`.
- Frida hygiene throughout: canonical `frida_capture.py` invocation, `kill_retail.py`
  after runs, restart frida-server if captures degrade.

## Out of scope (separate next steps)
- Porting `FUN_0048b850`/`FUN_00483170` (the actual movement — makes the port player
  walk; the movement scenario's parity converges once it lands).
- `tas_diff.py` full both-sides anchor-aligned diff CLI (the gallery covers the visual
  case; tas_diff is the headless/CI form).
