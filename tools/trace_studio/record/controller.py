"""record/controller.py — subprocess controllers for the browser self-service loop
(lifted verbatim from trace_studio_serve.py):

  RecordController   owns the `frida_capture.py --record-trace` subprocess (the
                     retail recorder), paranoid about never leaving a stray.
  CaptureController  runs `trace_studio.py capture` so the browser can drive the
                     record→view→pin→apply→re-view loop without the CLI.
"""
from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

from ..paths import ROOT
from .recover import recover_raw


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
                recovered = recover_raw(out, run_dir)    # salvage a killed finalize
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
        self.kind: str = "capture"        # capture | recapture | drill (job identity)

    def _alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, trace: str, session: str, target: str,
              call_trace: bool, caprange: str | None, only: str = "both",
              kind: str = "capture") -> dict:
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
            if only and only != "both":
                cmd += ["--only", only]
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
            self.kind = kind
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
