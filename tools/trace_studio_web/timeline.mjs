// timeline.mjs — the 4-track sync-anchor-aligned timeline (read-only in step 3).
// retail-anchors / retail-trace / port-anchors / port-trace, drawn from the pure
// align.mjs core. Zoom (pxPerFrame) + horizontal pan; click an anchor or the ruler
// to move the cursor; pick the sync segment above the tracks.
import { html, useMemo, useRef, useState } from "/vendor/htm-preact-standalone.mjs";
import { parseSegments, sideLayout, absToX, itemAbs } from "/align.mjs";

const BTN = [[0x04, "U"], [0x08, "D"], [0x02, "L"], [0x01, "R"], [0x10, "Z"], [0x100, "ESC"]];
const btnNames = (hex) => { const m = parseInt(hex, 16) || 0;
  return BTN.filter(([b]) => m & b).map(([, n]) => n).join("+") || "·"; };

// anchor name → color class (by family)
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

export function Timeline({ traceOps, anchors, manifest, cursor, setCursor, onSeekWindow }) {
  const [ppf, setPpf] = useState(1.0);             // pixels per frame
  const [syncSeg, setSyncSeg] = useState(null);    // null → last segment
  const scrollRef = useRef(null);

  const L = useMemo(() => {
    const segs = parseSegments(traceOps || []);
    const port = sideLayout(segs, anchors.port || [], syncSeg);
    const retail = sideLayout(segs, anchors.retail || [], syncSeg);
    // global rel range across both sides (anchors + the trace items)
    let lo = 0, hi = 0;
    const note = (rel) => { if (rel < lo) lo = rel; if (rel > hi) hi = rel; };
    for (const f of (anchors.port || [])) note(f.frame - port.syncFrame);
    for (const f of (anchors.retail || [])) note(f.frame - retail.syncFrame);
    segs.forEach((s, k) => s.items.forEach(it => {
      note(itemAbs(it, k, port.bases) - port.syncFrame);
      note(itemAbs(it, k, retail.bases) - retail.syncFrame);
    }));
    return { segs, port, retail, lo: lo - 20, hi: hi + 40 };
  }, [traceOps, anchors, syncSeg]);

  const { segs, port, retail, lo, hi } = L;
  const contentW = Math.max(400, (hi - lo) * ppf);
  const relX = (rel) => (rel - lo) * ppf;                       // rel-frame → screen x
  const sideX = (abs, side) => relX(abs - (side === "port" ? port : retail).syncFrame);

  // the captured video window (port frames) in sync-rel coords → a highlighted band
  const winBase = manifest?.port?.base_abs;
  const N = (manifest?.frame_range ? manifest.frame_range[1] + 1 : manifest?.n_frames) || 0;
  const winRel = (winBase != null && N) ? [winBase - port.syncFrame, winBase - port.syncFrame + N] : null;

  // cursor (a sync-relative frame) → screen x; click ruler/anchor sets it
  const cursorX = relX(cursor);
  const onRulerClick = (e) => {
    const r = scrollRef.current.getBoundingClientRect();
    const x = e.clientX - r.left + scrollRef.current.scrollLeft;
    setCursorRel(Math.round(x / ppf) + lo);
  };
  const setCursorRel = (rel) => {
    setCursor(rel);
    // map sync-rel → port window index for the videos (port_abs = syncFrame_port + rel)
    const base = manifest?.port?.base_abs;
    if (base != null) onSeekWindow(port.syncFrame + rel - base);
  };

  const ANCHOR_H = 22, LANE_H = 16;

  const anchorLane = (side, firings) => html`<div class="tl-row anchors" style="height:${ANCHOR_H}px">
    ${(firings || []).map((f, i) => html`<div class=${"chip " + anchorCls(f.anchor)}
        style="left:${sideX(f.frame, side)}px" title=${`${f.anchor} @${f.frame}`}
        onClick=${() => setCursorRel(f.frame - (side === "port" ? port : retail).syncFrame)}
        key=${i}>${shortAnchor(f.anchor)}</div>`)}
  </div>`;

  // trace sub-lanes for a side: inputs (held-button spans) · pins · esc
  const traceLanes = (side) => {
    const lay = side === "port" ? port : retail;
    const spans = [], pins = [], escs = [];
    segs.forEach((s, k) => {
      const ins = s.items.filter(i => i.kind === "input").sort((a, b) => a.frame - b.frame);
      ins.forEach((it, j) => {
        const x0 = sideX(itemAbs(it, k, lay.bases), side);
        const next = ins[j + 1];
        const x1 = next ? sideX(itemAbs(next, k, lay.bases), side) : x0 + 4 * ppf;
        const lbl = btnNames(it.buttons);
        if (lbl !== "·") spans.push(html`<div class="span" style="left:${x0}px;width:${Math.max(2, x1 - x0)}px"
          title=${`${lbl} (seg ${k}+${it.frame})`} key=${`s${k}-${j}`}>${lbl}</div>`);
      });
      s.items.forEach((it, j) => {
        const x = sideX(itemAbs(it, k, lay.bases), side);
        if (it.kind === "phasepin") pins.push(html`<div class="pin pp" style="left:${x}px" title=${`phasepin seg${k}+${it.frame}`} key=${`p${k}-${j}`}>⟲</div>`);
        if (it.kind === "rngseed") pins.push(html`<div class="pin rp" style="left:${x}px" title=${`rngseed seg${k}+${it.frame}=${it.value}`} key=${`r${k}-${j}`}>🎲</div>`);
        if (it.kind === "esc") escs.push(html`<div class="pin esc" style="left:${x}px" title=${`esc seg${k}+${it.frame}`} key=${`e${k}-${j}`}>⎋</div>`);
      });
    });
    return html`<div>
      <div class="tl-row sub" style="height:${LANE_H}px"><span class="lane-tag">inputs</span>${spans}</div>
      <div class="tl-row sub" style="height:${LANE_H}px"><span class="lane-tag">pins</span>${pins}</div>
      <div class="tl-row sub" style="height:${LANE_H}px"><span class="lane-tag">esc</span>${escs}</div>
    </div>`;
  };

  return html`<div class="timeline">
    <div class="tl-bar">
      <span class="dim">sync:</span>
      ${segs.map((s, k) => html`<button class=${"seg " + ((syncSeg ?? segs.length - 1) === k ? "on" : "")}
        onClick=${() => setSyncSeg(k)} key=${k}>${k === 0 ? "boot" : shortAnchor(s.waitAnchor)}</button>`)}
      <span class="spacer"></span>
      <span class="dim">zoom</span>
      <button onClick=${() => setPpf(p => Math.max(0.05, p / 1.5))}>−</button>
      <span class="dim">${ppf.toFixed(2)}px/f</span>
      <button onClick=${() => setPpf(p => Math.min(20, p * 1.5))}>+</button>
      <button onClick=${() => setPpf(1)}>1:1</button>
      ${winRel && html`<button class="seg" onClick=${() => setCursorRel(winRel[0])} title="jump cursor to the captured video window">⊕ window</button>`}
    </div>
    <div class="tl-scroll" ref=${scrollRef} onWheel=${e => { if (e.shiftKey) { e.preventDefault(); scrollRef.current.scrollLeft += e.deltaY; } }}>
      <div class="tl-content" style="width:${contentW}px" onClick=${onRulerClick}>
        ${winRel && html`<div class="tl-window" style="left:${relX(winRel[0])}px;width:${(winRel[1] - winRel[0]) * ppf}px" title="captured video window"></div>`}
        <div class="tl-cursor" style="left:${cursorX}px"></div>
        <div class="tl-grp"><div class="tl-label">retail<br/>anchors</div>${anchorLane("retail", anchors.retail)}</div>
        <div class="tl-grp"><div class="tl-label">retail<br/>trace</div>${traceLanes("retail")}</div>
        <div class="tl-grp"><div class="tl-label">port<br/>anchors</div>${anchorLane("port", anchors.port)}</div>
        <div class="tl-grp"><div class="tl-label">port<br/>trace</div>${traceLanes("port")}</div>
      </div>
    </div>
    <div class="hint">click an anchor or the ruler to move the cursor · shift+wheel to pan · pick a sync segment above · cursor = ${cursor} frames from sync</div>
  </div>`;
}
