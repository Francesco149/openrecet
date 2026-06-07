// web/components/VerdictPanel.mjs — the phase/RNG verdict text (flow_diff --verdict:
// ALIGNED / CONST-OFFSET / DRIFT + rngcalls). Lifted from the old app.mjs VerdictPanel;
// segment-aware — shows the ACTIVE segment's verdict when the timeline carries one,
// else the whole-session manifest verdict. For ONE gameplay segment they're the same.
import { html } from "/vendor/htm-preact-standalone.mjs";

export function VerdictPanel({ view, cur }) {
  const { seg } = view.locate(cur);
  const v = (seg && seg.verdict) || view.manifest.verdict;
  const t = v && v.text;
  return html`<section class="panel"><h3>phase/RNG verdict
      ${view.segments.length > 1 && html`<span class="dim">· seg#${seg.idx}</span>`}</h3>
    <pre class="verdict">${t || "(capture with --call-trace for the verdict)"}</pre></section>`;
}
