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

import atexit
import json
import os
import re
import signal
import socketserver
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")

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


# ─── trace recorder (frida-attach to the running retail game) ────────────────

def _recover_raw(out: Path, run_dir: Path) -> bool:
    """Reconstruct a `.raw.jsonl` from frida_capture's LIVE-STREAMED run-dir when
    its finalize was interrupted before it could assemble the file. The streamed
    pieces survive a kill: run_dir/trace.jsonl (sparse input change-points,
    line-buffered) + run_dir/agent.log ([anchor]/[esc_record]/[save_capture]
    lines). We rebuild the exact same format finalize writes (header + DENSE
    sticky-filled inputs + anchors + esc + {savefile}/{save_write}), including the
    18 MB boot-save blob that was streamed next to the raw. Returns True on write."""
    import hashlib
    trace_jsonl = run_dir / "trace.jsonl"
    agent_log = run_dir / "agent.log"
    if not trace_jsonl.exists():
        return False
    inputs: dict[int, int] = {}
    for ln in trace_jsonl.read_text().splitlines():
        ln = ln.strip()
        if not ln:
            continue
        try:
            o = json.loads(ln)
        except json.JSONDecodeError:
            continue
        if "frame" in o and "buttons" in o:
            inputs[int(o["frame"])] = int(str(o["buttons"]), 16)
    if not inputs:
        return False
    anchors: list[dict] = []
    escs: list[int] = []
    saves: list[dict] = []
    maxf = max(inputs)
    if agent_log.exists():
        for ln in agent_log.read_text(errors="replace").splitlines():
            m = re.match(r"\[anchor\] (\S+) @ frame=(\d+) gframe=(\d+) rng=(\d+)", ln)
            if m:
                fr = int(m.group(2))
                anchors.append({"anchor": m.group(1), "frame": fr,
                                "gframe": int(m.group(3)), "rng": int(m.group(4))})
                maxf = max(maxf, fr)
                continue
            m = re.match(r"\[esc_record\] frame=(\d+)", ln)
            if m:
                fr = int(m.group(1)); escs.append(fr); maxf = max(maxf, fr)
                continue
            m = re.match(r"\[save_capture\] (boot|write) #(\d+) @frame=(\d+) .*→ (\S+)", ln)
            if m:
                saves.append({"which": m.group(1), "index": int(m.group(2)),
                              "frame": int(m.group(3)), "file": m.group(4)})
    n = maxf + 1
    seed = next((a["rng"] for a in sorted(anchors, key=lambda a: a["frame"])), None)
    lines = [json.dumps({"_rec": "openrecet-tas-raw-v1", "frames": n,
                         "start_abs": 0, "rng_seed_at_start": seed,
                         "_recovered": True})]
    sticky = 0
    for i in range(n):
        sticky = inputs.get(i, sticky)
        lines.append(json.dumps({"frame": i, "buttons": f"0x{sticky:04x}"}))
    for a in anchors:
        lines.append(json.dumps({"anchor": a["anchor"], "frame": a["frame"],
                                 "gframe": a["gframe"], "rng": a["rng"]}))
    for ef in escs:
        lines.append(json.dumps({"esc": ef}))
    # Re-emit save rows (recompute sha/size from the streamed .bin next to the raw).
    for sv in saves:
        blob = out.parent / sv["file"]
        if not blob.exists():
            continue
        data = blob.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        if sv["which"] == "boot":
            lines.append(json.dumps({"savefile": sv["file"], "sha256": sha,
                                     "size": len(data)}))
        else:
            lines.append(json.dumps({"save_write": {
                "index": sv["index"], "frame": sv["frame"], "file": sv["file"],
                "sha256": sha, "size": len(data)}}))
    out.write_text("\n".join(lines) + "\n")
    return True


class RecordController:
    """Owns the `frida_capture.py --record-trace` subprocess driven by the studio
    record panel. Spawns it in its OWN process group so the WHOLE group can be
    signalled — and is paranoid about never leaving a stray recorder: stop()
    SIGINTs (frida_capture finalises + writes the trace on SIGINT), escalates to
    SIGKILL if it hangs, and a process-exit hook force-kills any survivor."""

    PIDFILE = ROOT / "runs" / "recordings" / ".studio_recorder.json"

    def __init__(self, remote: str):
        self.remote = remote
        self.lock = threading.Lock()
        self.proc: subprocess.Popen | None = None
        self.pgid: int | None = None
        self.name: str | None = None
        self.out: Path | None = None
        self.log: Path | None = None
        self.run_dir: Path | None = None
        self.started: float = 0.0

    @classmethod
    def reap_orphan(cls) -> None:
        """At studio startup, SIGKILL a recorder orphaned by a previously
        hard-killed server. Scoped to the pgid WE recorded — never touches a
        recorder started by hand (e.g. tools/record-trace.sh)."""
        try:
            info = json.loads(cls.PIDFILE.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return
        pgid = info.get("pgid")
        try:
            if pgid:
                os.killpg(pgid, signal.SIGKILL)
                print(f"trace_studio: reaped orphaned recorder pgid={pgid}",
                      flush=True)
        except (ProcessLookupError, PermissionError):
            pass
        cls.PIDFILE.unlink(missing_ok=True)

    def _clear_pidfile(self) -> None:
        self.PIDFILE.unlink(missing_ok=True)

    def _alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, name: str) -> dict:
        with self.lock:
            if self._alive():
                return {"ok": False, "error": f"already recording '{self.name}'"}
            safe = re.sub(r"[^\w.-]", "_", name) or time.strftime("rec-%Y%m%d-%H%M%S")
            rec_dir = ROOT / "runs" / "recordings"
            rec_dir.mkdir(parents=True, exist_ok=True)
            out = rec_dir / f"{safe}.raw.jsonl"
            log = rec_dir / f"{safe}.reclog.txt"
            run_dir = rec_dir / f"_rt_{safe}"
            cmd = [sys.executable, str(ROOT / "tools" / "frida_capture.py"),
                   "--remote", self.remote,
                   "--record-trace", str(out), "--run-dir", str(run_dir)]
            logf = log.open("w")
            try:
                # start_new_session ⇒ own process group; inherit the dev-shell env
                # (serve runs under `nix develop`, so frida is importable here).
                self.proc = subprocess.Popen(
                    cmd, cwd=str(ROOT), stdout=logf, stderr=subprocess.STDOUT,
                    start_new_session=True)
            except Exception as e:                       # noqa: BLE001
                logf.close()
                return {"ok": False, "error": f"spawn failed: {e!r}"}
            self.pgid = os.getpgid(self.proc.pid)
            self.name, self.out, self.log = safe, out, log
            self.run_dir = run_dir
            self.started = time.time()
            try:
                self.PIDFILE.write_text(json.dumps(
                    {"pid": self.proc.pid, "pgid": self.pgid, "name": safe}))
            except OSError:
                pass
            return {"ok": True, "name": safe, "out": str(out), "pid": self.proc.pid}

    def stop(self, grace_s: float = 90.0) -> dict:
        """Stop the recorder. frida_capture writes the .raw.jsonl LAST, after a
        SIGINT-triggered finalize (script.unload + remote session.detach, which
        can be slow), so we: SIGINT, then WAIT PATIENTLY for the file to appear
        (the definitive done signal) — never SIGKILL mid-finalize and lose the
        trace. If the file never lands (finalize hung), RECOVER it from the live-
        streamed run_dir/{trace.jsonl,agent.log} before force-killing. Either way
        the process is dead at the end (no stray)."""
        with self.lock:
            if self.proc is None:
                return {"ok": False, "error": "not recording"}
            name, out, run_dir = self.name, self.out, self.run_dir

            def _done() -> bool:
                return bool(out and out.exists() and out.stat().st_size > 0)

            if self.proc.poll() is None:
                self._signal_group(signal.SIGINT)
                deadline = time.time() + grace_s
                while time.time() < deadline:
                    if _done() or self.proc.poll() is not None:
                        break
                    time.sleep(0.3)

            written = _done()
            recovered = False
            if not written and run_dir:
                recovered = _recover_raw(out, run_dir)   # salvage a killed finalize
                written = recovered

            # Make sure nothing is left running — graceful first, then hard.
            if self.proc.poll() is None:
                self._signal_group(signal.SIGTERM)
                time.sleep(2.0)
            if self.proc.poll() is None:
                self._signal_group(signal.SIGKILL)
                time.sleep(0.5)

            res = {"ok": True, "name": name, "out": str(out) if out else None,
                   "written": written, "recovered": recovered,
                   "bytes": (out.stat().st_size if (out and out.exists()) else 0)}
            self.proc = None
            self.pgid = None
            self.run_dir = None
            self._clear_pidfile()
            return res

    def _signal_group(self, sig) -> None:
        try:
            if self.pgid is not None:
                os.killpg(self.pgid, sig)
        except ProcessLookupError:
            pass
        except Exception:                                # noqa: BLE001
            try:
                if self.proc:
                    self.proc.send_signal(sig)
            except Exception:                            # noqa: BLE001
                pass

    def status(self) -> dict:
        with self.lock:
            running = self._alive()
            tail = ""
            if self.log and self.log.exists():
                lines = self.log.read_text(errors="replace").splitlines()
                tail = "\n".join(lines[-8:])
            return {
                "running": running,
                "name": self.name,
                "elapsed_s": round(time.time() - self.started, 1) if self.started else 0,
                "out": str(self.out) if self.out else None,
                "exists": bool(self.out and self.out.exists()),
                "bytes": (self.out.stat().st_size if (self.out and self.out.exists()) else 0),
                "remote": self.remote,
                "log_tail": tail,
            }

    def force_cleanup(self) -> None:
        """Last-resort teardown on server shutdown — never leave a recorder."""
        if self._alive():
            self._signal_group(signal.SIGINT)
            time.sleep(1.0)
            if self._alive():
                self._signal_group(signal.SIGKILL)
        self._clear_pidfile()


# ─── capture controller (runs `trace_studio.py capture` for the browser loop) ─

class CaptureController:
    """Runs a capture/re-capture as a background subprocess so the browser can
    drive the record→view→pin→apply→re-view loop without the CLI. One at a time."""

    def __init__(self, remote: str, sess_root: Path):
        self.remote = remote
        self.sess_root = sess_root
        self.lock = threading.Lock()
        self.proc: subprocess.Popen | None = None
        self.session: str | None = None
        self.log: Path | None = None
        self.started: float = 0.0
        self.last_rc: int | None = None

    def _alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, trace: str, session: str, target: str,
              call_trace: bool, caprange: str | None) -> dict:
        with self.lock:
            if self._alive():
                return {"ok": False, "error": f"a capture is already running "
                        f"({self.session})"}
            self.sess_root.mkdir(parents=True, exist_ok=True)
            log = self.sess_root / f".capture-{re.sub(r'[^\w.-]', '_', session)}.log"
            cmd = [sys.executable, str(ROOT / "tools" / "trace_studio.py"),
                   "capture", trace, "--session", session, "--target", target,
                   "--remote", self.remote]
            if call_trace:
                cmd.append("--call-trace")
            if caprange:
                cmd += ["--caprange", caprange]
            logf = log.open("w")
            try:
                self.proc = subprocess.Popen(
                    cmd, cwd=str(ROOT), stdout=logf, stderr=subprocess.STDOUT,
                    start_new_session=True)
            except Exception as e:                       # noqa: BLE001
                logf.close()
                return {"ok": False, "error": f"spawn failed: {e!r}"}
            self.session, self.log, self.started = session, log, time.time()
            self.last_rc = None
            return {"ok": True, "session": session}

    def status(self) -> dict:
        with self.lock:
            running = self._alive()
            if not running and self.proc is not None:
                self.last_rc = self.proc.poll()
            tail = ""
            if self.log and self.log.exists():
                lines = [l for l in self.log.read_text(errors="replace").splitlines()
                         if l.strip()]
                tail = lines[-1] if lines else ""
            return {"running": running, "session": self.session,
                    "elapsed_s": round(time.time() - self.started, 1) if self.started else 0,
                    "last_rc": self.last_rc, "log_tail": tail}

    def force_cleanup(self) -> None:
        if self._alive():
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception:                            # noqa: BLE001
                pass


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
                self._send_json(capturer.start(
                    trace, d.get("session", ""), d.get("target", "both"),
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
                    bool(d.get("call_trace", man.get("call_trace", True))), None))
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
                    import trace_studio_apply
                    res = trace_studio_apply.apply(
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
