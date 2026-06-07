"""model/session.py — versioned, segment-centric session manifest.

A v2 session.json is the v1 manifest **superset** + `schema_version: 2` + a
`timeline` (ordered gameplay segments + zero-frame load_seam entries). Keeping it a
superset is deliberate: the existing web UI reads the same top-level v1 fields
untouched (the SPA rewrite that consumes `timeline` is Phase 4), so a v2 capture
opens in today's viewer AND tomorrow's.

v1 sessions (no `schema_version`) migrate IN MEMORY on load by synthesizing a
single gameplay segment from the global window/videos/verdict/state — so old
sessions "still open" without rewriting them on disk.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

SCHEMA_VERSION = 2


@dataclass
class Session:
    """A loaded session manifest with a uniform segmented view, whatever the
    on-disk schema version. `raw` is the full manifest dict (v1 superset)."""
    raw: dict
    path: Path | None = None

    @property
    def schema_version(self) -> int:
        return int(self.raw.get("schema_version", 1))

    @property
    def name(self) -> str:
        if self.raw.get("session"):
            return self.raw["session"]
        return self.path.parent.name if self.path else ""

    @property
    def timeline(self) -> list[dict]:
        """Always a segmented timeline. v2: the stored list. v1: synthesized
        (one gameplay segment from the global fields) — the migration."""
        tl = self.raw.get("timeline")
        if isinstance(tl, list) and tl:
            return tl
        return [self._synthetic_gameplay()]

    def _synthetic_gameplay(self) -> dict:
        m = self.raw
        fr = m.get("frame_range") or [0, max(0, int(m.get("n_frames", 0)) - 1)]
        return {
            "kind": "gameplay", "idx": 0,
            "frames": list(fr), "n_frames": m.get("n_frames", 0),
            "cadence": int(m.get("stride", 1) or 1),
            "videos": dict(m.get("videos") or {}),
            "verdict": m.get("verdict"),
            "state": m.get("state"),
            "call_trace": m.get("call_trace"),
            "_synthetic": True,           # marks a migrated-from-v1 segment
        }

    @property
    def load_seams(self) -> list[dict]:
        return [e for e in self.timeline if e.get("kind") == "load_seam"]

    @property
    def gameplay_segments(self) -> list[dict]:
        return [e for e in self.timeline if e.get("kind") == "gameplay"]


def load_session(path: Path) -> Session:
    """Load a session.json (or a session DIR containing one). v1 manifests load
    fine — the segmented view is synthesized lazily via Session.timeline."""
    p = Path(path)
    mf = p / "session.json" if p.is_dir() else p
    return Session(raw=json.loads(mf.read_text()), path=mf)


def make_v2_manifest(v1_fields: dict, timeline: list[dict]) -> dict:
    """Stamp a v1-superset manifest with the v2 schema marker + timeline. The
    caller fills v1_fields (n_frames, videos, verdict, caprange, …) exactly as the
    old monolith did; this adds `schema_version` + `timeline` on top."""
    out = dict(v1_fields)
    out["schema_version"] = SCHEMA_VERSION
    out["timeline"] = timeline
    return out


def write_session(sess_dir: Path, manifest: dict) -> Path:
    out = Path(sess_dir) / "session.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    return out
