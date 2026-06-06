"""trace_studio_serve.py — the studio's local http server.

Serves the single-page viewer (trace_studio_web/) + each session's artifacts.
Critically implements HTTP Range for the .mp4 scrub videos — HTML5 <video>
seeking REQUIRES byte-range support, and stdlib SimpleHTTPRequestHandler does
not provide it. Also exposes a tiny JSON API:

    GET  /api/sessions                 → [{name, ...summary}]
    GET  /s/<session>/session.json     → manifest
    GET  /s/<session>/state.jsonl      → per-frame state
    GET  /s/<session>/edits.jsonl      → current marks
    GET  /s/<session>/<file>.mp4       → ranged video
    POST /s/<session>/edits            → append one mark {frame,kind,note,box?}
"""
from __future__ import annotations

import json
import re
import socketserver
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse, parse_qs

_CTYPE = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".jsonl": "application/x-ndjson; charset=utf-8",
    ".mp4": "video/mp4",
    ".png": "image/png",
}


def make_handler(sess_root: Path, web_dir: Path, default_session: str | None):

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
            if not p.is_file():
                self._send_bytes(b"not found", "text/plain", 404)
                return
            ctype = self._ctype(p)
            size = p.stat().st_size
            rng = self.headers.get("Range")
            if rng and ctype == "video/mp4":
                self._send_ranged(p, size, ctype, rng)
                return
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(size))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(p.read_bytes())

        def _send_ranged(self, p: Path, size: int, ctype: str, rng: str):
            m = re.match(r"bytes=(\d*)-(\d*)", rng.strip())
            if not m:
                self._send_bytes(b"bad range", "text/plain", 416)
                return
            a, b = m.group(1), m.group(2)
            if a == "":                       # suffix range: last N bytes
                length = int(b)
                start = max(0, size - length)
                end = size - 1
            else:
                start = int(a)
                end = int(b) if b else size - 1
            end = min(end, size - 1)
            if start > end:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.end_headers()
                return
            length = end - start + 1
            self.send_response(206)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(length))
            self.end_headers()
            if self.command == "HEAD":
                return
            with p.open("rb") as f:
                f.seek(start)
                remaining = length
                while remaining > 0:
                    chunk = f.read(min(65536, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)

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
            if path in ("/studio.js", "/style.css"):
                self._send_file(web_dir / path.lstrip("/"))
                return
            if path == "/api/sessions":
                self._send_json(self._sessions())
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
        def do_POST(self):
            u = urlparse(self.path)
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
          port: int = 8778, default_session: str | None = None) -> None:
    handler = make_handler(sess_root, web_dir, default_session)

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True
        daemon_threads = True

    httpd = Server((host, port), handler)
    url = f"http://{host}:{port}/"
    if default_session:
        url += f"?session={default_session}"
    print(f"trace_studio: serving {sess_root} at {url}", flush=True)
    print("trace_studio: Ctrl-C to stop", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\ntrace_studio: stopped", flush=True)
        httpd.shutdown()
