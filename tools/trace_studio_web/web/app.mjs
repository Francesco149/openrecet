// web/app.mjs — the v2 SPA root (composition only). S6 landed the core scrub
// experience (Filmstrip + VideoStage + ScrubBar + DiffRibbon); S7 adds the side
// panels on one shared model + one job poller: JobTray (GET /api/jobs), RecordPanel,
// IteratePanel, the registry-driven MarkBar (GET /api/registries), StatePanel,
// VerdictPanel. The trace editor (S9) is still the only thing the old monolith has
// that this doesn't.
import { html, render, useState, useEffect, useRef, useCallback } from "/vendor/htm-preact-standalone.mjs";
import { qparam, postJSON } from "/store.mjs";
import { useStudioModel, useRegistries, useJobs, jobOf } from "/web/model.mjs";
import { recapture } from "/web/actions.mjs";
import { SessionPicker } from "/web/components/SessionPicker.mjs";
import { Filmstrip } from "/web/components/Filmstrip.mjs";
import { VideoStage } from "/web/components/VideoStage.mjs";
import { ScrubBar } from "/web/components/ScrubBar.mjs";
import { DiffRibbon } from "/web/components/DiffRibbon.mjs";
import { DrillBar } from "/web/components/DrillBar.mjs";
import { TraceEditor } from "/web/components/TraceEditor.mjs";
import { JobTray } from "/web/components/JobTray.mjs";
import { RecordPanel } from "/web/components/RecordPanel.mjs";
import { IteratePanel } from "/web/components/IteratePanel.mjs";
import { MarkBar } from "/web/components/MarkBar.mjs";
import { StatePanel } from "/web/components/StatePanel.mjs";
import { VerdictPanel } from "/web/components/VerdictPanel.mjs";

const SESS = qparam("session");

// Viewport branch for the responsive panel layout (S7c). Wide (maximized) keeps the
// per-frame State alone in a sticky sidebar beside the videos with Verdict+Marks below;
// narrow (≈half-screen) drops the sidebar, folds Marks into the session-tools
// disclosure, and sets Verdict+State side-by-side — so frame state stays the focus and
// the occasional actions tuck away.
function useWide(minPx) {
  const [wide, setWide] = useState(true);
  useEffect(() => {
    const mq = window.matchMedia(`(min-width:${minPx}px)`);
    const on = () => setWide(mq.matches);
    on();
    mq.addEventListener("change", on);
    return () => mq.removeEventListener("change", on);
  }, [minPx]);
  return wide;
}

// The panel cluster, arranged by viewport. The collapsible "session tools" always holds
// the occasional actions (mark · record · iterate); State + Verdict are the always-on
// reference. Wide: a left column (videos → tools fold → Verdict full-width bottom) beside
// a full-height sticky State sidebar. Narrow: full-width videos → tools fold →
// Verdict|State side-by-side at equal height.
function layout(p) {
  const { wide, SESS, view, cur, setCur, N, panels, setPendingBox, pendingBox,
          registries, marks, setMarks, manifest, reload, recJob, capJob, pollJobs } = p;
  const videoBlock = html`<div class="vidblock">
    <${VideoStage} sess=${SESS} view=${view} cur=${cur} panels=${panels} onBox=${setPendingBox} />
    <${ScrubBar} N=${N} cur=${cur} setCur=${setCur} />
    <${DiffRibbon} view=${view} cur=${cur} setCur=${setCur} />
    <div class="hint">filmstrip = scrubber · ←/→ ±10 · ,/. ±1 · Home/End · 1/2/3 panels ·
      drag a box on a frame → crop ref${pendingBox ? ` · box ${pendingBox.join(",")}` : ""}</div>
  </div>`;
  const statePanel = html`<${StatePanel} view=${view} cur=${cur} />`;
  const verdictPanel = html`<${VerdictPanel} view=${view} cur=${cur} />`;
  const tools = html`<details class="tools-fold">
    <summary>⚙ session tools <span class="dim">— mark · record · iterate</span></summary>
    <div class="panels">
      <${MarkBar} sess=${SESS} view=${view} cur=${cur} setCur=${setCur}
        markTypes=${registries.marks} marks=${marks} setMarks=${setMarks}
        pendingBox=${pendingBox} setPendingBox=${setPendingBox} />
      <${RecordPanel} recJob=${recJob} pollJobs=${pollJobs} />
      <${IteratePanel} sess=${SESS} manifest=${manifest} stale=${manifest.stale}
        reload=${reload} capJob=${capJob} pollJobs=${pollJobs} />
    </div>
  </details>`;

  if (wide) return [html`<div class="workarea">
    <div class="scrub-col">
      ${videoBlock}
      ${tools}
      <div class="below-panels">${verdictPanel}</div>
    </div>
    <aside class="ref-col">${statePanel}</aside>
  </div>`];
  return [
    videoBlock,
    tools,
    html`<div class="two-col">${verdictPanel}${statePanel}</div>`,
  ];
}

function App() {
  const { view, loading, error, manifest, reload, marks: marks0,
          traceOps, notes } = useStudioModel(SESS);
  const [cur, setCur] = useState(0);
  const [panels, setPanels] = useState({ port: true, retail: true, diff: true });
  const [pendingBox, setPendingBox] = useState(null);
  const [marks, setMarks] = useState([]);
  const registries = useRegistries();
  const [jobsStatus, pollJobs] = useJobs();
  const wide = useWide(1280);
  useEffect(() => { if (marks0) setMarks(marks0); }, [marks0]);

  // ── trace-editor working state (the editable trace ops + notes sidecar) ──
  // editTrace is the local working copy seeded from the persisted ops; onEdit debounces
  // a POST /trace (which marks the session stale). The edit→recapture loop flushes any
  // pending save first so a re-capture never misses the last keystroke.
  const [editTrace, setEditTrace] = useState(null);
  const [notesState, setNotesState] = useState([]);
  const [localStale, setLocalStale] = useState(false);
  const [edOpen, setEdOpen] = useState(false);
  const saveTimer = useRef(0);
  useEffect(() => { if (traceOps) setEditTrace(traceOps); }, [traceOps]);
  useEffect(() => { if (notes) setNotesState(notes); }, [notes]);
  const onEdit = useCallback((newOps) => {
    setEditTrace(newOps); setLocalStale(true);
    clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => postJSON(`/s/${SESS}/trace`, { ops: newOps }), 500);
  }, []);
  const onNotes = useCallback((ns) => {
    setNotesState(ns); postJSON(`/s/${SESS}/notes`, { notes: ns });
  }, []);
  const flushEdit = useCallback(() => {
    if (!saveTimer.current) return Promise.resolve();
    clearTimeout(saveTimer.current); saveTimer.current = 0;
    return editTrace ? postJSON(`/s/${SESS}/trace`, { ops: editTrace }) : Promise.resolve();
  }, [editTrace]);
  const onRecapture = useCallback(
    () => flushEdit().then(() => recapture(SESS, { pollJobs })), [flushEdit, pollJobs]);

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
  const recJob = jobOf(jobsStatus, "record");
  const capJob = jobOf(jobsStatus, "capture");
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
      <${JobTray} status=${jobsStatus} />
      <${Filmstrip} view=${view} cur=${cur} setCur=${setCur} />
      <${DrillBar} sess=${SESS} view=${view} cur=${cur} />
      <details class="trace-editor-fold" onToggle=${(e) => setEdOpen(e.currentTarget.open)}>
        <summary>⚙ trace editor <span class="dim">— advanced: edit inputs · pins ·
          {wait} anchors · extend + recapture</span></summary>
        ${edOpen && html`<${TraceEditor}
          editTrace=${editTrace || []} onEdit=${onEdit} capturedOps=${view.capturedOps}
          anchors=${view.anchors} manifest=${manifest} stale=${manifest.stale || localStale}
          notes=${notesState} onNotes=${onNotes}
          pendingBox=${pendingBox} setPendingBox=${setPendingBox}
          cur=${cur} setCur=${setCur} view=${view} onRecapture=${onRecapture} />`}
      </details>
      <div class="layout-bar"><span>panels:</span>
        ${["port", "retail", "diff"].map((p) =>
          html`<button class=${"ly " + (panels[p] ? "on" : "")}
            onClick=${() => setPanels((s) => ({ ...s, [p]: !s[p] }))}>${p}</button>`)}
        <span class="spacer"></span>
        <span class="dim">seg#${seg.idx} · frame ${k}/${seg.nFrames - 1} · label ${seg.labelOf(k)}</span>
      </div>
      ${layout({
        wide, SESS, view, cur, setCur, N, panels, setPendingBox, pendingBox,
        registries, marks, setMarks, manifest, reload, recJob, capJob, pollJobs,
      })}
    </main>
  </div>`;
}

render(html`<${App} />`, document.getElementById("app"));
