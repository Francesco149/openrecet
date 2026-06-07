// web/components/IteratePanel.mjs — apply pins → re-capture → re-view, the
// self-service phase/RNG-pin loop. Lifted from the old app.mjs IteratePanel; the
// ambient capture status comes from the shared useJobs() poller (`capJob` prop) rather
// than a private /capture/status poller, and the recapture navigation loop runs over
// the unified /api/jobs (jobStatus).
import { html } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, jobStatus } from "/store.mjs";
import { toast } from "/web/util.mjs";

export function IteratePanel({ sess, manifest, stale, reload, capJob, pollJobs }) {
  const recapture = (only) => {
    toast(only === "port" ? "re-capturing port…" : "re-capturing…");
    postJSON(`/s/${sess}/recapture`, only ? { only } : {}).then((r) => {
      if (!r.ok) return toast("re-capture: " + (r.error || "fail"), true);
      pollJobs();
      const poll = () => jobStatus("capture").then((s) => {
        if (!s || s.running) { setTimeout(poll, 2000); return; }
        if (s.rc === 0 || s.rc === null) { toast("capture updated"); location.reload(); }
        else toast("capture rc=" + s.rc, true);
      });
      poll();
    });
  };
  const apply = () => {
    toast("applying pins…");
    postJSON(`/s/${sess}/apply`, {}).then((r) => {
      if (!r.ok) return toast("apply: " + (r.error || "fail"), true);
      toast(`applied ${r.pins_added} pin(s)`); reload();
      if (r.pins_added > 0) recapture();
    });
  };
  const clone = () => {
    const name = prompt("clone session as:"); if (!name) return;
    postJSON(`/s/${sess}/clone`, { name }).then((r) => r.ok
      ? (location.search = "?session=" + encodeURIComponent(r.name))
      : toast("clone: " + (r.error || "fail"), true));
  };
  const toggleDiv = (e) => postJSON(`/s/${sess}/divergent`, { on: e.target.checked })
    .then(() => { toast(e.target.checked ? "divergent editing on" : "mirror editing"); reload(); });

  const running = capJob && capJob.running;
  const done = capJob && !capJob.running && capJob.session;
  return html`<section class="panel"><h3>iterate
      ${stale && html`<span class="stale-dot" title="edits not yet captured">● STALE</span>`}
      ${running && html`<span class="dim"> · ⟳ ${capJob.elapsed_s}s</span>`}</h3>
    <div class="mark-row">
      <button onClick=${apply}>✓ apply pins</button>
      <button onClick=${() => recapture()}>⟳ re-capture</button>
      <button onClick=${() => recapture("port")}
        title="re-run only the port vs the cached retail (fast)">⟳ port only</button>
    </div>
    <div class="mark-row">
      <button onClick=${clone}>⎘ clone</button>
      <label class="ck"><input type="checkbox" checked=${!!manifest.divergent}
        onChange=${toggleDiv}/> divergent edit</label>
    </div>
    <div class="rec-status">${done
      ? (capJob.rc === 0 || capJob.rc === null ? `✓ ${capJob.session}` : `✗ rc=${capJob.rc}`)
      : "—"}</div>
  </section>`;
}
