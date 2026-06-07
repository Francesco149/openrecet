"""server/jobs.py — one unified view of the studio's background jobs.

The studio runs at most two long-lived subprocesses: a recorder (frida-attach to
retail) and a capturer (drive port[+retail] → session). Recapture and drill are
just `CaptureController.start(...)` invocations, so there is exactly one record slot
+ one capture slot. `JobsRegistry.list()` normalizes the two controllers' differently
shaped `status()` dicts (record: name/out/bytes/exists; capture: session/last_rc)
into one list the SPA's JobTray polls via GET /api/jobs — replacing the three
separate pollers (record/capture/recapture) the old UI ran.

Shape:
    {"jobs": [{id, kind, running, label, elapsed_s, rc, detail, ...}], "running": bool}
  - id      : "record" | "capture"               (the slot)
  - kind    : "record" | "capture" | "recapture" | "drill"   (what it's doing)
  - running : bool
  - label   : the recording name / the session name
  - rc      : last return code (capture only; None while running / for record)
  - detail  : the log tail (one line) for a status blurb
The {running} top-level (any job running) lets the existing store.useStatus hook —
which keys its poll interval on `.running` — drive the JobTray unchanged.
"""
from __future__ import annotations


class JobsRegistry:
    def __init__(self, recorder, capturer):
        self.recorder = recorder
        self.capturer = capturer

    def list(self) -> dict:
        r = self.recorder.status()
        c = self.capturer.status()
        jobs = [
            {
                "id": "record", "kind": "record",
                "running": bool(r.get("running")),
                "label": r.get("name"),
                "elapsed_s": r.get("elapsed_s", 0),
                "rc": None,
                "detail": r.get("log_tail", ""),
                "out": r.get("out"),
                "exists": r.get("exists"),
                "bytes": r.get("bytes"),
            },
            {
                "id": "capture",
                "kind": getattr(self.capturer, "kind", None) or "capture",
                "running": bool(c.get("running")),
                "label": c.get("session"),
                "elapsed_s": c.get("elapsed_s", 0),
                "rc": c.get("last_rc"),
                "detail": c.get("log_tail", ""),
                "session": c.get("session"),
            },
        ]
        return {"jobs": jobs, "running": any(j["running"] for j in jobs)}
