// web/app.mjs — the v2 SPA root (composition only). S6: the core scrub experience —
// Filmstrip (the segmented scrubber) + VideoStage (lockstep port|retail|diff) +
// ScrubBar + DiffRibbon, on the segmented client model. State/marks/jobs panels land
// in S7; the trace editor in S9.
import { html, render, useState, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { qparam } from "/store.mjs";
import { useStudioModel } from "/web/model.mjs";
import { SessionPicker } from "/web/components/SessionPicker.mjs";
import { Filmstrip } from "/web/components/Filmstrip.mjs";
import { VideoStage } from "/web/components/VideoStage.mjs";
import { ScrubBar } from "/web/components/ScrubBar.mjs";
import { DiffRibbon } from "/web/components/DiffRibbon.mjs";

const SESS = qparam("session");

function App() {
  const { view, loading, error, manifest } = useStudioModel(SESS);
  const [cur, setCur] = useState(0);
  const [panels, setPanels] = useState({ port: true, retail: true, diff: true });
  const [pendingBox, setPendingBox] = useState(null);
  const N = view ? view.totalFrames : 1;

  // keyboard nav (global ordinal space)
  useEffect(() => {
    const onKey = (e) => {
      if (/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      const k = e.key, step = (d) => setCur((c) => Math.max(0, Math.min(N - 1, c + d)));
      if (k === "ArrowLeft") step(-10); else if (k === "ArrowRight") step(10);
      else if (k === ",") step(-1); else if (k === ".") step(1);
      else if (k === "Home") setCur(0); else if (k === "End") setCur(N - 1);
      else if (k === "1") setPanels((p) => ({ ...p, port: !p.port }));
      else if (k === "2") setPanels((p) => ({ ...p, retail: !p.retail }));
      else if (k === "3") setPanels((p) => ({ ...p, diff: !p.diff }));
      else return;
      e.preventDefault();
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [N]);

  if (loading) return html`<div class="pad">loading ${SESS}…</div>`;
  if (error) return html`<div class="pad">
    <${SessionPicker} current=${SESS} /> <div class="err-box">error: ${error}</div></div>`;
  if (!view) return html`<div class="pad">no session — <${SessionPicker} current=${SESS} /></div>`;

  const { seg, k } = view.locate(cur);
  return html`<div>
    <header>
      <h1>trace studio <span class="dim">v2</span> · <span class="accent">${SESS}</span></h1>
      <div class="status">
        <span>${view.totalFrames}f · ${view.fps}fps · ${view.target}${
          view.callTrace ? " · flow-trace" : ""}</span>
        <span class="sep">·</span><${SessionPicker} current=${SESS} />
      </div>
    </header>
    <main>
      <div class="note">trace: ${manifest.working_trace || manifest.trace}
        · caprange ${JSON.stringify(manifest.caprange)}</div>
      ${manifest.capture_error && html`<div class="err-box">⚠ ${manifest.capture_error}</div>`}
      <${Filmstrip} view=${view} cur=${cur} setCur=${setCur} />
      <div class="layout-bar"><span>panels:</span>
        ${["port", "retail", "diff"].map((p) =>
          html`<button class=${"ly " + (panels[p] ? "on" : "")}
            onClick=${() => setPanels((s) => ({ ...s, [p]: !s[p] }))}>${p}</button>`)}
        <span class="spacer"></span>
        <span class="dim">seg#${seg.idx} · frame ${k}/${seg.nFrames - 1} · label ${seg.labelOf(k)}</span>
      </div>
      <${VideoStage} sess=${SESS} view=${view} cur=${cur} panels=${panels} onBox=${setPendingBox} />
      <${ScrubBar} N=${N} cur=${cur} setCur=${setCur} />
      <${DiffRibbon} view=${view} cur=${cur} setCur=${setCur} />
      <div class="hint">filmstrip = scrubber · ←/→ ±10 · ,/. ±1 · Home/End · 1/2/3 panels ·
        drag a box on a frame → crop ref${pendingBox ? ` · box ${pendingBox.join(",")}` : ""}</div>
    </main>
  </div>`;
}

render(html`<${App} />`, document.getElementById("app"));
