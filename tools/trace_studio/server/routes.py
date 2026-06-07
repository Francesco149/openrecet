"""server/routes.py — the POST/GET dispatch table (replaces the do_POST regex ladder).

Each handler is `(h, match, raw) -> None` where `h` is the BaseHTTPRequestHandler
instance: it calls the existing `h._send_json` / `h._send_file` / `h._send_bytes`
helpers and reads shared state off `h.server` (sess_root / web_dir / recorder /
capturer / …, stashed in app.serve()). `raw` is the request body bytes (read once by
the dispatcher; GET handlers ignore it).

The wire API is byte-for-byte the same as the old `app.py` ladder — same paths,
methods, request bodies, and status codes (incl. the plain-text 400/403/404/500
branches) — so the existing web UI keeps working through the refactor. New endpoints
(/api/jobs, /api/registries, /drill) are added in later phases by appending rows here.
"""
from __future__ import annotations

import json
import re
import shutil
import time
from pathlib import Path


# ── body parsing ─────────────────────────────────────────────────────────────
def _json(raw: bytes) -> dict:
    """Parse a JSON request body, tolerating an empty body — matches the old
    `_read_body` (returns {} on missing/invalid JSON)."""
    try:
        return json.loads(raw or b"{}")
    except json.JSONDecodeError:
        return {}


# ── GET: session list (was Handler._sessions) ────────────────────────────────
def list_sessions(sess_root: Path) -> list[dict]:
    out: list[dict] = []
    if not sess_root.exists():
        return out
    for d in sorted(sess_root.iterdir()):
        mf = d / "session.json"
        if mf.is_file():
            try:
                m = json.loads(mf.read_text())
            except json.JSONDecodeError:
                m = {}
            out.append({
                "name": d.name,
                "n_frames": m.get("n_frames"),
                "target": m.get("target"),
                "videos": list((m.get("videos") or {})),
                "call_trace": m.get("call_trace"),
            })
    return out


def h_sessions(h, m, raw):
    h._send_json(list_sessions(h.server.sess_root))


def h_jobs(h, m, raw):
    # Unified job list (record + capture/recapture/drill) for the SPA JobTray.
    h._send_json(h.server.jobs.list())


def h_registries(h, m, raw):
    # Mark-type + analyzer registries, so the SPA renders its MarkBar / analysis
    # views from data (add a kind in edits/marks.py → it surfaces with no JS edit).
    from ..analysis import registry as analyzers
    from ..edits import marks
    h._send_json({"marks": marks.registry(), "analyzers": analyzers.registry()})


# ── POST: record ─────────────────────────────────────────────────────────────
def h_record_start(h, m, raw):
    d = _json(raw)
    h._send_json(h.server.recorder.start(d.get("name", "")))


def h_record_stop(h, m, raw):
    h._send_json(h.server.recorder.stop())


# ── POST: capture / recapture ────────────────────────────────────────────────
def h_capture_cancel(h, m, raw):
    # Abort the running capture/recapture/drill (the single capture slot) — the
    # JobTray ✕. Also reaps an orphaned capture left by a prior server instance.
    h._send_json(h.server.capturer.cancel())



def h_capture(h, m, raw):
    d = _json(raw)
    trace = d.get("trace")
    if not trace:
        h._send_bytes(b"need trace", "text/plain", 400)
        return
    # The SERVER owns the session name (so /api/jobs can report it and the client
    # can open it). Derive from the trace basename + a timestamp when not given;
    # cmd_capture's --session takes precedence.
    session = d.get("session")
    if not session:
        base = Path(trace).name
        for suf in (".raw.jsonl", ".trace.jsonl", ".jsonl"):
            if base.endswith(suf):
                base = base[: -len(suf)]
                break
        session = f"{re.sub(r'[^\w.-]', '_', base)}-{time.strftime('%Y%m%d-%H%M%S')}"
    h._send_json(h.server.capturer.start(
        trace, session, d.get("target", "both"),
        bool(d.get("call_trace", True)), d.get("caprange")))


def h_recapture(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    mf = sdir / "session.json"
    if not mf.is_file():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    man = json.loads(mf.read_text())
    d = _json(raw)
    working = man.get("working_trace") or man.get("trace")
    h._send_json(h.server.capturer.start(
        working, sess, d.get("target", man.get("target", "both")),
        bool(d.get("call_trace", man.get("call_trace", True))), None,
        only=d.get("only", "both"), kind="recapture"))


def h_drill(h, m, raw):
    # DRILL: recapture one overview sub-window DENSE. Body: {at, span?=48, target?,
    # call_trace?, session_out?}. Maps the overview viewer index `at` → the
    # anchor-relative dense window via model/drill.drill_window (shared with the CLI),
    # then spawns a stride-1 --reset-trace capture into a child session (async; the
    # JobTray watches it as kind=drill).
    from ..model.drill import drill_window
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    mf = sdir / "session.json"
    if not mf.is_file():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    man = json.loads(mf.read_text())
    d = _json(raw)
    try:
        src, start, span2, default_child = drill_window(
            man, sess, int(d.get("at", 0)), int(d.get("span", 48)))
    except (ValueError, TypeError) as e:
        h._send_bytes(str(e).encode(), "text/plain", 400)
        return
    child = d.get("session_out") or default_child
    res = h.server.capturer.start(
        src, child, d.get("target", man.get("target", "both")),
        bool(d.get("call_trace", man.get("call_trace", True))),
        f"{start},{span2}", kind="drill",
        extra_args=["--capstride", "1", "--reset-trace"])
    if res.get("ok"):
        res = {"ok": True, "session": child, "at": int(d.get("at", 0)),
               "caprange": [start, span2]}
    h._send_json(res)


# ── POST: apply pins ─────────────────────────────────────────────────────────
def h_apply(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not (sdir / "session.json").is_file():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    d = _json(raw)
    try:
        from ..edits import apply as edits_apply
        res = edits_apply.apply(sdir, auto_pin=bool(d.get("auto_pin", False)))
        h._send_json(res)
    except Exception as e:                       # noqa: BLE001
        h._send_json({"ok": False, "error": repr(e)}, 500)


# ── POST: working trace ──────────────────────────────────────────────────────
def h_trace(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not (sdir / "session.json").is_file():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    d = _json(raw)
    ops = d.get("ops")
    if not isinstance(ops, list):
        h._send_bytes(b"ops must be a list", "text/plain", 400)
        return
    side = d.get("side", "")           # "", "port", "retail" (divergent)
    fname = {"port": "edit.port.trace.jsonl",
             "retail": "edit.retail.trace.jsonl"}.get(side, "edit.trace.jsonl")
    (sdir / fname).write_text("".join(json.dumps(o) + "\n" for o in ops))
    # mark the session stale (edits not yet captured)
    try:
        man = json.loads((sdir / "session.json").read_text())
        man["stale"] = True
        (sdir / "session.json").write_text(json.dumps(man, indent=2))
    except Exception:                  # noqa: BLE001
        pass
    h._send_json({"ok": True, "n": len(ops), "file": fname})


def h_notes(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not sdir.is_dir():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    d = _json(raw)
    notes = d.get("notes", [])
    if not isinstance(notes, list):
        h._send_bytes(b"notes must be a list", "text/plain", 400)
        return
    (sdir / "notes.jsonl").write_text("".join(json.dumps(n) + "\n" for n in notes))
    h._send_json({"ok": True, "n": len(notes)})


def h_divergent(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    mf = sdir / "session.json"
    if not mf.is_file():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    on = bool(_json(raw).get("on"))
    man = json.loads(mf.read_text())
    man["divergent"] = on
    if on:                                  # split the shared trace per side
        src = sdir / "edit.trace.jsonl"
        for f in ("edit.port.trace.jsonl", "edit.retail.trace.jsonl"):
            if src.is_file() and not (sdir / f).is_file():
                (sdir / f).write_text(src.read_text())
    mf.write_text(json.dumps(man, indent=2))
    h._send_json({"ok": True, "divergent": on})


def h_clone(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not sdir.is_dir():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    raw_name = _json(raw).get("name", "")
    name = re.sub(r"[^\w.-]", "_", raw_name) or (sess + "-copy")
    dst = h.server.sess_root / name
    if dst.exists():
        h._send_json({"ok": False, "error": f"{name} already exists"})
        return
    shutil.copytree(sdir, dst)
    try:
        m2 = json.loads((dst / "session.json").read_text())
        m2["session"] = name
        m2["cloned_from"] = sess
        (dst / "session.json").write_text(json.dumps(m2, indent=2))
    except Exception:                       # noqa: BLE001
        pass
    h._send_json({"ok": True, "name": name})


def h_edits_set(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not sdir.is_dir():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    d = _json(raw)
    edits = d.get("edits", [])
    if not isinstance(edits, list):
        h._send_bytes(b"edits must be a list", "text/plain", 400)
        return
    (sdir / "edits.jsonl").write_text(
        "".join(json.dumps(e) + "\n" for e in edits))
    h._send_json({"ok": True, "n": len(edits)})


def h_edits_append(h, m, raw):
    sess = m.group(1)
    sdir = h.server.sess_root / sess
    if not sdir.is_dir():
        h._send_bytes(b"no session", "text/plain", 404)
        return
    try:
        edit = json.loads(raw or b"{}")
    except json.JSONDecodeError:
        h._send_bytes(b"bad json", "text/plain", 400)
        return
    # minimal validation
    if "frame" not in edit or "kind" not in edit:
        h._send_bytes(b"need frame+kind", "text/plain", 400)
        return
    with (sdir / "edits.jsonl").open("a") as f:
        f.write(json.dumps(edit) + "\n")
    h._send_json({"ok": True, "edit": edit})


# ── the tables (first match wins, in order) ──────────────────────────────────
# POST tried in do_POST; GET tried in do_GET AFTER root + static-asset handling
# and BEFORE the /s/<sess>/<file> ranged catch-all (preserving the old order).
POST_ROUTES = [
    (r"^/record/start$",          h_record_start),
    (r"^/record/stop$",           h_record_stop),
    (r"^/capture$",               h_capture),
    (r"^/capture/cancel$",        h_capture_cancel),
    (r"^/s/([^/]+)/recapture$",   h_recapture),
    (r"^/s/([^/]+)/drill$",       h_drill),
    (r"^/s/([^/]+)/apply$",       h_apply),
    (r"^/s/([^/]+)/trace$",       h_trace),
    (r"^/s/([^/]+)/notes$",       h_notes),
    (r"^/s/([^/]+)/divergent$",   h_divergent),
    (r"^/s/([^/]+)/clone$",       h_clone),
    (r"^/s/([^/]+)/edits/set$",   h_edits_set),
    (r"^/s/([^/]+)/edits$",       h_edits_append),
]

GET_ROUTES = [
    (r"^/api/sessions$",    h_sessions),
    (r"^/api/jobs$",        h_jobs),
    (r"^/api/registries$",  h_registries),
]
