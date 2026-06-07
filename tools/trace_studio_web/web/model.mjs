// web/model.mjs — the v2 manifest + sidecars → one segmented client view.
//
// Wraps store.useSession (no refetch logic duplicated) and derives a `view` the SPA
// components consume. THE coordinate contract (see docs/plans/trace-studio-v2.md §The
// coordinate contract) lives here, in three deliberately-separate spaces:
//   • global ordinal g ∈ [0,totalFrames)         — the scrub position / cursor
//   • (segment, local ordinal) (s,k) = locate(g) — k ∈ [0, seg.nFrames)
//   • anchor-relative label L = frames[0]+k*cadence — display + diff lookup + the value
//     apply/drill consume (the cli drill's caprange.start + at*stride math)
// Video time is (k+0.5)/fps for EVERY segment src; state is keyed by ordinal; diff by
// label. For ONE gameplay segment every formula collapses to the old whole-session UI.
import { useMemo, useState, useEffect } from "/vendor/htm-preact-standalone.mjs";
import { useSession, useStatus, getJSON } from "/store.mjs";

// verdict → filmstrip fill class. Prefer the verdict TEXT (the specific word) over the
// exit code; check the worst outcome first (a text may mention several).
export function classify(verdict) {
  if (!verdict || verdict.available === false) return "gray";   // unreached / no flow-trace
  const t = verdict.text || "";
  if (/DRIFT|DESYNC/.test(t)) return "red";
  if (/CONST-OFFSET/.test(t)) return "amber";
  if (/PHASE-CLEAN|ALIGNED/.test(t) || verdict.exit_code === 0) return "green";
  return "gray";
}

// v1 manifest (no timeline) → one synthesized gameplay entry (mirrors
// model/session.py:_synthetic_gameplay so old sessions still open).
function synthGameplay(m) {
  const fr = m.frame_range || [0, Math.max(0, (m.n_frames || 0) - 1)];
  return {
    kind: "gameplay", idx: 0, frames: [...fr], n_frames: m.n_frames || 0,
    cadence: (m.stride > 1 ? m.stride : 1), videos: { ...(m.videos || {}) },
    verdict: m.verdict, state: m.state, call_trace: m.call_trace, _synthetic: true,
  };
}

export function buildView(data) {
  const m = data.manifest;
  if (!m) return null;
  const fps = m.fps || 30;
  const timeline = (Array.isArray(m.timeline) && m.timeline.length)
    ? m.timeline : [synthGameplay(m)];

  // diff magnitude by STRIDED LABEL (manifest.diff.per_frame[].frame is the label)
  const diffByLabel = new Map();
  for (const d of (m.diff?.per_frame || [])) diffByLabel.set(d.frame, d);

  // gameplay segments, each carrying its global offset + the ordinal↔label↔time maps
  const segments = [];
  let off = 0;
  for (const e of timeline) {
    if (e.kind !== "gameplay") continue;
    const cadence = e.cadence || 1;
    const frames = e.frames || [0, Math.max(0, (e.n_frames || 0) - 1)];
    const nFrames = e.n_frames || (frames[1] - frames[0] + 1);
    const videos = (e.videos && Object.keys(e.videos).length)
      ? e.videos : (m.videos || {});
    const offsetGlobal = off;
    segments.push({
      idx: e.idx ?? segments.length, nFrames, cadence, frames, videos,
      verdict: e.verdict ?? null,
      verdictClass: classify(e.verdict ?? m.verdict),
      offsetGlobal,
      labelOf: (k) => frames[0] + k * cadence,
      videoTime: (k) => (Math.min(Math.max(k, 0), nFrames - 1) + 0.5) / fps,
      diffAt: (k) => diffByLabel.get(frames[0] + k * cadence) || null,
    });
    off += nFrames;
  }
  const totalFrames = off || 1;
  const seams = timeline.filter((e) => e.kind === "load_seam");

  // global ordinal → {seg, k}. segments are in increasing-offset order.
  const locate = (g) => {
    const gg = Math.max(0, Math.min(totalFrames - 1, g | 0));
    let seg = segments[0];
    for (const s of segments) if (s.offsetGlobal <= gg) seg = s;
    return { seg, k: gg - (seg ? seg.offsetGlobal : 0) };
  };

  return {
    manifest: m, fps, target: m.target, callTrace: !!m.call_trace,
    segments, seams, timeline, totalFrames, diffByLabel,
    locate, globalOf: (s, k) => (s ? s.offsetGlobal : 0) + k,
    state: data.state || [],
    anchors: data.anchors || { port: [], retail: [] },
    traceOps: data.traceOps || [], capturedOps: data.capturedOps || [],
    notes: data.notes || [], marks: data.marks || [],
  };
}

// the hook the SPA root uses: session data + the derived view (memoized on the bits
// that change the model).
export function useStudioModel(sess) {
  const data = useSession(sess);
  const view = useMemo(() => buildView(data), [
    data.manifest, data.state, data.marks, data.anchors,
    data.traceOps, data.capturedOps, data.notes,
  ]);
  return { ...data, view };
}

// ─── S7 registries + jobs ─────────────────────────────────────────────────────
// The mark-type + analyzer registries (served at GET /api/registries), fetched once
// per load. The MarkBar renders one button per `marks[]` entry — adding a kind in
// edits/marks.py surfaces it here with zero JS edits.
export function useRegistries() {
  const [reg, setReg] = useState({ marks: [], analyzers: [] });
  useEffect(() => { getJSON("/api/registries").then(setReg).catch(() => {}); }, []);
  return reg;
}

// THE single jobs poller (replaces the old per-panel /record/status + /capture/status
// pollers). store.useStatus keys its interval on the top-level `.running` that
// /api/jobs returns, so this idles when nothing runs and polls while a job is live.
// Returns [{jobs:[record,capture], running}, poll].
export function useJobs(intervalMs = 1500) {
  return useStatus("/api/jobs", intervalMs);
}
export const jobOf = (status, id) =>
  (status && status.jobs ? status.jobs.find((j) => j.id === id) : null) || null;
