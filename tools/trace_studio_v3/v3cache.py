#!/usr/bin/env python3
"""Trace Studio v3 — content-addressed capture cache + stored frame identity.

Two jobs, both from the plan's P2:

1. **Cache the retail drive** (kill pain #1, slow captures). The proxy writes its
   container to a transient %LOCALAPPDATA%\\openrecet\\v3 that the NEXT capture
   clobbers. This module copies a finished capture into a KEYED, persistent cache
   dir, so a re-run with the same retail-determining inputs reuses it (zero
   re-drive) and any sub-window is a slice of the cached container (orv3.slice_window).
   The key hashes ONLY what determines retail's pixels — the scenario trace + save
   + pins + the arm spec — so a port-side fix never invalidates the retail cache.

2. **Store frame identity** (kill pains #2/#3, sync whack-a-mole). The plan's
   thesis: identity must be STORED, never implied by a filename. Each cache entry
   carries a `v3meta.json` recording `(anchor, occurrence, offset0, count)` — so a
   kept frame's identity is `(anchor#occ, offset0 + index)`, IDENTICAL on both sides
   for the same logical moment regardless of how far the load stretched the
   absolute present-count. orv3_sync.py JOINs on it (E3-proven).
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3       # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
CACHE_ROOT = ROOT / "runs" / "studio-v3-cache"


def localappdata_v3() -> Path:
    """%LOCALAPPDATA%\\openrecet\\v3 as a WSL path (where the proxy writes the
    live capture before it's cached)."""
    out = subprocess.run(["cmd.exe", "/c", "echo %LOCALAPPDATA%"],
                         capture_output=True, text=True, cwd="/mnt/c").stdout.strip()
    wsl = subprocess.run(["wslpath", "-u", out], capture_output=True, text=True,
                         check=True).stdout.strip()
    return Path(wsl) / "openrecet" / "v3"


@dataclass
class FrameIdentity:
    """The STORED identity of a cache entry's window. Each kept frame index k has
    identity (anchor#occ, offset0 + k) — the v3 pairing key (E3)."""
    side: str            # "port" | "retail"
    scenario: str
    anchor: str          # the semantic anchor the window is relative to (e.g. HOUSE_FREEROAM)
    anchor_occ: int      # which occurrence of that anchor (1-based)
    anchor_frame: int    # absolute present-count the anchor fired at (informational/cross-check)
    offset0: int         # frames-since-anchor of kept frame 0 (the window start offset)
    count: int           # kept-frame count
    present_first: int   # absolute present-count of kept frame 0 (== anchor_frame + offset0)

    def offset_of(self, index: int) -> int:
        return self.offset0 + index

    def key_of(self, index: int) -> tuple[str, int, int]:
        """The join key for kept frame `index`: (anchor, occurrence, offset)."""
        return (self.anchor, self.anchor_occ, self.offset0 + index)


def cache_key(trace_path: Path, arm: dict | None) -> str:
    """8-hex content key over the retail-determining inputs: the scenario trace
    bytes + the arm spec (anchor/offset/count). Save bytes are referenced BY the
    trace ({savefile} sha in the trace), so hashing the trace text covers them.
    A port-side code change does not enter the key ⇒ the retail cache survives it."""
    h = hashlib.sha256()
    h.update(trace_path.read_bytes())
    if arm:
        h.update(json.dumps(arm, sort_keys=True).encode())
    return h.hexdigest()[:8]


def entry_dir(scenario: str, key: str, side: str) -> Path:
    return CACHE_ROOT / f"{scenario}-{key}" / side


def store(dest: Path, ident: FrameIdentity, src: Path | None = None) -> Path:
    """Copy the live capture (v3cap.bin + v3ref_*.raw) from `src` (default the
    proxy's %LOCALAPPDATA% dir) into `dest`, and write v3meta.json = the stored
    identity. Returns `dest`."""
    src = src or localappdata_v3()
    cap = src / "v3cap.bin"
    if not cap.exists():
        raise FileNotFoundError(f"no live capture at {cap}")
    dest.mkdir(parents=True, exist_ok=True)
    # clear any stale prior entry so a shorter window can't leave orphan refs
    for f in [dest / "v3cap.bin", *dest.glob("v3ref_*.raw"), dest / "v3meta.json"]:
        f.unlink(missing_ok=True)
    shutil.copy2(cap, dest / "v3cap.bin")
    for ref in sorted(src.glob("v3ref_*.raw")):
        shutil.copy2(ref, dest / ref.name)
    (dest / "v3meta.json").write_text(json.dumps(asdict(ident), indent=1))
    return dest


def load_meta(entry: Path) -> FrameIdentity:
    return FrameIdentity(**json.loads((entry / "v3meta.json").read_text()))


# ── cache LOOKUP: find a cached full-extent that a sub-window can be sliced from ──
# The auto-drive loop (orv3_window.py) asks "is the requested window already in a
# cached full-extent?" — if so it SLICES (zero re-drive), else it drives. Lookup is a
# scan of CACHE_ROOT (a full-extent per scenario isn't keyed by the per-window
# offset/count, so the loop can't reconstruct the dir name), guarded so it can never
# serve a STALE entry: the dir name encodes hash(trace+arm), and arm is reconstructible
# from the stored meta (anchor/offset0/count), so re-hashing the CURRENT trace and
# checking it still equals the dir's key proves the entry was captured from THIS trace.

def extent_contains(meta: FrameIdentity, off: int, n: int) -> bool:
    """Does the cached entry's identity-offset extent [offset0, offset0+count)
    fully contain the requested sub-window [off, off+n)?"""
    return meta.offset0 <= off and off + n <= meta.offset0 + meta.count


def dir_key(scenario: str, entry_parent_name: str) -> str | None:
    """Extract the content key from a cache dir name `{scenario}-{key}` (the scenario
    itself may contain hyphens, so strip the known prefix rather than rsplit)."""
    prefix = f"{scenario}-"
    return entry_parent_name[len(prefix):] if entry_parent_name.startswith(prefix) else None


def pick_extent(candidates: list[dict], anchor: str, off: int, n: int) -> dict | None:
    """Pure selection: among candidate {dir, meta, key_ok} entries, keep those whose
    anchor matches, whose stored trace-key still verifies (key_ok), and whose extent
    contains [off, off+n); return the WIDEST (largest count) — a wider full-extent
    serves more re-windows. None if nothing qualifies. Filesystem-free ⇒ unit-tested."""
    ok = [c for c in candidates
          if c["meta"].anchor == anchor and c["key_ok"]
          and extent_contains(c["meta"], off, n)]
    if not ok:
        return None
    return max(ok, key=lambda c: c["meta"].count)


def find_extent(scenario: str, side: str, anchor: str, off: int, n: int,
                trace_path: Path) -> Path | None:
    """Scan CACHE_ROOT for a cached `side` full-extent of `scenario` that contains the
    sub-window [off, off+n) under `anchor` AND was captured from the CURRENT trace
    (the dir-key re-hash guard). Returns the entry dir to slice, or None (drive it)."""
    candidates = []
    for meta_json in sorted(CACHE_ROOT.glob(f"{scenario}-*/{side}/v3meta.json")):
        entry = meta_json.parent
        try:
            meta = load_meta(entry)
        except (OSError, ValueError, TypeError):
            continue
        if meta.side != side:
            continue
        key = dir_key(scenario, entry.parent.name)
        # arm is reconstructible from the stored extent — re-hash the current trace and
        # require it still equals the dir's key ⇒ the entry is for THIS trace, not stale.
        arm = {"anchor": meta.anchor, "offset": meta.offset0, "count": meta.count}
        key_ok = key is not None and cache_key(trace_path, arm) == key
        candidates.append({"dir": entry, "meta": meta, "key_ok": key_ok})
    best = pick_extent(candidates, anchor, off, n)
    return best["dir"] if best else None


def preserve_live(scenario: str, side: str, anchor: str, offset0: int,
                  trace_path: Path, arm: dict, *, anchor_occ: int = 1,
                  src: Path | None = None) -> tuple[Path, FrameIdentity]:
    """Cache the LIVE proxy capture (%LOCALAPPDATA%) under a content key + its
    stored identity, in one call — the mechanism both capture drivers use. The
    anchor's absolute present-count is DERIVED from the container (present_first −
    offset0); present_first = anchor_frame + offset0 by construction, proven equal
    to the agent's reported arm frame. Returns (dest_dir, identity)."""
    src = src or localappdata_v3()
    c = orv3.Container.load(src / "v3cap.bin")
    if not c.frames:
        raise ValueError("live container has no kept frames — nothing to cache")
    present_first = c.frames[0].present
    ident = FrameIdentity(side=side, scenario=scenario, anchor=anchor,
                          anchor_occ=anchor_occ, anchor_frame=present_first - offset0,
                          offset0=offset0, count=c.n_frames, present_first=present_first)
    dest = entry_dir(scenario, cache_key(Path(trace_path), arm), side)
    store(dest, ident, src=src)
    return dest, ident


if __name__ == "__main__":
    import sys
    # quick inspector: print a cache entry's stored identity
    if len(sys.argv) < 2:
        raise SystemExit("usage: v3cache.py <cache-entry-dir>  — print stored identity")
    print(json.dumps(asdict(load_meta(Path(sys.argv[1]))), indent=1))
