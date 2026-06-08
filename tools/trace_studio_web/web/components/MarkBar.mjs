// web/components/MarkBar.mjs — mark-the-frame, rendered from the /api/registries
// mark-type list (so adding a kind in edits/marks.py surfaces a button with zero JS
// edits). Lifted from the old app.mjs MarksPanel; the coordinate-contract change is
// that a mark persists the LOCAL ordinal `k` (view.locate(cur).k) as its `frame` —
// apply.py does its own `caprange.start + k`, and marks/apply only run on dense
// 1-segment sessions where k === the global cursor anyway.
import { html, useState, useRef, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, BUST } from "/store.mjs";
import { toast } from "/web/util.mjs";

const pad5 = (n) => String(n).padStart(5, "0");
const frameURL = (sess, side, label) => `/s/${sess}/${side}/frames/frame_${pad5(label)}.png?v=${BUST}`;

// One side's crop: the box region of that side's native-res frame_<label>.png, scaled to `w`
// (the full frame is positioned behind a clip window — box-select coords map 1:1). Shows a
// "—" placeholder if that side's frame png is absent.
function CropCell({ sess, side, label, box, w }) {
  const [nat, setNat] = useState(null);
  const [bad, setBad] = useState(false);
  const [x0, y0, x1, y1] = box;
  const bw = Math.max(1, x1 - x0), bh = Math.max(1, y1 - y0), scale = w / bw, h = Math.round(bh * scale);
  if (bad) return html`<div class="crop-cell missing" style=${`width:${w}px;height:${h}px`}><span class="crop-lbl">${side} —</span></div>`;
  const imgStyle = nat
    ? `position:absolute;left:${-x0 * scale}px;top:${-y0 * scale}px;width:${nat.w * scale}px;height:${nat.h * scale}px`
    : "position:absolute;visibility:hidden";
  return html`<div class=${"crop-cell s-" + side} style=${`width:${w}px;height:${h}px`}>
    <img src=${frameURL(sess, side, label)} style=${imgStyle}
      onLoad=${(e) => setNat({ w: e.target.naturalWidth, h: e.target.naturalHeight })}
      onError=${() => setBad(true)} />
    <span class="crop-lbl">${side}</span></div>`;
}

// White-diff of the box region (|retail − port| per pixel, thresholded). Computed client-side
// on a canvas from both native PNGs (same-origin → getImageData is allowed). Hidden if either
// side's frame is missing.
function DiffCell({ sess, label, box, w }) {
  const ref = useRef(null);
  const [ok, setOk] = useState(true);
  const [x0, y0, x1, y1] = box;
  const bw = Math.max(1, x1 - x0), bh = Math.max(1, y1 - y0), h = Math.round(bh * w / bw);
  useEffect(() => {
    let dead = false;
    const load = (side) => new Promise((res, rej) => { const im = new Image(); im.onload = () => res(im); im.onerror = rej; im.src = frameURL(sess, side, label); });
    Promise.all([load("retail"), load("port")]).then(([r, p]) => {
      if (dead || !ref.current) return;
      const pix = (im) => { const c = document.createElement("canvas"); c.width = bw; c.height = bh; const g = c.getContext("2d"); g.drawImage(im, x0, y0, bw, bh, 0, 0, bw, bh); return g.getImageData(0, 0, bw, bh); };
      const rd = pix(r).data, pd = pix(p).data;
      const cvs = ref.current; cvs.width = bw; cvs.height = bh; const g = cvs.getContext("2d");
      const out = g.createImageData(bw, bh);
      for (let i = 0; i < rd.length; i += 4) {
        const d = Math.abs(rd[i] - pd[i]) + Math.abs(rd[i + 1] - pd[i + 1]) + Math.abs(rd[i + 2] - pd[i + 2]);
        const v = d > 24 ? 255 : 0; out.data[i] = out.data[i + 1] = out.data[i + 2] = v; out.data[i + 3] = 255;
      }
      g.putImageData(out, 0, 0);
    }).catch(() => { if (!dead) setOk(false); });
    return () => { dead = true; };
  }, [sess, label]);                                       // canvas is native-res; `w` is CSS only
  if (!ok) return html`<div class="crop-cell missing" style=${`width:${w}px;height:${h}px`}><span class="crop-lbl">diff —</span></div>`;
  return html`<div class="crop-cell" style=${`width:${w}px;height:${h}px`}>
    <canvas ref=${ref} style=${`width:${w}px;height:${h}px;image-rendering:pixelated`}></canvas>
    <span class="crop-lbl">white-diff</span></div>`;
}

// full-screen lightbox: retail | port | white-diff at a zoomable scale. Starts at 2× (or
// shrunk to fit the window), mouse-wheel to zoom, ✕ / Esc / backdrop-click to close.
function CropModal({ sess, label, box, onClose }) {
  const [x0, y0, x1, y1] = box;
  const bw = Math.max(1, x1 - x0), bh = Math.max(1, y1 - y0);
  const [scale, setScale] = useState(() => {
    const fitW = (window.innerWidth * 0.92) / (3 * bw + 32);
    const fitH = (window.innerHeight * 0.82) / (bh + 24);
    return Math.max(0.25, Math.min(2, fitW, fitH));        // 2×, or fit-to-window if too big
  });
  const rootRef = useRef(null);
  const pan = useRef({ active: false });
  useEffect(() => {
    const el = rootRef.current; if (!el) return;
    const onWheel = (e) => { e.preventDefault(); setScale((s) => Math.max(0.2, Math.min(16, s * (e.deltaY < 0 ? 1.15 : 1 / 1.15)))); };
    const onKey = (e) => { if (e.key === "Escape") onClose(); };
    el.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("keydown", onKey);
    return () => { el.removeEventListener("wheel", onWheel); window.removeEventListener("keydown", onKey); };
  }, [onClose]);
  const w = Math.round(bw * scale);
  // left OR middle drag pans (scrolls the overflowing lightbox); a non-dragged left click on
  // the backdrop closes (the ✕ + Esc always close).
  const onDown = (e) => {
    if (e.button !== 0 && e.button !== 1) return;
    const el = rootRef.current;
    pan.current = { active: true, btn: e.button, x: e.clientX, y: e.clientY, sl: el.scrollLeft, st: el.scrollTop, moved: false };
    if (e.button === 1) e.preventDefault();
  };
  const onMove = (e) => {
    if (!pan.current.active) return;
    const el = rootRef.current, dx = e.clientX - pan.current.x, dy = e.clientY - pan.current.y;
    if (Math.abs(dx) + Math.abs(dy) > 3) pan.current.moved = true;
    el.scrollLeft = pan.current.sl - dx; el.scrollTop = pan.current.st - dy;
  };
  const onUp = (e) => {
    if (!pan.current.active) return;
    const { btn, moved } = pan.current; pan.current.active = false;
    if (btn === 0 && !moved && e.target === rootRef.current) onClose();
  };
  return html`<div class="lightbox" ref=${rootRef}
      onPointerDown=${onDown} onPointerMove=${onMove} onPointerUp=${onUp}>
    <button class="lb-close" onClick=${(e) => { e.stopPropagation(); onClose(); }} title="close (Esc)">✕</button>
    <div class="lb-info">retail | port | white-diff · frame ${label} · ${Math.round(scale * 100)}% <span class="dim">(wheel zoom · drag pan)</span></div>
    <div class="lb-stage">
      <${CropCell} sess=${sess} side="retail" label=${label} box=${box} w=${w} />
      <${CropCell} sess=${sess} side="port" label=${label} box=${box} w=${w} />
      <${DiffCell} sess=${sess} label=${label} box=${box} w=${w} />
    </div>
  </div>`;
}

// retail | port | white-diff thumbnail of a marked box. Click opens the lightbox.
function CropThumb({ sess, label, box, thumbW = 116 }) {
  const [open, setOpen] = useState(false);
  return html`<div class="crop-row" onClick=${() => setOpen(true)}
      title=${`retail | port | white-diff · frame ${label} · box ${box.join(",")} · click to open`}>
    <${CropCell} sess=${sess} side="retail" label=${label} box=${box} w=${thumbW} />
    <${CropCell} sess=${sess} side="port" label=${label} box=${box} w=${thumbW} />
    <${DiffCell} sess=${sess} label=${label} box=${box} w=${thumbW} />
    ${open && html`<${CropModal} sess=${sess} label=${label} box=${box} onClose=${() => setOpen(false)} />`}
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
    <div class="marks">${marks.length ? marks.map((m, i) => {
      // a mark's frame can fall OUTSIDE the captured window (e.g. the window was shrunk after
      // marking) — there is then no frame png. Surface that clearly instead of blank crops.
      const seg0 = view.segments[0];
      const nCap = seg0 ? seg0.nFrames : view.totalFrames;
      const oow = m.box && seg0 && (m.frame < 0 || m.frame >= nCap);
      return html`<div class="m" key=${i}>
        <div><button class="x" onClick=${() => del(i)}>✕</button>
          <span class="k">${m.kind}</span> @<a href="#"
            onClick=${(e) => { e.preventDefault(); seek(m); }}>${m.frame}</a>
          ${m.note ? " — " + m.note : ""}${m.box ? html` <span class="dim">[box]</span>` : ""}</div>
        ${oow
          ? html`<div class="crop-oow" title="re-capture with a wider/shifted window to include this frame">⚠ frame ${m.frame} is outside the captured window (0–${Math.max(0, nCap - 1)})</div>`
          : (m.box && seg0 && html`<${CropThumb} sess=${sess} label=${seg0.labelOf(m.frame)} box=${m.box} />`)}
      </div>`;
    })
      : "(none)"}</div>
  </section>`;
}
