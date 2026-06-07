// web/components/JobTray.mjs — one status strip for the studio's background jobs,
// fed by the single shared useJobs() poller (GET /api/jobs). Replaces the old UI's
// three separate pollers (record-status + capture-status + the recapture loop): the
// app owns one poller and passes its `status` here. Shows the record slot + the
// capture slot (kind = capture|recapture|drill); a running job spins with its elapsed
// time + log-tail; a finished capture shows ✓/✗ rc.
import { html } from "/vendor/htm-preact-standalone.mjs";

function Chip({ job }) {
  const { id, kind, running, label, elapsed_s, rc, detail } = job;
  let cls = "job", txt;
  if (running) {
    cls += " run";
    txt = html`<span class="spin">⟳</span> ${kind}${label ? " " + label : ""}
      <span class="dim">· ${elapsed_s}s</span>`;
  } else if (id === "capture" && label) {
    const ok = rc === 0 || rc === null;
    cls += ok ? " ok" : " bad";
    txt = html`${ok ? "✓" : "✗"} ${kind} ${label}${ok ? "" : ` · rc=${rc}`}`;
  } else if (id === "record" && job.exists) {
    cls += " ok";
    txt = html`■ recorded ${label || ""}
      <span class="dim">· ${(job.bytes / 1024 | 0)}KB</span>`;
  } else {
    cls += " idle";
    txt = html`<span class="dim">${id}: idle</span>`;
  }
  return html`<div class=${cls} title=${detail || ""}>${txt}</div>`;
}

export function JobTray({ status }) {
  const jobs = (status && status.jobs) || [];
  if (!jobs.length) return null;
  const tail = status.running
    ? (jobs.find((j) => j.running) || {}).detail : "";
  return html`<div class="jobtray">
    ${jobs.map((j) => html`<${Chip} job=${j} key=${j.id} />`)}
    ${tail && html`<div class="job-tail dim">${tail}</div>`}
  </div>`;
}
