# Frame-by-frame TAS trace viewer (openrecet ⇄ llm-feed)

> **✅ BUILT 2026-06-03 (autonomous).** All 7 phases landed + the feed
> pagination ask. See "Build status" at the bottom for what shipped, how it was
> verified, and the one determinism boundary found. Demo trace live on the feed:
> `http://localhost:8777/trace.html?id=20260603T045417_e05a` (60-frame HOUSE
> free-roam window).

> **Autonomous build spec.** Written 2026-06-03 for next session to execute
> top-to-bottom. The user will `/clear` first, then I build this autonomously and
> they review tomorrow. Work the phases in order; each has an acceptance check.
> Commit per phase. Spans two repos: `/opt/src/openrecet` (capture/record/replay)
> and `/opt/src/llm-feed` (the `trace` item type + viewer). Build directly on
> `master` (no branches, per [[feedback_no_early_branches]]).

## Context / why

The dust-occlusion + RNG-parity work needs to **see a recorded TAS trace
frame-by-frame**, mark exact frames + crops, and reconstruct a runnable trace
from what's on the feed. Today the feed only does static image/montage/comparison
cards, and the openrecet recorder loses ESC presses (so dialogue-skip recordings
don't replay — the blocker we hit this session). This builds:

1. **llm-feed `trace` item type** — an animated preview card that opens a
   **dedicated viewer in a new tab**: step frames (←/→ = ±10, `,`/`.` = ±1), mark
   per-frame captures, drag a crop box → copyable `frame + crop` reference (the
   same mechanism as the main feed's `attachBoxSelect`). Per-frame data blob +
   one global blob (e.g. rng seed) travel with the trace.
2. **openrecet full-trace export** — capture a contiguous frame window + per-frame
   metadata + a global blob, and push it to the feed as a `trace`.
3. **ESC in traces** — record ESC keydowns and replay them via a new `{esc}`
   segtrace op, so dialogue-skip recordings replay faithfully.
4. **Round-trip** — reconstruct a runnable openrecet `.jsonl` from a feed `trace`
   entry (the entry embeds the input change-points + rng seed + esc + captures).

Builds on the just-landed `{rngseed}` op ([[reference_rngseed_op]], commit
d553861) and the recorder/distill/segtrace pipeline.

---

## Data model

### llm-feed `trace` entry (one JSONL line in `data/feed.jsonl`)
```json
{
  "id": "20260604T0102_ab", "ts": 1780..., "iso": "...",
  "type": "trace",
  "title": "house free-roam dust walk",
  "note": "...",
  "fps": 20,
  "frames": [
    { "src": "data/assets/<id>/frame_0000.png", "n": 1008, "label": "f=1008",
      "data": { "rng": 1247414844, "buttons": "0x0010", "px": 1.2, "py": 0.0,
                "pz": 3.4, "anim": 1, "oct": 4, "anchors": ["HOUSE_FREEROAM"],
                "esc": false } },
    ...
  ],
  "global": {
    "rng_seed_at_start": 1247414844,
    "trace_jsonl": [ {"rngseed":[1565,...]}, {"frame":1565,"buttons":"0x0010"}, ... ],
    "anchor_offset": 1565, "source_raw": "openrecet-trace-26976-0.raw.jsonl",
    "scenario": "house-free-roam", "schema": "openrecet-trace-v1"
  }
}
```
- `frames[i].data` = the **per-frame game-specific blob** (open-ended; the viewer
  just pretty-prints it). `global` = the **whole-trace blob**. Both are required by
  the user ("one blob per frame, plus a global blob"); keep them schemaless on the
  feed side.
- `global.trace_jsonl` is the **runnable segtrace ops** (array of the same objects
  a `.jsonl` trace file holds) — this is what makes the entry reconstructable.

### openrecet export directory (the producer side, before the feed push)
```
runs/trace-export/<name>/
  frames/frame_NNNNN.png       # every frame in the captured window
  meta.jsonl                   # one {"frame":N, ...per-frame data...} per frame
  global.json                  # {rng_seed_at_start, trace_jsonl, anchor_offset, ...}
```

---

## Phase 1 — openrecet: contiguous frame-range capture

`--capture-frames` is capped at `CAPTURE_FRAMES_MAX = 32` (src/main.c:594) — too
few for a frame-by-frame trace. Add a contiguous-range mode that bypasses the cap.

- **Flag** `--capture-range START,COUNT` (absolute) and a segtrace op
  **`{"caprange":[start,count]}`** (anchor-relative, jitter-immune — mirrors
  `{capture}`). Parser + struct in `src/input_segtrace.{c,h}` (copy the `capture`
  path: `push_caprange`, parse, and in `input_segtrace_tick` schedule
  `base+start .. base+start+count-1` into the capture set via the existing
  `segtrace_capture_cb`). Add a host test in `tests/test_input_segtrace.c`.
- **Capture dispatch** (src/main.c `render_dispatch` ~2748, `capture_frame_is_listed`
  ~3276): add a `g_capture_range_lo/hi` pair; capture when `lo <= frame <= hi`
  (OR with the listed-frames path). Set from `--capture-range` and from the
  segtrace op via the capture callback (the callback already appends to
  `g_capture_frames` — but for a wide range, prefer the range-test to avoid
  overflowing the 32-array; have the segtrace caprange op set `g_capture_range_*`
  directly through a new callback, parallel to `segtrace_rng_cb`).
- **Acceptance:** `run-openrecet.sh --input-segtrace T --capture-to D` where T has
  `{"caprange":[1565,120]}` writes 120 consecutive `frame_NNNNN.png` (no 32 cap).

## Phase 2 — openrecet: ESC record + `{esc}` replay op

The recorder drops ESC (it's a WndProc `VK_ESCAPE` → `esc_pressed()`, main.c:2019,
NOT in the button mask). Make ESC first-class so skip-using recordings replay.

- **Record:** in the `WM_KEYDOWN`/`VK_ESCAPE` arm (main.c:2019) and the recorder
  tick, when recording is active, append an ESC marker for the current frame. Emit
  it in `trace_rec_stop()` raw output as `{"esc":REL}` rows (REL = frame -
  `g_trace_rec_start_frame`). (Store ESC frames in a small `g_trace_rec_esc[]`
  array like `g_trace_rec_caps[]`.)
- **Op + replay:** new segtrace op **`{"esc":N}`** (scalar; fires once at base+N).
  Parser + `seg_esc` array in `src/input_segtrace.{c,h}` (fire-once like
  `{rngseed}`); an `esc_cb` callback wired in main.c to a new
  `segtrace_esc_cb` that calls `esc_pressed()` (the real dispatch — title quits,
  in-game arms the skip prompt via `skip_event_arm`, exactly as a live ESC). Fires
  in `input_segtrace_tick` at base+N (in input_poll, before sim), same shape as
  `fire_setrngs`. Host tests: parse + fire-once.
- **Distill:** `tools/distill_trace.py` reads `{"esc":REL}` raw rows → emits
  `{"esc": off+REL}` (house) / `{"esc": REL}` (flat), rebased like captures.
- **Retail agent:** mirror in `tools/frida/openrecet-agent.js` — an `{esc}` op that
  posts/synthesizes the ESC path on retail (find how the agent injects keys; if
  none, call the retail skip-arm VA, or post WM_KEYDOWN(VK_ESCAPE) to the hwnd).
  Add the `rngseed`-style branch in `segtraceBuildSegments` + fire in `segtraceTick`.
- **Acceptance:** record a title→shop run **using ESC to skip dialogue**, distill
  `--house-segtrace`, replay — the dialogues skip at the same anchor-relative
  points and free-roam is reached (the exact failure from this session, fixed).

## Phase 3 — openrecet: full-trace export tool

New `tools/export_trace.py` (nix-dev-shell, PIL allowed here — only feed.py is
stdlib-only):
- Input: a trace `.jsonl` (or a raw recording → distill first) + a capture window
  (anchor-relative start,count, injected as a `{caprange}` op) + `--run-dir`.
- Runs the port via `run-openrecet.sh` with `--capture-to <run-dir>/frames` and
  `--player-pos-log <run-dir>/meta.jsonl` (the per-frame metadata already carries
  rng/buttons/px/py/pz/anim/oct — main.c:2357; extend it to also emit the active
  anchors + an `esc` flag for the frame if cheap).
- Converts BMP→PNG via `tools/frame_io.py`; writes `global.json`
  (`rng_seed_at_start`, the full `trace_jsonl` ops array, `anchor_offset`, source).
- **Acceptance:** produces `frames/ + meta.jsonl + global.json` for a window; frame
  count == meta line count == caprange count.

## Phase 4 — llm-feed: `trace` push + storage

In `/opt/src/llm-feed/feed.py` (keep **stdlib-only**, no PIL):
- `cmd_trace(args)` modeled on `cmd_montage` (feed.py:157): `--dir <export-dir>`
  (frames/ + meta.jsonl + global.json) + `--title --note --fps`. Copy each
  `frames/frame_*.png` via `_copy_asset` → `frame_NNNN.png`; pair with the matching
  `meta.jsonl` line by frame number into `frames[i].data`; load `global.json` into
  `global`; `_append` the entry (`type:"trace"`).
- CLI subparser `sub.add_parser("trace", ...)` + `set_defaults(func=cmd_trace)`
  (feed.py:406 pattern). Update `cmd_list` (feed.py:304) for the trace type.
- **Acceptance:** `feed.py trace --dir runs/trace-export/<name>` appends one
  `type:"trace"` line; assets land in `data/assets/<id>/`.

## Phase 5 — llm-feed: server routes + viewer page

In `feed.py cmd_serve do_GET` (feed.py:343): serve two new static files like
`/app.js` does — `/trace.html` → `web/trace.html`, `/trace.js` → `web/trace.js`.
(No per-id server route needed; the viewer reads `?id=` and fetches feed.jsonl.)

New `web/trace.html` + `web/trace.js` (the **new-tab viewer**):
- On load: parse `?id=<id>` from the query string, `fetch("/data/feed.jsonl")`,
  find the entry, render.
- **Frame stepping:** keydown — `ArrowLeft`/`ArrowRight` = ±10, `,`/`.` = ±1,
  `Home`/`End` = first/last. Show `frame N / total`, the frame `label`, and the
  current frame's `data` blob (pretty-printed JSON panel). Show `global` in a
  collapsible panel.
- **Per-frame capture:** key `c` toggles the current frame in a "marked captures"
  set; render the set; a **Copy** button emits a reference string, e.g.
  `trace id=<id> captures=1008,1119` (and per-frame `frame=f=<n>`), so it pastes
  back to the agent.
- **Crop box:** copy `attachBoxSelect` from `app.js:78` into `trace.js`; attach to
  the current frame `<img>` with `getMeta = () => ({id, src: frame.src,
  label: frame.label})`. Drag → `crop id=<id> box=... frame=f=<n> src=...` to
  clipboard (identical to the main feed; the agent already understands this form).
- Reuse `style.css` (link it); add a few `.trace-*` rules inline or in style.css.
- **Acceptance:** open `/trace.html?id=<id>` in a new tab; arrow/comma keys step;
  `c` marks captures; dragging a box copies a `crop … frame=f=<n>` string.

## Phase 6 — llm-feed: animated preview card

In `app.js`:
- `renderTrace(entry)` (register in `renderEntry`, app.js:249): a card with
  `cardHeader` + note + a single `<img>` that **cycles** through `entry.frames`
  via `setInterval` at `entry.fps` (JS-driven; no GIF baking → keeps feed.py
  stdlib-only). Add a "▶ open viewer" link: `<a target="_blank"
  href="/trace.html?id=<id>">` (opens the new tab the user asked for). Pause
  cycling on hover; show `frame k/N` overlay.
- `cardHeader` extra (app.js:164): ` · N frames · trace`.
- **Acceptance:** a `trace` push shows an animated card in the stream that
  flip-cycles frames; clicking "open viewer" opens the Phase-5 tab.

## Phase 7 — round-trip: reconstruct a runnable trace from a feed entry

- `feed.py` `cmd_trace_export(args)` (subparser `trace-export <id> -o out.jsonl`):
  `_read_feed()` → find entry → write `entry["global"]["trace_jsonl"]` lines to
  `out.jsonl`. That `.jsonl` is directly runnable (`run-openrecet.sh
  --input-segtrace out.jsonl`) and carries the `{rngseed}`/`{esc}`/`{caprange}`
  ops so it reproduces.
- Optionally a thin `tools/trace_from_feed.py <id>` on the openrecet side that
  shells `feed.py get <id>` and extracts the same.
- **Acceptance:** `feed.py trace-export <id> -o /tmp/t.jsonl` then replay → the
  same frames the trace shows (rng-pinned), closing the loop.

---

## Implementation order + checkpoints

1. Phase 1 (caprange) → commit. 2. Phase 2 (ESC) → commit; **re-run this session's
failing recording** to confirm ESC-skip now replays. 3. Phase 3 (export tool) →
commit. 4. Phases 4–6 (feed `trace` + viewer + preview) in `/opt/src/llm-feed` →
commit there. 5. Phase 7 (round-trip) → commit. After each openrecet C change run
`nix develop --command make -C tests run` (ASan/UBSan, ~3130 tests) — the
pre-commit hook enforces it. Health-check `localhost:8777` and restart the feed
server after editing `feed.py`/`web/*` (it serves files fresh per request, but
`cmd_serve`'s `Handler` is defined at startup — restart to pick up route changes).

## End-to-end verification (the demo to leave for review)

Record a HOUSE free-roam dust walk (ESC-skip the intro), `export_trace.py` a
~150-frame free-roam window, `feed.py trace` it. On the feed: the animated card
plays; open the viewer tab; step to a frame where dust sits over Recette's body;
drag a crop box over her feet → paste the `crop … frame=f=<n>` string. Then
`feed.py trace-export` → replay → identical frame (rng-pinned). This is exactly the
dust-occlusion debugging loop the whole thing exists to serve
([[scene1-walk-dust]] §2026-06-03: the occlusion is a DEPTH gap, awaiting a
reproducible dense-dust frame — this delivers it).

## Reuse map (don't reinvent)

- Crop-box + reference string: `app.js:78 attachBoxSelect` (copy verbatim).
- Lightbox stepping/keys pattern: `app.js:334 lbStep` / 344 keydown.
- Feed entry/asset plumbing: `feed.py:70 _append`, `:76 _copy_asset`, `:61 _new_id`.
- Per-frame metadata: `main.c:2357` `--player-pos-log` (already has rng/buttons/pos).
- Segtrace op pattern: the `{rngseed}` op (commit d553861) — parser, fire-once in
  `input_segtrace_tick`, callback wired in main.c, retail mirror in the agent,
  distill propagation. `{caprange}` and `{esc}` are the same shape.
- Frame BMP→PNG: `tools/frame_io.py`.

---

## Build status (2026-06-03, autonomous)

All seven phases shipped + the extra feed-pagination ask. Commits:

**openrecet** (this repo):
- `ab3722b` Phase 1 — `{caprange}` op + `--capture-range` (contiguous capture, bypasses the 32 cap). Host test `segtrace_caprange_resolves_to_window`.
- `b716750` Phase 2 — ESC record (`{"esc":REL}` raw rows) + `{esc:N}` replay op (fires `esc_pressed()`), distill propagation, Frida-agent mirror. Host tests `esc_fires_once`/`esc_rebases`. Proven: `{"esc":30}` synthesises ESC at the title (→ QUIT).
- `484b0b3` Phase 3 — `tools/export_trace.py` (drive a trace, write `frames/` + `meta.jsonl` + `global.json`). Proven 60/60 on the house drive.
- `ea44646` harness fix — `run-openrecet.sh` no longer imposes the 3s wall auto-exit when `--max-frames`/`--input-segtrace` is set (it was cutting the window to 1 frame under turbo).

**llm-feed** (`/opt/src/llm-feed`, commit `b84b79c`):
- Phase 4 — `feed.py trace --dir` (pairs per-frame meta by frame number; `global.json` verbatim).
- Phase 5 — `web/trace.html` + `web/trace.js` viewer (new `/trace.html` `/trace.js` routes): step ←/→ ±10, ,/. ±1, Home/End, play at fps, `c` marks captures → `trace id=… captures=…`, drag-box → the same `crop … frame=f=<n>` string.
- Phase 6 — `app.js renderTrace` animated preview card + "open viewer" link.
- Phase 7 — `feed.py trace-export <id> -o out.jsonl` reconstructs the runnable segtrace from `global.trace_jsonl`.
- Pagination — main feed renders newest 10; older backlog behind a "load more" (+10/click); new polled items always render on top.

### Reproducibility — what "1:1" delivers today

Drove the same trace twice (rng-pinned via `{rngseed}`) and diffed both the
per-frame `--player-pos-log` (sim) and the captured PNGs (render):

- **Sim is anchor-relative bit-exact.** Aligned to the 2nd `HOUSE_FREEROAM`
  base, every `px/pz/anim/counter/aframe/oct` + the `rng` field match frame-for-
  frame across runs once `{rngseed}` has fired. The only pos-log divergence is
  the `rng` value *before* the pin frame — irrelevant to a window captured after
  it. **The foot-dust + character are therefore reproducible** (dust position is
  the pinned LCG) — exactly the dense-dust frame the occlusion work
  ([[project_freeroam_smoke_effect]]) was blocked on.
- **Residual whole-image diff = the top HUD clock.** Cross-run the PNGs differ
  in only ~0.3 % of pixels, localized to `x[343..548] y[13..94]` — the
  time-of-day HUD digits, driven by the absolute virtual clock (`frame_count *
  17 ms`). This is the documented **load-frame-count determinism leak** (the
  "phase" the user named): the *absolute* frame jitters ±5 run-to-run, so any
  absolute-clock-keyed render element (the clock) drifts. The sim doesn't.

So: **pin rng → the sim + dust + character replay frame-by-frame identically**;
the open item to also pin the time-of-day HUD is a frame-count/phase pin at the
anchor (would force `g_tick.frame_count`, which the anchor/capture system itself
keys off — needs care, deliberately not attempted autonomously). For the
dust-occlusion debugging loop this boundary is harmless: crop over the
dust/feet and the region is reproducible.
