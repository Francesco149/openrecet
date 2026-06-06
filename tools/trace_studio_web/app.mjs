// app.mjs — Trace Studio (Preact + htm, no build step).
// Step 0: the existing scrubber studio rebuilt as components. The timeline editor
// (4 tracks) is layered on in later steps; this keeps the working loop alive.
import {
  html, render, useState, useEffect, useRef, useMemo, useCallback,
} from "/vendor/htm-preact-standalone.mjs";
import {
  BUST, qparam, postJSON, getText, parseJSONL,
  useSession, useSessions, useStatus,
} from "/store.mjs";

const SESS = qparam("session");

// ─── tiny helpers ────────────────────────────────────────────────────────────
let toastEl = null, toastT = 0;
export function toast(t, err) {
  if (toastEl) toastEl.remove();
  toastEl = document.createElement("div");
  toastEl.className = "toast" + (err ? " err" : "");
  toastEl.textContent = t;
  document.body.appendChild(toastEl);
  clearTimeout(toastT);
  toastT = setTimeout(() => { if (toastEl) { toastEl.remove(); toastEl = null; } }, 4000);
}
function copy(t) {
  navigator.clipboard?.writeText(t).then(() => toast("copied ✓ " + t), () => toast(t, true));
}
const fmt = (v) => v === undefined ? "·"
  : (typeof v === "number" ? (Number.isInteger(v) ? v : v.toFixed(3)) : v);

// ─── box-select on a video → crop ref (returns box + copies a `crop …` string) ─
function attachBox(v, getCtx, onBox) {
  let sx = 0, sy = 0, drag = false, moved = false, box = null;
  const clamp = (cx, cy) => { const r = v.getBoundingClientRect();
    return [Math.min(Math.max(cx, r.left), r.right), Math.min(Math.max(cy, r.top), r.bottom)]; };
  const toNat = (cx, cy) => { const r = v.getBoundingClientRect();
    return [Math.round((cx - r.left) / r.width * (v.videoWidth || r.width)),
            Math.round((cy - r.top) / r.height * (v.videoHeight || r.height))]; };
  v.addEventListener("pointerdown", e => {
    if (e.button !== 0) return; drag = true; moved = false; sx = e.clientX; sy = e.clientY;
    try { v.setPointerCapture(e.pointerId); } catch {} e.preventDefault();
  });
  v.addEventListener("pointermove", e => {
    if (!drag) return;
    if (!moved && Math.abs(e.clientX - sx) + Math.abs(e.clientY - sy) < 4) return;
    moved = true;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    if (!box) { box = document.createElement("div"); box.className = "box-sel"; document.body.appendChild(box); }
    box.style.left = Math.min(ax, bx) + "px"; box.style.top = Math.min(ay, by) + "px";
    box.style.width = Math.abs(bx - ax) + "px"; box.style.height = Math.abs(by - ay) + "px";
  });
  v.addEventListener("pointerup", e => {
    if (!drag) return; drag = false; try { v.releasePointerCapture(e.pointerId); } catch {}
    if (box) { box.remove(); box = null; } if (!moved) return;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    const [x0, y0] = toNat(Math.min(ax, bx), Math.min(ay, by));
    const [x1, y1] = toNat(Math.max(ax, bx), Math.max(ay, by));
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    const ctx = getCtx();
    copy(`crop id=${SESS} box=${x0},${y0},${x1},${y1} size=${v.videoWidth}x${v.videoHeight} frame=f=${ctx.frame} panel=${ctx.panel}`);
    if (onBox) onBox([x0, y0, x1, y1]);
  });
}

// ─── video stage (port|retail|diff, lockstep seek) ───────────────────────────
function VideoStage({ sess, manifest, fps, cur, panels, onBox }) {
  const refs = useRef({});
  const N = (manifest.frame_range ? manifest.frame_range[1] + 1 : manifest.n_frames) || 1;
  // seek all videos to cur (+0.5 frame for exactness with all-intra)
  useEffect(() => {
    const t = (Math.min(cur, N - 1) + 0.5) / fps;
    for (const k in refs.current) {
      const v = refs.current[k];
      if (v && Math.abs(v.currentTime - t) > 1e-4) v.currentTime = t;
    }
  }, [cur, fps, N]);
  const order = ["port", "retail", "diff"];
  return html`<div class="stage">
    ${order.map(panel => {
      const src = (manifest.videos || {})[panel];
      if (!panels[panel]) return null;
      return html`<div class="vpanel" key=${panel}>
        <video class="tv" muted playsinline preload="auto"
          ref=${el => {
            if (el && refs.current[panel] !== el) {
              refs.current[panel] = el;
              if (src) { el.src = `/s/${sess}/${src}?v=${BUST}`;
                attachBox(el, () => ({ frame: cur, panel }), onBox);
                el.addEventListener("loadeddata", () => { el.currentTime = (cur + 0.5) / fps; }); }
            }
          }}></video>
        <div class="label">${panel}${src ? "" : " (none)"}</div>
      </div>`;
    })}
  </div>`;
}

// ─── scrub bar + frame nav ───────────────────────────────────────────────────
function ScrubBar({ N, cur, setCur, anchors, manifest }) {
  const step = d => setCur(Math.max(0, Math.min(N - 1, cur + d)));
  return html`<div class="scrub">
    <button onClick=${() => setCur(0)} title="first (Home)">⏮</button>
    <button onClick=${() => step(-10)} title="−10 (←)">−10</button>
    <button onClick=${() => step(-1)} title="−1 (,)">−1</button>
    <div class="track-wrap">
      <input type="range" min="0" max=${N - 1} value=${cur}
        onInput=${e => setCur(+e.target.value)} />
    </div>
    <button onClick=${() => step(1)} title="+1 (.)">+1</button>
    <button onClick=${() => step(10)} title="+10 (→)">+10</button>
    <button onClick=${() => setCur(N - 1)} title="last (End)">⏭</button>
    <span class="pos">${cur} / ${N - 1}</span>
  </div>`;
}

// ─── per-frame state (searchable; diff-highlighted) ──────────────────────────
function StatePanel({ row, hasCT, filter, setFilter }) {
  if (!hasCT) return html`<div class="state">(capture with --call-trace for state)</div>`;
  if (!row) return html`<div class="state">(no state at this frame)</div>`;
  let keys = [...new Set([...Object.keys(row.port || {}), ...Object.keys(row.retail || {})])].sort();
  if (filter) keys = keys.filter(k => k.toLowerCase().includes(filter.toLowerCase()));
  return html`<div>
    <input class="filter" placeholder="filter fields…" value=${filter}
      onInput=${e => setFilter(e.target.value)} />
    <div class="state"><table><tr><th>field</th><th>retail</th><th>port</th></tr>
    ${keys.map(k => {
      const r = row.retail?.[k], p = row.port?.[k];
      const cls = (r === undefined || p === undefined) ? ""
        : (JSON.stringify(r) === JSON.stringify(p) ? "same" : "diff");
      return html`<tr class=${cls} key=${k}><td>${k}</td><td>${fmt(r)}</td><td>${fmt(p)}</td></tr>`;
    })}</table></div></div>`;
}

// ─── marks panel ─────────────────────────────────────────────────────────────
function MarksPanel({ sess, marks, setMarks, cur, setCur, pendingBox, setPendingBox, note, setNote }) {
  const save = (m) => { setMarks(m); postJSON(`/s/${sess}/edits/set`, { edits: m }); };
  const add = (kind) => {
    if (!note && !pendingBox) {
      const i = marks.findIndex(m => m.frame === cur && m.kind === kind && !m.note && !m.box);
      if (i >= 0) { const m = marks.slice(); m.splice(i, 1); save(m); toast(`removed ${kind} @ ${cur}`); return; }
    }
    const mk = { frame: cur, kind }; if (note) mk.note = note; if (pendingBox) mk.box = pendingBox;
    save([...marks, mk]); toast(`marked ${kind} @ ${cur}`); setNote(""); setPendingBox(null);
  };
  const del = (i) => { const m = marks.slice(); m.splice(i, 1); save(m); };
  return html`<section class="panel"><h3>mark this frame</h3>
    <div class="mark-row">
      ${[["phasepin", "⟲ pin phase"], ["rngpin", "🎲 pin RNG"], ["anchor", "⚓ anchor"],
         ["feature", "✦ feature"], ["note", "✎ note"]].map(([k, lbl]) =>
        html`<button onClick=${() => add(k)}>${lbl}</button>`)}
    </div>
    <input type="text" placeholder="optional note…" value=${note} onInput=${e => setNote(e.target.value)} />
    ${pendingBox && html`<div class="dim">box attached: ${pendingBox.join(",")}</div>`}
    <h3 style="margin-top:.6rem">marks <button class="mini" onClick=${() => save([])}>clear all</button></h3>
    <div class="marks">${marks.length ? marks.map((m, i) =>
      html`<div class="m"><button class="x" onClick=${() => del(i)}>✕</button>
        <span class="k">${m.kind}</span> @<a href="#" onClick=${e => { e.preventDefault(); setCur(m.frame); }}>${m.frame}</a>
        ${m.note ? " — " + m.note : ""}${m.box ? html` <span class="dim">[box]</span>` : ""}</div>`)
      : "(none)"}</div>
  </section>`;
}

// ─── verdict ─────────────────────────────────────────────────────────────────
function VerdictPanel({ manifest }) {
  const t = manifest.verdict?.text;
  return html`<section class="panel"><h3>phase/RNG verdict</h3>
    <pre class="verdict">${t || "(capture with --call-trace for the verdict)"}</pre></section>`;
}

// ─── record panel ────────────────────────────────────────────────────────────
function RecordPanel() {
  const [rec, pollRec] = useStatus("/record/status");
  const [cap, pollCap] = useStatus("/capture/status");
  const [name, setName] = useState("");
  const [target, setTarget] = useState("both");
  const lastOut = rec && !rec.running && rec.exists ? rec.out : null;
  const start = () => postJSON("/record/start", { name }).then(r =>
    r.ok ? (toast("recording → " + r.out), pollRec()) : toast("start: " + (r.error || "fail"), true));
  const stop = () => { toast("stopping (finalising)…"); postJSON("/record/stop").then(r =>
    r.ok ? (toast(r.written ? `wrote ${(r.bytes/1024|0)}KB${r.recovered ? " (recovered)" : ""} → ${r.out}` : "no trace written — check log", !r.written), pollRec())
         : toast("stop fail", true)); };
  const view = () => { if (!lastOut) return toast("no recording yet", true);
    postJSON("/capture", { trace: lastOut, target, call_trace: true }).then(r => {
      if (!r.ok) return toast("capture: " + (r.error || "fail"), true);
      const poll = () => fetch("/capture/status").then(x => x.json()).then(s => {
        if (s.running) { pollCap(); setTimeout(poll, 2000); }
        else if (s.last_rc === 0 || s.last_rc === null) location.search = "?session=" + s.session;
        else toast("capture rc=" + s.last_rc, true);
      }); poll();
    }); };
  return html`<section class="panel rec-panel">
    <h3>record a trace <span class="dim">(retail · frida attach)</span></h3>
    <div class="rec-row">
      <input type="text" placeholder="trace name…" value=${name} onInput=${e => setName(e.target.value)} />
      <button onClick=${start} disabled=${rec?.running}>● start</button>
      <button onClick=${stop} disabled=${!rec?.running}>■ stop</button>
    </div>
    <div class="rec-status">${rec ? (rec.running
      ? `● recording "${rec.name}" · ${rec.elapsed_s}s · ${(rec.bytes/1024|0)}KB`
      : (rec.exists ? `■ stopped → ${rec.out}` : `idle · ${rec.remote || ""}`)) : "…"}</div>
    <div class="rec-row">
      <select onChange=${e => setTarget(e.target.value)}>
        <option value="openrecet">port only (fast)</option>
        <option value="both" selected>port + retail + diff</option>
      </select>
      <button onClick=${view} disabled=${!lastOut || cap?.running}>▶ view in studio</button>
    </div>
    <div class="rec-help dim">Get Recettear to the <b>title</b> via Steam first, then start.</div>
  </section>`;
}

// ─── iterate panel (apply + recapture + stale) ───────────────────────────────
function IteratePanel({ sess, manifest, reload }) {
  const [cap, pollCap] = useStatus("/capture/status");
  const stale = manifest.stale;
  const recapture = () => { toast("re-capturing…"); postJSON(`/s/${sess}/recapture`).then(r => {
    if (!r.ok) return toast("re-capture: " + (r.error || "fail"), true);
    const poll = () => fetch("/capture/status").then(x => x.json()).then(s => {
      if (s.running) { pollCap(); setTimeout(poll, 2000); }
      else if (s.last_rc === 0 || s.last_rc === null) { toast("capture updated"); location.reload(); }
      else toast("capture rc=" + s.last_rc, true);
    }); poll(); }); };
  const apply = () => { toast("applying pins…"); postJSON(`/s/${sess}/apply`, {}).then(r => {
    if (!r.ok) return toast("apply: " + (r.error || "fail"), true);
    toast(`applied ${r.pins_added} pin(s)`); reload();
    if (r.pins_added > 0) recapture();
  }); };
  return html`<section class="panel"><h3>iterate
      ${stale && html`<span class="stale-dot" title="edits not yet captured">● STALE</span>`}
      ${cap?.running && html`<span class="dim"> · ⟳ ${cap.elapsed_s}s</span>`}</h3>
    <div class="mark-row">
      <button onClick=${apply}>✓ apply pins</button>
      <button onClick=${recapture}>⟳ re-capture</button>
    </div>
    <div class="rec-status">${cap && !cap.running && cap.session ?
      (cap.last_rc === 0 || cap.last_rc === null ? `✓ ${cap.session}` : `✗ rc=${cap.last_rc}`) : "—"}</div>
  </section>`;
}

// ─── session picker (fuzzy) ──────────────────────────────────────────────────
function SessionPicker() {
  const list = useSessions();
  const go = (v) => { const names = list.map(s => s.name);
    const hit = names.includes(v) ? v : names.find(n => n.toLowerCase().includes(v.toLowerCase()));
    if (hit) location.search = "?session=" + encodeURIComponent(hit); };
  return html`<span>
    <input list="sess-list" placeholder="session…" value=${SESS}
      onChange=${e => go(e.target.value.trim())}
      onKeyDown=${e => { if (e.key === "Enter") go(e.target.value.trim()); }} />
    <datalist id="sess-list">${list.map(s =>
      html`<option value=${s.name}>${s.n_frames ? s.n_frames + "f" : ""}</option>`)}</datalist>
  </span>`;
}

// ─── app ─────────────────────────────────────────────────────────────────────
function App() {
  const { manifest, state, marks: marks0, anchors, loading, error, reload } = useSession(SESS);
  const [cur, setCur] = useState(0);
  const [marks, setMarks] = useState([]);
  const [panels, setPanels] = useState({ port: true, retail: true, diff: true });
  const [filter, setFilter] = useState("");
  const [note, setNote] = useState("");
  const [pendingBox, setPendingBox] = useState(null);
  useEffect(() => { if (marks0) setMarks(marks0); }, [marks0]);

  const N = manifest ? ((manifest.frame_range ? manifest.frame_range[1] + 1 : manifest.n_frames) || 1) : 1;
  const fps = manifest?.fps || 30;

  // keyboard nav
  useEffect(() => {
    const onKey = (e) => {
      if (/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
      const k = e.key, step = d => setCur(c => Math.max(0, Math.min(N - 1, c + d)));
      if (k === "ArrowLeft") step(-10); else if (k === "ArrowRight") step(10);
      else if (k === ",") step(-1); else if (k === ".") step(1);
      else if (k === "Home") setCur(0); else if (k === "End") setCur(N - 1);
      else if (k === "1") setPanels(p => ({ ...p, port: !p.port }));
      else if (k === "2") setPanels(p => ({ ...p, retail: !p.retail }));
      else if (k === "3") setPanels(p => ({ ...p, diff: !p.diff }));
      else return;
      e.preventDefault();
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [N]);

  if (loading) return html`<div class="pad">loading ${SESS}…</div>`;
  if (error) return html`<div class="pad">
    <${SessionPicker} /> <div class="err-box">error: ${error}</div></div>`;

  return html`<div>
    <header>
      <h1>trace studio · <span class="accent">${SESS}</span></h1>
      <div class="status">
        <span>${N} frames · ${fps}fps · ${manifest.target}${manifest.call_trace ? " · flow-trace" : ""}</span>
        <span class="sep">·</span><${SessionPicker} />
      </div>
    </header>
    <main>
      <div class="note">trace: ${manifest.working_trace || manifest.trace} · caprange ${JSON.stringify(manifest.caprange)}</div>
      ${manifest.capture_error && html`<div class="err-box">⚠ ${manifest.capture_error}</div>`}
      <div class="layout-bar"><span>panels:</span>
        ${["port", "retail", "diff"].map(p =>
          html`<button class=${"ly " + (panels[p] ? "on" : "")} onClick=${() => setPanels(s => ({ ...s, [p]: !s[p] }))}>${p}</button>`)}
      </div>
      <${VideoStage} sess=${SESS} manifest=${manifest} fps=${fps} cur=${cur} panels=${panels}
        onBox=${setPendingBox} />
      <${ScrubBar} N=${N} cur=${cur} setCur=${setCur} anchors=${anchors} manifest=${manifest} />
      <div class="hint">←/→ ±10 · ,/. ±1 · Home/End · 1/2/3 toggle panels · drag a box on a frame → crop ref</div>
      <div class="panels">
        <${RecordPanel} />
        <${IteratePanel} sess=${SESS} manifest=${manifest} reload=${reload} />
        <${MarksPanel} sess=${SESS} marks=${marks} setMarks=${setMarks} cur=${cur} setCur=${setCur}
          pendingBox=${pendingBox} setPendingBox=${setPendingBox} note=${note} setNote=${setNote} />
        <section class="panel"><h3>per-frame state</h3>
          <${StatePanel} row=${state[cur]} hasCT=${manifest.call_trace} filter=${filter} setFilter=${setFilter} /></section>
        <${VerdictPanel} manifest=${manifest} />
      </div>
    </main>
  </div>`;
}

render(html`<${App} />`, document.getElementById("app"));
