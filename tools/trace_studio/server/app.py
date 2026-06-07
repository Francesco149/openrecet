"""server/app.py — the studio's local http server (lifted from trace_studio_serve.py).

Serves the single-page viewer (trace_studio_web/) + each session's artifacts (with
HTTP Range for the .mp4 scrub videos, via server/ranged.py) + a small JSON API:

    GET  /api/sessions                 → [{name, ...summary}]
    GET  /s/<session>/session.json     → manifest
    GET  /s/<session>/state.jsonl      → per-frame state
    GET  /s/<session>/edits.jsonl      → current marks
    GET  /s/<session>/<file>.mp4       → ranged video
    POST /s/<session>/edits            → append one mark {frame,kind,note,box?}

The request handlers live in server/routes.py as a dispatch table (POST_ROUTES /
GET_ROUTES); this module is the thin BaseHTTPRequestHandler that owns the response
helpers + the non-tabular GET fall-throughs (root redirect, static assets, the
/s/<sess>/<file> ranged catch-all). Shared state (sess_root / web_dir / recorder /
capturer / default_session) is stashed on the Server instance so the route handlers
read it off `self.server` instead of a closure.
"""
from __future__ import annotations

import atexit
import re
import socketserver
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from ..paths import DEFAULT_REMOTE
from ..record.controller import CaptureController, RecordController
from . import ranged, routes

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


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):           # quiet
        pass

    # ── response helpers (the route handlers call these) ─────────────────────
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
        import json
        self._send_bytes(json.dumps(obj).encode(), _CTYPE[".json"], code)

    def _send_file(self, p: Path):
        # HTTP Range (mp4 seeking) handled in server/ranged.py.
        ranged.send_file(self, p, self._ctype(p))

    # ── GET ──────────────────────────────────────────────────────────────────
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        web_dir = self.server.web_dir
        if path == "/" or path == "/index.html":
            qs = parse_qs(u.query)
            if "session" not in qs and self.server.default_session:
                # redirect to the default session for convenience
                self.send_response(302)
                self.send_header(
                    "Location", f"/?session={self.server.default_session}")
                self.end_headers()
                return
            self._send_file(web_dir / "index.html")
            return
        # Static assets from web_dir (app.mjs, store.mjs, vendor/*.mjs, css, the
        # new web/ SPA subtree …). Excludes the API + session prefixes.
        if path not in ("/", "/index.html") and "/s/" not in path \
                and not path.startswith("/api") and not path.startswith("/record") \
                and not path.startswith("/capture"):
            rel = path.lstrip("/")
            if ".." not in rel:
                cand = (web_dir / rel).resolve()
                if cand.is_file() and str(cand).startswith(str(web_dir.resolve())):
                    self._send_file(cand)
                    return
        # tabular GET routes (/api/sessions, /record/status, /capture/status, …)
        for rx, fn in routes.GET_ROUTES:
            mm = re.match(rx, path)
            if mm:
                return fn(self, mm, b"")
        # /s/<sess>/<file> ranged catch-all
        m = re.match(r"^/s/([^/]+)/(.+)$", path)
        if m:
            sess, rel = m.group(1), m.group(2)
            if ".." in rel:
                self._send_bytes(b"no", "text/plain", 403)
                return
            self._send_file(self.server.sess_root / sess / rel)
            return
        self._send_bytes(b"not found", "text/plain", 404)

    # ── POST ─────────────────────────────────────────────────────────────────
    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""
        for rx, fn in routes.POST_ROUTES:
            m = re.match(rx, u.path)
            if m:
                return fn(self, m, raw)
        self._send_bytes(b"not found", "text/plain", 404)


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

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True
        daemon_threads = True

    httpd = Server((host, port), Handler)
    # Shared state the route handlers read off self.server.
    httpd.sess_root = sess_root
    httpd.web_dir = web_dir
    httpd.default_session = default_session
    httpd.recorder = recorder
    httpd.capturer = capturer

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
