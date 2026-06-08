// align.mjs — PURE alignment core for the timeline editor. No DOM, no imports.
// The verification workhorse leans on this, so it's kept pure + tested (align.test.mjs).
//
// A trace is anchor-segmented: ops before the 1st {wait} are segment 0 (base frame 0);
// each {wait ANCHOR} opens a new segment whose base is the absolute frame that anchor
// RESOLVES to on a given side (next firing strictly after the previous segment's base —
// mirroring the replay resolver). A trace op's frame is relative to its segment base.
//
// The timeline x-axis is "frames relative to a chosen SYNC anchor": both sides count
// from the sync anchor's firing, so shared anchors + mirrored ops line up and a divergent
// anchor (fired at a different offset, or not at all) shows as a horizontal gap.

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

// ─── per-side layout for a chosen sync anchor ────────────────────────────────
// syncSeg = the segment index to anchor the view on (default: last segment). The sync
// frame for a side = that segment's resolved base. Returns { bases, syncFrame }.
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

// ─── resolve ONE side: segment bases AND a display PLACEMENT for every firing ──
// resolveBases gives each segment's base; this ALSO returns, per firing, the (seg, rel) its
// chip should DISPLAY at — the segment it BELONGS to, found by walking the RESOLVED bases,
// NOT by "last base ≤ frame" (which mis-assigns a firing to the highest of several
// segments that fall back to the SAME base when a side's anchor stream is incomplete). A
// firing that resolves segment k's wait sits at {seg:k, rel:0}; any other firing sits in the
// latest segment whose RESOLVED base ≤ its frame, at rel = frame − that base. So a divergent
// trace still places every chip in a sensible band instead of piling them onto one frame.
// The single walk produces bases identical to resolveBases (asserted in the tests).
export function resolveSide(segments, firings) {
  const bases = [];
  const resolverSeg = new Map();        // firing index → the segment it resolved
  let cursor = 0;
  for (let k = 0; k < segments.length; k++) {
    if (k === 0) { bases.push({ base: 0, ok: true, anchor: null }); continue; }
    const name = segments[k].waitAnchor;
    const i = firings.findIndex(f => f.anchor === name && f.frame > cursor);
    if (i >= 0) {
      bases.push({ base: firings[i].frame, ok: true, anchor: name });
      cursor = firings[i].frame;
      resolverSeg.set(i, k);
    } else {
      bases.push({ base: cursor, ok: false, anchor: name });
    }
  }
  // resolved bases in segment order (monotonic) — the candidates for non-resolver firings.
  const resolved = [];
  for (let k = 0; k < bases.length; k++) if (bases[k].ok) resolved.push({ seg: k, base: bases[k].base });
  const placements = firings.map((f, i) => {
    if (resolverSeg.has(i)) return { seg: resolverSeg.get(i), rel: 0 };
    let seg = 0;
    for (const r of resolved) if (r.base <= f.frame) seg = r.seg;
    return { seg, rel: f.frame - bases[seg].base };
  });
  return { bases, placements };
}

// ─── THE alignment system: lay segments out as sequential, NON-OVERLAPPING bands ──
// Band k occupies [X[k], X[k]+W[k]); an entity in segment k at seg-relative frame f draws at
// X[k]+f on BOTH sides ⇒ shared anchors + {wait}-mirrored ops coincide, a per-segment
// load-stretch collapses to the fixed inter-band `gap` (a side's huge base is NEVER a screen
// coord — only its seg-relative offsets are), a within-segment offset stays a gap, and every
// segment gets its own band whether or not its wait resolved on a side (so a divergent /
// incomplete trace is inspectable, never frozen onto one frame). Width fits the widest
// content in the band across BOTH sides. All inputs explicit + side-effect-free (golden-
// tested). See docs/findings/trace-editor-segment-alignment.md.
export function editorLayout(segments, portFirings, retailFirings, opts = {}) {
  const { emitted = [], notes = [], capSeg = -1, capStart = 0, capCount = 0,
          gap = 16, minBand = 8, pad = 4 } = opts;
  const port = resolveSide(segments, portFirings);
  const retail = resolveSide(segments, retailFirings);
  const n = segments.length;
  const ext = new Array(n).fill(0);
  const bump = (seg, rel) => { if (seg >= 0 && seg < n && rel > ext[seg]) ext[seg] = rel; };
  segments.forEach((s, k) => s.items.forEach(it => bump(k, it.frame)));
  emitted.forEach(e => bump(e.seg, e.frame));
  notes.forEach(nt => bump(nt.seg, nt.frame));
  port.placements.forEach(p => bump(p.seg, p.rel));
  retail.placements.forEach(p => bump(p.seg, p.rel));
  if (capSeg >= 0) bump(capSeg, capStart + capCount);
  const W = ext.map(e => Math.max(minBand, e + pad));
  const X = [];
  let x = 0;
  for (let k = 0; k < n; k++) { X.push(x); x += W[k] + gap; }
  return { port, retail, X, W, ext, gap };
}

// layout position → {seg, rel}: the band whose origin is the largest ≤ pos (screen→segment
// inverse, for click-to-edit / click-to-scrub). Positions left of band 0 clamp to seg 0.
export function bandAt(X, pos) {
  let seg = 0;
  for (let k = 0; k < X.length; k++) if (X[k] <= pos) seg = k;
  return { seg, rel: pos - (X[seg] ?? 0) };
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
