// web/components/TraceEditor.mjs — the captured-frame-index trace editor (wired to the v2
// SPA + the GLOBAL cursor).
//
// THE MODEL (see docs/findings/trace-editor-segment-alignment.md): the x-axis is the DENSE
// captured-frame index — one tick per real frame of the trace running on that side. Because
// the capture is phase-synced + RNG-pinned and runs 1:1 on both sides, the n-th captured
// frame is the SAME logical moment on each side, so placing each side's anchors + inputs at
// their captured index makes a 1:1 capture align with ZERO forcing logic. Where the traces
// diverge (different per-side frame counts) the two rows simply drift apart — that IS the
// divergence; you iterate edits until they re-converge. Loads are suppressed (0 captured
// frames) so the index re-syncs at every load boundary regardless of how stretched a load is.
//
// Mapping: a side's absolute engine frame → captured index = align.capIndexOfAbs(abs,
// base_abs, loads); a trace op's (segment, frame) → absolute via the segment base
// (resolveBases). The inverse (click index → trace frame) is absOfCapIndex. NO segment bands,
// NO forced alignment — the alignment is inherent in the 1:1 capture.
//
// Layout: a fixed LABEL GUTTER + a horizontally-scrolling lane pane. Per side: emitted
// ANCHORS + emitted INPUTS (read-only) + the editable TRACE (one row per button, held
// intervals; click to add a press, alt-click to remove) + phasepin / rngseed / esc rows.
// Clicking a read-only row / anchor / ruler scrubs the shared cursor.
import { html, useMemo, useRef, useState, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, resolveBases, loadSpans, capIndexOfAbs, absOfCapIndex, itemAbs } from "/align.mjs";
import { toast } from "/web/util.mjs";

// the button bits we draw a row for (src/input.c input_binding_mask)
const BTNROWS = [[0x04, "↑"], [0x08, "↓"], [0x02, "←"], [0x01, "→"],
                 [0x10, "Z"], [0x20, "X"], [0x40, "C"], [0x80, "V"]];
const DEFAULT_BTNS = new Set([0x04, 0x08, 0x02, 0x01, 0x10, 0x20, 0x40, 0x80]);  // ↑↓←→ Z X C V
const PRESS_LEN = 3;                                            // default added-press length

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
  const [ppf, setPpf] = useState(2.0);
  const [waitName, setWaitName] = useState("LOADING_END");
  const scrollRef = useRef(null);

  // locate the {caprange} op (its segment + [start,count]) — drives the len/pos edit controls.
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
    // per side: base_abs + loads + segment bases (for the trace) + captured-input bases.
    const mkSide = (firings, baseAbs, nFrames) => {
      const f = firings || [];
      const loads = loadSpans(f);
      return {
        firings: f, baseAbs, nFrames, loads,
        bases: resolveBases(segs, f),
        capBases: resolveBases(capSegs, f),
        ci: (abs) => (baseAbs == null ? null : capIndexOfAbs(abs, baseAbs, loads)),
      };
    };
    const port = mkSide(anchors.port, manifest?.port?.base_abs, manifest?.n_frames || 0);
    const retail = mkSide(anchors.retail, manifest?.retail?.base_abs, manifest?.n_frames_retail || 0);
    const maxN = Math.max(port.nFrames, retail.nFrames, 1);
    // edit reference: the side that maps a clicked index back to a trace (seg, frame).
    const editSide = port.baseAbs != null ? port : (retail.baseAbs != null ? retail : null);
    // button rows: defaults + any present in the trace
    const present = new Set(DEFAULT_BTNS);
    (editTrace || []).forEach(o => { if (o && "buttons" in o) { const m = parseInt(o.buttons, 16) || 0; BTNROWS.forEach(([b]) => { if (m & b) present.add(b); }); } });
    const btns = BTNROWS.filter(([b]) => present.has(b));
    return { segs, capSegs, port, retail, maxN, editSide, btns };
  }, [editTrace, capturedOps, anchors, manifest]);

  const { segs, capSegs, port, retail, maxN, editSide, btns } = L;
  const sideOf = (s) => (s === "port" ? port : retail);

  // the captured frame count of the scrub axis (the cursor's range).
  const segN = (view && view.totalFrames) || manifest?.n_frames || maxN || 1;
  const lo = -8, hi = maxN + 8;
  const relX = (g) => (g - lo) * ppf;
  const contentW = Math.max(400, (hi - lo) * ppf);

  // bring the cursor into view on open.
  useEffect(() => {
    const el = scrollRef.current;
    if (el) el.scrollLeft = Math.max(0, relX(cur) - el.clientWidth / 2);
  }, []);  // once, on mount

  // screen x → captured index (rounded).
  const xToG = (clientX) => {
    const r = scrollRef.current.getBoundingClientRect();
    return Math.round((clientX - r.left + scrollRef.current.scrollLeft) / ppf) + lo;
  };
  // a captured index → a trace (seg, frame) on the edit side (inverse of input placement).
  const gToTrace = (g) => {
    if (!editSide) return { seg: 0, frame: Math.max(0, g) };
    const abs = absOfCapIndex(g, editSide.baseAbs, editSide.loads);
    let seg = 0; for (let k = 0; k < editSide.bases.length; k++) if (editSide.bases[k].ok && editSide.bases[k].base <= abs) seg = k;
    return { seg, frame: Math.max(0, abs - editSide.bases[seg].base) };
  };
  const scrubTo = (g) => setCur(Math.max(0, Math.min(segN - 1, g)));
  const onRulerClick = (e) => scrubTo(xToG(e.clientX));

  // zoom keeping the cursor centered.
  const zoom = (factor) => setPpf(p => {
    const np = Math.max(0.05, Math.min(40, p * factor));
    const el = scrollRef.current;
    if (el) requestAnimationFrame(() => { el.scrollLeft = (cur - lo) * np - el.clientWidth / 2; });
    return np;
  });

  // ── editing: pins drag, + input button add/remove via dense recompress ──
  const delItem = (item) => { const next = (editTrace || []).slice(); next.splice(item.idx, 1); onEdit(next); };
  const startDrag = (e, item, segIdx) => {
    e.stopPropagation(); e.preventDefault();
    const snap = (editTrace || []).slice();
    const base = editSide ? ((editSide.bases[segIdx] || {}).base || 0) : 0;
    const move = (ev) => {
      // clicked index → abs → this segment's relative frame.
      const g = xToG(ev.clientX);
      const abs = editSide ? absOfCapIndex(g, editSide.baseAbs, editSide.loads) : (base + g);
      const f = Math.max(0, abs - base);
      const op = { ...snap[item.idx] };
      if (item.kind === "phasepin") op.phasepin = f;
      else if (item.kind === "rngseed") op.rngseed = [f, op.rngseed[1]];
      else if (item.kind === "esc") op.esc = f;
      const next = snap.slice(); next[item.idx] = op; onEdit(next);
    };
    const up = () => { document.removeEventListener("pointermove", move); document.removeEventListener("pointerup", up); };
    document.addEventListener("pointermove", move); document.addEventListener("pointerup", up);
  };

  // ── place a {wait ANCHOR} at the cursor: split that segment, re-base what follows. ──
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
    const { seg, frame: F } = gToTrace(cur);
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
    const { seg: sg, frame: f } = gToTrace(cur);
    const op = kind === "phasepin" ? { phasepin: f } : { rngseed: [f, 19937] };
    const next = (editTrace || []).slice();
    let idx = (next[0] && "savefile" in next[0]) ? 1 : 0;
    if (sg > 0) { let wc = 0; for (let i = 0; i < next.length; i++) { if (next[i] && "wait" in next[i]) { wc++; if (wc === sg) { idx = i + 1; break; } } } }
    next.splice(idx, 0, op); onEdit(next);
  };

  // ── notes (sidecar {seg, frame, text, box?}) ─────────────────────────────────
  const addNote = () => {
    const text = prompt("note" + (pendingBox ? " (crop attached)" : "") + ":"); if (!text) return;
    const { seg, frame } = gToTrace(cur);
    const note = { seg, frame, text }; if (pendingBox) note.box = pendingBox;
    onNotes([...(notes || []), note]); if (setPendingBox) setPendingBox(null);
  };
  const delNote = (i) => { const n = (notes || []).slice(); n.splice(i, 1); onNotes(n); };

  // ── {caprange} len/pos edit controls (the capture window SIZE; ✎ until re-captured) ──
  const capPending = !!(capInfo && manifest && manifest.caprange &&
    (capInfo.start !== manifest.caprange[0] || capInfo.count !== manifest.caprange[1]));
  const editCapOp = (mut) => {
    if (!capInfo) { toast("this trace has no {caprange} to edit", true); return; }
    const next = (editTrace || []).slice();
    const [s, c] = next[capInfo.capIdx].caprange;
    const { ns, nc } = mut(s, c);
    if (ns === s && nc === c) { toast("at the edge — start ≥ 0, length ≥ 1", true); return; }
    next[capInfo.capIdx] = { caprange: [ns, nc] };
    const ti = next.findIndex(o => o && "calltrace" in o);
    if (ti >= 0) { const [cs, cc] = next[ti].calltrace; next[ti] = { calltrace: [Math.max(0, cs + (ns - s)), Math.max(1, cc + (nc - c))] }; }
    onEdit(next);
  };
  const bumpDuration = (d) => editCapOp((s, c) => ({ ns: s, nc: Math.max(1, c + d) }));
  const slideWindow = (d) => editCapOp((s, c) => ({ ns: Math.max(0, s + d), nc: c }));

  // ── lane builders (everything via the captured index) ─────────────────────────
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
    const { seg, frame } = gToTrace(xToG(e.clientX));
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

  // captured index → screen x for a side's content (null if that side has no captured axis).
  const gx = (side, abs) => { const g = side.ci(abs); return g == null ? null : relX(g); };
  const inWin = (g) => g != null && g >= lo && g <= hi;

  // emitted anchor chips, placed at the captured index they fired on (per side).
  const anchorChips = (sideName) => {
    const side = sideOf(sideName);
    return (anchors[sideName] || []).map((f, i) => {
      const g = side.ci(f.frame);
      if (!inWin(g)) return null;                     // outside the captured window → hidden
      return html`<div class=${"chip " + anchorCls(f.anchor)} style="left:${relX(g)}px"
        data-full=${`${f.anchor} @${f.frame} · frame ${g}`}
        onClick=${(e) => { e.stopPropagation(); scrubTo(g); }}
        key=${i}>${shortAnchor(f.anchor)}</div>`;
    });
  };

  // emitted (read-only) input spans, per side (mapped via that side's captured-input bases).
  const emittedSpans = (sideName) => {
    const side = sideOf(sideName), out = [];
    capSegs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      ins.forEach((it, j) => {
        const x0 = gx(side, itemAbs(it, k, side.capBases));
        const nx = ins[j + 1];
        const x1 = nx ? gx(side, itemAbs(nx, k, side.capBases)) : (x0 == null ? null : x0 + 4 * ppf);
        const lbl = btnNames(it.buttons);
        if (x0 != null && x1 != null && lbl !== "·")
          out.push(html`<div class="span ro" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" title=${lbl} key=${`${k}-${j}`}>${lbl}</div>`);
      });
    });
    return out;
  };

  // editable per-button held-interval bars, per side (so divergence shows as drift).
  const btnBars = (button, sideName) => {
    const side = sideOf(sideName), out = [];
    segs.forEach((s, k) => {
      const base = (side.bases[k] || {}).base ?? 0;
      const arr = denseOf(k, Math.max(0, ...segInputs(k).map(i => i.frame)) + 1);
      let f = 0;
      while (f < arr.length) {
        if (arr[f] & button) { let e = f; while (e < arr.length && (arr[e] & button)) e++;
          const x0 = gx(side, base + f), x1 = gx(side, base + e);
          if (x0 != null && x1 != null) out.push(html`<div class="bar" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" key=${`${k}-${f}`}></div>`);
          f = e; }
        else f++;
      }
    });
    return out;
  };

  const pinItems = (kind, sideName) => {
    const side = sideOf(sideName), out = [];
    segs.forEach((s, k) => s.items.forEach((it, j) => {
      if (it.kind !== kind) return;
      const x = gx(side, itemAbs(it, k, side.bases));
      if (x == null) return;
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

  // load markers: where this side's captured frames skip (a suppressed load) — a thin tick so
  // the "frame counter jumps" is legible. Placed at the captured index of the load boundary.
  const loadMarks = (sideName) => {
    const side = sideOf(sideName);
    return side.loads.filter(ld => ld.start >= (side.baseAbs ?? 0)).map((ld, i) => {
      const g = side.ci(ld.end);
      if (!inWin(g)) return null;
      return html`<div class="tl-loadmark" style="left:${relX(g)}px" title=${`load (${ld.end - ld.start} frames suppressed)`} key=${i}></div>`;
    });
  };

  const noteMarks = () => (notes || []).map((nt, i) => {
    const side = editSide || port;
    const x = gx(side, ((side.bases[nt.seg] || {}).base ?? 0) + nt.frame);
    if (x == null) return null;
    return html`<div class="pin note" style="left:${x}px"
      title=${`${nt.text}${nt.box ? " · crop " + nt.box.join(",") : ""} · seg${nt.seg}+${nt.frame} · click=scrub · alt-click=delete`}
      onClick=${(e) => { e.stopPropagation(); if (e.altKey) delNote(i); else { const g = side.ci(((side.bases[nt.seg] || {}).base ?? 0) + nt.frame); if (g != null) scrubTo(g); } }}
      key=${i}>📝</div>`;
  });

  // ── flat row list: [label, heightPx, side, vnode] ─────────────────────────────
  const sideRows = (sideName) => {
    const rows = [
      ["anchors", H.anchors, sideName, html`<div class="tl-row anchors" style="height:${H.anchors}px">${loadMarks(sideName)}${anchorChips(sideName)}</div>`],
      ["inputs", H.inputs, sideName, html`<div class="tl-row ro" style="height:${H.inputs}px">${emittedSpans(sideName)}</div>`],
    ];
    for (const [b, name] of btns)
      rows.push([name, RH, sideName, html`<div class="tl-row btn" style="height:${RH}px" onClick=${(e) => onBtnRow(e, b)} title="click to add a press · alt-click to remove">${btnBars(b, sideName)}</div>`]);
    rows.push(["phasepin", RH, sideName, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("phasepin", sideName)}</div>`]);
    rows.push(["rngseed", RH, sideName, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("rngseed", sideName)}</div>`]);
    rows.push(["esc", RH, sideName, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("esc", sideName)}</div>`]);
    return rows;
  };
  const rows = [...sideRows("retail"), ...sideRows("port"),
    ["📝 notes", RH, "note", html`<div class="tl-row notes" style="height:${RH}px">${noteMarks()}</div>`]];

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="legend"><span class="sw s-retail"></span>retail <span class="sw s-port"></span>port</span>
      <span class="sep">·</span>
      <span class="dim">captured frames: retail ${retail.nFrames} · port ${port.nFrames}</span>
      <span class="sep">·</span>
      ${capInfo ? html`<span class="dim">window</span>
        <span class="capctl" title="capture-window DURATION (length). ⟳ re-capture to apply.">
          <span class="dim">len</span>
          ${[-120, -30, -10, -1].map(d => html`<button class="seg" onClick=${() => bumpDuration(d)} key=${d}>${d}</button>`)}
          <span class="capnum">${capInfo.count}f</span>
          ${[1, 10, 30, 120].map(d => html`<button class="seg" onClick=${() => bumpDuration(d)} key=${d}>+${d}</button>`)}
        </span>
        <span class="capctl" title="capture-window POSITION (start). ⟳ re-capture to apply.">
          <span class="dim">pos</span>
          ${[-30, -10, -1].map(d => html`<button class="seg" onClick=${() => slideWindow(d)} key=${d}>${d}</button>`)}
          <span class="capnum">@${capInfo.start}</span>
          ${[1, 10, 30].map(d => html`<button class="seg" onClick=${() => slideWindow(d)} key=${d}>+${d}</button>`)}
        </span>
        ${capPending && html`<span class="cappend" title="pending window edit — ⟳ re-capture to realise it">✎ pending</span>`}`
        : html`<span class="dim">no {caprange}</span>`}
      <span class="sep">·</span><span class="dim">add@cursor:</span>
      <button class="seg" onClick=${() => addAtCursor("phasepin")} title="add a phasepin at the cursor">+⟲</button>
      <button class="seg" onClick=${() => addAtCursor("rngseed")} title="add an rngseed at the cursor">+🎲</button>
      <button class="seg" onClick=${addNote} title="add a note at the cursor">+📝</button>
      <span class="sep">·</span>
      <select class="seg" value=${waitName} onChange=${e => setWaitName(e.target.value)} title="anchor to wait on">
        ${["LOADING_END", "HOUSE_FREEROAM", "FREEROAM_START", "NEW_GAME", "LOADING_START",
           "PAUSE_OPEN", "PAUSE_CLOSE", "TITLE_RETURN", "TEXT_ANIM_END", "CONV_POSE_END"].map(a =>
          html`<option value=${a}>${a}</option>`)}
      </select>
      <button class="seg" onClick=${() => addWaitAnchor(waitName)}
        title="insert {wait} at the cursor — splits the segment + re-bases what follows">⚓ +wait</button>
      <span class="spacer"></span>
      ${stale && html`<span class="stale-dot">● edits not captured</span>`}
      ${onRecapture && html`<button class="seg recap" onClick=${onRecapture}
        title="re-capture the working trace with the current edits, then reload">⟳ re-capture</button>`}
      <span class="dim">zoom</span>
      <button onClick=${() => zoom(1 / 1.6)}>−</button>
      <span class="dim">${ppf.toFixed(1)}px/f</span>
      <button onClick=${() => zoom(1.6)}>+</button>
      <button onClick=${() => setPpf(2)}>1:1</button>
    </div>
    <div class="tl-body">
      <div class="tl-gutter">
        ${rows.map(([label, h, side], i) => html`<div class=${"gut-row s-" + side} style="height:${h}px" key=${i}>
          <span class="side-dot"></span>${label}</div>`)}
      </div>
      <div class="tl-scroll" ref=${scrollRef}
        onWheel=${e => { if (e.shiftKey) { e.preventDefault(); scrollRef.current.scrollLeft += e.deltaY; } }}>
        <div class="tl-content" style="width:${contentW}px" onClick=${onRulerClick}>
          <div class="tl-cursor" style="left:${relX(cur)}px"></div>
          ${rows.map(([, , side, lanes], i) => html`<div class=${"tl-grp s-" + side} key=${i}>${lanes}</div>`)}
        </div>
      </div>
    </div>
    <div class="hint">click an anchor / read-only row / ruler to scrub · click a button row to add a press, alt-click to remove · drag pins · shift+wheel pan · captured frame ${cur} / ${segN}</div>
  </div>`;
}
