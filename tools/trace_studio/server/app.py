"""server/app.py — the studio's local http server (lifted from trace_studio_serve.py).

Serves the single-page viewer (trace_studio_web/) + each session's artifacts (with
HTTP Range for the .mp4 scrub videos, via server/ranged.py) + a tiny JSON API:

    GET  /api/sessions                 → [{name, ...summary}]
    GET  /s/<session>/session.json     → manifest
    GET  /s/<session>/state.jsonl      → per-frame state
    GET  /s/<session>/edits.jsonl      → current marks
    GET  /s/<session>/<file>.mp4       → ranged video
    POST /s/<session>/edits            → append one mark {frame,kind,note,box?}

The do_POST route ladder is kept verbatim (the dispatch-table refactor is Phase 4,
co-designed with the SPA); only the apply + controller imports moved into the package.
"""
from __future__ import annotations

import atexit
import json
import re
import socketserver
import time
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from ..paths import DEFAULT_REMOTE
from ..record.controller import CaptureController, RecordController
from . import ranged

_CTYPE = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".mjs": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".jsonl": "application/x-ndjson; charset=utf-8",
    ".mp4": "video/mp4",
    ".png": "image/png",
}


def make_handler(sess_root: Path, web_dir: Path, default_session: str | None,
                 recorder: "RecordController", capturer: "CaptureController"):

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):           # quiet
            pass

        # ── helpers ──────────────────────────────────────────────────────────
        def _ctype(self, p: Path) -> str:
            return _CTYPE.get(p.suffix.lower(), "application/octet-stream")

        def _send_bytes(self, data: bytes, ctype: str, code: int = 200):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(data)

        def _send_json(self, obj, code: int = 200):
            self._send_bytes(json.dumps(obj).encode(), _CTYPE[".json"], code)

        def _send_file(self, p: Path):
            # HTTP Range (mp4 seeking) handled in server/ranged.py.
            ranged.send_file(self, p, self._ctype(p))

        def _sessions(self) -> list[dict]:
            out = []
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

        # ── GET ──────────────────────────────────────────────────────────────
        def do_HEAD(self):
            self.do_GET()

        def do_GET(self):
            u = urlparse(self.path)
            path = u.path
            if path == "/" or path == "/index.html":
                qs = parse_qs(u.query)
                if "session" not in qs and default_session:
                    # redirect to the default session for convenience
                    self.send_response(302)
                    self.send_header("Location", f"/?session={default_session}")
                    self.end_headers()
                    return
                self._send_file(web_dir / "index.html")
                return
            # Static assets from web_dir (app.mjs, store.mjs, vendor/*.mjs, css …).
            if path not in ("/", "/index.html") and "/s/" not in path \
                    and not path.startswith("/api") and not path.startswith("/record") \
                    and not path.startswith("/capture"):
                rel = path.lstrip("/")
                if ".." not in rel:
                    cand = (web_dir / rel).resolve()
                    if cand.is_file() and str(cand).startswith(str(web_dir.resolve())):
                        self._send_file(cand)
                        return
            if path == "/api/sessions":
                self._send_json(self._sessions())
                return
            if path == "/record/status":
                self._send_json(recorder.status())
                return
            if path == "/capture/status":
                self._send_json(capturer.status())
                return
            m = re.match(r"^/s/([^/]+)/(.+)$", path)
            if m:
                sess, rel = m.group(1), m.group(2)
                if ".." in rel:
                    self._send_bytes(b"no", "text/plain", 403)
                    return
                self._send_file(sess_root / sess / rel)
                return
            self._send_bytes(b"not found", "text/plain", 404)

        # ── POST ─────────────────────────────────────────────────────────────
        def _read_body(self) -> dict:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b"{}"
            try:
                return json.loads(body or b"{}")
            except json.JSONDecodeError:
                return {}

        def do_POST(self):
            u = urlparse(self.path)
            if u.path == "/record/start":
                d = self._read_body()
                self._send_json(recorder.start(d.get("name", "")))
                return
            if u.path == "/record/stop":
                self._send_json(recorder.stop())
                return
            if u.path == "/capture":
                d = self._read_body()
                trace = d.get("trace")
                if not trace:
                    self._send_bytes(b"need trace", "text/plain", 400)
                    return
                # The SERVER owns the session name (so /capture/status can report it
                # and the client can open it). Derive from the trace basename + a
                # timestamp when not given; cmd_capture's --session takes precedence.
                session = d.get("session")
                if not session:
                    base = Path(trace).name
                    for suf in (".raw.jsonl", ".trace.jsonl", ".jsonl"):
                        if base.endswith(suf):
                            base = base[: -len(suf)]; break
                    session = f"{re.sub(r'[^\w.-]', '_', base)}-{time.strftime('%Y%m%d-%H%M%S')}"
                self._send_json(capturer.start(
                    trace, session, d.get("target", "both"),
                    bool(d.get("call_trace", True)), d.get("caprange")))
                return
            m = re.match(r"^/s/([^/]+)/recapture$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                mf = sdir / "session.json"
                if not mf.is_file():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                man = json.loads(mf.read_text())
                d = self._read_body()
                working = man.get("working_trace") or man.get("trace")
                self._send_json(capturer.start(
                    working, sess, d.get("target", man.get("target", "both")),
                    bool(d.get("call_trace", man.get("call_trace", True))), None,
                    only=d.get("only", "both")))
                return
            m = re.match(r"^/s/([^/]+)/apply$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                if not (sdir / "session.json").is_file():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                d = self._read_body()
                try:
                    from ..edits import apply as edits_apply
                    res = edits_apply.apply(
                        sdir, auto_pin=bool(d.get("auto_pin", False)))
                    self._send_json(res)
                except Exception as e:                   # noqa: BLE001
                    self._send_json({"ok": False, "error": repr(e)}, 500)
                return
            m = re.match(r"^/s/([^/]+)/trace$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                if not (sdir / "session.json").is_file():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                d = self._read_body()
                ops = d.get("ops")
                if not isinstance(ops, list):
                    self._send_bytes(b"ops must be a list", "text/plain", 400)
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
                self._send_json({"ok": True, "n": len(ops), "file": fname})
                return
            m = re.match(r"^/s/([^/]+)/notes$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                if not sdir.is_dir():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                d = self._read_body()
                notes = d.get("notes", [])
                if not isinstance(notes, list):
                    self._send_bytes(b"notes must be a list", "text/plain", 400)
                    return
                (sdir / "notes.jsonl").write_text("".join(json.dumps(n) + "\n" for n in notes))
                self._send_json({"ok": True, "n": len(notes)})
                return
            m = re.match(r"^/s/([^/]+)/divergent$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                mf = sdir / "session.json"
                if not mf.is_file():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                on = bool(self._read_body().get("on"))
                man = json.loads(mf.read_text())
                man["divergent"] = on
                if on:                                  # split the shared trace per side
                    src = sdir / "edit.trace.jsonl"
                    for f in ("edit.port.trace.jsonl", "edit.retail.trace.jsonl"):
                        if src.is_file() and not (sdir / f).is_file():
                            (sdir / f).write_text(src.read_text())
                mf.write_text(json.dumps(man, indent=2))
                self._send_json({"ok": True, "divergent": on})
                return
            m = re.match(r"^/s/([^/]+)/clone$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                if not sdir.is_dir():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                raw = self._read_body().get("name", "")
                name = re.sub(r"[^\w.-]", "_", raw) or (sess + "-copy")
                dst = sess_root / name
                if dst.exists():
                    self._send_json({"ok": False, "error": f"{name} already exists"})
                    return
                import shutil
                shutil.copytree(sdir, dst)
                try:
                    m2 = json.loads((dst / "session.json").read_text())
                    m2["session"] = name
                    m2["cloned_from"] = sess
                    (dst / "session.json").write_text(json.dumps(m2, indent=2))
                except Exception:                       # noqa: BLE001
                    pass
                self._send_json({"ok": True, "name": name})
                return
            m = re.match(r"^/s/([^/]+)/edits/set$", u.path)
            if m:
                sess = m.group(1)
                sdir = sess_root / sess
                if not sdir.is_dir():
                    self._send_bytes(b"no session", "text/plain", 404)
                    return
                d = self._read_body()
                edits = d.get("edits", [])
                if not isinstance(edits, list):
                    self._send_bytes(b"edits must be a list", "text/plain", 400)
                    return
                (sdir / "edits.jsonl").write_text(
                    "".join(json.dumps(e) + "\n" for e in edits))
                self._send_json({"ok": True, "n": len(edits)})
                return
            m = re.match(r"^/s/([^/]+)/edits$", u.path)
            if not m:
                self._send_bytes(b"not found", "text/plain", 404)
                return
            sess = m.group(1)
            sdir = sess_root / sess
            if not sdir.is_dir():
                self._send_bytes(b"no session", "text/plain", 404)
                return
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b"{}"
            try:
                edit = json.loads(body)
            except json.JSONDecodeError:
                self._send_bytes(b"bad json", "text/plain", 400)
                return
            # minimal validation
            if "frame" not in edit or "kind" not in edit:
                self._send_bytes(b"need frame+kind", "text/plain", 400)
                return
            with (sdir / "edits.jsonl").open("a") as f:
                f.write(json.dumps(edit) + "\n")
            self._send_json({"ok": True, "edit": edit})

    return Handler


def serve(sess_root: Path, web_dir: Path, host: str = "127.0.0.1",
          port: int = 8778, default_session: str | None = None,
          remote: str = DEFAULT_REMOTE) -> None:
    # Reap a recorder orphaned by a previously hard-killed studio before we start.
    RecordController.reap_orphan()
    recorder = RecordController(remote)
    capturer = CaptureController(remote, sess_root)
    # Never leave a stray recorder/capture if the studio dies any which way.
    atexit.register(recorder.force_cleanup)
    atexit.register(capturer.force_cleanup)
    handler = make_handler(sess_root, web_dir, default_session, recorder, capturer)

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True
        daemon_threads = True

    httpd = Server((host, port), handler)
    url = f"http://{host}:{port}/"
    if default_session:
        url += f"?session={default_session}"
    print(f"trace_studio: serving {sess_root} at {url}", flush=True)
    print(f"trace_studio: record target (frida) = {remote}", flush=True)
    print("trace_studio: Ctrl-C to stop", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\ntrace_studio: stopped", flush=True)
    finally:
        recorder.force_cleanup()
        capturer.force_cleanup()
        httpd.shutdown()
