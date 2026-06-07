// web/app.mjs — the v2 SPA root (composition only). S5: model skeleton — header +
// session picker + a raw segment dump that verifies the segmented client model
// (the Filmstrip / VideoStage / DiffRibbon / panels land in later phases).
import { html, render } from "/vendor/htm-preact-standalone.mjs";
import { qparam } from "/store.mjs";
import { useStudioModel } from "/web/model.mjs";
import { SessionPicker } from "/web/components/SessionPicker.mjs";

const SESS = qparam("session");

// ── S5 model dump: prove the v2 view (segments / seams / coordinate maps) ──────
function SegDump({ view }) {
  const m = view.manifest;
  const seamRows = view.timeline
    .filter((e) => e.kind === "load_seam")
    .map((e, i) => html`<div key=${"s" + i}><span class="seam">⟂ load_seam</span> ${e.anchor}
      · port ${e.port_ticks}t / retail ${e.retail_ticks}t</div>`);
  const segRows = view.segments.map((s) => html`<div key=${"g" + s.idx}>
    <span class="game">▦ gameplay#${s.idx}</span> frames=[${s.frames.join(",")}]
    n=${s.nFrames} cadence=${s.cadence} off=${s.offsetGlobal}
    <span class=${"badge " + s.verdictClass}>${s.verdictClass}</span>
    · label[0..${s.nFrames - 1}]=${s.labelOf(0)}..${s.labelOf(s.nFrames - 1)}
    · vtime(0)=${s.videoTime(0).toFixed(3)}
    · diff(0)=${JSON.stringify(s.diffAt(0))}</div>`);
  const samples = [0, Math.floor(view.totalFrames / 2), view.totalFrames - 1]
    .map((g) => { const { seg, k } = view.locate(g);
      return html`<div key=${"l" + g}>g=${g} → seg#${seg.idx} k=${k}
        label=${seg.labelOf(k)} time=${seg.videoTime(k).toFixed(3)}s</div>`; });
  const reduces = view.segments.length === 1 && view.totalFrames === m.n_frames;
  return html`<div class="seg-dump">
    <div><span class="k">session</span> ${m.session} · schema_version=${m.schema_version || 1}
      · <span class="k">totalFrames</span>=${view.totalFrames}
      · manifest.n_frames=${m.n_frames} · fps=${view.fps}</div>
    <div class="k">── timeline (${view.timeline.length} entries: ${view.seams.length} seam,
      ${view.segments.length} gameplay) ──</div>
    ${seamRows}
    ${segRows}
    <div class="k">── locate() samples ──</div>
    ${samples}
    <div class=${"badge " + (reduces ? "green" : "amber")}>
      ${reduces ? "✓ 1-segment reduction holds (totalFrames === n_frames)"
                : "multi-segment (totalFrames ≠ n_frames — expected when split)"}</div>
  </div>`;
}

function App() {
  const { view, loading, error } = useStudioModel(SESS);
  if (loading) return html`<div class="pad">loading ${SESS}…</div>`;
  if (error) return html`<div class="pad">
    <${SessionPicker} current=${SESS} /> <div class="err-box">error: ${error}</div></div>`;
  if (!view) return html`<div class="pad">no session — <${SessionPicker} current=${SESS} /></div>`;
  return html`<div>
    <header>
      <h1>trace studio <span class="dim">v2</span> · <span class="accent">${SESS}</span></h1>
      <div class="status">
        <span>${view.totalFrames} frames · ${view.fps}fps · ${view.target}${
          view.callTrace ? " · flow-trace" : ""}</span>
        <span class="sep">·</span><${SessionPicker} current=${SESS} />
      </div>
    </header>
    <main>
      <${SegDump} view=${view} />
    </main>
  </div>`;
}

render(html`<${App} />`, document.getElementById("app"));
