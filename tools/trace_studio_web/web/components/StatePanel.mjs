// web/components/StatePanel.mjs — per-frame port-vs-retail flow-trace state, at the
// cursor. Lifted from the old app.mjs StatePanel; the only change is the coordinate
// contract: the row is read by GLOBAL ordinal (`view.state[cur]`, == state at
// view.locate(cur).offsetGlobal+k), since state.jsonl is keyed by the viewer index.
// Needs a --call-trace session (ov-both has none → shows the empty hint). Diff-rows
// are highlighted; a filter narrows the field list.
import { html, useState } from "/vendor/htm-preact-standalone.mjs";
import { fmt } from "/web/util.mjs";

export function StatePanel({ view, cur }) {
  const [filter, setFilter] = useState("");
  if (!view.callTrace)
    return html`<section class="panel"><h3>per-frame state</h3>
      <div class="state">(capture with --call-trace for state)</div></section>`;
  const row = view.state[cur];
  if (!row)
    return html`<section class="panel"><h3>per-frame state</h3>
      <div class="state">(no state at this frame)</div></section>`;

  let keys = [...new Set([
    ...Object.keys(row.port || {}), ...Object.keys(row.retail || {})])].sort();
  if (filter) keys = keys.filter((k) => k.toLowerCase().includes(filter.toLowerCase()));

  return html`<section class="panel"><h3>per-frame state <span class="dim">@ f${cur}</span></h3>
    <input class="filter" placeholder="filter fields…" value=${filter}
      onInput=${(e) => setFilter(e.target.value)} />
    <div class="state"><table><tr><th>field</th><th>retail</th><th>port</th></tr>
    ${keys.map((k) => {
      const r = row.retail?.[k], p = row.port?.[k];
      const cls = (r === undefined || p === undefined) ? ""
        : (JSON.stringify(r) === JSON.stringify(p) ? "same" : "diff");
      return html`<tr class=${cls} key=${k}><td>${k}</td><td>${fmt(r)}</td><td>${fmt(p)}</td></tr>`;
    })}</table></div></section>`;
}
