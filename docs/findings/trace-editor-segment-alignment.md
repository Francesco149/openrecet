# Trace-editor alignment — the captured-frame-index model

How the Trace Studio editor (`tools/trace_studio_web/web/components/TraceEditor.mjs`) lays
port + retail out on one timeline. The pure core is `tools/trace_studio_web/align.mjs` (JS,
the browser) mirrored by `tools/trace_studio/model/segments.py` (Python source-of-truth),
pinned together by the golden cross-check `tools/test_trace_studio_segments.py`. This doc is
the **semantics**; the tests are the executable spec.

## The principle: the capture is already 1:1 — don't reconstruct alignment, use it

The harness drives both sides with the **same** input trace under turbo + a pinned RNG seed +
a pinned phase, so the capture runs **1:1**: the n-th captured frame on the port is the same
logical moment as the n-th captured frame on retail. So the timeline x-axis is simply the
**dense captured-frame index** — one tick per real frame of the trace running on that side.
Place each side's anchors + inputs at their captured index and a 1:1 capture aligns with
**zero forcing logic**; where the traces diverge (different per-side frame counts) the two
rows just drift apart — that *is* the divergence, and you iterate edits until they reconverge.

(This replaced two failed approaches that tried to *reconstruct* alignment from the trace's
`{wait}` segments + anchor offsets — a single-sync-anchor model and a sequential-band model.
Both broke on real traces because segment-relative positions diverge across a load, and a
mid-capture divergence put corresponding 1:1 frames at different x. The captured index has
none of that complexity: the alignment is inherent in the capture.)

## Loads (the one subtlety) — suppressed, so the index re-syncs at every boundary

Loads are captured with **zero frames** (the load-screen-suppression optimisation: the engine
runs the load but no frames are recorded). So crossing a load advances the captured index by
0 on **both** sides — the index re-syncs at every load boundary **regardless of how stretched
the load is** (turbo retail's load can run 14k ticks where the port's runs 200; both record 0
captured frames). The requirement is only that both sides capture the **same** frames at the
boundary, which holds in a 1:1 region. No load-matching logic is needed; the suppression does
it.

In the captured PNGs a suppressed load shows as a **gap in the frame numbers** (e.g. frame 161
is missing — it was the load tick), so scrubbing the video "skips" across it. The editor draws
a faint `tl-loadmark` tick at each load boundary so that jump is legible.

## The map: absolute engine frame ↔ dense captured index

Per side, from the manifest `base_abs` (the absolute engine frame of captured frame 0) and the
`LOADING_START→LOADING_END` pairs (`loadSpans`):

```
capIndexOfAbs(abs, baseAbs, loads) = (abs − baseAbs) − (suppressed load frames strictly before abs)
absOfCapIndex(g,  baseAbs, loads)  = walk g captured frames from baseAbs, skipping each load
```

Only loads **at/after `baseAbs`** count (a load before the window produced no captured frames
in it). A frame before `baseAbs` maps to a negative index (off the left of the captured axis).

- **Anchors** (per side): a firing at absolute frame `F` → `capIndexOfAbs(F)`. Shown if it
  lands within the captured axis; pre-window firings (BOOT, the load that produced `base_abs`)
  fall left of 0.
- **Inputs / pins** (the trace): an op at `(segment, frame)` → absolute `segBase[seg] + frame`
  (segment bases from `resolveBases` on that side's anchors) → `capIndexOfAbs`. Drawn per side,
  so the same shared trace op lands at the same index in a 1:1 region and drifts on divergence.
- **Cursor**: at the global scrub ordinal `cur` (already the dense captured index). A
  scrub-click sets `cur` to the clicked index.
- **Editing**: a click index `g` → `absOfCapIndex(g)` on the edit-reference side → the segment
  it falls in → segment-relative frame → the trace op. (The edit side is the one with a
  non-null `base_abs`, port preferred.)

Worked example — `merchants-guild` (port enters the guild and **diverges to cyan**, so its
anchor stream stops at the guild load; retail plays the dialogue):

| event | port `g` | retail `g` | |
|---|---|---|---|
| guild `LOADING_END` (= each side's `base_abs`) | 0 | 0 | aligned ✓ |
| next load (`LOADING_START`/`PAUSE_CLOSE`/`LOADING_END`) | 161 | 161 | aligned ✓ (1:1) |
| dialogue (`TEXT_ANIM…`, frames 162–933) | — | 162… | one-sided (the divergence) |

The 1:1 region (frames 0–161: guild entry + world-map menuing) aligns automatically; at frame
~162 the port diverges and retail's dialogue is shown alone — no forcing, no overlay.

## Independence from capture health

The editor maps from `base_abs` + the anchor/trace files — never from band reconstruction. A
side whose capture failed (`base_abs: null`) simply has no captured axis (its lanes are empty);
the other side still works. The `{caprange}` len/pos controls still edit the capture **size**
(a `✎ pending` flag until the next ⟳ re-capture realises it).

## API (pure, golden-tested)

`align.mjs` / `segments.py` (mirrored):
- `parseSegments(ops)` → `[{waitAnchor, items:[{kind,frame,...}]}]`
- `resolveBases(segments, firings)` → `[{base, ok, anchor}]` (segment bases on a side)
- `loadSpans(firings)` → `[{start, end}]` (completed LOADING pairs; dangling START dropped)
- `capIndexOfAbs(abs, baseAbs, loads)` → dense captured-frame index
- `absOfCapIndex(g, baseAbs, loads)` → absolute engine frame (the inverse)
- `itemAbs(item, segIdx, bases)` → a trace op's absolute frame on a side

`sideLayout`/`absToX`/`xToAbs`/`divergenceReport` remain as the legacy single-sync helpers
behind the textual divergence report + the golden cross-check.
