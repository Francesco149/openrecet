// align.mjs — PURE alignment core for the timeline editor. No DOM, no imports.
// The verification workhorse leans on this, so it's kept pure + tested (align.test.mjs).
//
// A trace is anchor-segmented: ops before the 1st {wait} are segment 0 (base frame 0);
// each {wait ANCHOR} opens a new segment whose base is the absolute frame that anchor
// RESOLVES to on a given side (next firing strictly after the previous segment's base —
// mirroring the replay resolver). A trace op's frame is relative to its segment base.
//
// THE TIMELINE MODEL (the workhorse) is the CAPTURED-FRAME INDEX: the editor x-axis is the
// dense captured-frame ordinal (the MP4 / scrub position), per side. Because the capture is
// phase-synced + RNG-pinned and runs 1:1 on both sides, the n-th captured frame is the SAME
// logical moment on each side — so placing each side's anchors + inputs at their captured
// index makes a 1:1 capture align with NO forcing; where the traces diverge (different frame
// counts) the two sides simply drift apart. Loads are suppressed (0 captured frames) so the
// index re-syncs across every load boundary regardless of how stretched the load is. The
// abs↔index map is `capIndexOfAbs` / `absOfCapIndex` (below); segment bases (`resolveBases`)
// map a trace op's (segment, frame) to an absolute frame. See
// docs/findings/trace-editor-segment-alignment.md.
//
// (sideLayout/absToX/xToAbs/divergenceReport are the older single-sync-anchor helpers, kept
// for the divergence report + the golden cross-check.)

// ─── parse a trace (array of op objects, in file order) into segments ─────────
// Returns [{ waitAnchor: string|null, items: [{kind, frame, idx, op}] }].
//   kind ∈ "input" | "phasepin" | "rngseed" | "esc"   (frame = segment-relative)
export function parseSegments(ops) {
  const segs = [{ waitAnchor: null, items: [] }];
  ops.forEach((op, idx) => {
    if (op == null || typeof op !== "object") return;
    if ("wait" in op) { segs.push({ waitAnchor: op.wait, items: [] }); return; }
    const seg = segs[segs.length - 1];
    if ("frame" in op && "buttons" in op)
      seg.items.push({ kind: "input", frame: +op.frame, buttons: op.buttons, idx, op });
    else if ("phasepin" in op)
      seg.items.push({ kind: "phasepin", frame: +op.phasepin, idx, op });
    else if ("rngseed" in op && Array.isArray(op.rngseed))
      seg.items.push({ kind: "rngseed", frame: +op.rngseed[0], value: op.rngseed[1], idx, op });
    else if ("esc" in op)
      seg.items.push({ kind: "esc", frame: +op.esc, idx, op });
    // caprange/calltrace/savefile/wait_until/gframe/poke → not drawn as items
  });
  return segs;
}

// ─── resolve each segment's base absolute frame from one side's anchor firings ─
// firings: ordered [{anchor, frame}]. Returns [{base:number|null, ok:bool, anchor}].
// Segment 0 → base 0. Segment k>0 → first firing of its waitAnchor with frame > cursor.
// Unresolved (the divergence: that side never fired the anchor after the cursor) → ok:false,
// base = cursor (so following segments stay placed, just flagged).
export function resolveBases(segments, firings) {
  const out = [];
  let cursor = 0;
  for (let k = 0; k < segments.length; k++) {
    if (k === 0) { out.push({ base: 0, ok: true, anchor: null }); continue; }
    const name = segments[k].waitAnchor;
    const hit = firings.find(f => f.anchor === name && f.frame > cursor);
    if (hit) { out.push({ base: hit.frame, ok: true, anchor: name }); cursor = hit.frame; }
    else { out.push({ base: cursor, ok: false, anchor: name }); }
  }
  return out;
}

// ─── per-side layout for a chosen sync anchor (legacy single-sync helper) ─────
export function sideLayout(segments, firings, syncSeg) {
  const bases = resolveBases(segments, firings);
  const k = (syncSeg == null) ? bases.length - 1 : syncSeg;
  const syncFrame = bases[k] ? bases[k].base : 0;
  return { bases, syncFrame };
}

// absolute engine frame → screen x (px). xZero = where syncFrame sits (px), default 0.
export function absToX(abs, syncFrame, pxPerFrame, xZero = 0) {
  return xZero + (abs - syncFrame) * pxPerFrame;
}
export function xToAbs(x, syncFrame, pxPerFrame, xZero = 0) {
  return Math.round((x - xZero) / pxPerFrame) + syncFrame;
}

// item segment-relative frame → absolute on a side (base + frame).
export function itemAbs(item, segIdx, bases) {
  const b = bases[segIdx];
  return (b ? b.base : 0) + item.frame;
}

// ─── load spans: LOADING_START→LOADING_END pairs (absolute frames) ────────────
// One side's firings → [{start, end}] for each completed load. A load captures ZERO frames
// (suppressed), so its [start,end) is subtracted when mapping abs→captured-index. A dangling
// LOADING_START (no END) is dropped (no completed span).
export function loadSpans(firings) {
  const spans = [];
  let start = null;
  for (const f of firings) {
    if (f.anchor === "LOADING_START") start = f.frame;
    else if (f.anchor === "LOADING_END" && start != null) { spans.push({ start, end: f.frame }); start = null; }
  }
  return spans;
}

// ─── absolute engine frame → DENSE captured-frame index (THE timeline axis) ───
// The capture began at `baseAbs`; each load in `loads` is suppressed (0 captured frames) so
// its frames are subtracted. capIndex = (abs − baseAbs) − (suppressed load frames strictly
// before abs). Frames before baseAbs come out negative (off the captured axis). THE alignment
// property: in a 1:1 region both sides' corresponding frames get the SAME index (identical
// gameplay ⇒ identical per-side counts, and suppressed loads re-sync the index at every load
// boundary regardless of load stretch) ⇒ they align with no forcing; on divergence they
// drift. Only loads at/after baseAbs count (a load BEFORE the window produced no captured
// frames in it).
export function capIndexOfAbs(abs, baseAbs, loads) {
  let suppressed = 0;
  for (const ld of loads) {
    if (ld.start < baseAbs) continue;
    if (abs >= ld.end) suppressed += ld.end - ld.start;
    else if (abs > ld.start) suppressed += abs - ld.start;
  }
  return abs - baseAbs - suppressed;
}

// ─── inverse: a dense captured index → the absolute engine frame ──────────────
// Walk the captured frames from baseAbs, skipping each suppressed load — for click-to-edit
// (timeline index → the trace's absolute frame → segment+frame). A negative index returns a
// frame before baseAbs (caller clamps).
export function absOfCapIndex(g, baseAbs, loads) {
  const within = loads.filter(ld => ld.start >= baseAbs).slice().sort((a, b) => a.start - b.start);
  let abs = baseAbs, remaining = g;
  for (const ld of within) {
    const until = ld.start - abs;                 // captured frames from abs to the load start
    if (remaining < until) return abs + remaining;
    remaining -= until;
    abs = ld.end;                                 // skip the load (0 captured frames)
  }
  return abs + remaining;
}

// ─── distinct anchor names present, for the sync-anchor picker ───────────────
export function anchorNames(...firingLists) {
  const s = new Set();
  for (const list of firingLists) for (const f of list) s.add(f.anchor);
  return [...s];
}

// ─── divergence report: which waits resolved on each side + their offsets ────
// Returns [{seg, anchor, portBase, retailBase, portOk, retailOk, deltaFromSync}].
export function divergenceReport(segments, portFirings, retailFirings, syncSeg) {
  const p = sideLayout(segments, portFirings, syncSeg);
  const r = sideLayout(segments, retailFirings, syncSeg);
  return segments.map((s, k) => ({
    seg: k, anchor: s.waitAnchor,
    portBase: p.bases[k].base, retailBase: r.bases[k].base,
    portOk: p.bases[k].ok, retailOk: r.bases[k].ok,
    // anchor-relative position on each side (frames from sync); differ ⇒ divergence
    portRel: p.bases[k].base - p.syncFrame,
    retailRel: r.bases[k].base - r.syncFrame,
  }));
}
