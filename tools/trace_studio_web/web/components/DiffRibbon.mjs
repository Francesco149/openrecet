// web/components/DiffRibbon.mjs — per-frame port-vs-retail diff strip for the active
// segment. One cell per LOCAL ordinal k; cell heat = that frame's meanabs (the
// manifest.diff.per_frame value, keyed by the strided label seg.labelOf(k)). Click a
// cell to seek there; "⟶ worst" jumps to the max-divergence frame, "next ⟂" to the
// next over-threshold frame after the cursor — the "click-to-seek to first non-black".
import { html, useMemo } from "/vendor/htm-preact-standalone.mjs";
import { toast } from "/web/util.mjs";

// meanabs → green→yellow→red. ABSOLUTE scale (not per-segment max): a near-clean
// segment (ov-both ~0.002) stays green; a real divergence (mean ~>4) goes red.
function heat(meanabs) {
  const t = Math.max(0, Math.min(1, meanabs / 6));
  const r = t < 0.5 ? Math.round(510 * t) : 255;
  const g = t < 0.5 ? 200 : Math.round(200 * (1 - (t - 0.5) * 2));
  return `rgb(${r},${g},78)`;
}

export function DiffRibbon({ view, cur, setCur }) {
  const { seg, k } = view.locate(cur);
  const cells = useMemo(() => {
    const out = [];
    for (let i = 0; i < seg.nFrames; i++) {
      const d = seg.diffAt(i);
      out.push({ i, meanabs: d ? d.meanabs : 0, differ: d ? d.differ : 0, has: !!d });
    }
    return out;
  }, [seg]);
  const worst = cells.reduce((a, c) => (c.meanabs > a.meanabs ? c : a), cells[0] || { i: 0, meanabs: 0 });
  const here = cells[k] || { meanabs: 0, differ: 0, has: false };
  const thr = Math.max(0.25, worst.meanabs * 0.25);

  const goWorst = () => setCur(view.globalOf(seg, worst.i));
  const nextDiv = () => {
    for (let i = k + 1; i < cells.length; i++)
      if (cells[i].meanabs >= thr) { setCur(view.globalOf(seg, i)); return; }
    toast(`no frame ≥ ${thr.toFixed(2)} meanabs after ${k}`);
  };

  return html`<div class="diff-ribbon-wrap">
    <div class="diff-ribbon" title="port-vs-retail per-frame diff — click to seek">
      ${cells.map((c) => html`<div key=${c.i}
        class=${"diff-cell" + (c.i === k ? " cur" : "")}
        style=${`background:${c.has ? heat(c.meanabs) : "#11161c"}`}
        title=${`f${c.i} (label ${seg.labelOf(c.i)}): meanabs ${c.meanabs} · ${c.differ}px`}
        onClick=${() => setCur(view.globalOf(seg, c.i))}></div>`)}
    </div>
    <div class="diff-bar">
      <span>diff f${k}: <b>${here.meanabs}</b> meanabs · ${here.differ}px${here.has ? "" : " (none)"}</span>
      <span class="spacer"></span>
      <button onClick=${nextDiv} title="next frame over threshold after the cursor">next ⟂</button>
      <button onClick=${goWorst} title=${`worst frame: f${worst.i} (${worst.meanabs})`}>⟶ worst</button>
    </div>
  </div>`;
}
