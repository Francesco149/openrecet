// web/components/CheatSheet.mjs — a collapsible RNG/phase-pin cheatsheet pinned to
// the bottom of the studio page. Step-by-step for the common parity-debug flows so
// the pin loop is at hand until it's muscle memory. Static content; no state.
import { html } from "/vendor/htm-preact-standalone.mjs";

export function CheatSheet() {
  return html`<details class="cheatsheet">
    <summary>📋 RNG / phase-pin cheatsheet <span class="dim">— the parity-debug flow</span></summary>
    <div class="cheat-body">

      <h4>A · The standard loop (90% of divergences)</h4>
      <ol>
        <li><b>Capture</b> both targets with the flow-trace:
          <code>trace_studio.py capture &lt;trace&gt; --target both --call-trace</code>
          (stores the phase/RNG <b>verdict</b> in the session).</li>
        <li><b>Read the Verdict panel.</b> Each field is one of:
          <ul>
            <li><span class="ok">ALIGNED</span> — bit-/eps-equal. 1:1.</li>
            <li><span class="warn">CONST-OFFSET</span> — a fixed phase shift (a load-dependent
              counter origin). <b>Accept</b> — it's phase, not logic.</li>
            <li><span class="bad">DRIFT</span> — a per-frame-varying divergence ⇒ real <b>logic</b>
              (or within-frame RNG order — see C).</li>
            <li><span class="bad">DESYNC</span> (rngcalls) — the two sides consume a different
              <i>count</i> of LCG draws ⇒ an unpinned RNG origin or a real extra/missing consumer.</li>
          </ul></li>
        <li><b>Pin</b> from the verdict: <code>trace_studio.py apply &lt;session&gt; --auto-pin</code>
          (CONST-OFFSET → <code>{phasepin: F}</code>; DESYNC → <code>{rngseed: [F, 19937]}</code>),
          or drop a "⟲ pin phase" / "re-seed" mark in the viewer. <b>F = the caprange start.</b></li>
        <li><b>Recapture:</b> <code>trace_studio.py recapture &lt;session&gt;</code>
          (re-drives BOTH with the pins; keeps them). <code>--only port</code> = fast port-only loop.</li>
        <li><b>Re-read the verdict.</b> Now ALIGNED / CONST-OFFSET everywhere ⇒
          <b>1:1 given the same phase+RNG</b> (the pin doubles as the parity test — each side ran
          its own code from the identical origin). Still DRIFT/DESYNC ⇒ go to C.</li>
      </ol>

      <h4>B · Symptom → pin (the common scenarios)</h4>
      <table class="cheat-tbl">
        <tr><th>What you see</th><th>Why</th><th>Pin</th></tr>
        <tr><td>Townsfolk / window-NPCs at different positions</td>
            <td>bg-NPC warmup rides the load-dependent RNG history</td>
            <td><code>{phasepin}</code> (re-runs the 180× warmup @ seed 19937)</td></tr>
        <tr><td>Recette / Tear idle breathing / anim out of phase</td>
            <td>anim cycle free-runs from a load-dependent origin</td>
            <td><code>{phasepin}</code> (zeros FRAME/TIMER/COUNTER)</td></tr>
        <tr><td>Sparkles / foot-dust / wing particles shimmer differently</td>
            <td>RNG-jittered particles at a different LCG origin</td>
            <td><code>{phasepin}</code> + canonical <code>{rngseed: [F, 19937]}</code></td></tr>
        <tr><td>A counter (e.g. <code>fade_tick</code>) shows CONST-OFFSET</td>
            <td>load-dependent counter origin (NOT in the pin set)</td>
            <td><b>accept</b> — it's phase, not a bug</td></tr>
        <tr><td>Still DRIFT/DESYNC after pinning</td>
            <td>real logic, or within-frame RNG consumption order</td>
            <td>drill → C</td></tr>
      </table>

      <h4>C · When pinning doesn't fix it (the deep case)</h4>
      <ul>
        <li><b>DRIFT after a clean pin = a real logic divergence</b> (same inputs ⇒ different output).
          Annotate the suspect function on BOTH sides (port <code>CALL_TRACE_FIELD</code> +
          retail <code>tools/flow/retail_fields.json</code>) and let <code>flow_diff --verdict</code>
          name the diverging field. State probes &gt; pixels.</li>
        <li><b>Stream bit-exact but a consumer's output differs?</b> If <code>rng (raw state)</code>
          AND <code>rngcalls</code> are ALIGNED yet a particle/value diverges, the consumer is reading
          a <b>different slice of the identical stream</b> (within-frame consumption order).
          <b>Forensic LCG:</b> take the frame-start seed (the <code>rng</code> field), generate the
          LCG sequence, and find which <i>draw-offset</i> reproduces each side's value. The offset
          delta = how many draws one side consumes before the consumer that the other doesn't —
          it <b>names the mis-ordered/extra consumer</b>.</li>
      </ul>

      <h4>D · What <code>{phasepin}</code> normalizes (and what it doesn't)</h4>
      <p><b>Pinned:</b> <code>db054</code> (per-scene clock) · player + companion anim
        (FRAME/TIMER/COUNTER) · <code>b154</code> (shared cursor bob) · dialogue <code>rmb</code>
        screen-shake · bg-NPC warmup (re-seed 19937) · <code>g_sim_frame_count</code>
        (sparkle/effect gate).<br/>
        <b>NOT pinned:</b> the in-game <code>fade_tick</code> counter (stays CONST-OFFSET, accepted).
        Two co-advancing counters bumped at <i>different call sites</i> (e.g. <code>db054</code> vs
        <code>g_sim_frame_count</code>) can hold a load-dependent <i>relative</i> phase that zeroing
        both at one frame does not fix — verify with a state probe, not just the pin.</p>

      <h4>E · Commands</h4>
      <pre>trace_studio.py apply &lt;session&gt; --auto-pin     # pins from the stored verdict
trace_studio.py recapture &lt;session&gt;            # re-drive both, keep pins
trace_studio.py recapture &lt;session&gt; --only port # fast port-only loop
flow_diff.py --retail R --port P --verdict --align-field db054   # the verdict
flow_diff.py --rng-drill rng_callsites.json     # retail per-callsite RNG breakdown</pre>

    </div>
  </details>`;
}
