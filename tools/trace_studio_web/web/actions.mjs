// web/actions.mjs — side-effecting API flows shared by the panels. Keeping the
// edit→recapture loop in ONE place is the S9 robustification: the TraceEditor and the
// IteratePanel kick the identical flow (POST /recapture → watch the unified /api/jobs
// capture slot → navigate when it settles), so the loop behaves the same wherever it's
// triggered.
import { postJSON, jobStatus } from "/store.mjs";
import { toast } from "/web/util.mjs";

// Re-capture the working trace, then act when the capture job settles.
//   only    — "port" to re-run just the port vs the cached retail (fast); else both.
//   pollJobs— the shared useJobs() poker, so the JobTray lights up immediately.
//   onDone  — called on rc 0/null (default: full reload to pick up new videos/state).
// Returns the POST promise (the settle-poll runs detached).
export function recapture(sess, { only, pollJobs, onDone } = {}) {
  toast(only === "port" ? "re-capturing port…" : "re-capturing…");
  return postJSON(`/s/${sess}/recapture`, only ? { only } : {}).then((r) => {
    if (!r.ok) { toast("re-capture: " + (r.error || "fail"), true); return r; }
    if (pollJobs) pollJobs();
    const poll = () => jobStatus("capture").then((s) => {
      if (!s || s.running) { setTimeout(poll, 2000); return; }
      if (s.rc === 0 || s.rc === null) {
        toast("capture updated");
        (onDone || (() => location.reload()))();
      } else { toast("capture rc=" + s.rc, true); }
    });
    poll();
    return r;
  });
}

// Abort the running capture (capture | recapture | drill — the one capture slot).
// Kills the tracked subprocess AND reaps an orphaned capture a prior server left
// running (the backend /proc-scan), so a capture stuck across a `serve` restart
// is still cancellable. Frames already on disk are kept.
export function cancelCapture({ pollJobs } = {}) {
  toast("cancelling capture…");
  return postJSON(`/capture/cancel`, {}).then((r) => {
    if (!r.ok) { toast("cancel: " + (r.error || "nothing running"), true); return r; }
    toast("capture cancelled");
    if (pollJobs) pollJobs();
    return r;
  });
}
