// web/components/TraceEditor.mjs — the captured-frame-index trace VIEWER (read-only for now;
// only the capture window is editable). Wired to the v2 SPA + the GLOBAL cursor.
//
// THE MODEL (see docs/findings/trace-editor-segment-alignment.md): the x-axis is the DENSE
// captured-frame index — one tick per real frame of the trace running on that side. The
// capture is phase-synced + RNG-pinned and runs 1:1, so the n-th captured frame is the same
// logical moment on each side; placing each side's anchors at their captured index makes a
// 1:1 capture align with ZERO forcing, and where the traces diverge the rows drift apart.
// Loads are suppressed (0 captured frames) so the index re-syncs at every load boundary.
//
// ANCHORS are placed PER SIDE by their true absolute frame → align.capIndexOfAbs (they
// diverge honestly). EMITTED INPUTS + pins are the SAME 1:1 driving signal, so they are
// mapped ONCE via the REFERENCE side (the side with a valid base_abs) — NOT per-side segment
// bases, which resolve to different anchors after a divergence (the resolver's strictly-after
// rule lets a `{wait PAUSE_OPEN}` grab a later dialogue PAUSE_OPEN, throwing the input 160
// frames off). Read-only: editing inputs/pins is deferred until the model is battle-tested;
// only the {caprange} window (len/pos) is editable.
import { html, useMemo, useRef, useState, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, resolveBases, loadSpans, capIndexOfAbs, itemAbs } from "/align.mjs";
import { toast } from "/web/util.mjs";

// the button bits we draw a row for (src/input.c input_binding_mask)
const BTNROWS = [[0x04, "↑"], [0x08, "↓"], [0x02, "←"], [0x01, "→"],
                 [0x10, "Z"], [0x20, "X"], [0x40, "C"], [0x80, "V"]];
const DEFAULT_BTNS = new Set([0x04, 0x08, 0x02, 0x01, 0x10, 0x20, 0x40, 0x80]);  // ↑↓←→ Z X C V

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

const RH = 14;                                   // per-button / pin lane height
const H = { anchors: 22 };

export function TraceEditor({ editTrace, onEdit, capturedOps, anchors, manifest, stale,
                             notes, cur, setCur, view, onRecapture }) {
  const [ppf, setPpf] = useState(2.0);
  const scrollRef = useRef(null);

  // locate the {caprange} op (its [start,count]) — the only editable thing (len/pos controls).
  const capInfo = useMemo(() => {
    const ops = editTrace || [];
    for (let i = 0; i < ops.length; i++) {
      const op = ops[i];
      if (op && "caprange" in op) return { capIdx: i, start: op.caprange[0], count: op.caprange[1] };
    }
    return null;
  }, [editTrace]);

  const L = useMemo(() => {
    const capSegs = parseSegments(capturedOps || []);
    const mkSide = (firings, baseAbs, nFrames) => {
      const f = firings || [];
      const loads = loadSpans(f);
      return {
        firings: f, baseAbs, nFrames, loads,
        capBases: resolveBases(capSegs, f),
        ci: (abs) => (baseAbs == null ? null : capIndexOfAbs(abs, baseAbs, loads)),
      };
    };
    const port = mkSide(anchors.port, manifest?.port?.base_abs, manifest?.n_frames || 0);
    const retail = mkSide(anchors.retail, manifest?.retail?.base_abs, manifest?.n_frames_retail || 0);
    const maxN = Math.max(port.nFrames, retail.nFrames, 1);
    // REFERENCE side for the (1:1, shared) emitted inputs + pins: the side with a valid
    // base_abs (port preferred). Mapping via one reliable side avoids the per-side segment-base
    // divergence that throws inputs off after a resolver quirk.
    const ref = port.baseAbs != null ? port : (retail.baseAbs != null ? retail : null);
    // button rows: defaults + any present in the emitted trace
    const present = new Set(DEFAULT_BTNS);
    (capturedOps || []).forEach(o => { if (o && "buttons" in o) { const m = parseInt(o.buttons, 16) || 0; BTNROWS.forEach(([b]) => { if (m & b) present.add(b); }); } });
    const btns = BTNROWS.filter(([b]) => present.has(b));
    return { capSegs, port, retail, maxN, ref, btns };
  }, [capturedOps, anchors, manifest]);

  const { capSegs, port, retail, maxN, ref, btns } = L;
  const sideOf = (s) => (s === "port" ? port : retail);

  const segN = (view && view.totalFrames) || manifest?.n_frames || maxN || 1;
  const lo = -8, hi = maxN + 8;
  const relX = (g) => (g - lo) * ppf;
  const contentW = Math.max(400, (hi - lo) * ppf);
  const inWin = (g) => g != null && g >= lo && g <= hi;

  useEffect(() => {
    const el = scrollRef.current;
    if (el) el.scrollLeft = Math.max(0, relX(cur) - el.clientWidth / 2);
  }, []);  // once, on mount

  const xToG = (clientX) => {
    const r = scrollRef.current.getBoundingClientRect();
    return Math.round((clientX - r.left + scrollRef.current.scrollLeft) / ppf) + lo;
  };
  const scrubTo = (g) => setCur(Math.max(0, Math.min(segN - 1, g)));
  const onRulerClick = (e) => scrubTo(xToG(e.clientX));
  const zoom = (factor) => setPpf(p => {
    const np = Math.max(0.05, Math.min(40, p * factor));
    const el = scrollRef.current;
    if (el) requestAnimationFrame(() => { el.scrollLeft = (cur - lo) * np - el.clientWidth / 2; });
    return np;
  });

  // ── the ONLY editable thing: the {caprange} window (len/pos), applied on re-capture ──
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
  // the captured window on the captured-index axis is [0, nFrames); a pending length edit
  // projects the new right edge (≈ frames, ignoring suppressed loads in the delta).
  const capN = manifest?.n_frames || port.nFrames || 0;
  const winR = capInfo && manifest?.caprange ? capN + (capInfo.count - manifest.caprange[1]) : capN;

  // ── per-side ANCHORS (placed by true absolute frame → they diverge honestly) ──────────
  const anchorChips = (sideName) => {
    const side = sideOf(sideName);
    return (anchors[sideName] || []).map((f, i) => {
      const g = side.ci(f.frame);
      if (!inWin(g)) return null;
      return html`<div class=${"chip " + anchorCls(f.anchor)} style="left:${relX(g)}px"
        data-full=${`${f.anchor} @${f.frame} · frame ${g}`}
        onClick=${(e) => { e.stopPropagation(); scrubTo(g); }}
        key=${i}>${shortAnchor(f.anchor)}</div>`;
    });
  };
  // suppressed-load boundary ticks (where the captured frame counter jumps).
  const loadMarks = (sideName) => {
    const side = sideOf(sideName);
    return side.loads.filter(ld => ld.start >= (side.baseAbs ?? 0)).map((ld, i) => {
      const g = side.ci(ld.end);
      return inWin(g) ? html`<div class="tl-loadmark" style="left:${relX(g)}px" title=${`load (${ld.end - ld.start}f suppressed)`} key=${i}></div>` : null;
    });
  };

  // ── EMITTED inputs + pins (the 1:1 signal) mapped ONCE via the reference side ──────────
  const emittedBars = (button) => {
    if (!ref) return [];
    const out = [];
    capSegs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      if (!ins.length) return;
      const maxF = Math.max(...ins.map(i => i.frame)) + 2;
      const arr = new Array(maxF).fill(0); let st = 0, p = 0;
      for (let f = 0; f < maxF; f++) { while (p < ins.length && ins[p].frame <= f) { st = parseInt(ins[p].buttons, 16) || 0; p++; } arr[f] = st; }
      const base = (ref.capBases[k] || {}).base ?? 0;
      let f = 0;
      while (f < arr.length) {
        if (arr[f] & button) { let e = f; while (e < arr.length && (arr[e] & button)) e++;
          const g0 = ref.ci(base + f), g1 = ref.ci(base + e);
          if (g0 != null) out.push(html`<div class="bar" style="left:${relX(g0)}px;width:${Math.max(2, relX(g1 ?? g0 + 1) - relX(g0))}px" key=${`${k}-${f}`}></div>`);
          f = e; }
        else f++;
      }
    });
    return out;
  };
  const emittedPins = (kind) => {
    if (!ref) return [];
    const out = [];
    capSegs.forEach((s, k) => s.items.forEach((it, j) => {
      if (it.kind !== kind) return;
      const g = ref.ci(itemAbs(it, k, ref.capBases));
      if (!inWin(g)) return;
      const cls = kind === "phasepin" ? "pp" : kind === "rngseed" ? "rp" : "esc";
      const glyph = kind === "phasepin" ? "⟲" : kind === "rngseed" ? "🎲" : "⎋";
      out.push(html`<div class=${"pin " + cls} style="left:${relX(g)}px"
        title=${`${kind} seg${k}+${it.frame}${it.value != null ? "=" + it.value : ""} · frame ${g}`}
        key=${`${k}${j}`}>${glyph}</div>`);
    }));
    return out;
  };
  const noteMarks = () => (notes || []).map((nt, i) => {
    const g = ref ? ref.ci(((ref.capBases[nt.seg] || {}).base ?? 0) + nt.frame) : null;
    if (!inWin(g)) return null;
    return html`<div class="pin note" style="left:${relX(g)}px"
      title=${`${nt.text}${nt.box ? " · crop " + nt.box.join(",") : ""} · frame ${g}`}
      onClick=${(e) => { e.stopPropagation(); scrubTo(g); }} key=${i}>📝</div>`;
  });

  // ── flat row list: [label, heightPx, side, vnode] ─────────────────────────────────────
  const anchorRow = (sideName) => ["anchors", H.anchors, sideName,
    html`<div class="tl-row anchors" style="height:${H.anchors}px">${loadMarks(sideName)}${anchorChips(sideName)}</div>`];
  // anchors per side (diverge); inputs + pins ONCE (shared 1:1 signal, neutral lanes).
  const rows = [
    anchorRow("retail"),
    anchorRow("port"),
    ...btns.map(([b, name]) => [name, RH, "in", html`<div class="tl-row ro" style="height:${RH}px">${emittedBars(b)}</div>`]),
    ["phasepin", RH, "in", html`<div class="tl-row pinrow" style="height:${RH}px">${emittedPins("phasepin")}</div>`],
    ["rngseed", RH, "in", html`<div class="tl-row pinrow" style="height:${RH}px">${emittedPins("rngseed")}</div>`],
    ["esc", RH, "in", html`<div class="tl-row pinrow" style="height:${RH}px">${emittedPins("esc")}</div>`],
    ["📝 notes", RH, "note", html`<div class="tl-row notes" style="height:${RH}px">${noteMarks()}</div>`],
  ];

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="legend"><span class="sw s-retail"></span>retail <span class="sw s-port"></span>port</span>
      <span class="sep">·</span>
      <span class="dim">captured frames: retail ${retail.nFrames} · port ${port.nFrames}${ref ? "" : " · no base_abs"}</span>
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
      <span class="spacer"></span>
      <span class="dim ro-badge" title="the trace lanes are read-only for now (only the capture window is editable)">🔒 read-only</span>
      ${stale && html`<span class="stale-dot">● edits not captured</span>`}
      ${onRecapture && html`<button class="seg recap" onClick=${onRecapture}
        title="re-capture the working trace with the current window, then reload">⟳ re-capture</button>`}
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
          ${capN > 0 && html`<div class=${"tl-window" + (capPending ? " pending" : "")}
            style="left:${relX(0)}px;width:${Math.max(2, (winR - 0) * ppf)}px"
            title="captured window (the frames recorded)"></div>`}
          <div class="tl-cursor" style="left:${relX(cur)}px"></div>
          ${rows.map(([, , side, lanes], i) => html`<div class=${"tl-grp s-" + side} key=${i}>${lanes}</div>`)}
        </div>
      </div>
    </div>
    <div class="hint">click an anchor / row / ruler to scrub · shift+wheel pan · captured frame ${cur} / ${segN} · anchors per-side (diverge); inputs are the shared 1:1 signal (read-only)</div>
  </div>`;
}
