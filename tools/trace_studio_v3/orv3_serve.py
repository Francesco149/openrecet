#!/usr/bin/env python3
"""Trace Studio v3 — viewer server.

Serves a baked view dir (orv3_view: manifest.json + port|retail|diff PNGs) under the
v3 SPA, preserving the v2 viewer UX (3-panel lockstep scrub + diff ribbon + per-frame
state). Frames are static PNGs (the bit-exact ref bake), so the server is a thin static
file server — no jobs/recorder/ranged-mp4 machinery. The on-demand replay endpoints
(zoom / draw isolation / semantic diff) layer on in P3d.

Static roots, in dispatch order:
  /manifest.json, /port/*, /retail/*, /diff/*   ← the baked VIEW dir
  /style.css, /vendor/*                          ← the v2 web dir (reuse the base theme)
  / , /app.mjs, /studio.css, …                   ← the v3 web dir (the SPA)

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_serve.py \
      --view runs/studio-v3-cache/<scen>-<key>/port/view [--port 8780]
Open http://localhost:<port>/ . Launch a long-lived serve via setsid/nohup so it
survives the tool-call shell (the v2 lesson).
"""
from __future__ import annotations

import argparse
import sys
from functools import partial
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

HERE = Path(__file__).resolve().parent
V3WEB = HERE / "web"
V2WEB = HERE.parent / "trace_studio_web"        # reuse style.css + vendor (htm-preact)

MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
        ".mjs": "application/javascript; charset=utf-8",
        ".js": "application/javascript; charset=utf-8",
        ".json": "application/json; charset=utf-8", ".png": "image/png",
        ".svg": "image/svg+xml", ".map": "application/json"}


class Handler(SimpleHTTPRequestHandler):
    view_dir: Path = Path(".")

    # reuse the v2 base theme + component styles verbatim (preserve the UX), so the v3
    # SPA only ships a tiny v3.css of additions.
    SHARED = {"/style.css": V2WEB / "style.css", "/studio.css": V2WEB / "web" / "studio.css"}

    def _resolve(self) -> Path | None:
        """Map the request path to a file under one of the static roots, refusing any
        traversal outside them."""
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        if path == "/":
            path = "/index.html"
        rel = path.lstrip("/")
        # 1) the baked view artifacts
        if path == "/manifest.json" or path.split("/", 2)[1] in ("port", "retail", "diff"):
            base, sub = self.view_dir, rel
        # 2) reuse the v2 CSS (explicit map) + vendored htm-preact
        elif path in self.SHARED:
            f = self.SHARED[path]
            return f if f.is_file() else None
        elif path.startswith("/vendor/"):
            base, sub = V2WEB, rel
        # 3) the v3 SPA
        else:
            base, sub = V3WEB, rel
        cand = (base / sub).resolve()
        return cand if cand.is_file() and str(cand).startswith(str(base.resolve())) else None

    def do_GET(self) -> None:
        f = self._resolve()
        if not f:
            self.send_error(404, f"not found: {self.path}")
            return
        data = f.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", MIME.get(f.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")   # always pick up a re-bake
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):   # quiet (a long-lived serve shouldn't spam)
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description="Trace Studio v3 viewer server")
    ap.add_argument("--view", type=Path, required=True,
                    help="baked view dir (orv3_view output: manifest.json + port|retail|diff/)")
    ap.add_argument("--port", type=int, default=8780)
    args = ap.parse_args()
    view = args.view.resolve()
    if not (view / "manifest.json").is_file():
        print(f"no manifest.json in {view} — run orv3_view.py first", file=sys.stderr)
        return 2
    Handler.view_dir = view
    httpd = HTTPServer(("0.0.0.0", args.port), partial(Handler))
    print(f"trace studio v3 — serving {view}\n  http://localhost:{args.port}/", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
