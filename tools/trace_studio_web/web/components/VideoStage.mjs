// web/components/VideoStage.mjs — port|retail|diff videos in lockstep, segment-aware.
// Lifted from the old app.mjs VideoStage. The global cursor `cur` maps via
// view.locate(cur) → {seg, k}; each panel's src is seg.videos[panel] (falls back to
// the whole-session videos in model.mjs) and the seek time is seg.videoTime(k) =
// (k+0.5)/fps. For ONE gameplay segment this is byte-identical to the old behavior;
// when the cursor crosses into another segment the <video src> swaps + re-seeks.
import { html, useRef, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { BUST } from "/store.mjs";
import { copy } from "/web/util.mjs";

const ORDER = ["port", "retail", "diff"];

// box-select on a video → crop ref (copies a `crop …` string + reports the box).
function attachBox(v, getCtx, onBox) {
  let sx = 0, sy = 0, drag = false, moved = false, box = null;
  const clamp = (cx, cy) => { const r = v.getBoundingClientRect();
    return [Math.min(Math.max(cx, r.left), r.right),
            Math.min(Math.max(cy, r.top), r.bottom)]; };
  const toNat = (cx, cy) => { const r = v.getBoundingClientRect();
    return [Math.round((cx - r.left) / r.width * (v.videoWidth || r.width)),
            Math.round((cy - r.top) / r.height * (v.videoHeight || r.height))]; };
  v.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return; drag = true; moved = false; sx = e.clientX; sy = e.clientY;
    try { v.setPointerCapture(e.pointerId); } catch {} e.preventDefault();
  });
  v.addEventListener("pointermove", (e) => {
    if (!drag) return;
    if (!moved && Math.abs(e.clientX - sx) + Math.abs(e.clientY - sy) < 4) return;
    moved = true;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    if (!box) { box = document.createElement("div"); box.className = "box-sel";
      document.body.appendChild(box); }
    box.style.left = Math.min(ax, bx) + "px"; box.style.top = Math.min(ay, by) + "px";
    box.style.width = Math.abs(bx - ax) + "px"; box.style.height = Math.abs(by - ay) + "px";
  });
  v.addEventListener("pointerup", (e) => {
    if (!drag) return; drag = false; try { v.releasePointerCapture(e.pointerId); } catch {}
    if (box) { box.remove(); box = null; } if (!moved) return;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    const [x0, y0] = toNat(Math.min(ax, bx), Math.min(ay, by));
    const [x1, y1] = toNat(Math.max(ax, bx), Math.max(ay, by));
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    const c = getCtx();
    copy(`crop id=${c.sess} box=${x0},${y0},${x1},${y1} size=${v.videoWidth}x${v.videoHeight} `
       + `frame=f=${c.frame} label=${c.label} panel=${c.panel}`);
    if (onBox) onBox([x0, y0, x1, y1]);
  });
}

export function VideoStage({ sess, view, cur, panels, onBox }) {
  const refs = useRef({});
  const { seg, k } = view.locate(cur);
  // latest ctx for the box-select closure (frame = LOCAL ordinal, matching marks/apply)
  const ctxRef = useRef({});
  ctxRef.current = { sess, frame: k, label: seg.labelOf(k), seg, k };

  // src swap when the active segment's per-panel video changes
  useEffect(() => {
    for (const panel of ORDER) {
      const el = refs.current[panel]; if (!el) continue;
      const fn = seg.videos[panel];
      if (fn && !el.src.endsWith(`${fn}?v=${BUST}`)) el.src = `/s/${sess}/${fn}?v=${BUST}`;
    }
  }, [sess, seg]);

  // lockstep seek to (k+0.5)/fps on cursor / segment change
  useEffect(() => {
    const t = seg.videoTime(k);
    for (const panel of ORDER) {
      const el = refs.current[panel];
      if (el && Math.abs(el.currentTime - t) > 1e-4) el.currentTime = t;
    }
  }, [cur, seg, k]);

  return html`<div class="stage">
    ${ORDER.map((panel) => {
      if (!panels[panel]) return null;
      const fn = seg.videos[panel];
      return html`<div class="vpanel" key=${panel}>
        <video class="tv" muted playsinline preload="auto"
          ref=${(el) => {
            if (el && refs.current[panel] !== el) {
              refs.current[panel] = el;
              if (fn) {
                el.src = `/s/${sess}/${fn}?v=${BUST}`;
                attachBox(el, () => ({ ...ctxRef.current, panel }), onBox);
                el.addEventListener("loadeddata", () => {
                  el.currentTime = ctxRef.current.seg.videoTime(ctxRef.current.k); });
              }
            }
          }}></video>
        <div class="label">${panel}${fn ? "" : " (none)"}</div>
      </div>`;
    })}
  </div>`;
}
