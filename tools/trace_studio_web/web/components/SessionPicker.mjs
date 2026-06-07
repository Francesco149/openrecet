// web/components/SessionPicker.mjs — fuzzy session jump (lifted from app.mjs).
// Navigates via location.search so it stays on the current entry (the SPA index.html).
import { html } from "/vendor/htm-preact-standalone.mjs";
import { useSessions } from "/store.mjs";

export function SessionPicker({ current }) {
  const list = useSessions();
  const go = (v) => {
    const names = list.map((s) => s.name);
    const hit = names.includes(v) ? v
      : names.find((n) => n.toLowerCase().includes(v.toLowerCase()));
    if (hit) location.search = "?session=" + encodeURIComponent(hit);
  };
  return html`<span>
    <input list="sess-list" placeholder="session…" value=${current || ""}
      onChange=${(e) => go(e.target.value.trim())}
      onKeyDown=${(e) => { if (e.key === "Enter") go(e.target.value.trim()); }} />
    <datalist id="sess-list">${list.map((s) =>
      html`<option value=${s.name}>${s.n_frames ? s.n_frames + "f" : ""}</option>`)}</datalist>
  </span>`;
}
