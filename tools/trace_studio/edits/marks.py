"""edits/marks.py — the mark-type registry (single source of truth).

A "mark" is a viewer annotation at a frame (kind + optional note/box). Some kinds are
AUTO-APPLIED by edits/apply.py into trace ops (phasepin/rngpin → {phasepin}/{rngseed});
the rest become worklist items for a human/Claude. This list is served verbatim at
GET /api/registries so the SPA MarkBar renders one button per entry — adding a
throwaway mark kind here surfaces it end-to-end with zero JS edits. apply.py imports
APPLY_KINDS / WORKLIST_KINDS from here so the kind→behavior split has one home.
"""
from __future__ import annotations

MARK_TYPES = [
    {"kind": "phasepin", "label": "⟲ pin phase", "applies": True,
     "hint": "insert {phasepin: F} — zero db054 / anim / b154 at this frame"},
    {"kind": "rngpin", "label": "🎲 pin RNG", "applies": True,
     "hint": "insert {rngseed: [F, value]} — re-seed the LCG at this frame"},
    {"kind": "note", "label": "✎ note", "applies": False,
     "hint": "worklist: free text (anchor/feature reminders — say so in the note)"},
]

# kinds apply.py auto-inserts as trace ops vs routes to the worklist.
APPLY_KINDS = frozenset(m["kind"] for m in MARK_TYPES if m["applies"])
WORKLIST_KINDS = frozenset(m["kind"] for m in MARK_TYPES if not m["applies"])


def registry() -> list[dict]:
    return MARK_TYPES
