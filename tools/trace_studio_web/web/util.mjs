// web/util.mjs — shared UI helpers (toast, clipboard, number fmt). Lifted from the
// old app.mjs so every component shares one toast/clipboard path.
let toastEl = null, toastT = 0;
export function toast(t, err) {
  if (toastEl) toastEl.remove();
  toastEl = document.createElement("div");
  toastEl.className = "toast" + (err ? " err" : "");
  toastEl.textContent = t;
  document.body.appendChild(toastEl);
  clearTimeout(toastT);
  toastT = setTimeout(() => { if (toastEl) { toastEl.remove(); toastEl = null; } }, 4000);
}
export function copy(t) {
  navigator.clipboard?.writeText(t).then(
    () => toast("copied ✓ " + t), () => toast(t, true));
}
export const fmt = (v) => v === undefined ? "·"
  : (typeof v === "number" ? (Number.isInteger(v) ? v : v.toFixed(3)) : v);
