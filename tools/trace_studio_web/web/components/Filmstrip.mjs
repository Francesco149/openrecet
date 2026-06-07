// web/components/Filmstrip.mjs — the segmented timeline that IS the scrubber.
// Renders the v2 timeline in order: gameplay blocks (flex-grow ∝ nFrames, fill ∝
// verdictClass) separated by load-seam chevrons (hover shows the port/retail tick
// stretch). Clicking inside a block seeks to that global ordinal; the cursor line
// lives inside the active block at k/(nFrames-1). For ONE gameplay segment it's a
// single full-width block (+ a leading seam chevron for the load).
import { html } from "/vendor/htm-preact-standalone.mjs";

export function Filmstrip({ view, cur, setCur }) {
  const { seg, k } = view.locate(cur);
  const seekIn = (s, e) => {
    const r = e.currentTarget.getBoundingClientRect();
    const frac = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
    setCur(s.offsetGlobal + Math.min(s.nFrames - 1, Math.floor(frac * s.nFrames)));
  };
  let gi = 0;        // gameplay-entry counter → view.segments[gi]
  return html`<div class="filmstrip">
    ${view.timeline.map((e, i) => {
      if (e.kind === "load_seam") {
        const pt = e.port_ticks == null ? "?" : e.port_ticks;
        const rt = e.retail_ticks == null ? "?" : e.retail_ticks;
        return html`<div class="seam-chev" key=${"s" + i}
          title=${`load seam (${e.anchor}) · port ${pt}t / retail ${rt}t — 0 captured frames`}>⟂</div>`;
      }
      const s = view.segments[gi++];
      if (!s) return null;
      const active = s === seg;
      const cursorPct = s.nFrames > 1 ? (k / (s.nFrames - 1)) * 100 : 0;
      return html`<div class=${"film-seg " + s.verdictClass + (active ? " active" : "")}
        style=${`flex:${s.nFrames} 1 0`} key=${"g" + s.idx}
        title=${`gameplay#${s.idx} · ${s.nFrames}f${s.cadence > 1 ? " · ×" + s.cadence : ""} · ${s.verdictClass}`}
        onClick=${(ev) => seekIn(s, ev)}>
        ${active && html`<div class="film-cursor" style=${`left:${cursorPct}%`}></div>`}
        <span class="film-lbl">${s.nFrames}f${s.cadence > 1 ? " ·×" + s.cadence : ""}</span>
      </div>`;
    })}
  </div>`;
}
