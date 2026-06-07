// web/components/MarkBar.mjs — mark-the-frame, rendered from the /api/registries
// mark-type list (so adding a kind in edits/marks.py surfaces a button with zero JS
// edits). Lifted from the old app.mjs MarksPanel; the coordinate-contract change is
// that a mark persists the LOCAL ordinal `k` (view.locate(cur).k) as its `frame` —
// apply.py does its own `caprange.start + k`, and marks/apply only run on dense
// 1-segment sessions where k === the global cursor anyway.
import { html, useState } from "/vendor/htm-preact-standalone.mjs";
import { postJSON } from "/store.mjs";
import { toast } from "/web/util.mjs";

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
      html`<div class="m"><button class="x" onClick=${() => del(i)}>✕</button>
        <span class="k">${m.kind}</span> @<a href="#"
          onClick=${(e) => { e.preventDefault(); seek(m); }}>${m.frame}</a>
        ${m.note ? " — " + m.note : ""}${m.box ? html` <span class="dim">[box]</span>` : ""}</div>`)
      : "(none)"}</div>
  </section>`;
}
