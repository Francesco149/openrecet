// web/components/RecordPanel.mjs — record a retail trace (frida attach) → capture →
// open it in the studio. Lifted from the old app.mjs RecordPanel; the change is that
// it no longer runs its own /record/status poller — the app owns ONE useJobs() poller
// and passes the record slot (`recJob`) + a `pollJobs` kick down. The capture-start
// action still drives its own short navigation loop, but over the unified /api/jobs
// (via jobStatus) so /capture/status is no longer referenced by the SPA.
import { html, useState } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, jobStatus } from "/store.mjs";
import { toast } from "/web/util.mjs";

export function RecordPanel({ recJob, pollJobs }) {
  const [name, setName] = useState("");
  const [target, setTarget] = useState("both");
  const [phase, setPhase] = useState("idle");   // idle|finalizing|capturing|done|error
  const [cap, setCap] = useState(null);          // capture job snapshot during a view()
  const lastOut = recJob && recJob.exists && !recJob.running ? recJob.out : null;

  const start = () => {
    setPhase("idle");
    postJSON("/record/start", { name }).then((r) =>
      r.ok ? (toast("recording → " + r.out), pollJobs())
           : toast("start: " + (r.error || "fail"), true));
  };
  const stop = () => {
    setPhase("finalizing");                      // the stop POST blocks ~90s finalising
    postJSON("/record/stop").then((r) => {
      setPhase("idle"); pollJobs();
      toast(r.ok ? (r.written
        ? `wrote ${(r.bytes / 1024 | 0)}KB${r.recovered ? " (recovered)" : ""} → ${r.out}`
        : "no trace written — check log") : "stop fail", !(r.ok && r.written));
    }).catch(() => { setPhase("error"); toast("stop request failed", true); });
  };
  const view = () => {
    if (!lastOut) return toast("no recording yet — stop a recording first", true);
    setPhase("capturing"); setCap({ session: "…", elapsed_s: 0, detail: "starting capture…" });
    postJSON("/capture", { trace: lastOut, target, call_trace: true }).then((r) => {
      if (!r.ok) { setPhase("error"); setCap({ detail: r.error || "capture failed to start" }); return; }
      pollJobs();
      setCap({ session: r.session, elapsed_s: 0, detail: "driving capture…" });
      const poll = () => jobStatus("capture").then((s) => {
        if (!s) { setTimeout(poll, 1500); return; }   // transient: retry
        setCap(s);
        if (s.running) { setTimeout(poll, 1200); return; }
        if (s.rc === 0 || s.rc === null) {            // done (0-frame: the banner explains)
          setPhase("done"); toast("capture done → opening " + s.session);
          location.search = "?session=" + encodeURIComponent(s.session);
        } else { setPhase("error"); toast("capture failed rc=" + s.rc, true); }
      }).catch(() => { setPhase("error"); setCap({ detail: "lost contact with capture" }); });
      poll();
    }).catch(() => { setPhase("error"); setCap({ detail: "capture request failed" }); });
  };

  let status, cls = "";
  if (phase === "capturing") status = `⟳ capturing ${cap?.session || ""} · ${cap?.elapsed_s || 0}s\n${cap?.detail || ""}`;
  else if (phase === "done") status = `✓ capture done → ${cap?.session}`;
  else if (phase === "error") { status = `✗ ${cap?.detail || "error — see /tmp"}`; cls = "err"; }
  else if (phase === "finalizing") status = `■ finalising trace (detach frida + write)…\n${recJob?.detail || ""}`;
  else if (recJob?.running) status = `● recording "${recJob.label}" · ${recJob.elapsed_s}s · ${(recJob.bytes / 1024 | 0)}KB\n${recJob?.detail || ""}`;
  else if (recJob?.exists) status = `■ stopped → ${recJob.out}`;
  else status = "idle";

  const busy = phase === "capturing" || phase === "finalizing";
  return html`<section class="panel rec-panel">
    <h3>record a trace <span class="dim">(retail · frida attach)</span></h3>
    <div class="rec-row">
      <input type="text" placeholder="trace name…" value=${name}
        onInput=${(e) => setName(e.target.value)} disabled=${recJob?.running || busy} />
      <button onClick=${start} disabled=${recJob?.running || busy}>● start</button>
      <button onClick=${stop} disabled=${!recJob?.running}>■ stop</button>
    </div>
    <div class=${"rec-status " + cls}>${status}</div>
    <div class="rec-row">
      <select onChange=${(e) => setTarget(e.target.value)} disabled=${busy}>
        <option value="openrecet">port only (fast)</option>
        <option value="both" selected>port + retail + diff</option>
      </select>
      <button onClick=${view} disabled=${!lastOut || busy}>${
        phase === "capturing" ? "⟳ capturing…" : "▶ view in studio"}</button>
    </div>
    <div class="rec-help dim">Get Recettear to the <b>title</b> via Steam first, then start.
      A new-game recording can't cross-replay (0 frames) — record a Continue/Load trace for video.</div>
  </section>`;
}
