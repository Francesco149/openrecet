// web/components/DrillBar.mjs — densify a strided overview sub-window in the browser
// (S8). The drill backend landed in S4: POST /s/<sess>/drill → model/drill.drill_window
// maps an overview VIEWER INDEX `at` → the dense anchor-relative window
// caprange.start + at*stride and spawns a `--capstride 1 --reset-trace` child capture
// (kind=drill). This is just the UI: shown only on a strided overview (active segment
// cadence > 1 — a cadence-1 session is already dense, nothing to drill). The cursor's
// global ordinal IS the 0-based viewer index `at` (the first gameplay segment is at
// offsetGlobal 0), so "@cursor" passes `cur` straight through; "whole segment" drills
// the active segment's full span. The capture is watched via the unified /api/jobs
// (the JobTray shows it as a `drill` job); on success it opens the child session.
import { html, useState } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, jobStatus } from "/store.mjs";
import { toast } from "/web/util.mjs";

export function DrillBar({ sess, view, cur }) {
  const { seg } = view.locate(cur);
  const [span, setSpan] = useState(48);
  const [busy, setBusy] = useState(false);
  if (!seg || seg.cadence <= 1) return null;     // already dense → nothing to densify

  const drill = (at, sp) => {
    setBusy(true); toast(`drilling @${at} ×${sp}…`);
    postJSON(`/s/${sess}/drill`, { at, span: sp }).then((r) => {
      if (!r.ok) { setBusy(false); return toast("drill: " + (r.error || "fail"), true); }
      const child = r.session;
      toast(`drill capturing → ${child} (watch the JobTray)`);
      const poll = () => jobStatus("capture").then((s) => {
        if (!s || s.running) { setTimeout(poll, 1500); return; }   // wait for the dense capture
        if (s.rc === 0 || s.rc === null) {
          toast("drill done → " + child);
          location.search = "?session=" + encodeURIComponent(child);
        } else { setBusy(false); toast("drill rc=" + s.rc, true); }
      });
      poll();
    }).catch(() => { setBusy(false); toast("drill request failed", true); });
  };

  return html`<div class="drillbar">
    <span class="dim">drill (densify ×${seg.cadence}):</span>
    <label class="span-in">span
      <input type="number" min="2" max="240" value=${span} disabled=${busy}
        onInput=${(e) => setSpan(Math.max(2, +e.target.value || 48))} /></label>
    <button disabled=${busy}
      title=${`dense capture of ${span} frames from viewer index ${cur}`}
      onClick=${() => drill(cur, span)}>⤓ @cursor f${cur}</button>
    <button disabled=${busy}
      title=${`dense capture of the whole active segment (${seg.nFrames}f ×${seg.cadence})`}
      onClick=${() => drill(seg.offsetGlobal, seg.nFrames * seg.cadence)}>⤓ whole segment</button>
    ${busy && html`<span class="dim">⟳ capturing…</span>`}
  </div>`;
}
