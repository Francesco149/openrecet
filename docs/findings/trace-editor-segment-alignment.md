# Trace-editor segment alignment (the band model)

How the Trace Studio editor (`tools/trace_studio_web/web/components/TraceEditor.mjs`) lays
port + retail out on one axis so a trace can be inspected and edited frame-by-frame. The
pure core is `tools/trace_studio_web/align.mjs` (JS, used by the browser) mirrored by
`tools/trace_studio/model/segments.py` (Python source-of-truth), pinned together by the
golden cross-check `tools/test_trace_studio_segments.py`. This doc is the **semantics**;
the tests are the executable spec.

## The problem it solves

A trace is **anchor-segmented**: ops before the first `{wait}` are segment 0; each
`{wait ANCHOR}` opens a new segment whose **base** is the absolute frame that anchor
resolves to *on a given side* (the first firing strictly after the previous segment's
base — mirroring the replay resolver). A trace op's frame is segment-relative.

Under turbo both sides tick deterministically, but a **load the port skips runs for
thousands of ticks on retail** (it plays the intro / map-load video the port fast-forwards):
on `town-map-load-rerecord` the first `LOADING_END` lands at frame **389** on the port and
**14548** on retail. So a side's per-segment *base* can differ by ~14k frames even though
the content inside each segment ticks in lockstep.

Two earlier models both failed:

1. **Single global sync anchor** (subtract one anchor's frame from both sides): only that
   one boundary lines up; everything past the load is thrown ~14k px to the right.
2. **Piecewise re-base onto the port's absolute frames** (`refFrame`, since removed): aligns
   shared anchors *when every segment resolves monotonically*, but a side with an
   **incomplete anchor stream** (retail crashed / never reached the 2nd load) has several
   segments **fall back to the same base** — they *stack*. "Last base ≤ frame" then assigns a
   firing to the **highest** stacked segment, drawing retail's capture-window content onto a
   later band (the observed overlay). It also overlaps a segment whose recorded inputs run
   *past* the next segment's base.

## The model: sequential, non-overlapping bands

Lay the segments out as **bands** on one shared axis. Band `k` occupies
`[X[k], X[k] + W[k])`; an entity in segment `k` at segment-relative frame `f` draws at
`X[k] + f` — **the same on both sides**. This is the one invariant everything else follows:

- **Shared anchors + `{wait}`-mirrored ops coincide** — both sides put segment-`k` content at
  `X[k] + f`.
- **A per-segment load-stretch collapses** to the fixed inter-band `gap` (retail's 14k-frame
  base is never used as a screen coordinate; only its *segment-relative* offsets are).
- **A genuine within-segment offset stays a gap** — an anchor that fires at `f=5` on one side
  and `f=40` on the other lands 35 frames apart inside the band.
- **Segments never overlap** — each gets its own band whether or not its wait resolved on a
  side, so a divergent/incomplete trace is fully inspectable and editable, never "frozen"
  onto one frame.

### Band origins and widths

```
X[0] = 0
X[k] = X[k-1] + W[k-1] + gap
W[k] = max(minBand, pad + max segment-relative frame any content reaches in band k,
                         taken across BOTH sides)
```

Content that sizes a band: editable items (inputs/pins), emitted (read-only) inputs, notes,
the **placed anchor chips** (per side — see placement), and, for the captured segment, the
capture-window end `capStart + capCount`. Defaults: `gap = 16`, `minBand = 8`, `pad = 4`
(layout-frames). A band is at least `minBand` wide so an empty segment stays clickable.

Widths take the **max across both sides** so each side's content fits. For a well-formed
trace the within-segment offsets match across sides, so the band is naturally sized; for a
degenerate trace it is merely wider, never wrong.

## Anchor placement (the subtle part)

Each recorded firing must display in **the band of the segment it belongs to**, found by
**walking the resolved bases**, *not* by "last base ≤ frame" (which mis-assigns when
unresolved bases stack). `resolveSide(segments, firings)` returns:

- `bases[k] = {base, ok}` — identical to `resolveBases` (mirrors the replay resolver: the
  first firing of segment `k`'s wait-anchor strictly after the previous base; unresolved ⇒
  `ok:false`, `base` = the unchanged cursor).
- `placements[i] = {seg, rel}` for each firing `i`:
  - a firing that **resolves** segment `k`'s wait sits at `{k, 0}`;
  - any other firing sits in the **latest segment whose resolved base ≤ its frame**, at
    `rel = frame − base[seg]`. Only *resolved* bases are candidates, so a firing is never
    assigned to a stacked/unresolved later segment.

Worked example — divergent `town-map-load` (retail never reached the 2nd load, so retail
segs 2/3/4 all fall back to base 11806):

| firing (retail)      | old "last base ≤ frame" | band model (`resolveSide`) |
|----------------------|-------------------------|----------------------------|
| `LOADING_END @11806` | seg 4 (wrong band)      | **seg 2** (the resolver)   |
| `HOUSE_FREEROAM @11806` | seg 4 (wrong band)   | **seg 2** (within seg 2)   |

Retail segs 3/4 then carry **no anchor chips** (retail never fired them) — the divergence is
shown honestly as empty retail lanes, while the editable inputs for those future segments
still sit in their own bands aligned with the port's.

Well-formed `town-map-load-rerecord`: both sides place every anchor in the **same** band at
the **same** rel (BOOT→0, NEW_GAME→1, LOADING_START→1, LOADING_END→2, HOUSE_FREEROAM→2,
LOADING_START→3, PAUSE_OPEN→3, LOADING_END→5), so the ~14k-frame load-stretch collapses and
the two sides align band-for-band. Note `{wait PAUSE_OPEN}` (seg 4) is **unresolved on both
sides** because PAUSE_OPEN fires the same frame as LOADING_START (not *strictly* after) — its
band is empty of anchors, which is faithful to the resolver; its inputs still get a band.

## Cursor + capture window

The captured segment `capSeg` is the segment holding the `{caprange}` op; the window is
`[capStart, capStart+capCount)` in that segment's relative frames. The SPA's global scrub
ordinal `cur` maps (via `view.locate`) to a local ordinal `k`; the editor cursor sits at
`X[capSeg] + capStart + k·cadence` (no `base_abs` needed — that is a capture detail, and is
`null` on a side whose capture failed). A click maps back: `bandAt(X, pos)` → `{seg, rel}`;
inside `capSeg` it snaps to the nearest captured frame `k = round((rel − capStart)/cadence)`,
elsewhere it clamps to the captured band (the video only exists there).

## Independence from capture health

The editor lanes are built **only** from the anchor files (`anchors.{port,retail}.jsonl`) +
the trace + notes — never from captured frames or `base_abs`. A side whose capture failed
(no video, `base_abs: null`) still aligns and edits correctly; only its video pane is empty.
The retail-capture truncation (Frida `connection-terminated` after a long load) is a separate
capture-harness issue, tracked apart from alignment.

## API (pure, golden-tested)

`align.mjs` / `segments.py` (mirrored):
- `parseSegments(ops)` → `[{waitAnchor, items:[{kind,frame,...}]}]`
- `resolveBases(segments, firings)` → `[{base, ok, anchor}]`
- `resolveSide(segments, firings)` → `{bases, placements:[{seg,rel}]}`
- `editorLayout(segments, portFirings, retailFirings, {emitted, notes, capSeg, capStart, capCount, gap, minBand, pad})`
  → `{port, retail, X, W, ext, gap}` — the whole layout in one pure call
- `bandAt(X, pos)` → `{seg, rel}` (screen → segment inverse)

`absToX`/`xToAbs`/`itemAbs`/`divergenceReport` remain for the legacy single-anchor math +
the divergence report; the editor itself runs entirely on the band model above.
