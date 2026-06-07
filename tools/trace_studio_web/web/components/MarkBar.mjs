// web/components/MarkBar.mjs — mark-the-frame, rendered from the /api/registries
// mark-type list (so adding a kind in edits/marks.py surfaces a button with zero JS
// edits). Lifted from the old app.mjs MarksPanel; the coordinate-contract change is
// that a mark persists the LOCAL ordinal `k` (view.locate(cur).k) as its `frame` —
// apply.py does its own `caprange.start + k`, and marks/apply only run on dense
// 1-segment sessions where k === the global cursor anyway.
import { html, useState } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, BUST } from "/store.mjs";
import { toast } from "/web/util.mjs";

const pad5 = (n) => String(n).padStart(5, "0");

// Crop preview of a marked box, sampled from the captured PORT png at the mark's
// strided label (frame_<label>.png is native-res → the box-select coords map 1:1).
// Scales the crop to a fixed thumb width and positions the full frame behind a
// clip window, measuring the png's natural size on load. Hides itself if the frame
// png is absent (e.g. a mark whose label wasn't captured).
function CropThumb({ sess, label, box, thumbW = 132 }) {
  const [nat, setNat] = useState(null);
  const [bad, setBad] = useState(false);
  const [x0, y0, x1, y1] = box;
  const bw = Math.max(1, x1 - x0), bh = Math.max(1, y1 - y0);
  const scale = thumbW / bw;
  if (bad) return null;
  const imgStyle = nat
    ? `position:absolute;left:${-x0 * scale}px;top:${-y0 * scale}px;`
      + `width:${nat.w * scale}px;height:${nat.h * scale}px`
    : "position:absolute;visibility:hidden";
  return html`<div class="crop-thumb"
      style=${`width:${thumbW}px;height:${Math.round(bh * scale)}px`}
      title=${`port frame ${label} · box ${box.join(",")}`}>
    <img src=${`/s/${sess}/port/frames/frame_${pad5(label)}.png?v=${BUST}`} style=${imgStyle}
      onLoad=${(e) => setNat({ w: e.target.naturalWidth, h: e.target.naturalHeight })}
      onError=${() => setBad(true)} />
  </div>`;
}

export function MarkBar({ sess, view, cur, setCur, markTypes, marks, setMarks,
                         pendingBox, setPendingBox }) {
  const [note, setNote] = useState("");
  const { seg, k } = view.locate(cur);
  const save = (m) => { setMarks(m); postJSON(`/s/${sess}/edits/set`, { edits: m }); };

  const add = (kind) => {
    // bare (no note/box) re-click toggles the mark off
    if (!note && !pendingBox) {
      const i = marks.findIndex(
        (m) => m.frame === k && m.kind === kind && !m.note && !m.box);
      if (i >= 0) {
        const m = marks.slice(); m.splice(i, 1); save(m);
        toast(`removed ${kind} @ ${k}`); return;
      }
    }
    const mk = { frame: k, kind };
    if (note) mk.note = note;
    if (pendingBox) mk.box = pendingBox;
    save([...marks, mk]);
    toast(`marked ${kind} @ ${k}`); setNote(""); setPendingBox(null);
  };
  const del = (i) => { const m = marks.slice(); m.splice(i, 1); save(m); };
  const seek = (m) => setCur(view.globalOf(seg, m.frame));

  return html`<section class="panel"><h3>mark this frame <span class="dim">@ f${k}</span></h3>
    <div class="mark-row">
      ${markTypes.length
        ? markTypes.map((mt) => html`<button title=${mt.hint}
            onClick=${() => add(mt.kind)}>${mt.label}</button>`)
        : html`<span class="dim">(loading mark types…)</span>`}
    </div>
    <input type="text" placeholder="optional note…" value=${note}
      onInput=${(e) => setNote(e.target.value)} />
    ${pendingBox && html`<div class="dim">box attached: ${pendingBox.join(",")}
      <button class="mini" onClick=${() => setPendingBox(null)}>✕</button></div>`}
    <h3 style="margin-top:.6rem">marks
      <button class="mini" onClick=${() => save([])}>clear all</button></h3>
    <div class="marks">${marks.length ? marks.map((m, i) =>
      html`<div class="m" key=${i}>
        <div><button class="x" onClick=${() => del(i)}>✕</button>
          <span class="k">${m.kind}</span> @<a href="#"
            onClick=${(e) => { e.preventDefault(); seek(m); }}>${m.frame}</a>
          ${m.note ? " — " + m.note : ""}${m.box ? html` <span class="dim">[box]</span>` : ""}</div>
        ${m.box && view.segments[0] && html`<${CropThumb} sess=${sess}
          label=${view.segments[0].labelOf(m.frame)} box=${m.box} />`}
      </div>`)
      : "(none)"}</div>
  </section>`;
}
