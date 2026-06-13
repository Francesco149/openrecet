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


def notes_file(scenario: str) -> Path:
    """The WSL path to a scenario's viewer-notes json (the user's flagged
    divergences). Lives under %LOCALAPPDATA% — a WINDOWS-LOCAL dir — because the
    native viewer is a Windows process and CANNOT fopen-WRITE a \\\\wsl.localhost
    UNC path (the same limitation replay.exe hit). orv3_view writes this file's
    Windows form into view.json (`notes_path`) for the viewer to read+write;
    orv3_notes.py reads back this WSL form so Claude can see the flags. One file
    per scenario, keyed inside by the stable identity label (survives re-windows)."""
    p = localappdata_v3() / "notes" / f"{scenario}.json"
    p.parent.mkdir(parents=True, exist_ok=True)
    return p


@dataclass
class FrameIdentity:
    """The STORED identity of a cache entry's window.

    Legacy (single-anchor) shape: each kept frame index k has identity
    (anchor#occ, offset0 + k) — valid only while the kept set is CONTIGUOUS in
    present space (one anchor, no mid-window load seams; the HOUSE toy).

    meta v2 (multi-anchor, the plan's E3 design): `anchors` stores the run's full
    anchor stream [{name, occ, frame}] in the side's keep-trigger clock (port:
    engine frame == present; retail: agent frame == present). A kept frame's
    identity is then (most-recent-anchor#occ, present − that anchor's frame) —
    resolved per frame from its STORED present, so a mid-window load that the port
    suppresses (kept set skips presents) and retail stretches (kept set includes
    load frames) still re-syncs at each segment's anchor by construction.

    `arm_offset`/`arm_count` store the DRIVE REQUEST (the caprange / proxy-arm
    window) verbatim — `count` is the KEPT frame count, which is smaller whenever
    loads are suppressed mid-window, so the cache-key arm reconstruction must not
    be derived from it."""
    side: str            # "port" | "retail"
    scenario: str
    anchor: str          # the BASE anchor the window was armed relative to (e.g. HOUSE_FREEROAM)
    anchor_occ: int      # which occurrence of that anchor (1-based)
    anchor_frame: int    # absolute present-count the base anchor fired at
    offset0: int         # arm-space offset of the window start (== arm_offset)
    count: int           # KEPT-frame count (≤ arm_count when loads are suppressed)
    present_first: int   # absolute present-count of kept frame 0
    arm_offset: int | None = None   # the drive request, verbatim (None = legacy: offset0)
    arm_count: int | None = None    # the drive request, verbatim (None = legacy: count)
    anchors: list | None = None     # full anchor stream [{name, occ, frame}] (None = legacy)

    def offset_of(self, index: int) -> int:
        return self.offset0 + index

    def key_of(self, index: int) -> tuple[str, int, int]:
        """LEGACY join key for kept frame `index` (contiguity assumption)."""
        return (self.anchor, self.anchor_occ, self.offset0 + index)

    # ── meta v2: per-frame identity from the stored anchor stream ──
    def key_of_present(self, present: int) -> tuple[str, int, int]:
        """The E3 join key for a kept frame at absolute present-count `present`:
        (most-recent anchor ≤ present, its occurrence, frames-since-it). Anchors on
        the SAME frame are aliases of one moment; the tie-break must be identical
        on both sides AND match a legacy single-anchor entry, so: prefer the
        entry's BASE anchor (both sides arm by the same one — e.g. HOUSE_FREEROAM,
        which fires the same frame as LOADING_END), else sorted-last name."""
        best = None
        for a in self.anchors or []:
            if a["frame"] <= present:
                k = (a["frame"], a["name"] == self.anchor, a["name"])
                if best is None or k > (best["frame"], best["name"] == self.anchor,
                                        best["name"]):
                    best = a
        if best is None:    # no anchor at/before the frame — fall back to the base
            return (self.anchor, self.anchor_occ, present - self.anchor_frame)
        return (best["name"], best["occ"], present - best["frame"])

    def anchor_seq(self) -> dict[tuple[str, int], int]:
        """(name, occ) → firing position, the cross-side TOTAL ORDER for sorting
        join keys ((anchor position, delta) sorts columns chronologically even
        though deltas reset at every anchor)."""
        seq = {}
        for a in sorted(self.anchors or [], key=lambda a: (a["frame"], a["name"])):
            seq.setdefault((a["name"], a["occ"]), len(seq))
        return seq

    @property
    def eff_arm_offset(self) -> int:
        return self.offset0 if self.arm_offset is None else self.arm_offset

    @property
    def eff_arm_count(self) -> int:
        return self.count if self.arm_count is None else self.arm_count


def read_anchor_stream(path: Path) -> list[dict]:
    """Parse a run's anchors.jsonl ({"anchor": name, "frame": N} per line, both the
    port's --anchor-trace-record and the retail agent's stream) into the meta-v2
    anchor list [{name, occ, frame}], occurrences counted per name in frame order."""
    rows = []
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line.startswith("{"):
            continue
        try:
            d = json.loads(line)
            rows.append({"name": str(d["anchor"]), "frame": int(d["frame"])})
        except (ValueError, KeyError):
            continue
    rows.sort(key=lambda r: r["frame"])
    seen: dict[str, int] = {}
    for r in rows:
        seen[r["name"]] = seen.get(r["name"], 0) + 1
        r["occ"] = seen[r["name"]]
    return rows


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


def store(dest: Path, ident: FrameIdentity, src: Path | None = None,
          call_trace_path: Path | None = None) -> Path:
    """Copy the live capture (v3cap.bin + references: v3refs.txt hash lines and/or
    v3ref_*.raw) from `src` (default the proxy's %LOCALAPPDATA% dir) into `dest`,
    and write v3meta.json = the stored identity. Returns `dest`.

    `call_trace_path` (a --state drive's run_dir/call_trace.jsonl) is copied in as the
    entry's `call_trace.jsonl` sidecar — the engine-state pillar orv3_state.py keys by
    identity for the viewer's game-state panel. Always cleared first so a no-state
    re-drive can't leave a stale state file behind the new pixels."""
    src = src or localappdata_v3()
    cap = src / "v3cap.bin"
    if not cap.exists():
        raise FileNotFoundError(f"no live capture at {cap}")
    dest.mkdir(parents=True, exist_ok=True)
    # clear any stale prior entry so a shorter window can't leave orphan refs (and a
    # no-state re-drive can't leave a stale call_trace.jsonl behind fresh pixels)
    for f in [dest / "v3cap.bin", *dest.glob("v3ref_*.raw"),
              dest / "v3refs.txt", dest / "v3meta.json", dest / "call_trace.jsonl"]:
        f.unlink(missing_ok=True)
    shutil.copy2(cap, dest / "v3cap.bin")
    for ref in sorted(src.glob("v3ref_*.raw")):
        shutil.copy2(ref, dest / ref.name)
    if (src / "v3refs.txt").exists():
        shutil.copy2(src / "v3refs.txt", dest / "v3refs.txt")
    if call_trace_path is not None and Path(call_trace_path).exists():
        shutil.copy2(call_trace_path, dest / "call_trace.jsonl")
    (dest / "v3meta.json").write_text(json.dumps(asdict(ident), indent=1))
    return dest


def load_meta(entry: Path) -> FrameIdentity:
    return FrameIdentity(**json.loads((entry / "v3meta.json").read_text()))


@dataclass
class LoadedSide:
    """A cache entry PARSED ONCE — meta + container + the per-frame identity index —
    so the 91+58 MB containers parse once per re-window loop, not once per phase.

    v2's pain #1 has a v3 echo: the sync / view / draws phases each re-parsed the same
    container in pure Python (the `orv3.Container.load` walk is ~0.7 s for a 2600-frame
    retail container, paid 3× per side). This is the parse-once handoff: the orchestrator
    `load_side`s each window side once, then threads the SAME object through
    `orv3_sync.sync_entries` and `orv3_view.write_view_json` (which itself re-calls sync).
    `index` is keyed by the stored identity (meta v2: most-recent anchor ≤ present;
    legacy: offset arithmetic) — the join key both the sync and the view timeline use."""
    entry: Path
    meta: FrameIdentity
    cont: "orv3.Container"
    index: dict          # identity key tuple -> orv3.Frame, in kept-frame order

    @property
    def dims(self) -> list:
        return [self.cont.dev.get("w"), self.cont.dev.get("h")]


def load_side(entry: Path) -> LoadedSide:
    """Parse a cache entry's meta + container ONCE and build its identity index."""
    entry = Path(entry)
    meta = load_meta(entry)
    cont = orv3.Container.load(entry / "v3cap.bin")
    index = {}
    for f in cont.frames:
        key = meta.key_of_present(f.present) if meta.anchors else meta.key_of(f.index)
        index[key] = f
    return LoadedSide(entry, meta, cont, index)


def as_side(x) -> LoadedSide:
    """Accept a LoadedSide verbatim (parse-once handoff) OR a Path/str (parse it now,
    for the standalone CLIs). Idempotent, so a function can take either and a caller
    that already parsed once pays nothing."""
    return x if isinstance(x, LoadedSide) else load_side(Path(x))


# ── cache LOOKUP: find a cached full-extent that a sub-window can be sliced from ──
# The auto-drive loop (orv3_window.py) asks "is the requested window already in a
# cached full-extent?" — if so it SLICES (zero re-drive), else it drives. Lookup is a
# scan of CACHE_ROOT (a full-extent per scenario isn't keyed by the per-window
# offset/count, so the loop can't reconstruct the dir name), guarded so it can never
# serve a STALE entry: the dir name encodes hash(trace+arm), and arm is reconstructible
# from the stored meta (anchor/offset0/count), so re-hashing the CURRENT trace and
# checking it still equals the dir's key proves the entry was captured from THIS trace.

def extent_contains(meta: FrameIdentity, off: int, n: int) -> bool:
    """Does the cached entry's ARM-space extent [arm_offset, arm_offset+arm_count)
    fully contain the requested sub-window [off, off+n)? Arm space (the drive
    request), NOT kept count — a mid-window suppressed load makes kept < armed
    without shrinking the covered window."""
    lo, hi = meta.eff_arm_offset, meta.eff_arm_offset + meta.eff_arm_count
    return lo <= off and off + n <= hi


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
        # arm is reconstructible from the stored ARM SPEC (meta v2) / extent (legacy)
        # — re-hash the current trace and require it still equals the dir's key ⇒
        # the entry is for THIS trace, not stale.
        arm = {"anchor": meta.anchor, "offset": meta.eff_arm_offset,
               "count": meta.eff_arm_count}
        key_ok = key is not None and cache_key(trace_path, arm) == key
        candidates.append({"dir": entry, "meta": meta, "key_ok": key_ok})
    best = pick_extent(candidates, anchor, off, n)
    return best["dir"] if best else None


def preserve_live(scenario: str, side: str, anchor: str, offset0: int,
                  trace_path: Path, arm: dict, *, anchor_occ: int = 1,
                  src: Path | None = None,
                  anchors_path: Path | None = None,
                  call_trace_path: Path | None = None) -> tuple[Path, FrameIdentity]:
    """Cache the LIVE proxy capture (%LOCALAPPDATA%) under a content key + its
    stored identity, in one call — the mechanism both capture drivers use.

    `anchors_path` (the run's anchors.jsonl) upgrades the entry to meta v2: the
    full anchor stream is stored so each kept frame's identity resolves per frame
    (most-recent anchor ≤ its present) — required once a window spans load seams.
    The base anchor's absolute frame then comes from the STREAM (its anchor_occ'th
    firing); without a stream it falls back to the legacy derivation
    (present_first − offset0, valid only for a contiguous kept set).
    Returns (dest_dir, identity)."""
    src = src or localappdata_v3()
    c = orv3.Container.load(src / "v3cap.bin")
    if not c.frames:
        raise ValueError("live container has no kept frames — nothing to cache")
    present_first = c.frames[0].present
    anchors = None
    anchor_frame = present_first - offset0
    if anchors_path is not None and Path(anchors_path).exists():
        anchors = read_anchor_stream(Path(anchors_path))
        base = next((a for a in anchors
                     if a["name"] == anchor and a["occ"] == anchor_occ), None)
        if base is not None:
            anchor_frame = base["frame"]
        else:
            print(f"[cache] WARNING: {anchor}#{anchor_occ} not in {anchors_path} — "
                  f"falling back to legacy anchor_frame derivation")
    ident = FrameIdentity(side=side, scenario=scenario, anchor=anchor,
                          anchor_occ=anchor_occ, anchor_frame=anchor_frame,
                          offset0=offset0, count=c.n_frames, present_first=present_first,
                          arm_offset=int(arm.get("offset", offset0)),
                          arm_count=int(arm.get("count", c.n_frames)),
                          anchors=anchors)
    dest = entry_dir(scenario, cache_key(Path(trace_path), arm), side)
    store(dest, ident, src=src, call_trace_path=call_trace_path)
    return dest, ident


if __name__ == "__main__":
    import sys
    # quick inspector: print a cache entry's stored identity
    if len(sys.argv) < 2:
        raise SystemExit("usage: v3cache.py <cache-entry-dir>  — print stored identity")
    print(json.dumps(asdict(load_meta(Path(sys.argv[1]))), indent=1))
