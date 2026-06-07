"""analysis/registry.py — the analyzer registry (served at GET /api/registries).

Describes the per-frame divergence views the SPA can render, decoupled from the
component code so adding/relabelling a view is one data entry. Each descriptor names
where its data lives in the session (manifest field or sidecar file) so the client
knows what to fetch:
  - diff    : manifest.diff.per_frame[].meanabs  (the DiffRibbon source)
  - state   : state.jsonl per-frame {port,retail} (the StatePanel source)
  - verdict : manifest.verdict (the phase/RNG verdict text + exit_code)
"""
from __future__ import annotations

ANALYZERS = [
    {"id": "diff", "label": "pixel diff",
     "source": "manifest.diff.per_frame", "field": "meanabs"},
    {"id": "state", "label": "state field",
     "source": "state.jsonl"},
    {"id": "verdict", "label": "phase/RNG verdict",
     "source": "manifest.verdict"},
]


def registry() -> list[dict]:
    return ANALYZERS
