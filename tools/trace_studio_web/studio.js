"use strict";
// ── trace studio viewer ──────────────────────────────────────────────────────
// Video-backed scrub of a port/retail/diff trace window. The three <video>s are
// all-intra (keyint=1) so seeking is frame-exact; we seek them in lockstep by
// frame index (currentTime = (i+0.5)/fps). Per-frame state overlay + verdict
// from the session manifest; marks POST to /s/<session>/edits.

const $ = (id) => document.getElementById(id);
const qs = new URLSearchParams(location.search);
const BUST = Date.now();          // per-page-load cache-buster for re-captured videos

let M = null;            // session.json manifest
let SESS = qs.get("session") || "";
let FPS = 30, N = 0;
let cur = 0;
let state = [];          // per-frame [{frame, port:{}, retail:{}}]
let diffPer = {};        // frame -> {differ, meanabs}
const videos = {};       // panel -> <video>
let playing = false, playTimer = 0;

// ── toast + clipboard (mirrors llm-feed) ─────────────────────────────────────
let toastEl = null, toastT = 0;
function toast(t, err) {
  if (toastEl) toastEl.remove();
  toastEl = document.createElement("div");
  toastEl.className = "toast" + (err ? " err" : "");
  toastEl.textContent = t;
  document.body.appendChild(toastEl);
  clearTimeout(toastT);
  toastT = setTimeout(() => { if (toastEl) { toastEl.remove(); toastEl = null; } }, 4000);
}
function copy(t, ok) {
  if (navigator.clipboard?.writeText)
    navigator.clipboard.writeText(t).then(() => toast(ok || ("copied ✓ " + t)),
                                          () => toast(t, true));
  else toast(t, true);
}

// ── seek + render ────────────────────────────────────────────────────────────
function timeFor(i) { return (i + 0.5) / FPS; }

function seekAll(i) {
  cur = Math.max(0, Math.min(N - 1, i | 0));
  const t = timeFor(cur);
  for (const k in videos) {
    const v = videos[k];
    if (v && Math.abs(v.currentTime - t) > 1e-4) v.currentTime = t;
  }
  $("ts-track").value = cur;
  $("ts-pos").textContent = `${cur} / ${N - 1}`;
  renderState();
  renderDiffStat();
  renderApplyCmd();
}
function step(d) { stop(); seekAll(cur + d); }

// ── state overlay ────────────────────────────────────────────────────────────
function renderState() {
  const row = state[cur] || state.find(r => r.frame === cur) || null;
  const box = $("ts-state");
  if (!row || (!Object.keys(row.port || {}).length && !Object.keys(row.retail || {}).length)) {
    box.textContent = M && M.call_trace ? "(no state at this frame)" : "(capture with --call-trace for state)";
    return;
  }
  const keys = [...new Set([...Object.keys(row.port || {}), ...Object.keys(row.retail || {})])].sort();
  let h = "<table><tr><th>field</th><th>retail</th><th>port</th></tr>";
  for (const k of keys) {
    const r = row.retail?.[k], p = row.port?.[k];
    const same = JSON.stringify(r) === JSON.stringify(p);
    const cls = (r === undefined || p === undefined) ? "" : (same ? "same" : "diff");
    h += `<tr class="${cls}"><td>${k}</td><td>${fmt(r)}</td><td>${fmt(p)}</td></tr>`;
  }
  h += "</table>";
  box.innerHTML = h;
}
function fmt(v) { return v === undefined ? "·" : (typeof v === "number" ? (Number.isInteger(v) ? v : v.toFixed(3)) : v); }

function renderDiffStat() {
  const d = diffPer[cur];
  $("ts-diffstat").innerHTML = d
    ? `differ px: <b>${d.differ}</b> · mean |Δ|: <b>${d.meanabs}</b>`
      + (d.differ === 0 ? " — <span style='color:var(--good)'>bit-identical</span>" : "")
    : "—";
}
function renderApplyCmd() {
  $("ts-apply-cmd").textContent =
    `python3 tools/trace_studio.py apply ${SESS}`;
}

// ── marks (client-side state → POST /edits/set; toggle, delete, clear) ────────
let pendingBox = null;
let marks = [];
function saveMarks() {
  return fetch(`/s/${SESS}/edits/set`, {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ edits: marks }),
  });
}
function addMark(kind) {
  const note = $("ts-mark-note").value.trim();
  // toggle: pressing the same kind at the same frame (no new note/box) removes it
  if (!note && !pendingBox) {
    const i = marks.findIndex(m => m.frame === cur && m.kind === kind && !m.note && !m.box);
    if (i >= 0) { marks.splice(i, 1); saveMarks(); renderMarks(); toast(`removed ${kind} @ ${cur}`); return; }
  }
  const m = { frame: cur, kind };
  if (note) m.note = note;
  if (pendingBox) m.box = pendingBox;
  marks.push(m);
  saveMarks(); renderMarks();
  toast(`marked ${kind} @ frame ${cur}` + (note ? ` — ${note}` : ""));
  $("ts-mark-note").value = ""; pendingBox = null;
}
function deleteMark(i) { marks.splice(i, 1); saveMarks(); renderMarks(); }
function clearMarks() { if (!marks.length) return; marks = []; saveMarks(); renderMarks(); toast("cleared all marks"); }
function loadMarks() {
  fetch(`/s/${SESS}/edits.jsonl`, { cache: "no-cache" })
    .then(r => r.ok ? r.text() : "")
    .then(txt => {
      marks = txt.split("\n").map(l => l.trim()).filter(Boolean)
        .map(l => { try { return JSON.parse(l); } catch { return null; } }).filter(Boolean);
      renderMarks();
    });
}
function renderMarks() {
  const box = $("ts-marks");
  if (!marks.length) { box.innerHTML = "(none)"; return; }
  box.innerHTML = marks.map((m, i) =>
    `<div class="m"><button class="x" data-i="${i}" title="delete">✕</button> `
    + `<span class="k">${m.kind}</span> @<a href="#" data-f="${m.frame}">${m.frame}</a>`
    + (m.note ? ` — ${esc(m.note)}` : "")
    + (m.box ? ` <span class="dim">[box]</span>` : "") + "</div>").join("");
  box.querySelectorAll("a[data-f]").forEach(a =>
    a.onclick = (e) => { e.preventDefault(); seekAll(+a.dataset.f); });
  box.querySelectorAll("button.x").forEach(b =>
    b.onclick = () => deleteMark(+b.dataset.i));
}
function esc(s) { return s.replace(/[<>&]/g, c => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;" }[c])); }

// ── iterate: apply pins + re-capture (the self-service loop) ──────────────────
let capPoll = 0;
function pollCapture(onDone) {
  fetch("/capture/status", { cache: "no-cache" }).then(r => r.json()).then(s => {
    if (s.running) {
      $("ts-cap-status").textContent = `⟳ capturing ${s.session} · ${s.elapsed_s}s · ${s.log_tail || ""}`.slice(0, 200);
      if (!capPoll) capPoll = setInterval(() => pollCapture(onDone), 2000);
    } else {
      if (capPoll) { clearInterval(capPoll); capPoll = 0; }
      const ok = s.last_rc === 0 || s.last_rc === null;
      $("ts-cap-status").textContent = s.session
        ? (ok ? `✓ capture done (${s.session})` : `✗ capture rc=${s.last_rc} — ${s.log_tail || ""}`)
        : "—";
      if (onDone) onDone(ok, s);
    }
  }).catch(() => {});
}
function doApply() {
  toast("applying pins…");
  fetch(`/s/${SESS}/apply`, { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({}) }).then(r => r.json()).then(res => {
    if (!res.ok) { toast("apply failed: " + (res.error || "?"), true); return; }
    toast(`applied ${res.pins_added} pin(s)` + (res.pins_dupe ? ` (${res.pins_dupe} dup)` : ""));
    loadMarks();
    if ($("ts-apply-recap").checked && res.pins_added > 0) doRecapture();
  }).catch(() => toast("apply failed", true));
}
function doRecapture() {
  toast("re-capturing…");
  fetch(`/s/${SESS}/recapture`, { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({}) }).then(r => r.json()).then(res => {
    if (!res.ok) { toast("re-capture: " + (res.error || "failed"), true); return; }
    pollCapture((ok) => { if (ok) reloadSession(); });
  }).catch(() => toast("re-capture failed", true));
}
function reloadSession() {
  // reload videos + manifest without losing scroll: simplest is a soft reload
  const f = cur;
  toast("capture updated — reloading view");
  location.reload();
}

// ── record (frida-attach to retail) ──────────────────────────────────────────
let recTimer = 0;
let lastRecOut = null;
function recStatusText(s) {
  if (s.running) return `● recording "${s.name}" · ${s.elapsed_s}s · ${(s.bytes/1024).toFixed(0)} KB`;
  if (s.out && s.exists) return `■ stopped · wrote ${(s.bytes/1024).toFixed(0)} KB → ${s.out}`;
  return `idle · target ${s.remote}`;
}
function applyRecStatus(s) {
  $("ts-rec-status").textContent = recStatusText(s);
  $("ts-rec-start").disabled = !!s.running;
  $("ts-rec-stop").disabled = !s.running;
  if (s.out && s.exists && !s.running) { lastRecOut = s.out; $("ts-rec-view").disabled = false; }
  if (s.log_tail) $("ts-rec-status").title = s.log_tail;
}
function recView() {
  if (!lastRecOut) { toast("no recording to view yet", true); return; }
  const target = $("ts-rec-target").value;
  $("ts-rec-view").disabled = true;
  toast(`capturing ${lastRecOut} (${target})…`);
  fetch("/capture", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ trace: lastRecOut, target, call_trace: true }) })
    .then(r => r.json()).then(res => {
      if (!res.ok) { toast("capture start: " + (res.error || "failed"), true); $("ts-rec-view").disabled = false; return; }
      const sess = res.session;
      pollCapture((ok) => { if (ok) location.search = "?session=" + encodeURIComponent(sess); });
    }).catch(() => { toast("capture start failed", true); $("ts-rec-view").disabled = false; });
}
function pollRec() {
  fetch("/record/status", { cache: "no-cache" }).then(r => r.json())
    .then(s => {
      applyRecStatus(s);
      if (s.running && !recTimer) recTimer = setInterval(pollRec, 1500);
      if (!s.running && recTimer) { clearInterval(recTimer); recTimer = 0; }
    }).catch(() => {});
}
function recStart() {
  const name = $("ts-rec-name").value.trim();
  $("ts-rec-start").disabled = true;
  fetch("/record/start", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name }) }).then(r => r.json()).then(res => {
      if (!res.ok) { toast("record start: " + (res.error || "failed"), true); $("ts-rec-start").disabled = false; return; }
      toast("recording → " + res.out);
      pollRec();
    }).catch(() => { toast("record start failed", true); $("ts-rec-start").disabled = false; });
}
function recStop() {
  $("ts-rec-stop").disabled = true;
  toast("stopping recorder (finalising trace)…");
  fetch("/record/stop", { method: "POST" }).then(r => r.json()).then(res => {
    if (!res.ok) { toast("record stop: " + (res.error || "failed"), true); return; }
    const how = res.recovered ? " (recovered from stream)" : "";
    toast(res.written ? `wrote ${(res.bytes/1024).toFixed(0)} KB${how} → ${res.out}` : "stopped (no trace written — check the log)", !res.written);
    pollRec();
  }).catch(() => toast("record stop failed", true));
}

// ── play ─────────────────────────────────────────────────────────────────────
function play() {
  if (playing) return stop();
  playing = true; $("ts-play").textContent = "❚❚ pause";
  playTimer = setInterval(() => {
    if (cur >= N - 1) { stop(); return; }
    seekAll(cur + 1);
  }, Math.round(1000 / FPS));
}
function stop() {
  playing = false; if (playTimer) clearInterval(playTimer); playTimer = 0;
  $("ts-play").textContent = "▶ play";
}

// ── box-select on a video → crop ref (adapted from llm-feed) ─────────────────
function attachBox(v, panel) {
  let sx = 0, sy = 0, drag = false, moved = false, box = null;
  const clamp = (cx, cy) => {
    const r = v.getBoundingClientRect();
    return [Math.min(Math.max(cx, r.left), r.right), Math.min(Math.max(cy, r.top), r.bottom)];
  };
  const toNat = (cx, cy) => {
    const r = v.getBoundingClientRect();
    return [Math.round((cx - r.left) / r.width * (v.videoWidth || r.width)),
            Math.round((cy - r.top) / r.height * (v.videoHeight || r.height))];
  };
  v.addEventListener("pointerdown", e => {
    if (e.button !== 0) return;
    drag = true; moved = false; sx = e.clientX; sy = e.clientY;
    try { v.setPointerCapture(e.pointerId); } catch {}
    e.preventDefault();
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
    if (!drag) return; drag = false;
    try { v.releasePointerCapture(e.pointerId); } catch {}
    if (box) { box.remove(); box = null; }
    if (!moved) return;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    const [x0, y0] = toNat(Math.min(ax, bx), Math.min(ay, by));
    const [x1, y1] = toNat(Math.max(ax, bx), Math.max(ay, by));
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    pendingBox = [x0, y0, x1, y1];
    const s = `crop id=${SESS} box=${x0},${y0},${x1},${y1} `
      + `size=${v.videoWidth}x${v.videoHeight} frame=f=${cur} panel=${panel}`;
    copy(s, "copied crop ✓ (also attached to your next mark)\n" + s);
  });
}

// ── build the video stage ────────────────────────────────────────────────────
function buildStage() {
  const stage = $("ts-stage"); stage.innerHTML = "";
  const order = ["port", "retail", "diff"];
  for (const panel of order) {
    const src = (M.videos || {})[panel];
    const wrap = document.createElement("div");
    wrap.className = "vpanel"; wrap.dataset.panel = panel;
    if (!src) { wrap.classList.add("hidden"); }
    const v = document.createElement("video");
    v.preload = "auto"; v.muted = true; v.playsInline = true;
    if (src) v.src = `/s/${SESS}/${src}?v=${BUST}`;   // bust stale video after re-capture
    const lab = document.createElement("div");
    lab.className = "label"; lab.textContent = panel + (src ? "" : " (none)");
    wrap.appendChild(v); wrap.appendChild(lab);
    stage.appendChild(wrap);
    videos[panel] = src ? v : null;
    if (src) { attachBox(v, panel); v.addEventListener("loadeddata", () => seekAll(cur)); }
  }
}
function togglePanel(panel) {
  const wrap = document.querySelector(`.vpanel[data-panel="${panel}"]`);
  const btn = document.querySelector(`.ly[data-panel="${panel}"]`);
  if (!wrap || !videos[panel]) return;
  const on = wrap.classList.toggle("hidden");
  btn.classList.toggle("on", !on);
}

// ── wiring ───────────────────────────────────────────────────────────────────
function wire() {
  $("ts-first").onclick = () => step(-1e9);
  $("ts-last").onclick = () => step(1e9);
  $("ts-b10").onclick = () => step(-10);
  $("ts-b1").onclick = () => step(-1);
  $("ts-f1").onclick = () => step(1);
  $("ts-f10").onclick = () => step(10);
  $("ts-play").onclick = play;
  $("ts-track").oninput = (e) => { stop(); seekAll(+e.target.value); };
  $("ts-refresh-marks").onclick = loadMarks;
  $("ts-clear-marks").onclick = clearMarks;
  $("ts-apply").onclick = doApply;
  $("ts-recapture").onclick = doRecapture;
  document.querySelectorAll(".mk").forEach(b => b.onclick = () => addMark(b.dataset.kind));
  document.querySelectorAll(".ly").forEach(b => b.onclick = () => togglePanel(b.dataset.panel));

  document.addEventListener("keydown", (e) => {
    if (/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
    const k = e.key;
    if (k === "ArrowLeft") step(-10);
    else if (k === "ArrowRight") step(10);
    else if (k === ",") step(-1);
    else if (k === ".") step(1);
    else if (k === "Home") step(-1e9);
    else if (k === "End") step(1e9);
    else if (k === " ") { e.preventDefault(); play(); }
    else if (k === "1") togglePanel("port");
    else if (k === "2") togglePanel("retail");
    else if (k === "3") togglePanel("diff");
    else if (k === "p" || k === "P") addMark("phasepin");
    else if (k === "r" || k === "R") addMark("rngpin");
    else if (k === "a" || k === "A") addMark("anchor");
    else if (k === "f" || k === "F") addMark("feature");
    else return;
    e.preventDefault();
  });
}

function renderAnchors() {
  const host = $("ts-anchors"); host.innerHTML = "";
  const an = (M.anchors || {}).retail || [];
  for (const a of an) {
    if (a.frame < 0 || a.frame >= N) continue;
    const t = document.createElement("div");
    t.className = "tick"; t.dataset.label = a.anchor;
    t.style.left = (a.frame / Math.max(1, N - 1) * 100) + "%";
    t.onclick = () => seekAll(a.frame);
    host.appendChild(t);
  }
}

async function loadSessionList() {
  const sel = $("ts-session-sel");
  const dl = $("ts-session-list");
  let names = [];
  try {
    const list = await (await fetch("/api/sessions")).json();
    names = list.map(s => s.name);
    dl.innerHTML = list.map(s =>
      `<option value="${s.name}">${s.n_frames ? `${s.n_frames}f · ${s.target || ""}` : ""}</option>`).join("");
  } catch { dl.innerHTML = ""; }
  sel.value = SESS || "";
  const go = () => {
    const v = sel.value.trim();
    if (!v) return;
    // exact match, else first fuzzy (substring) match
    const hit = names.includes(v) ? v : names.find(n => n.toLowerCase().includes(v.toLowerCase()));
    if (hit) location.search = "?session=" + encodeURIComponent(hit);
  };
  sel.onchange = go;
  sel.onkeydown = (e) => { if (e.key === "Enter") go(); };
}

function wireRecord() {
  $("ts-rec-start").onclick = recStart;
  $("ts-rec-stop").onclick = recStop;
  $("ts-rec-view").onclick = recView;
  pollRec();
  pollCapture();          // reflect any in-flight capture
}

async function load() {
  wireRecord();                 // record panel works with or without a session
  await loadSessionList();
  if (!SESS) { $("ts-status").textContent = "pick a session"; return; }
  let m;
  try { m = await (await fetch(`/s/${SESS}/session.json`, { cache: "no-cache" })).json(); }
  catch { $("ts-status").textContent = `no session ${SESS}`; return; }
  M = m; FPS = m.fps || 30;
  N = (m.frame_range ? m.frame_range[1] + 1 : m.n_frames) || 1;
  $("ts-title").textContent = SESS;
  $("ts-status").textContent = `${N} frames · ${FPS} fps · target ${m.target}`
    + (m.call_trace ? " · flow-trace" : "");
  document.title = SESS + " · trace studio";
  $("ts-note").textContent = `trace: ${m.trace}  ·  caprange ${JSON.stringify(m.caprange)}`;
  $("ts-track").max = N - 1;
  $("ts-verdict").textContent = (m.verdict && m.verdict.text)
    ? m.verdict.text : "(capture with --call-trace for the phase/RNG verdict)";

  // diff per-frame stats
  if (m.diff && m.diff.per_frame) for (const d of m.diff.per_frame) diffPer[d.frame] = d;

  // state stream
  if (m.state) {
    try {
      const txt = await (await fetch(`/s/${SESS}/${m.state}`, { cache: "no-cache" })).text();
      const byFrame = {};
      txt.split("\n").map(l => l.trim()).filter(Boolean).forEach(l => {
        try { const r = JSON.parse(l); byFrame[r.frame] = r; } catch {}
      });
      state = []; for (let i = 0; i < N; i++) state[i] = byFrame[i] || { frame: i, port: {}, retail: {} };
    } catch {}
  }

  buildStage();
  renderAnchors();
  wire();
  loadMarks();
  seekAll(0);
}

load();
