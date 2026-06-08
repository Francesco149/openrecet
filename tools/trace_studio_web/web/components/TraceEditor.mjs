// web/components/TraceEditor.mjs — the segment-band trace editor (S9: re-homed from the
// legacy /timeline.mjs into the v2 SPA, wired to the GLOBAL cursor).
//
// Layout: a fixed readable LABEL GUTTER + a horizontally-scrolling lane pane (rows
// height-matched). Per side: emitted ANCHORS + emitted INPUTS (read-only) + the
// editable TRACE — one row PER BUTTON (held intervals; click to add a press, alt-click
// to remove) + a row each for phasepin / rngseed / esc. Zoom centers on the cursor; a
// toggle limits the view to the captured window. Clicking read-only rows / anchors scrubs.
//
// THE BAND MODEL (align.editorLayout — see docs/findings/trace-editor-segment-alignment.md):
// the trace's anchor segments are laid out as sequential, NON-OVERLAPPING bands on one
// shared axis. Band k occupies [X[k], X[k]+W[k]); an entity in segment k at segment-relative
// frame f draws at X[k]+f on BOTH sides ⇒ shared anchors + {wait}-mirrored ops coincide, a
// per-segment load-stretch (retail's LOADING_END can be frame 11806 where the port's is 363)
// collapses to the fixed inter-band GAP, a genuine within-segment offset stays a gap, and
// every segment gets its OWN band whether or not its wait resolved on a side — so a divergent
// / incomplete trace is fully inspectable + editable, never piled onto one frame. Anchor
// chips are placed by `resolveSide` (the segment a firing BELONGS to, not "last base ≤
// frame"). The whole positioning is `bandPos(seg, rel) = relX(X[seg] + rel)`; the inverse is
// `bandAt(X, pos)`. Everything (items/pins/emitted/notes/cursor/capture-window) is segment-
// relative, so it is side-agnostic except the per-side anchor chips.
//
// CURSOR BRIDGE (the v2 wiring): the SPA's scrub position is the GLOBAL ordinal `cur`. We
// DERIVE the editor cursor from `cur` via view.locate (so scrubbing the filmstrip/video moves
// the timeline line) and map scrub-clicks back to `cur` (so the timeline drives the video).
// The cursor lives in the CAPTURED band: pos = X[capSeg] + capStart + k·cadence (no base_abs
// — that is a capture detail, and is null on a side whose capture failed).
//
// THE extend/edit/recapture loop is a primary workflow: tweak inputs/pins, ⇥ extend the
// captured window, then ⟳ re-capture — all without leaving the editor or re-recording.
import { html, useMemo, useRef, useState, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, editorLayout, bandAt } from "/align.mjs";
import { toast } from "/web/util.mjs";

// the button bits we draw a row for (src/input.c input_binding_mask)
const BTNROWS = [[0x04, "↑"], [0x08, "↓"], [0x02, "←"], [0x01, "→"],
                 [0x10, "Z"], [0x20, "X"], [0x40, "C"], [0x80, "V"]];
const DEFAULT_BTNS = new Set([0x04, 0x08, 0x02, 0x01, 0x10, 0x20, 0x40, 0x80]);  // ↑↓←→ Z X C V
const PRESS_LEN = 3;                                            // default added-press length
const GAP = 16, MINBAND = 8, PAD = 4;                          // band layout (match align.mjs)

function anchorCls(name) {
  if (/^LOADING/.test(name)) return "a-load";
  if (/FREEROAM/.test(name)) return "a-free";
  if (/^CONV_POSE/.test(name)) return "a-conv";
  if (/^EXTRA_SPRITE/.test(name)) return "a-fx";
  if (/^(TEXT_ANIM|DLG_LINE)/.test(name)) return "a-text";
  if (/^PAUSE/.test(name)) return "a-pause";
  return "a-misc";
}
const shortAnchor = (n) => (n || "").replace(/^EXTRA_SPRITE_/, "FX_").replace(/^CONV_POSE_/, "CP_")
  .replace(/^LOADING_/, "LD_").replace(/^TEXT_ANIM_/, "TA_").replace(/^HOUSE_FREEROAM/, "HF")
  .replace(/^FREEROAM_START/, "FREE").replace(/^DLG_LINE_/, "DL_");
const btnNames = (hex) => { const m = parseInt(hex, 16) || 0;
  return BTNROWS.filter(([b]) => m & b).map(([, n]) => n).join("") || "·"; };

const RH = 14;                                   // per-button / pin lane height
const H = { anchors: 22, inputs: 16 };

export function TraceEditor({ editTrace, onEdit, capturedOps, anchors, manifest, stale,
                             notes, onNotes, pendingBox, setPendingBox,
                             cur, setCur, view, onRecapture }) {
  const [ppf, setPpf] = useState(1.0);
  const [winOnly, setWinOnly] = useState(false);
  const [waitName, setWaitName] = useState("LOADING_END");
  const scrollRef = useRef(null);

  // locate the {caprange} op: its segment index + [start,count] (segment-relative). The
  // captured window — and the cursor — live in this segment.
  const capInfo = useMemo(() => {
    const ops = editTrace || [];
    let seg = 0;
    for (let i = 0; i < ops.length; i++) {
      const op = ops[i];
      if (op && "wait" in op) seg++;
      else if (op && "caprange" in op) return { capIdx: i, seg, start: op.caprange[0], count: op.caprange[1] };
    }
    return null;
  }, [editTrace]);

  const L = useMemo(() => {
    const segs = parseSegments(editTrace || []);
    const capSegs = parseSegments(capturedOps || []);
    // content that sizes each band: emitted (read-only) inputs + notes (items/anchors/window
    // are added inside editorLayout from segs/firings/caprange).
    const emitted = [];
    capSegs.forEach((s, k) => s.items.forEach(it => { if (it.kind === "input") emitted.push({ seg: k, frame: it.frame }); }));
    const noteExt = (notes || []).map(nt => ({ seg: nt.seg, frame: nt.frame }));
    const capSeg = capInfo ? capInfo.seg : -1;
    const lay = editorLayout(segs, anchors.port || [], anchors.retail || [], {
      emitted, notes: noteExt, capSeg,
      capStart: capInfo ? capInfo.start : 0, capCount: capInfo ? capInfo.count : 0,
      gap: GAP, minBand: MINBAND, pad: PAD,
    });
    const n = Math.max(1, segs.length);
    let lo = -PAD - 6;
    let hi = (lay.X[n - 1] ?? 0) + (lay.W[n - 1] ?? MINBAND) + PAD + 6;
    if (winOnly && capSeg >= 0) {
      lo = (lay.X[capSeg] ?? 0) + capInfo.start - 10;
      hi = (lay.X[capSeg] ?? 0) + capInfo.start + capInfo.count + 10;
    }
    // which button rows to show: defaults + any present in the trace
    const present = new Set(DEFAULT_BTNS);
    (editTrace || []).forEach(o => { if (o && "buttons" in o) { const m = parseInt(o.buttons, 16) || 0; BTNROWS.forEach(([b]) => { if (m & b) present.add(b); }); } });
    const btns = BTNROWS.filter(([b]) => present.has(b));
    return { segs, capSegs, lay, lo, hi, btns, capSeg };
  }, [editTrace, capturedOps, anchors, notes, winOnly, capInfo]);

  const { segs, capSegs, lay, lo, hi, btns, capSeg } = L;
  const X = lay.X;
  const sideLay = (side) => (side === "port" ? lay.port : lay.retail);

  // ── project the GLOBAL ordinal `cur` onto the editor's band axis ──────────────
  // view.locate gives the active gameplay segment + its local ordinal k; the editor cursor
  // sits in the CAPTURED trace band at X[capSeg] + capStart + k·cadence.
  const { seg: gseg, k: segK } = (view && view.locate) ? view.locate(cur) : { seg: null, k: 0 };
  const cadence = gseg ? gseg.cadence : 1;
  const offG = gseg ? gseg.offsetGlobal : 0;
  const segN = gseg ? gseg.nFrames : 1;
  const cursorSeg = capSeg >= 0 ? capSeg : 0;
  const cursorFrame = capSeg >= 0 ? ((capInfo ? capInfo.start : 0) + segK * cadence) : segK;
  const cursorPos = (X[cursorSeg] ?? 0) + cursorFrame;

  const relX = (pos) => (pos - lo) * ppf;
  const contentW = Math.max(400, (hi - lo) * ppf);
  const bandPos = (seg, rel) => relX((X[seg] ?? 0) + rel);

  // On open (lazy-mounted with the fold), bring the cursor (captured window) into view.
  useEffect(() => {
    const el = scrollRef.current;
    if (el) el.scrollLeft = Math.max(0, relX(cursorPos) - el.clientWidth / 2);
  }, []);  // once, on mount

  // screen x → layout position (sub-pixel; callers round as needed)
  const xToPos = (clientX) => {
    const r = scrollRef.current.getBoundingClientRect();
    return (clientX - r.left + scrollRef.current.scrollLeft) / ppf + lo;
  };
  // layout position → {seg, frame} for EDITING (clamped ≥ 0) — which band the click landed in
  const hitAt = (clientX) => { const { seg, rel } = bandAt(X, xToPos(clientX)); return { seg, frame: Math.max(0, Math.round(rel)) }; };

  // scrub: a layout position → set the global cursor, snapping INTO the captured window
  // (the video only exists there; clicks in other bands clamp to the window's near edge).
  const scrubTo = (pos) => {
    const { seg, rel } = bandAt(X, pos);
    let k;
    if (capSeg < 0) k = Math.max(0, Math.round(rel));
    else if (seg < capSeg) k = 0;
    else if (seg > capSeg) k = segN - 1;
    else k = Math.round((rel - (capInfo ? capInfo.start : 0)) / cadence);
    setCur(offG + Math.max(0, Math.min(segN - 1, k)));
  };
  const onRulerClick = (e) => scrubTo(xToPos(e.clientX));

  // zoom keeping the cursor centered in the viewport
  const zoom = (factor) => setPpf(p => {
    const np = Math.max(0.02, Math.min(20, p * factor));
    const el = scrollRef.current;
    if (el) requestAnimationFrame(() => { el.scrollLeft = (cursorPos - lo) * np - el.clientWidth / 2; });
    return np;
  });
  const scrollToBand = (k) => { const el = scrollRef.current; if (el) el.scrollLeft = Math.max(0, relX(X[k] ?? 0) - 40); };

  // ── editing: pins/esc drag, + input button add/remove via dense recompress ──
  const startDrag = (e, item, segIdx) => {
    e.stopPropagation(); e.preventDefault();
    // drag within the band → a new segment-relative frame (subtract the band origin).
    const snap = (editTrace || []).slice();
    const move = (ev) => {
      const f = Math.max(0, Math.round(xToPos(ev.clientX) - (X[segIdx] ?? 0)));
      const op = { ...snap[item.idx] };
      if (item.kind === "phasepin") op.phasepin = f;
      else if (item.kind === "rngseed") op.rngseed = [f, op.rngseed[1]];
      else if (item.kind === "esc") op.esc = f;
      const next = snap.slice(); next[item.idx] = op; onEdit(next);
    };
    const up = () => { document.removeEventListener("pointermove", move); document.removeEventListener("pointerup", up); };
    document.addEventListener("pointermove", move); document.addEventListener("pointerup", up);
  };
  const delItem = (item) => { const next = (editTrace || []).slice(); next.splice(item.idx, 1); onEdit(next); };

  // ── place a {wait ANCHOR} at the cursor: split that segment, re-base everything
  // after the cursor (inputs/pins/esc/caprange/calltrace) to the new anchor. This is
  // how you build determinism incrementally — e.g. a {wait LOADING_END} after the Z
  // press so both sides resume in lock-step when the load completes. ──
  const opFrame = (op) => {
    if ("frame" in op && "buttons" in op) return op.frame;
    if ("phasepin" in op) return op.phasepin;
    if ("rngseed" in op) return op.rngseed[0];
    if ("esc" in op) return op.esc;
    if ("caprange" in op) return op.caprange[0];
    if ("calltrace" in op) return op.calltrace[0];
    return null;
  };
  const rebaseOp = (op, d) => {
    if ("frame" in op && "buttons" in op) return { ...op, frame: op.frame + d };
    if ("phasepin" in op) return { phasepin: op.phasepin + d };
    if ("rngseed" in op) return { rngseed: [op.rngseed[0] + d, op.rngseed[1]] };
    if ("esc" in op) return { esc: op.esc + d };
    if ("caprange" in op) return { caprange: [Math.max(0, op.caprange[0] + d), op.caprange[1]] };
    if ("calltrace" in op) return { calltrace: [Math.max(0, op.calltrace[0] + d), op.calltrace[1]] };
    return op;
  };
  const addWaitAnchor = (name) => {
    if (!name) return;
    const seg = cursorSeg, F = cursorFrame;
    const out = []; let curSeg = 0, done = false;
    for (const op of (editTrace || [])) {
      if (op && "wait" in op) {
        if (curSeg === seg && !done) { out.push({ wait: name }); done = true; }
        curSeg++; out.push(op); continue;
      }
      if (curSeg === seg && !done) {
        const f = opFrame(op);
        if (f != null && f >= F) { out.push({ wait: name }); done = true; out.push(rebaseOp(op, -F)); continue; }
      } else if (curSeg === seg && done) {
        out.push(rebaseOp(op, -F)); continue;
      }
      out.push(op);
    }
    if (!done) out.push({ wait: name });
    onEdit(out);
  };
  const addAtCursor = (kind) => {
    const sg = cursorSeg, f = cursorFrame;
    const op = kind === "phasepin" ? { phasepin: f } : { rngseed: [f, 19937] };
    const next = (editTrace || []).slice();
    let idx = (next[0] && "savefile" in next[0]) ? 1 : 0;
    if (sg > 0) { let wc = 0; for (let i = 0; i < next.length; i++) { if (next[i] && "wait" in next[i]) { wc++; if (wc === sg) { idx = i + 1; break; } } } }
    next.splice(idx, 0, op); onEdit(next);
  };

  // ── notes (sidecar: {seg, frame, text, box?}) — band-aligned annotations ─────
  const addNote = () => {
    const text = prompt("note" + (pendingBox ? " (crop attached)" : "") + ":"); if (!text) return;
    const note = { seg: cursorSeg, frame: cursorFrame, text }; if (pendingBox) note.box = pendingBox;
    onNotes([...(notes || []), note]); if (setPendingBox) setPendingBox(null);
  };
  const delNote = (i) => { const n = (notes || []).slice(); n.splice(i, 1); onNotes(n); };

  // ── capture-window bands. The GREEN band is the ACTUALLY-captured window (manifest
  // caprange) in the captured segment; the DASHED band is the live-edited {caprange} from
  // editTrace. Both in band-layout space (X[capSeg] + rel). `leaked` warns about markers in
  // the captured segment past the edited window END — reachable by growing the duration (a
  // warning only, never a clamp; future segments are separate captures, not leaks). ──
  const capEdit = (() => {
    if (!capInfo || capSeg < 0) return null;
    const base = X[capSeg] ?? 0;
    const m = (manifest && manifest.caprange) || [capInfo.start, capInfo.count];
    const winL = base + m[0], winR = base + m[0] + m[1];                 // captured (green)
    const editL = base + capInfo.start, editR = base + capInfo.start + capInfo.count;  // edited (dashed)
    const end = capInfo.start + capInfo.count;
    let leaked = 0;
    (segs[capSeg] ? segs[capSeg].items : []).forEach(it => { if (it.frame > end) leaked++; });
    (notes || []).forEach(nt => { if (nt.seg === capSeg && nt.frame > end) leaked++; });
    return { winL, winR, editL, editR, leaked, pending: capInfo.start !== m[0] || capInfo.count !== m[1] };
  })();

  // apply a {caprange} mutation; keep {calltrace} aligned (same start/len deltas).
  const editCapOp = (mut) => {
    if (!capInfo) { toast("this trace has no {caprange} to edit", true); return; }
    const next = (editTrace || []).slice();
    const [s, c] = next[capInfo.capIdx].caprange;
    const { ns, nc } = mut(s, c);
    if (ns === s && nc === c) { toast("at the edge — start ≥ 0, length ≥ 1", true); return; }
    next[capInfo.capIdx] = { caprange: [ns, nc] };
    const ti = next.findIndex(o => o && "calltrace" in o);
    if (ti >= 0) {
      const [cs, cc] = next[ti].calltrace;
      next[ti] = { calltrace: [Math.max(0, cs + (ns - s)), Math.max(1, cc + (nc - c))] };
    }
    onEdit(next);
  };
  // trace DURATION: move the captured END (length ≥ 1). Orphaned markers are the ⚠ warning.
  const bumpDuration = (d) => editCapOp((s, c) => ({ ns: s, nc: Math.max(1, c + d) }));
  // capture WINDOW: slide the whole window (length fixed; start ≥ 0).
  const slideWindow = (d) => editCapOp((s, c) => ({ ns: Math.max(0, s + d), nc: c }));

  // ── lane builders (everything via bandPos) ───────────────────────────────────
  const segInputs = (k) => (segs[k] ? segs[k].items : []).filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
  const denseOf = (k, maxF) => {
    const ins = segInputs(k); const arr = new Array(maxF + 1).fill(0); let st = 0, p = 0;
    for (let f = 0; f <= maxF; f++) { while (p < ins.length && ins[p].frame <= f) { st = parseInt(ins[p].buttons, 16) || 0; p++; } arr[f] = st; }
    return arr;
  };
  const writeDense = (k, arr) => {
    const ops = []; let prev = -1;
    for (let f = 0; f < arr.length; f++) if (arr[f] !== prev) { ops.push({ frame: f, buttons: "0x" + (arr[f] >>> 0).toString(16).padStart(4, "0") }); prev = arr[f]; }
    const old = new Set(segInputs(k).map(i => i.idx));
    const kept = (editTrace || []).filter((_, i) => !old.has(i));
    let at = (kept[0] && "savefile" in kept[0]) ? 1 : 0;
    if (k > 0) { let wc = 0; for (let i = 0; i < kept.length; i++) { if (kept[i] && "wait" in kept[i]) { wc++; if (wc === k) { at = i + 1; break; } } } }
    kept.splice(at, 0, ...ops); onEdit(kept);
  };
  const onBtnRow = (e, button) => {
    e.stopPropagation();
    const { seg, frame } = hitAt(e.clientX);
    const ins = segInputs(seg); const maxF = Math.max(frame + PRESS_LEN, ...ins.map(i => i.frame), 0) + 1;
    const arr = denseOf(seg, maxF);
    if (e.altKey && (arr[frame] & button)) {          // remove the held interval around frame
      let a = frame; while (a > 0 && (arr[a - 1] & button)) a--;
      let b = frame; while (b < arr.length && (arr[b] & button)) b++;
      for (let i = a; i < b; i++) arr[i] &= ~button;
    } else {                                          // add a press
      for (let i = frame; i < frame + PRESS_LEN; i++) arr[i] |= button;
    }
    writeDense(seg, arr);
  };

  // segment-band dividers + the wait anchor that opens each band (legibility: the GAP after a
  // band is where a load-stretch / cross-side base shift is collapsed).
  const segMarks = () => segs.map((s, k) => k === 0 ? null : html`
    <div class="tl-segdiv" style="left:${relX(X[k] ?? 0)}px"></div>
    <div class="tl-seglbl" style="left:${relX(X[k] ?? 0)}px" key=${k}>${shortAnchor(s.waitAnchor)}</div>`);

  // emitted anchor chips, placed by resolveSide (the band a firing BELONGS to).
  const anchorChips = (side) => {
    const place = sideLay(side).placements;
    return (anchors[side] || []).map((f, i) => {
      const p = place[i] || { seg: 0, rel: 0 };
      return html`<div class=${"chip " + anchorCls(f.anchor)} style="left:${bandPos(p.seg, p.rel)}px"
        data-full=${`${f.anchor} @${f.frame} · seg${p.seg}+${p.rel}`}
        onClick=${(e) => { e.stopPropagation(); scrubTo((X[p.seg] ?? 0) + p.rel); }}
        key=${i}>${shortAnchor(f.anchor)}</div>`;
    });
  };

  // emitted (read-only) combined input spans — segment-relative (same on both sides).
  const emittedSpans = () => {
    const out = [];
    capSegs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      ins.forEach((it, j) => {
        const x0 = bandPos(k, it.frame);
        const nx = ins[j + 1]; const x1 = nx ? bandPos(k, nx.frame) : x0 + 4 * ppf;
        const lbl = btnNames(it.buttons);
        if (lbl !== "·") out.push(html`<div class="span ro" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" title=${lbl} key=${`${k}-${j}`}>${lbl}</div>`);
      });
    });
    return out;
  };

  // editable per-button held-interval bars (segment-relative).
  const btnBars = (button) => {
    const out = [];
    segs.forEach((s, k) => {
      const arr = denseOf(k, Math.max(0, ...segInputs(k).map(i => i.frame)) + 1);
      let f = 0;
      while (f < arr.length) {
        if (arr[f] & button) { let e = f; while (e < arr.length && (arr[e] & button)) e++;
          const x0 = bandPos(k, f), x1 = bandPos(k, e);
          out.push(html`<div class="bar" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" key=${`${k}-${f}`}></div>`); f = e; }
        else f++;
      }
    });
    return out;
  };

  const pinItems = (kind) => {
    const out = [];
    segs.forEach((s, k) => s.items.forEach((it, j) => {
      if (it.kind !== kind) return;
      const x = bandPos(k, it.frame);
      const cls = kind === "phasepin" ? "pp" : kind === "rngseed" ? "rp" : "esc";
      const glyph = kind === "phasepin" ? "⟲" : kind === "rngseed" ? "🎲" : "⎋";
      out.push(html`<div class=${"pin ed " + cls} style="left:${x}px"
        title=${`${kind} seg${k}+${it.frame}${it.value != null ? "=" + it.value : ""} · drag / alt-click delete`}
        onPointerDown=${(e) => startDrag(e, it, k)}
        onClick=${(e) => { e.stopPropagation(); if (e.altKey) delItem(it); }}
        key=${`${k}${j}`}>${glyph}</div>`);
    }));
    return out;
  };

  const noteMarks = () => (notes || []).map((nt, i) => {
    const x = bandPos(nt.seg, nt.frame);
    return html`<div class="pin note" style="left:${x}px"
      title=${`${nt.text}${nt.box ? " · crop " + nt.box.join(",") : ""} · seg${nt.seg}+${nt.frame} · click=scrub · alt-click=delete`}
      onClick=${(e) => { e.stopPropagation(); if (e.altKey) delNote(i); else scrubTo((X[nt.seg] ?? 0) + nt.frame); }}
      key=${i}>📝</div>`;
  });

  // ── build the flat row list: [label, heightPx, side, vnode] ───────────────────
  const sideRows = (side) => {
    const rows = [
      ["anchors", H.anchors, side, html`<div class="tl-row anchors" style="height:${H.anchors}px">${anchorChips(side)}</div>`],
      ["inputs", H.inputs, side, html`<div class="tl-row ro" style="height:${H.inputs}px">${emittedSpans()}</div>`],
    ];
    for (const [b, name] of btns)
      rows.push([name, RH, side, html`<div class="tl-row btn" style="height:${RH}px" onClick=${(e) => onBtnRow(e, b)} title="click to add a press · alt-click to remove">${btnBars(b)}</div>`]);
    rows.push(["phasepin", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("phasepin")}</div>`]);
    rows.push(["rngseed", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("rngseed")}</div>`]);
    rows.push(["esc", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("esc")}</div>`]);
    return rows;
  };
  const rows = [...sideRows("retail"), ...sideRows("port"),
    ["📝 notes", RH, "note", html`<div class="tl-row notes" style="height:${RH}px">${noteMarks()}</div>`]];

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="legend"><span class="sw s-retail"></span>retail <span class="sw s-port"></span>port</span>
      <span class="sep">·</span>
      <span class="dim">jump:</span>
      ${segs.map((s, k) => html`<button class=${"seg " + (cursorSeg === k ? "on" : "")}
        onClick=${() => scrollToBand(k)} title="scroll to this segment's band" key=${k}>${k === 0 ? "boot" : shortAnchor(s.waitAnchor)}</button>`)}
      <span class="sep">·</span>
      <button class=${"seg " + (winOnly ? "on" : "")} onClick=${() => setWinOnly(v => !v)}
        title="limit the view to the captured window">⊞ window-only</button>
      <span class="sep">·</span>
      ${capInfo ? html`<span class="dim">window</span>
        <span class="capctl" title="capture-window DURATION — its LENGTH (moves the END). Fine ±1/±10, coarse to ±120. The dashed band shows it live; ⟳ re-capture to apply.">
          <span class="dim">len</span>
          ${[-120, -60, -30, -10, -1].map(d => html`<button class="seg" onClick=${() => bumpDuration(d)} key=${d}>${d}</button>`)}
          <span class="capnum">${capInfo.count}f</span>
          ${capEdit && capEdit.leaked > 0 && html`<span class="capwarn" title=${`${capEdit.leaked} marker(s) past the window END — extend the duration to include them`}>⚠${capEdit.leaked}</span>`}
          ${[1, 10, 30, 60, 120].map(d => html`<button class="seg" onClick=${() => bumpDuration(d)} key=${d}>+${d}</button>`)}
        </span>
        <span class="capctl" title="capture-window POSITION — slides the whole window (start+end together, length fixed). The dashed band moves live; ⟳ re-capture to apply.">
          <span class="dim">pos</span>
          ${[-30, -10, -1].map(d => html`<button class="seg" onClick=${() => slideWindow(d)} key=${d}>${d}</button>`)}
          <span class="capnum">@${capInfo.start}</span>
          ${[1, 10, 30].map(d => html`<button class="seg" onClick=${() => slideWindow(d)} key=${d}>+${d}</button>`)}
        </span>
        ${capEdit && capEdit.pending && html`<span class="cappend" title="pending window edit — not captured yet">✎ pending</span>`}`
        : html`<span class="dim">no {caprange}</span>`}
      <span class="sep">·</span><span class="dim">add@cursor:</span>
      <button class="seg" onClick=${() => addAtCursor("phasepin")} title="add a phasepin at the cursor">+⟲</button>
      <button class="seg" onClick=${() => addAtCursor("rngseed")} title="add an rngseed at the cursor">+🎲</button>
      <button class="seg" onClick=${addNote} title="add a note at the cursor (attaches a video crop if you box-selected one)">+📝</button>
      <span class="sep">·</span>
      <select class="seg" value=${waitName} onChange=${e => setWaitName(e.target.value)} title="anchor to wait on">
        ${["LOADING_END", "HOUSE_FREEROAM", "FREEROAM_START", "NEW_GAME", "LOADING_START",
           "PAUSE_OPEN", "PAUSE_CLOSE", "TITLE_RETURN", "TEXT_ANIM_END", "CONV_POSE_END"].map(a =>
          html`<option value=${a}>${a}</option>`)}
      </select>
      <button class="seg" onClick=${() => addWaitAnchor(waitName)}
        title="insert {wait} at the cursor — splits the segment + re-bases what follows to this anchor">⚓ +wait</button>
      <span class="spacer"></span>
      ${stale && html`<span class="stale-dot">● edits not captured</span>`}
      ${onRecapture && html`<button class="seg recap" onClick=${onRecapture}
        title="re-capture the working trace with the current edits, then reload">⟳ re-capture</button>`}
      <span class="dim">zoom</span>
      <button onClick=${() => zoom(1 / 1.6)}>−</button>
      <span class="dim">${ppf.toFixed(2)}px/f</span>
      <button onClick=${() => zoom(1.6)}>+</button>
      <button onClick=${() => setPpf(1)}>1:1</button>
    </div>
    <div class="tl-body">
      <div class="tl-gutter">
        ${rows.map(([label, h, side], i) => html`<div class=${"gut-row s-" + side} style="height:${h}px" key=${i}>
          <span class="side-dot"></span>${label}</div>`)}
      </div>
      <div class="tl-scroll" ref=${scrollRef}
        onWheel=${e => { if (e.shiftKey) { e.preventDefault(); scrollRef.current.scrollLeft += e.deltaY; } }}>
        <div class="tl-content" style="width:${contentW}px" onClick=${onRulerClick}>
          ${segMarks()}
          ${capEdit && html`<div class="tl-window" style="left:${relX(capEdit.winL)}px;width:${Math.max(2, (capEdit.winR - capEdit.winL) * ppf)}px"></div>`}
          ${capEdit && html`<div class=${"tl-editwin" + (capEdit.pending ? " pending" : "")}
            style="left:${relX(capEdit.editL)}px;width:${Math.max(2, (capEdit.editR - capEdit.editL) * ppf)}px"
            title="edited capture window — ⟳ re-capture to apply"></div>`}
          <div class="tl-cursor" style="left:${relX(cursorPos)}px"></div>
          ${rows.map(([, , side, lanes], i) => html`<div class=${"tl-grp s-" + side} key=${i}>${lanes}</div>`)}
        </div>
      </div>
    </div>
    <div class="hint">click an anchor / read-only row / ruler to scrub · click a button row to add a press, alt-click to remove · drag pins · shift+wheel pan · cursor = seg${cursorSeg}+${cursorFrame} · ordinal f${cur}${cadence > 1 ? ` (k${segK}×${cadence})` : ""}</div>
  </div>`;
}
