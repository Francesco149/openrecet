// timeline.mjs — the sync-anchor-aligned timeline (read-only in step 3).
// Layout: a fixed readable LABEL GUTTER on the left + a horizontally-scrolling lane
// pane on the right (rows kept height-matched). Per side: emitted ANCHORS + emitted
// INPUTS (read-only, from the trace that was actually captured) + the editable TRACE
// (inputs/pins/esc). Clicking any read-only row / anchor scrubs the video.
import { html, useMemo, useRef, useState } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, sideLayout, itemAbs } from "/align.mjs";

const BTN = [[0x04, "U"], [0x08, "D"], [0x02, "L"], [0x01, "R"], [0x10, "Z"], [0x100, "ESC"]];
const btnNames = (hex) => { const m = parseInt(hex, 16) || 0;
  return BTN.filter(([b]) => m & b).map(([, n]) => n).join("+") || "·"; };

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

const GUTTER = 92;               // px — the fixed left label column
const H = { anchors: 24, inputs: 18, trace: 48 };   // row group heights (gutter + lanes match)

export function Timeline({ traceOps, capturedOps, anchors, manifest, cursor, setCursor, onSeekWindow }) {
  const [ppf, setPpf] = useState(1.0);
  const [syncSeg, setSyncSeg] = useState(null);
  const scrollRef = useRef(null);

  const L = useMemo(() => {
    const segs = parseSegments(traceOps || []);
    const capSegs = parseSegments(capturedOps || []);
    const port = sideLayout(segs, anchors.port || [], syncSeg);
    const retail = sideLayout(segs, anchors.retail || [], syncSeg);
    let lo = 0, hi = 0;
    const note = (rel) => { if (rel < lo) lo = rel; if (rel > hi) hi = rel; };
    for (const f of (anchors.port || [])) note(f.frame - port.syncFrame);
    for (const f of (anchors.retail || [])) note(f.frame - retail.syncFrame);
    segs.forEach((s, k) => s.items.forEach(it => {
      note(itemAbs(it, k, port.bases) - port.syncFrame);
      note(itemAbs(it, k, retail.bases) - retail.syncFrame);
    }));
    return { segs, capSegs, port, retail, lo: lo - 20, hi: hi + 60 };
  }, [traceOps, capturedOps, anchors, syncSeg]);

  const { segs, capSegs, port, retail, lo, hi } = L;
  const relX = (rel) => (rel - lo) * ppf;                     // rel-frame → x in the lane pane
  const contentW = Math.max(400, (hi - lo) * ppf);
  const sideSync = (side) => (side === "port" ? port : retail).syncFrame;
  const sideX = (abs, side) => relX(abs - sideSync(side));

  const winBase = manifest?.port?.base_abs;
  const N = (manifest?.frame_range ? manifest.frame_range[1] + 1 : manifest?.n_frames) || 0;
  const winRel = (winBase != null && N) ? [winBase - port.syncFrame, winBase - port.syncFrame + N] : null;

  const setCursorRel = (rel) => {
    setCursor(rel);
    const base = manifest?.port?.base_abs;
    if (base != null) onSeekWindow(port.syncFrame + rel - base);
  };
  const onRulerClick = (e) => {
    const r = scrollRef.current.getBoundingClientRect();
    const x = e.clientX - r.left + scrollRef.current.scrollLeft;
    setCursorRel(Math.round(x / ppf) + lo);
  };

  // input held-button spans for a parsed-ops set on one side
  const inputSpans = (theSegs, side, lay, ro) => {
    const out = [];
    theSegs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      ins.forEach((it, j) => {
        const x0 = sideX(itemAbs(it, k, lay.bases), side);
        const next = ins[j + 1];
        const x1 = next ? sideX(itemAbs(next, k, lay.bases), side) : x0 + 4 * ppf;
        const lbl = btnNames(it.buttons);
        if (lbl !== "·") out.push(html`<div class=${"span" + (ro ? " ro" : "")}
          style="left:${x0}px;width:${Math.max(2, x1 - x0)}px"
          title=${`${lbl} (seg ${k}+${it.frame})`} key=${`${k}-${j}`}>${lbl}</div>`);
      });
    });
    return out;
  };

  const anchorChips = (side) => (anchors[side] || []).map((f, i) =>
    html`<div class=${"chip " + anchorCls(f.anchor)} style="left:${sideX(f.frame, side)}px"
      title=${`${f.anchor} @${f.frame} — click to scrub`}
      onClick=${(e) => { e.stopPropagation(); setCursorRel(f.frame - sideSync(side)); }}
      key=${i}>${shortAnchor(f.anchor)}</div>`);

  // editable trace lanes: inputs (spans) · pins · esc  (read-only this step)
  const traceLanes = (side) => {
    const lay = side === "port" ? port : retail;
    const pins = [], escs = [];
    segs.forEach((s, k) => s.items.forEach((it, j) => {
      const x = sideX(itemAbs(it, k, lay.bases), side);
      if (it.kind === "phasepin") pins.push(html`<div class="pin pp" style="left:${x}px" title=${`phasepin seg${k}+${it.frame}`} key=${`p${k}${j}`}>⟲</div>`);
      if (it.kind === "rngseed") pins.push(html`<div class="pin rp" style="left:${x}px" title=${`rngseed seg${k}+${it.frame}=${it.value}`} key=${`r${k}${j}`}>🎲</div>`);
      if (it.kind === "esc") escs.push(html`<div class="pin esc" style="left:${x}px" title=${`esc seg${k}+${it.frame}`} key=${`e${k}${j}`}>⎋</div>`);
    }));
    return html`<div style="height:${H.trace}px;position:relative">
      <div class="tl-row sub" style="height:16px"><span class="lane-tag">inputs</span>${inputSpans(segs, side, lay, false)}</div>
      <div class="tl-row sub" style="height:16px"><span class="lane-tag">pins</span>${pins}</div>
      <div class="tl-row sub" style="height:16px"><span class="lane-tag">esc</span>${escs}</div>
    </div>`;
  };

  // rows: [label, heightKey, side, lanesVNode]  (gutter + lanes kept in lock-step;
  // side colour-codes retail vs port so the labels need not repeat the side name)
  const rows = [
    ["anchors", "anchors", "retail", html`<div class="tl-row anchors" style="height:${H.anchors}px">${anchorChips("retail")}</div>`],
    ["emitted inputs", "inputs", "retail", html`<div class="tl-row ro" style="height:${H.inputs}px">${inputSpans(capSegs, "retail", retail, true)}</div>`],
    ["trace ✎", "trace", "retail", traceLanes("retail")],
    ["anchors", "anchors", "port", html`<div class="tl-row anchors" style="height:${H.anchors}px">${anchorChips("port")}</div>`],
    ["emitted inputs", "inputs", "port", html`<div class="tl-row ro" style="height:${H.inputs}px">${inputSpans(capSegs, "port", port, true)}</div>`],
    ["trace ✎", "trace", "port", traceLanes("port")],
  ];

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="dim">sync:</span>
      ${segs.map((s, k) => html`<button class=${"seg " + ((syncSeg ?? segs.length - 1) === k ? "on" : "")}
        onClick=${() => setSyncSeg(k)} key=${k}>${k === 0 ? "boot" : shortAnchor(s.waitAnchor)}</button>`)}
      <span class="spacer"></span>
      ${manifest?.stale && html`<span class="stale-dot">● edits not captured</span>`}
      <span class="dim">zoom</span>
      <button onClick=${() => setPpf(p => Math.max(0.03, p / 1.5))}>−</button>
      <span class="dim">${ppf.toFixed(2)}px/f</span>
      <button onClick=${() => setPpf(p => Math.min(20, p * 1.5))}>+</button>
      <button onClick=${() => setPpf(1)}>1:1</button>
      ${winRel && html`<button class="seg" onClick=${() => setCursorRel(winRel[0])} title="jump to the captured video window">⊕ window</button>`}
    </div>
    <div class="tl-body">
      <div class="tl-gutter">
        ${rows.map(([label, hk, side], i) => html`<div class=${"gut-row s-" + side} style="height:${H[hk]}px" key=${i}>
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
    <div class="hint">click an anchor / read-only row / ruler to scrub · shift+wheel pan · pick a sync segment · the green band is the captured video window · cursor = ${cursor} frames from sync</div>
  </div>`;
}
