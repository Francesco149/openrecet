"""server/ranged.py — HTTP Range serving for the .mp4 scrub videos.

HTML5 <video> seeking REQUIRES byte-range support, and stdlib
SimpleHTTPRequestHandler does not provide it. Extracted verbatim from the former
trace_studio_serve handler so the Range logic is its own testable unit; the
functions operate on the live BaseHTTPRequestHandler (`h`).
"""
from __future__ import annotations

import re
from pathlib import Path


def send_file(h, p: Path, ctype: str) -> None:
    """Serve a file; if it's an mp4 with a Range header, serve a 206 partial."""
    if not p.is_file():
        h._send_bytes(b"not found", "text/plain", 404)
        return
    size = p.stat().st_size
    rng = h.headers.get("Range")
    if rng and ctype == "video/mp4":
        send_ranged(h, p, size, ctype, rng)
        return
    h.send_response(200)
    h.send_header("Content-Type", ctype)
    h.send_header("Content-Length", str(size))
    h.send_header("Accept-Ranges", "bytes")
    h.send_header("Cache-Control", "no-cache")
    h.end_headers()
    if h.command != "HEAD":
        h.wfile.write(p.read_bytes())


def send_ranged(h, p: Path, size: int, ctype: str, rng: str) -> None:
    m = re.match(r"bytes=(\d*)-(\d*)", rng.strip())
    if not m:
        h._send_bytes(b"bad range", "text/plain", 416)
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
        h.send_response(416)
        h.send_header("Content-Range", f"bytes */{size}")
        h.end_headers()
        return
    length = end - start + 1
    h.send_response(206)
    h.send_header("Content-Type", ctype)
    h.send_header("Content-Range", f"bytes {start}-{end}/{size}")
    h.send_header("Accept-Ranges", "bytes")
    h.send_header("Content-Length", str(length))
    h.end_headers()
    if h.command == "HEAD":
        return
    with p.open("rb") as f:
        f.seek(start)
        remaining = length
        while remaining > 0:
            chunk = f.read(min(65536, remaining))
            if not chunk:
                break
            h.wfile.write(chunk)
            remaining -= len(chunk)
