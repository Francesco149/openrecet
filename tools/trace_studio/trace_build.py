"""trace_build.py — construct a session's editable WORKING trace.

The working trace (edit.trace.jsonl) is what the user's pins land on (via `apply`)
and what re-captures reuse. Build it from a source (raw recording or a .jsonl):
distil if raw, localize its save into the session's _saves/, and inject the window
ops ({caprange} + optional {calltrace}) after the final {wait}.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

from .model.ops import raw_header
from .paths import ROOT


def distill_raw(raw: Path, out: Path, anchored: bool = False) -> None:
    """Distil a frida/F2 raw recording into a replayable trace (folds the embedded
    save into <out_dir>/_saves/). FLAT by default (single boot segment, no {wait}s —
    the 'no anchoring, sync at boot' first run); anchored=True = every-anchor-a-{wait}."""
    args = [sys.executable, str(ROOT / "tools" / "distill_trace.py"), str(raw)]
    args += (["--anchor-segments"] if anchored else [])
    args += ["-o", str(out)]
    r = subprocess.run(args, capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0 or not out.exists():
        raise SystemExit(f"trace_studio: distil failed for {raw}:\n{r.stderr[-800:]}")


def localize_save(src: Path, text: str, sess_dir: Path) -> str:
    """Copy a non-raw trace's save blob into the session's _saves/ and rewrite the
    {savefile} op to point there, so the working trace is self-contained."""
    import trace_save
    ref = trace_save.read_ref(src)
    if not ref or ref == trace_save.FRESH_REF:
        return text
    blob = (src.resolve().parent / ref).resolve()
    if not blob.exists():
        return text
    dst_dir = sess_dir / "_saves"
    dst_dir.mkdir(exist_ok=True)
    dst = dst_dir / blob.name
    if not dst.exists():
        shutil.copy2(blob, dst)
    new_ref = f"_saves/{blob.name}"
    out: list[str] = []
    for ln in text.splitlines():
        s = ln.strip()
        o = None
        if s and not s.startswith("#"):
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                o = None
        if isinstance(o, dict) and "savefile" in o:
            o["savefile"] = new_ref
            out.append(json.dumps(o))
        else:
            out.append(ln)
    return "\n".join(out) + "\n"


def ensure_window_ops(text: str, cr: tuple[int, int], call_trace: bool,
                      capstride: int = 1, after_wait: str | None = None) -> str:
    """Insert {caprange} (+ {calltrace} if requested, + {capstride:N} for an
    OVERVIEW when N>1), replacing any pre-existing window ops. Pins the user applied
    (phasepin/rngseed) sit after the {wait} too and are preserved.

    Placement: after the FIRST `{wait after_wait}` when given (so the window anchors
    at the free-roam entry the PORT reaches — `cr` is then relative to THAT anchor,
    and the replay keeps running through the later segments the port may not reach),
    else after the final {wait} (the legacy default). {capstride} is trace-global, so
    its placement is cosmetic — it's co-located with the window ops it modifies."""
    keep: list[str] = []
    for ln in text.splitlines():
        s = ln.strip()
        if s and not s.startswith("#"):
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                o = None
            if isinstance(o, dict) and (
                    "caprange" in o or "calltrace" in o or "capstride" in o):
                continue
        keep.append(ln)
    li = -1          # last {wait}
    first = -1       # first {wait after_wait}
    for i, ln in enumerate(keep):
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "wait" in o:
            li = i
            if after_wait and first < 0 and o.get("wait") == after_wait:
                first = i
    target = first if first >= 0 else li
    ins = [json.dumps({"caprange": [cr[0], cr[1]]})]
    if call_trace:
        ins.append(json.dumps({"calltrace": [cr[0], cr[1]]}))
    if capstride > 1:
        ins.append(json.dumps({"capstride": capstride}))
    at = target + 1 if target >= 0 else len(keep)
    keep[at:at] = ins
    return "\n".join(keep) + "\n"


def build_working_trace(src: Path, sess_dir: Path, working: Path,
                        cr: tuple[int, int], call_trace: bool,
                        anchored: bool = False, capstride: int = 1) -> None:
    """Distil if raw, localize its save, inject the window ops → write `working`."""
    from .model.ops import first_freeroam_wait
    after: str | None = None
    if raw_header(src):
        base = sess_dir / "recording.trace.jsonl"
        distill_raw(src, base, anchored=anchored)
        text = base.read_text()
        # Anchor the auto-window at the FIRST free-roam entry (port-reachable), not the
        # last {wait} — a later scene (e.g. the town a shop-exit leads to) the port may
        # not reach yet, which would make it capture 0 / 'window never reached'. The
        # replay still runs through the later segments (retail captures them).
        if anchored:
            after = first_freeroam_wait(text)
    else:
        text = localize_save(src, src.read_text(), sess_dir)
    working.write_text(ensure_window_ops(text, cr, call_trace, capstride, after_wait=after))
