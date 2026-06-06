// store.mjs — API helpers + the useStudio() hook (single source of truth).
import { useState, useEffect, useRef, useCallback } from "/vendor/htm-preact-standalone.mjs";

export const BUST = Date.now();           // per-load cache-bust for re-captured videos

// ─── low-level api ───────────────────────────────────────────────────────────
export async function getJSON(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url} → ${r.status}`);
  return r.json();
}
export async function getText(url) {
  const r = await fetch(url, { cache: "no-cache" });
  return r.ok ? r.text() : "";
}
export async function postJSON(url, body) {
  const r = await fetch(url, {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  try { return await r.json(); } catch { return { ok: r.ok }; }
}
export function parseJSONL(txt) {
  return txt.split("\n").map(l => l.trim()).filter(Boolean)
    .map(l => { try { return JSON.parse(l); } catch { return null; } }).filter(Boolean);
}

export function qparam(name) {
  const m = new RegExp(`[?&]${name}=([^&]+)`).exec(location.search);
  return m ? decodeURIComponent(m[1]) : "";
}

// ─── session loading ─────────────────────────────────────────────────────────
// Returns { manifest, state, marks, anchors:{port,retail}, loading, error, reload }.
export function useSession(sess) {
  const [data, setData] = useState({ loading: true });

  const reload = useCallback(async () => {
    if (!sess) { setData({ loading: false, error: "no session" }); return; }
    try {
      const manifest = await getJSON(`/s/${sess}/session.json`);
      const out = { manifest, loading: false, error: null,
                    marks: [], state: [], anchors: { port: [], retail: [] } };
      // per-frame state (optional)
      if (manifest.state) {
        const byFrame = {};
        parseJSONL(await getText(`/s/${sess}/${manifest.state}`))
          .forEach(r => { byFrame[r.frame] = r; });
        const n = (manifest.frame_range ? manifest.frame_range[1] + 1 : manifest.n_frames) || 0;
        out.state = Array.from({ length: n }, (_, i) => byFrame[i] || { frame: i, port: {}, retail: {} });
      }
      out.marks = parseJSONL(await getText(`/s/${sess}/edits.jsonl`));
      for (const side of ["port", "retail"]) {
        out.anchors[side] = parseJSONL(await getText(`/s/${sess}/anchors.${side}.jsonl`));
      }
      // the editable working trace (ops) + sidecar notes
      out.traceOps = parseJSONL(await getText(`/s/${sess}/edit.trace.jsonl`));
      out.notes = parseJSONL(await getText(`/s/${sess}/notes.jsonl`));
      setData(out);
    } catch (e) {
      setData({ loading: false, error: String(e.message || e) });
    }
  }, [sess]);

  useEffect(() => { reload(); }, [reload]);
  return { ...data, reload };
}

// ─── sessions list ───────────────────────────────────────────────────────────
export function useSessions() {
  const [list, setList] = useState([]);
  useEffect(() => { getJSON("/api/sessions").then(setList).catch(() => {}); }, []);
  return list;
}

// ─── a poller for /record/status and /capture/status ─────────────────────────
export function useStatus(url, intervalMs = 1500) {
  const [s, setS] = useState(null);
  const timer = useRef(0);
  const poll = useCallback(() => {
    getJSON(url).then(st => {
      setS(st);
      const running = st.running;
      if (running && !timer.current) timer.current = setInterval(poll, intervalMs);
      if (!running && timer.current) { clearInterval(timer.current); timer.current = 0; }
    }).catch(() => {});
  }, [url, intervalMs]);
  useEffect(() => { poll(); return () => { if (timer.current) clearInterval(timer.current); }; }, [poll]);
  return [s, poll];
}
