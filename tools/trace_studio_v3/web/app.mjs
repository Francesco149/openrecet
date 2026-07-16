// trace studio v3 — viewer SPA.
//
// Driven entirely by /manifest.json (orv3_view bake): an identity-OFFSET-keyed
// timeline of port|retail|diff PNG panels + per-frame metrics. Preserves the v2 UX
// (3-panel lockstep scrub + diff ribbon + per-frame state) on the same CSS — the
// only structural change is that sync is a STORED identity join (manifest.frames is
// already paired by offset), so there is no align/renumber/seam machinery: column i
// is the same logical moment on both sides BY CONSTRUCTION. Frames are static PNGs
// (the bit-exact ref bake); the on-demand replay/zoom/semantic layer lands in P3d.
import { html, render, useState, useEffect, useMemo, useRef, useCallback }
  from "/vendor/htm-preact-standalone.mjs";

const ORDER = ["port", "retail", "diff"];

// ── tiny utils (no store.mjs dependency) ──
function toast(msg, err) {
  let t = document.querySelector(".toast");
  if (!t) { t = document.createElement("div"); document.body.appendChild(t); }
  t.className = "toast" + (err ? " err" : "");
  t.textContent = msg;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => t.remove(), 2600);
}
function copy(text) {
  navigator.clipboard?.writeText(text).then(() => toast("copied: " + text),
    () => toast(text));
}
// meanabs → green→yellow→red, ABSOLUTE scale (matches v2 DiffRibbon): a near-clean
// frame stays green, a real divergence (mean ≳4) goes red.
function heat(meanabs) {
  const t = Math.max(0, Math.min(1, meanabs / 6));
  const r = t < 0.5 ? Math.round(510 * t) : 255;
  const g = t < 0.5 ? 200 : Math.round(200 * (1 - (t - 0.5) * 2));
  return `rgb(${r},${g},78)`;
}

// box-select on a panel image → copy a `box=…` crop string (the v2 crop affordance).
function attachBox(img, getCtx) {
  let sx = 0, sy = 0, drag = false, moved = false, box = null;
  const clamp = (cx, cy) => { const r = img.getBoundingClientRect();
    return [Math.min(Math.max(cx, r.left), r.right),
            Math.min(Math.max(cy, r.top), r.bottom)]; };
  const toNat = (cx, cy) => { const r = img.getBoundingClientRect();
    return [Math.round((cx - r.left) / r.width * (img.naturalWidth || r.width)),
            Math.round((cy - r.top) / r.height * (img.naturalHeight || r.height))]; };
  img.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return; drag = true; moved = false; sx = e.clientX; sy = e.clientY;
    try { img.setPointerCapture(e.pointerId); } catch {} e.preventDefault();
  });
  img.addEventListener("pointermove", (e) => {
    if (!drag) return;
    if (!moved && Math.abs(e.clientX - sx) + Math.abs(e.clientY - sy) < 4) return;
    moved = true;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    if (!box) { box = document.createElement("div"); box.className = "box-sel";
      document.body.appendChild(box); }
    box.style.left = Math.min(ax, bx) + "px"; box.style.top = Math.min(ay, by) + "px";
    box.style.width = Math.abs(bx - ax) + "px"; box.style.height = Math.abs(by - ay) + "px";
  });
  img.addEventListener("pointerup", (e) => {
    if (!drag) return; drag = false; try { img.releasePointerCapture(e.pointerId); } catch {}
    if (box) { box.remove(); box = null; } if (!moved) return;
    const [ax, ay] = clamp(sx, sy), [bx, by] = clamp(e.clientX, e.clientY);
    const [x0, y0] = toNat(Math.min(ax, bx), Math.min(ay, by));
    const [x1, y1] = toNat(Math.max(ax, bx), Math.max(ay, by));
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    const c = getCtx();
    copy(`box=${x0},${y0},${x1},${y1} (offset ${c.offset}, ${c.panel}, ${img.naturalWidth}x${img.naturalHeight})`);
  });
}

// ── the 3-panel stage (port|retail|diff images, lockstep on the cursor) ──
function Stage({ fr, panels }) {
  const ctxRef = useRef({});
  ctxRef.current = { offset: fr.offset };
  return html`<div class="stage">
    ${ORDER.map((panel) => {
      if (!panels[panel]) return null;
      const src = fr[panel];
      return html`<div class="vpanel" key=${panel}>
        ${src
          ? html`<img class="tv" src=${"/" + src} alt=${panel}
              ref=${(el) => { if (el && !el._box) { el._box = 1;
                attachBox(el, () => ({ ...ctxRef.current, panel })); } }} />`
          : html`<div class="tv missing">— no ${panel} —<br/>(honest gap)</div>`}
        <div class="label">${panel}${src ? "" : " (gap)"}</div>
      </div>`;
    })}
  </div>`;
}

function ScrubBar({ N, cur, setCur }) {
  const step = (d) => setCur(Math.max(0, Math.min(N - 1, cur + d)));
  return html`<div class="scrub">
    <button onClick=${() => setCur(0)} title="first (Home)">⏮</button>
    <button onClick=${() => step(-10)} title="−10 (←)">−10</button>
    <button onClick=${() => step(-1)} title="−1 (,)">−1</button>
    <div class="track-wrap"><input type="range" min="0" max=${N - 1} value=${cur}
      onInput=${(e) => setCur(+e.target.value)} /></div>
    <button onClick=${() => step(1)} title="+1 (.)">+1</button>
    <button onClick=${() => step(10)} title="+10 (→)">+10</button>
    <button onClick=${() => setCur(N - 1)} title="last (End)">⏭</button>
    <span class="pos">${cur} / ${N - 1}</span>
  </div>`;
}

// per-frame diff strip — one cell per column, heat = meanabs, click to seek.
function DiffRibbon({ frames, cur, setCur }) {
  const worst = useMemo(() => frames.reduce((a, f, i) =>
    (f.gt8 ?? -1) > (a.gt8 ?? -1) ? { i, gt8: f.gt8 } : a, { i: 0, gt8: -1 }), [frames]);
  const here = frames[cur] || frames[0] || {};
  const nextDiv = () => {
    for (let i = cur + 1; i < frames.length; i++)
      if ((frames[i].gt8 || 0) > 0) { setCur(i); return; }
    toast("no gt8>0 frame after " + cur);
  };
  return html`<div class="diff-ribbon-wrap">
    <div class="diff-ribbon" title="port-vs-retail per-frame diff — click to seek">
      ${frames.map((f, i) => html`<div key=${i}
        class=${"diff-cell" + (i === cur ? " cur" : "")}
        style=${`background:${f.diff ? heat(f.meanabs || 0) : "#11161c"}`}
        title=${`offset ${f.offset}: gt8 ${f.gt8 ?? "—"}px · meanabs ${f.meanabs ?? "—"}`
                + (f.gap ? ` · GAP (no ${f.gap})` : "")}
        onClick=${() => setCur(i)}></div>`)}
    </div>
    <div class="diff-bar">
      <span>offset ${here.offset}: <b>${here.gt8 ?? "—"}</b> gt8px ·
        ${here.meanabs ?? "—"} meanabs${here.gap ? ` · GAP (no ${here.gap})` : ""}</span>
      <span class="spacer"></span>
      <button onClick=${nextDiv} title="next gt8>0 frame after the cursor">next ⟂</button>
      <button onClick=${() => setCur(worst.i)}
        title=${`worst: offset ${frames[worst.i].offset} (gt8 ${worst.gt8}px)`}>⟶ worst</button>
    </div>
  </div>`;
}

// per-frame "state": the d3d/identity facts retail-vs-port, diff-highlighted.
function StatePanel({ m, fr }) {
  const rows = [
    ["present", fr.retail_present, fr.port_present, "neutral"],   // load stretch ⇒ differ, expected
    ["draws", fr.retail_draws, fr.port_draws],
    ["calls", fr.retail_calls, fr.port_calls],
    ["res bound", fr.retail_res, fr.port_res],
  ];
  const cell = (v) => v === null || v === undefined ? "—" : v;
  return html`<section class="panel"><h3>per-frame state
      <span class="dim">@ offset ${fr.offset}</span></h3>
    <div class="diffstat">
      diff: <b>${fr.gt8 ?? "—"}</b> gt8px · ${fr.meanabs ?? "—"} meanabs ·
      max |Δ| ${fr.maxd ?? "—"}${fr.gap ? html` · <span class="warn">GAP — no ${fr.gap}</span>` : ""}
    </div>
    <div class="state"><table><tr><th>field</th><th>retail</th><th>port</th></tr>
      <tr class="neutral"><td>identity</td><td colspan="2">${m.anchor}#${m.anchor_occ} +${fr.offset}</td></tr>
      ${rows.map(([k, r, p, mode]) => {
        const cls = mode === "neutral" ? "" :
          (r == null || p == null) ? "" : (r === p ? "same" : "diff");
        return html`<tr class=${cls} key=${k}><td>${k}</td><td>${cell(r)}</td><td>${cell(p)}</td></tr>`;
      })}
    </table></div></section>`;
}

function VerdictPanel({ m }) {
  const naiveNote = m.load_stretch
    ? `naive absolute-present pairing would pair 0 frames (load stretch ${m.load_stretch > 0 ? "+" : ""}${m.load_stretch}); identity join pairs by (anchor, offset).`
    : "";
  const jv = m.join_verdict ?? m.verdict;
  const txt = [
    `JOIN: ${jv}`,
    `(identity pairing only — NOT a parity/equality claim)`,
    `anchor:  ${m.anchor}#${m.anchor_occ}`,
    `columns: ${m.count}  (${m.n_diff} with both sides, ${m.n_gaps} honest gaps)`,
    `window:  offset ${m.offset0}..${m.offset0 + m.count - 1}`,
    `load stretch (retail − port present): ${m.load_stretch > 0 ? "+" : ""}${m.load_stretch} frames`,
    m.worst ? `worst:   offset ${m.worst.offset}  gt8=${m.worst.gt8}px` : "",
    "", naiveNote,
  ].filter((l) => l !== undefined).join("\n");
  return html`<section class="panel"><h3>sync / JOIN <span class="dim">(identity pairing)</span></h3>
    <pre class="verdict">${txt}</pre></section>`;
}

function App() {
  const [m, setM] = useState(null);
  const [error, setError] = useState(null);
  const [cur, setCur] = useState(0);
  const [panels, setPanels] = useState({ port: true, retail: true, diff: true });

  const load = useCallback(() => {
    fetch("/manifest.json?v=" + Date.now()).then((r) => {
      if (!r.ok) throw new Error("manifest " + r.status);
      return r.json();
    }).then(setM).catch((e) => setError(String(e)));
  }, []);
  useEffect(load, [load]);

  const N = m ? m.frames.length : 1;
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
      else if (k === "r") load();
      else return;
      e.preventDefault();
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [N, load]);

  if (error) return html`<div class="pad"><div class="err-box">error: ${error}</div></div>`;
  if (!m) return html`<div class="pad">loading…</div>`;
  const fr = m.frames[Math.min(cur, N - 1)];
  const jv = m.join_verdict ?? m.verdict;
  const joinOk = jv === "JOIN_COMPLETE" || jv === "ALIGNED";

  return html`<div>
    <header>
      <h1>trace studio <span class="dim">v3</span> · <span class="accent">${m.scenario}</span></h1>
      <div class="status">
        <span>${m.count}f · ${m.dims ? m.dims.join("×") : "?"} · ${m.anchor} ·
          <span class=${joinOk ? "accent" : "warn"}>${jv}</span></span>
      </div>
    </header>
    <main>
      <div class="note">port ${m.port_entry} · retail ${m.retail_entry}</div>
      <div class="layout-bar"><span>panels:</span>
        ${ORDER.map((p) => html`<button class=${"ly " + (panels[p] ? "on" : "")}
          onClick=${() => setPanels((s) => ({ ...s, [p]: !s[p] }))}>${p}</button>`)}
        <span class="spacer"></span>
        <span class="dim">offset ${fr.offset} · col ${cur}/${N - 1}</span>
      </div>
      <div class="workarea">
        <div class="scrub-col">
          <div class="vidblock">
            <${Stage} fr=${fr} panels=${panels} />
            <${ScrubBar} N=${N} cur=${cur} setCur=${setCur} />
            <${DiffRibbon} frames=${m.frames} cur=${cur} setCur=${setCur} />
            <div class="hint">←/→ ±10 · ,/. ±1 · Home/End · 1/2/3 panels · r reload ·
              drag a box on a frame → copy crop</div>
          </div>
          <div class="below-panels"><${VerdictPanel} m=${m} /></div>
        </div>
        <aside class="ref-col"><${StatePanel} m=${m} fr=${fr} /></aside>
      </div>
    </main>
  </div>`;
}

render(html`<${App} />`, document.getElementById("app"));
