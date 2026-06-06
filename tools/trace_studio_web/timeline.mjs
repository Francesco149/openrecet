// timeline.mjs — the sync-anchor-aligned timeline editor.
// Layout: a fixed readable LABEL GUTTER + a horizontally-scrolling lane pane (rows
// height-matched). Per side: emitted ANCHORS + emitted INPUTS (read-only) + the
// editable TRACE — one row PER BUTTON (held intervals; click to add a press, alt-click
// to remove) + a row each for phasepin / rngseed / esc. Sync-anchor aligned so a
// divergent anchor shows as a horizontal gap. Zoom centers on the cursor; a toggle
// limits the view to the captured window. Clicking read-only rows / anchors scrubs.
import { html, useMemo, useRef, useState } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, sideLayout, itemAbs } from "/align.mjs";

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
const shortAnchor = (n) => n.replace(/^EXTRA_SPRITE_/, "FX_").replace(/^CONV_POSE_/, "CP_")
  .replace(/^LOADING_/, "LD_").replace(/^TEXT_ANIM_/, "TA_").replace(/^HOUSE_FREEROAM/, "HF")
  .replace(/^FREEROAM_START/, "FREE").replace(/^DLG_LINE_/, "DL_");
const btnNames = (hex) => { const m = parseInt(hex, 16) || 0;
  return BTNROWS.filter(([b]) => m & b).map(([, n]) => n).join("") || "·"; };

const RH = 14;                                   // per-button / pin lane height
const H = { anchors: 22, inputs: 16 };

export function Timeline({ editTrace, onEdit, capturedOps, anchors, manifest, stale, notes, onNotes, pendingBox, setPendingBox, cursor, setCursor, onSeekWindow }) {
  const [ppf, setPpf] = useState(1.0);
  const [syncSeg, setSyncSeg] = useState(0);   // default: sync at BOOT so both sides
  const [winOnly, setWinOnly] = useState(false); // start aligned (no load-stretch gap)
  const [waitName, setWaitName] = useState("LOADING_END");
  const scrollRef = useRef(null);

  const L = useMemo(() => {
    const segs = parseSegments(editTrace || []);
    const capSegs = parseSegments(capturedOps || []);
    const port = sideLayout(segs, anchors.port || [], syncSeg);
    const retail = sideLayout(segs, anchors.retail || [], syncSeg);
    const winBase = manifest?.port?.base_abs;
    const N = (manifest?.frame_range ? manifest.frame_range[1] + 1 : manifest?.n_frames) || 0;
    const winRel = (winBase != null && N) ? [winBase - port.syncFrame, winBase - port.syncFrame + N] : null;
    let lo = 0, hi = 0;
    const note = (rel) => { if (rel < lo) lo = rel; if (rel > hi) hi = rel; };
    for (const f of (anchors.port || [])) note(f.frame - port.syncFrame);
    for (const f of (anchors.retail || [])) note(f.frame - retail.syncFrame);
    segs.forEach((s, k) => s.items.forEach(it => {
      note(itemAbs(it, k, port.bases) - port.syncFrame);
      note(itemAbs(it, k, retail.bases) - retail.syncFrame);
    }));
    lo -= 20; hi += 60;
    if (winOnly && winRel) { lo = winRel[0] - 10; hi = winRel[1] + 10; }
    // which button rows to show: defaults + any present in the trace
    const present = new Set(DEFAULT_BTNS);
    (editTrace || []).forEach(o => { if (o && "buttons" in o) { const m = parseInt(o.buttons, 16) || 0; BTNROWS.forEach(([b]) => { if (m & b) present.add(b); }); } });
    const btns = BTNROWS.filter(([b]) => present.has(b));
    return { segs, capSegs, port, retail, lo, hi, winRel, btns };
  }, [editTrace, capturedOps, anchors, syncSeg, winOnly, manifest]);

  const { segs, capSegs, port, retail, lo, hi, winRel, btns } = L;
  const relX = (rel) => (rel - lo) * ppf;
  const contentW = Math.max(400, (hi - lo) * ppf);
  const sideSync = (side) => (side === "port" ? port : retail).syncFrame;
  const sideLay = (side) => (side === "port" ? port : retail);
  const sideX = (abs, side) => relX(abs - sideSync(side));

  const setCursorRel = (rel) => {
    setCursor(rel);
    const base = manifest?.port?.base_abs;
    if (base != null) onSeekWindow(port.syncFrame + rel - base);
  };
  const xToRel = (clientX) => {
    const r = scrollRef.current.getBoundingClientRect();
    return Math.round((clientX - r.left + scrollRef.current.scrollLeft) / ppf) + lo;
  };
  const onRulerClick = (e) => setCursorRel(xToRel(e.clientX));

  // zoom keeping the cursor centered in the viewport
  const zoom = (factor) => setPpf(p => {
    const np = Math.max(0.03, Math.min(20, p * factor));
    const el = scrollRef.current;
    if (el) requestAnimationFrame(() => { el.scrollLeft = (cursor - lo) * np - el.clientWidth / 2; });
    return np;
  });

  // ── click x on a side → {seg, frame} ────────────────────────────────────────
  const hitSeg = (clientX, side) => {
    const abs = xToRel(clientX) + sideSync(side);
    const bases = sideLay(side).bases;
    let seg = 0; for (let k = 0; k < segs.length; k++) if (((bases[k] || {}).base ?? 0) <= abs) seg = k;
    return { seg, frame: Math.max(0, abs - ((bases[seg] || {}).base || 0)) };
  };

  // ── editing: pins/esc drag, + input button add/remove via dense recompress ──
  const startDrag = (e, item, segIdx, side) => {
    e.stopPropagation(); e.preventDefault();
    const base = (sideLay(side).bases[segIdx] || {}).base || 0, sync = sideSync(side);
    const snap = (editTrace || []).slice();
    const move = (ev) => {
      const r = scrollRef.current.getBoundingClientRect();
      const f = Math.max(0, Math.round((ev.clientX - r.left + scrollRef.current.scrollLeft) / ppf) + lo + sync - base);
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
    const abs = port.syncFrame + cursor;
    let seg = 0; for (let k = 0; k < segs.length; k++) if (((port.bases[k] || {}).base ?? 0) <= abs) seg = k;
    const F = Math.max(0, abs - ((port.bases[seg] || {}).base || 0));
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
    const abs = port.syncFrame + cursor;
    let sg = 0; for (let k = 0; k < segs.length; k++) if (((port.bases[k] || {}).base ?? 0) <= abs) sg = k;
    const f = Math.max(0, abs - ((port.bases[sg] || {}).base || 0));
    const op = kind === "phasepin" ? { phasepin: f } : { rngseed: [f, 19937] };
    const next = (editTrace || []).slice();
    let idx = (next[0] && "savefile" in next[0]) ? 1 : 0;
    if (sg > 0) { let wc = 0; for (let i = 0; i < next.length; i++) { if (next[i] && "wait" in next[i]) { wc++; if (wc === sg) { idx = i + 1; break; } } } }
    next.splice(idx, 0, op); onEdit(next);
  };

  // ── notes (sidecar: {seg, frame, text, box?}) — port-aligned annotations ────
  const addNote = () => {
    const text = prompt("note" + (pendingBox ? " (crop attached)" : "") + ":"); if (!text) return;
    const abs = port.syncFrame + cursor;
    let sg = 0; for (let k = 0; k < segs.length; k++) if (((port.bases[k] || {}).base ?? 0) <= abs) sg = k;
    const f = Math.max(0, abs - ((port.bases[sg] || {}).base || 0));
    const note = { seg: sg, frame: f, text }; if (pendingBox) note.box = pendingBox;
    onNotes([...(notes || []), note]); if (setPendingBox) setPendingBox(null);
  };
  const delNote = (i) => { const n = (notes || []).slice(); n.splice(i, 1); onNotes(n); };

  // extend the captured window so you can synthetically add inputs at the end
  const extendTrace = () => {
    const n = parseInt(prompt("extend the captured window by how many frames?", "120"), 10);
    if (!n || n <= 0) return;
    const next = (editTrace || []).slice();
    const ci = next.findIndex(o => o && "caprange" in o);
    if (ci < 0) { alert("this trace has no {caprange} to extend"); return; }
    const [s, c] = next[ci].caprange; next[ci] = { caprange: [s, c + n] };
    const ti = next.findIndex(o => o && "calltrace" in o);
    if (ti >= 0) { const [cs, cc] = next[ti].calltrace; next[ti] = { calltrace: [cs, cc + n] }; }
    onEdit(next);
  };
  const noteMarks = () => (notes || []).map((nt, i) => {
    const x = sideX(((port.bases[nt.seg] || {}).base || 0) + nt.frame, "port");
    return html`<div class="pin note" style="left:${x}px"
      title=${`${nt.text}${nt.box ? " · crop " + nt.box.join(",") : ""} · click=scrub · alt-click=delete`}
      onClick=${(e) => { e.stopPropagation(); if (e.altKey) delNote(i); else setCursorRel(((port.bases[nt.seg] || {}).base || 0) + nt.frame - port.syncFrame); }}
      key=${i}>📝</div>`;
  });

  const segInputs = (k) => segs[k].items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
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
  const onBtnRow = (e, button, side) => {
    e.stopPropagation();
    const { seg, frame } = hitSeg(e.clientX, side);
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

  // ── lane builders ───────────────────────────────────────────────────────────
  const anchorChips = (side) => (anchors[side] || []).map((f, i) =>
    html`<div class=${"chip " + anchorCls(f.anchor)} style="left:${sideX(f.frame, side)}px"
      data-full=${`${f.anchor} @${f.frame}`}
      onClick=${(e) => { e.stopPropagation(); setCursorRel(f.frame - sideSync(side)); }}
      key=${i}>${shortAnchor(f.anchor)}</div>`);

  // emitted (read-only) combined input spans
  const emittedSpans = (side) => {
    const lay = sideLay(side), out = [];
    capSegs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      ins.forEach((it, j) => {
        const x0 = sideX(itemAbs(it, k, lay.bases), side);
        const nx = ins[j + 1]; const x1 = nx ? sideX(itemAbs(nx, k, lay.bases), side) : x0 + 4 * ppf;
        const lbl = btnNames(it.buttons);
        if (lbl !== "·") out.push(html`<div class="span ro" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" title=${lbl} key=${`${k}-${j}`}>${lbl}</div>`);
      });
    });
    return out;
  };

  // editable per-button held-interval bars for one side
  const btnBars = (button, side) => {
    const lay = sideLay(side), out = [];
    segs.forEach((s, k) => {
      const arr = denseOf(k, Math.max(0, ...segInputs(k).map(i => i.frame)) + 1);
      let f = 0;
      while (f < arr.length) {
        if (arr[f] & button) { let e = f; while (e < arr.length && (arr[e] & button)) e++;
          const x0 = sideX((lay.bases[k]?.base || 0) + f, side), x1 = sideX((lay.bases[k]?.base || 0) + e, side);
          out.push(html`<div class="bar" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px" key=${`${k}-${f}`}></div>`); f = e; }
        else f++;
      }
    });
    return out;
  };

  const pinItems = (kind, side) => {
    const lay = sideLay(side), out = [];
    segs.forEach((s, k) => s.items.forEach((it, j) => {
      if (it.kind !== kind) return;
      const x = sideX(itemAbs(it, k, lay.bases), side);
      const cls = kind === "phasepin" ? "pp" : kind === "rngseed" ? "rp" : "esc";
      const glyph = kind === "phasepin" ? "⟲" : kind === "rngseed" ? "🎲" : "⎋";
      out.push(html`<div class=${"pin ed " + cls} style="left:${x}px"
        title=${`${kind} seg${k}+${it.frame}${it.value != null ? "=" + it.value : ""} · drag / alt-click delete`}
        onPointerDown=${(e) => startDrag(e, it, k, side)}
        onClick=${(e) => { e.stopPropagation(); if (e.altKey) delItem(it); }}
        key=${`${k}${j}`}>${glyph}</div>`);
    }));
    return out;
  };

  // ── build the flat row list: [label, heightPx, side, kind, vnode] ────────────
  const sideRows = (side) => {
    const rows = [
      ["anchors", H.anchors, side, html`<div class="tl-row anchors" style="height:${H.anchors}px">${anchorChips(side)}</div>`],
      ["emitted in", H.inputs, side, html`<div class="tl-row ro" style="height:${H.inputs}px">${emittedSpans(side)}</div>`],
    ];
    for (const [b, name] of btns)
      rows.push([name, RH, side, html`<div class="tl-row btn" style="height:${RH}px" onClick=${(e) => onBtnRow(e, b, side)} title="click to add a press · alt-click to remove">${btnBars(b, side)}</div>`]);
    rows.push(["phasepin", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("phasepin", side)}</div>`]);
    rows.push(["rngseed", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("rngseed", side)}</div>`]);
    rows.push(["esc", RH, side, html`<div class="tl-row pinrow" style="height:${RH}px">${pinItems("esc", side)}</div>`]);
    return rows;
  };
  const rows = [...sideRows("retail"), ...sideRows("port"),
    ["📝 notes", RH, "note", html`<div class="tl-row notes" style="height:${RH}px">${noteMarks()}</div>`]];

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="legend"><span class="sw s-retail"></span>retail <span class="sw s-port"></span>port</span>
      <span class="sep">·</span>
      <span class="dim">sync:</span>
      ${segs.map((s, k) => html`<button class=${"seg " + ((syncSeg ?? segs.length - 1) === k ? "on" : "")}
        onClick=${() => setSyncSeg(k)} key=${k}>${k === 0 ? "boot" : shortAnchor(s.waitAnchor)}</button>`)}
      <span class="sep">·</span>
      <button class=${"seg " + (winOnly ? "on" : "")} onClick=${() => setWinOnly(v => !v)}
        title="limit the view to the captured window">⊞ window-only</button>
      <button class="seg" onClick=${extendTrace} title="extend the captured window to add inputs at the end (then re-capture)">⇥ extend</button>
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
          ${winRel && html`<div class="tl-window" style="left:${relX(winRel[0])}px;width:${(winRel[1] - winRel[0]) * ppf}px"></div>`}
          <div class="tl-cursor" style="left:${relX(cursor)}px"></div>
          ${rows.map(([, , side, lanes], i) => html`<div class=${"tl-grp s-" + side} key=${i}>${lanes}</div>`)}
        </div>
      </div>
    </div>
    <div class="hint">click an anchor / read-only row / ruler to scrub · click a button row to add a press, alt-click to remove · drag pins · shift+wheel pan · cursor = ${cursor}f from sync</div>
  </div>`;
}
