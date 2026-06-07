// web/components/ScrubBar.mjs — fine-grain frame stepper (lifted from app.mjs),
// operating in GLOBAL ordinal space [0, totalFrames). The Filmstrip is the coarse
// scrubber; this is the ±1 nudge + the range slider.
import { html } from "/vendor/htm-preact-standalone.mjs";

export function ScrubBar({ N, cur, setCur }) {
  const step = (d) => setCur(Math.max(0, Math.min(N - 1, cur + d)));
  return html`<div class="scrub">
    <button onClick=${() => setCur(0)} title="first (Home)">⏮</button>
    <button onClick=${() => step(-10)} title="−10 (←)">−10</button>
    <button onClick=${() => step(-1)} title="−1 (,)">−1</button>
    <div class="track-wrap">
      <input type="range" min="0" max=${N - 1} value=${cur}
        onInput=${(e) => setCur(+e.target.value)} />
    </div>
    <button onClick=${() => step(1)} title="+1 (.)">+1</button>
    <button onClick=${() => step(10)} title="+10 (→)">+10</button>
    <button onClick=${() => setCur(N - 1)} title="last (End)">⏭</button>
    <span class="pos">${cur} / ${N - 1}</span>
  </div>`;
}
