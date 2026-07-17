#!/usr/bin/env python3
"""Trace Studio v3 — content-addressed capture cache + stored frame identity.

Two jobs, both from the plan's P2:

1. **Cache the retail drive** (kill pain #1, slow captures). The proxy writes its
   container to a transient %LOCALAPPDATA%\\openrecet\\v3 that the NEXT capture
   clobbers. This module copies a finished capture into a KEYED, persistent cache
   dir, so a re-run with the same capture provenance reuses it (zero re-drive) and any
   sub-window is a slice of the cached container (orv3.slice_window). EP-08: the 128-bit
   dir key hashes the SHARED capture provenance (scenario trace ⇒ {savefile} save + the
   arm spec + the staged d3d proxy + assets/recet.ini + cache-schema); per-side PE +
   frida agent are stored in v3meta.prov and validated on lookup — so a rebuilt proxy /
   changed assets re-drive both sides, an edited agent or a rebuilt exe re-drive only the
   affected side, and a port-side fix still never invalidates the retail cache.

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
import os
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3       # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
CACHE_ROOT = ROOT / "runs" / "studio-v3-cache"

# EP-08 cache provenance. Bumping CACHE_SCHEMA (or changing any input below) re-keys
# every entry ⇒ the next orv3_window re-drives (retail is serialized/slow — paid once).
# 2 = the full-provenance re-key (was an 8-hex trace+arm digest that never invalidated
# on a rebuilt proxy / edited frida agent). See docs/findings/parity-EP08-*.
CACHE_SCHEMA = 2

# The capture-determining inputs, as MODULE-LEVEL paths so tests monkeypatch them to
# temp files. `common_provenance` (SHARED by both sides ⇒ the dir key) vs
# `side_provenance` (per side ⇒ stored in v3meta, validated on lookup so a PORT rebuild
# never invalidates the RETAIL cache).
PROXY_DLL       = ROOT / "tools" / "trace_studio_v3" / "proxy" / "d3d8.dll"   # staged for BOTH sides
PORT_EXE        = ROOT / "build" / "openrecet.exe"
RETAIL_EXE      = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
AGENT_JS        = ROOT / "tools" / "frida" / "openrecet-agent.js"            # retail capture only
ASSETS_MANIFEST = ROOT / "vendor" / "assets-manifest.json"                  # optional (may be absent)
RECET_INI       = ROOT / "vendor" / "unpacked" / "recet.ini"                # optional config
# A v3cap.bin below this is truncated/empty ⇒ corrupt (real content corruption is still
# caught by the per-frame slice/replay verify; this is the cheap missing/empty guard).
_MIN_CONTAINER_BYTES = 1


def _sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _tool_sha(p: Path) -> str:
    """SHA-256 of a REQUIRED provenance input, or '@absent' if missing — an absent
    proxy/exe/agent is a real (distinct) provenance state, never silently equal."""
    p = Path(p)
    return _sha256_file(p) if p.is_file() else "@absent"


def _opt_sha(p: Path) -> str:
    """SHA-256 of an OPTIONAL provenance input (assets manifest / recet.ini) or '@none'."""
    p = Path(p)
    return _sha256_file(p) if p.is_file() else "@none"


def common_provenance(trace_path: Path) -> dict:
    """The determinants SHARED by both sides' captured containers (⇒ the cache DIR key).
    A change to any re-drives BOTH sides, because they all affect BOTH containers:
      - the scenario trace (⇒ the {savefile} save, whose sha lives in the trace text),
      - the staged d3d8 proxy DLL (staged next to the port AND the retail exe),
      - the optional assets manifest + recet.ini (config),
      - CACHE_SCHEMA (the cache format/semantics version).
    The arm/caprange window is folded in by key_from_common (it is per-request, not a
    file). Per-side PE/agent are deliberately NOT here (see side_provenance)."""
    return {
        "cache_schema": CACHE_SCHEMA,
        "trace_sha256": _sha256_file(Path(trace_path)),
        "proxy_sha256": _tool_sha(PROXY_DLL),
        "assets_manifest_sha256": _opt_sha(ASSETS_MANIFEST),
        "recet_ini_sha256": _opt_sha(RECET_INI),
    }


def side_provenance(side: str) -> dict:
    """The determinants specific to ONE side's container. Stored in v3meta.prov at drive
    time and validated on lookup — kept OUT of the shared dir key so a port rebuild never
    invalidates the retail cache. Port: its exe (no Frida). Retail: its exe + the agent."""
    if side == "port":
        return {"pe_sha256": _tool_sha(PORT_EXE), "agent_sha256": "@none"}
    return {"pe_sha256": _tool_sha(RETAIL_EXE), "agent_sha256": _tool_sha(AGENT_JS)}


def key_from_common(common: dict, arm: dict | None) -> str:
    """128-bit (32-hex) content key from a precomputed common_provenance + the arm spec.
    Split out so find_extent hashes the provenance FILES once and varies only the arm
    across candidates."""
    h = hashlib.sha256()
    h.update(json.dumps(common, sort_keys=True).encode())
    h.update(json.dumps(arm or {}, sort_keys=True).encode())
    return h.hexdigest()[:32]


def localappdata_v3() -> Path:
    """%LOCALAPPDATA%\\openrecet\\v3 as a WSL path (where the proxy writes the
    live capture before it's cached).

    Launching cmd.exe from WSL intermittently fails with a vsock error and an
    EMPTY stdout; a good result is an absolute Windows path (C:\\Users\\...).
    Three resolution tiers, robust to the interop being fully wedged:
      0. $OPENRECET_V3_DIR override (the escape hatch);
      1. cmd.exe echo %LOCALAPPDATA% (the canonical resolve; retried);
      2. a deterministic glob of /mnt/*/Users/*/AppData/Local/openrecet/v3 —
         the dir is fixed and the proxy already created it, so when cmd.exe is
         down we just pick the most-recently-written match."""
    env = os.environ.get("OPENRECET_V3_DIR")
    if env:
        return Path(env)

    out = ""
    for _ in range(8):
        out = subprocess.run(["cmd.exe", "/c", "echo %LOCALAPPDATA%"],
                             capture_output=True, text=True, cwd="/mnt/c").stdout.strip()
        if out and out != "%LOCALAPPDATA%" and (":\\" in out or ":/" in out):
            wsl = subprocess.run(["wslpath", "-u", out], capture_output=True,
                                 text=True, check=True).stdout.strip()
            return Path(wsl) / "openrecet" / "v3"

    # cmd.exe is wedged — fall back to the deterministic mounted-drive path.
    cands = sorted(
        (p for mnt in ("/mnt/c", "/mnt/d", "/mnt/e")
           for p in Path(mnt, "Users").glob("*/AppData/Local/openrecet/v3")
           if p.is_dir()),
        key=lambda p: p.stat().st_mtime, reverse=True)
    if cands:
        return cands[0]

    raise RuntimeError(
        "localappdata_v3: cmd.exe never returned %LOCALAPPDATA% (WSL interop "
        "flaky) and no /mnt/*/Users/*/AppData/Local/openrecet/v3 fallback exists "
        f"— set $OPENRECET_V3_DIR. last stdout={out!r}")


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
    prov: dict | None = None        # EP-08 provenance {"common": {...}, "side": {...}}; None = pre-EP08

    def offset_of(self, index: int) -> int:
        return self.offset0 + index

    def key_of(self, index: int) -> tuple[str, int, int]:
        """LEGACY join key for kept frame `index` (contiguity assumption)."""
        return (self.anchor, self.anchor_occ, self.offset0 + index)

    def _window_occ(self, entry: dict) -> int:
        """The occurrence of anchor `entry` RELATIVE TO THE WINDOW BASE
        (anchor_frame): its stored GLOBAL occ minus the firings of the same name
        strictly BEFORE the base anchor.  A cutscene's conv-pose / FX / blink can
        fire during retail's intro-video / load tail BEFORE the base anchor (the
        frames the PORT collapses), so the two sides' anchor streams have
        asymmetric PRE-base firings — counting those in the global occ shifts the
        SAME in-window firing to a different occ on each side (e.g. retail's
        CONV_POSE_BLINK at offset 21 is global occ 2 because of a load-tail blink
        at offset −30, while the port's is occ 1) and mispairs the whole window.
        Window-relative occ re-bases both sides to the shared base anchor.

        A PRE-base entry (frame < anchor_frame — outside the comparison window,
        present in the stream only for seq ordering, NEVER the most-recent anchor
        of an in-window frame) keeps its global occ.  A SYMMETRIC window (no
        pre-base firings — every confirmed HOUSE/guild/pause/title scenario) has
        pre == 0 ⇒ this is a no-op."""
        if entry["frame"] < self.anchor_frame:
            return entry["occ"]
        pre = sum(1 for a in (self.anchors or [])
                  if a["name"] == entry["name"] and a["frame"] < self.anchor_frame)
        return entry["occ"] - pre

    # ── meta v2: per-frame identity from the stored anchor stream ──
    def key_of_present(self, present: int) -> tuple[str, int, int]:
        """The E3 join key for a kept frame at absolute present-count `present`:
        (most-recent anchor ≤ present, its WINDOW-RELATIVE occurrence, frames-since-
        it).  Anchors on the SAME frame are aliases of one moment; the tie-break
        must be identical on both sides AND match a legacy single-anchor entry, so:
        prefer the entry's BASE anchor (both sides arm by the same one — e.g.
        HOUSE_FREEROAM, which fires the same frame as LOADING_END), else
        sorted-last name.  The occ is window-relative (see _window_occ) so a
        cutscene's pre-base load-tail firings don't shift it across sides."""
        best = None
        for a in self.anchors or []:
            if a["frame"] <= present:
                k = (a["frame"], a["name"] == self.anchor, a["name"])
                if best is None or k > (best["frame"], best["name"] == self.anchor,
                                        best["name"]):
                    best = a
        if best is None:    # no anchor at/before the frame — fall back to the base
            return (self.anchor, self.anchor_occ, present - self.anchor_frame)
        return (best["name"], self._window_occ(best), present - best["frame"])

    def key_of_present_rebased(self, present: int, origin_name: str
                               ) -> tuple[str, int, int]:
        """Like key_of_present, but number anchor occurrences from the most-recent
        firing of `origin_name` (≤ present) instead of from the WINDOW BASE.  Use
        when the two sides' windows armed on DIFFERENT occurrences of the base
        anchor — so `_window_occ` re-bases each to a different SEMANTIC origin and
        the SAME logical in-window anchor lands on a different window-occ per side
        (the cc08==4 case: port's window base is HOUSE_FREEROAM#2 = post-cc08-load,
        retail's is HOUSE_FREEROAM#1 = post-prologue, so the shared post-load HF is
        window-occ 2 on the port but 3 on retail ⇒ keys never match, 119/2698).
        Re-basing the occ count AND the key origin to a shared semantic anchor that
        is occ-1 on BOTH sides (e.g. CUSTOMER_SERVICE_ENTER) makes the post-origin
        segments pair by identity again (→ 2499/2698, the load-phase cancels).
        Frames before the first `origin_name` firing fall back to the base-relative
        key_of_present — they live across the load-stretch and don't pair anyway."""
        anchors = self.anchors or []
        origin = None
        for a in anchors:
            if a["name"] == origin_name and a["frame"] <= present:
                if origin is None or a["frame"] > origin["frame"]:
                    origin = a
        if origin is None:
            return self.key_of_present(present)
        of = origin["frame"]
        # most-recent anchor in [origin, present], tie-broken by (frame, name) —
        # symmetric across sides (no self.anchor preference, since the base differs).
        best = max((a for a in anchors if of <= a["frame"] <= present),
                   key=lambda a: (a["frame"], a["name"]))
        # occurrence of best.name counted FROM the origin (inclusive).
        occ = sum(1 for a in anchors
                  if a["name"] == best["name"] and of <= a["frame"] <= best["frame"])
        return (best["name"], occ, present - best["frame"])

    def anchor_seq(self) -> dict[tuple[str, int], int]:
        """(name, window-occ) → firing position, the cross-side TOTAL ORDER for
        sorting join keys ((anchor position, delta) sorts columns chronologically
        even though deltas reset at every anchor).  Uses the same window-relative
        occ as key_of_present so the keys resolve."""
        seq = {}
        for a in sorted(self.anchors or [], key=lambda a: (a["frame"], a["name"])):
            seq.setdefault((a["name"], self._window_occ(a)), len(seq))
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


def resolve_base_anchor(anchors: list[dict], name: str,
                        present_first: int) -> tuple[int, int] | None:
    """Auto-detect the WINDOW's base anchor from the stored stream: the MOST-RECENT
    firing of `name` at a frame ≤ `present_first` (the first kept frame's present).
    Returns (frame, occurrence), or None if `name` never fires at/before the window.

    Why not just occurrence #1: the two sides capture the run ASYMMETRICALLY. For a
    cutscene window the PORT keeps the full run (e.g. HOUSE_FREEROAM#1 at the first
    house entry + HOUSE_FREEROAM#2 at the cutscene) while RETAIL captures window-only
    (its sole HOUSE_FREEROAM = the window's). Pinning the base to occ #1 then labels
    the two sides by DIFFERENT firings (port HF#1@284, retail HF@2986) ⇒
    key_of_present mispairs EVERY frame (the iv1_2 0/299). A window is based on the
    firing at or before its first kept frame; picking that (and letting _window_occ
    re-base it to a shared window-relative occ) pairs the sides. A symmetric
    single-firing window resolves to occ #1 unchanged — a no-op for every confirmed
    HOUSE/guild/pause/title scenario."""
    cands = [a for a in anchors if a["name"] == name and a["frame"] <= present_first]
    if not cands:
        return None
    base = max(cands, key=lambda a: a["frame"])
    return base["frame"], base["occ"]


def cache_key(trace_path: Path, arm: dict | None) -> str:
    """128-bit (32-hex) content key over common_provenance(trace) + the arm spec — the
    SHARED (both-sides) capture determinants. Was an 8-hex trace+arm digest (pre-EP08)
    that never invalidated on a rebuilt proxy / changed assets; EP-08 widens it to 32
    hex and keys it by the full shared provenance (trace ⇒ {savefile} save, proxy,
    assets/ini, CACHE_SCHEMA). Per-side PE/agent are validated separately (v3meta.prov)
    so a PORT rebuild never invalidates the RETAIL cache (find_extent)."""
    return key_from_common(common_provenance(Path(trace_path)), arm)


def entry_dir(scenario: str, key: str, side: str) -> Path:
    return CACHE_ROOT / f"{scenario}-{key}" / side


def _store_call_trace(src: Path, dst: Path, kept_presents: set[int] | None) -> int:
    """Copy a --state call_trace.jsonl into the cache, WINDOWED to `kept_presents`
    (the container's kept d3d present-counts).  The call-trace `frame` IS the present-
    count the identity join keys on, so keeping only frame ∈ kept_presents makes the
    stored state sidecar cover EXACTLY the kept d3d frames — dropping BOTH the pre-
    window load-stretch a --state drive now emits UN-GATED (so the window head is
    covered, ★NEXT-d) AND any tail frame past the last kept present.  kept_presents=None
    ⇒ verbatim copy (back-compat).  Returns the kept line count (-1 on a verbatim copy)."""
    if kept_presents is None:
        shutil.copy2(src, dst)
        return -1
    kept = 0
    with src.open() as f, dst.open("w") as o:
        for ln in f:
            s = ln.strip()
            if not s.startswith("{"):
                continue
            try:
                if json.loads(s).get("frame") in kept_presents:
                    o.write(ln if ln.endswith("\n") else ln + "\n")
                    kept += 1
            except ValueError:
                continue
    return kept


def store(dest: Path, ident: FrameIdentity, src: Path | None = None,
          call_trace_path: Path | None = None,
          kept_presents: set[int] | None = None) -> Path:
    """Copy the live capture (v3cap.bin + references: v3refs.txt hash lines and/or
    v3ref_*.raw) from `src` (default the proxy's %LOCALAPPDATA% dir) into `dest`,
    and write v3meta.json = the stored identity. Returns `dest`.

    `call_trace_path` (a --state drive's run_dir/call_trace.jsonl) is copied in as the
    entry's `call_trace.jsonl` sidecar — the engine-state pillar orv3_state.py keys by
    identity for the viewer's game-state panel. Always cleared first so a no-state
    re-drive can't leave a stale state file behind the new pixels.  `kept_presents`
    windows that sidecar to the kept d3d present-counts (see _store_call_trace) — the
    join-correct slice that pairs one state row per kept d3d frame."""
    src = src or localappdata_v3()
    cap = src / "v3cap.bin"
    if not cap.exists():
        raise FileNotFoundError(f"no live capture at {cap}")
    dest.mkdir(parents=True, exist_ok=True)
    # clear any stale prior entry so a shorter window can't leave orphan refs (and a
    # no-state re-drive can't leave a stale call_trace.jsonl behind fresh pixels)
    for f in [dest / "v3cap.bin", *dest.glob("v3ref_*.raw"),
              dest / "v3refs.txt", dest / "v3meta.json", dest / "call_trace.jsonl",
              dest / "v3cap.census.json"]:
        f.unlink(missing_ok=True)
    shutil.copy2(cap, dest / "v3cap.bin")
    for ref in sorted(src.glob("v3ref_*.raw")):
        shutil.copy2(ref, dest / ref.name)
    if (src / "v3refs.txt").exists():
        shutil.copy2(src / "v3refs.txt", dest / "v3refs.txt")
    # GX-00 dynamic census: the proxy's per-forwarded-method call-count sidecar, so
    # d3d_census.py --dynamic can gate this cached side (capture-completeness).
    if (src / "v3cap.census.json").exists():
        shutil.copy2(src / "v3cap.census.json", dest / "v3cap.census.json")
    if call_trace_path is not None and Path(call_trace_path).exists():
        _store_call_trace(Path(call_trace_path), dest / "call_trace.jsonl", kept_presents)
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

    def reindex(self, join_anchor: str | None) -> dict:
        """A fresh {key: Frame} index keyed by key_of_present_rebased on `join_anchor`
        (the opt-in side-by-side join — see orv3_sync --join-anchor / FRONT cc08==4).
        Returns the DEFAULT self.index when join_anchor is falsy or the meta has no
        anchor stream (so callers can pass it unconditionally).  Recomputed from
        cont.frames — cheap (the container parse already happened); the view layer
        uses this so the viewer's columns/state pair under the re-based join."""
        if not join_anchor or not self.meta.anchors:
            return self.index
        return {self.meta.key_of_present_rebased(f.present, join_anchor): f
                for f in self.cont.frames}


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
# serve a STALE entry: the dir name encodes the 128-bit hash(common_provenance+arm)
# (trace ⇒ {savefile} save, proxy, assets/ini, cache-schema — all reconstructible from
# the CURRENT on-disk inputs + the stored meta), and the stored per-side PE/agent
# (v3meta.prov) are re-checked against the current build ⇒ a rebuilt proxy, edited
# agent, or new exe re-drives the affected side (EP-08 _staleness).

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


def _staleness(entry: Path, meta: FrameIdentity, key: str | None, common: dict,
               cur_side: dict, arm: dict) -> str | None:
    """Why a cached `side` entry can't be served for THIS lookup — None means it can.
    Checked in order (EP-08 'corrupt cache rejected' + 'explain every stale decision'):
      1. the container is present + non-empty (else incomplete/corrupt),
      2. it carries EP-08 per-side provenance + a 128-bit key (else pre-EP08 ⇒ re-key),
      3. the SHARED provenance key still matches (trace/proxy/assets/cache-schema),
      4. the stored PER-SIDE provenance matches the current PE/agent.
    find_extent logs every non-None reason for an otherwise-serviceable entry."""
    cap = entry / "v3cap.bin"
    if not cap.exists():
        return "container missing (incomplete/corrupt cache)"
    if cap.stat().st_size < _MIN_CONTAINER_BYTES:
        return "container empty (corrupt cache)"
    if meta.prov is None or key is None or len(key) != 32:
        return "pre-EP08 entry (re-key by full provenance)"
    if key_from_common(common, arm) != key:
        return "shared provenance changed (trace/proxy/assets/cache-schema)"
    stored_side = meta.prov.get("side") or {}
    if stored_side != cur_side:
        changed = [k for k in cur_side if stored_side.get(k) != cur_side.get(k)] or ["side"]
        return f"per-side provenance changed ({'/'.join(changed)})"
    return None


def find_extent(scenario: str, side: str, anchor: str, off: int, n: int,
                trace_path: Path) -> Path | None:
    """Scan CACHE_ROOT for a cached `side` full-extent of `scenario` that contains the
    sub-window [off, off+n) under `anchor` AND is provenance-fresh (EP-08): the SHARED
    key still matches the current trace/proxy/assets/cache-schema, and the stored
    per-side PE/agent match. Returns the entry dir to slice, or None (⇒ drive it). Every
    stale-but-otherwise-serviceable entry is logged with WHY."""
    common = common_provenance(Path(trace_path))   # hash the provenance FILES once
    cur_side = side_provenance(side)                # this side's current PE (+ agent)
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
        # arm is reconstructible from the stored ARM SPEC (meta v2) / extent (legacy).
        arm = {"anchor": meta.anchor, "offset": meta.eff_arm_offset,
               "count": meta.eff_arm_count}
        stale = _staleness(entry, meta, key, common, cur_side, arm)
        # explain a would-have-served-but-stale entry (anchor + extent match, but stale).
        if stale is not None and meta.anchor == anchor and extent_contains(meta, off, n):
            print(f"[cache] STALE {side} {entry.parent.name}: {stale} → re-drive")
        candidates.append({"dir": entry, "meta": meta, "key_ok": stale is None})
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
    kept_presents = set(c.presents)   # the exact kept d3d present-counts = state-join keys
    anchors = None
    anchor_frame = present_first - offset0
    if anchors_path is not None and Path(anchors_path).exists():
        anchors = read_anchor_stream(Path(anchors_path))
        # The window's base is AUTO-DETECTED (most-recent firing ≤ present_first),
        # NOT assumed to be occ #1 — the two sides capture the run asymmetrically
        # (the port keeps the full run, retail window-only), so occ #1 can name a
        # DIFFERENT firing per side and mispair the whole window. See resolve_base_anchor.
        base = resolve_base_anchor(anchors, anchor, present_first)
        if base is not None:
            anchor_frame, anchor_occ = base
        else:
            print(f"[cache] WARNING: no {anchor} firing ≤ present {present_first} in "
                  f"{anchors_path} — falling back to legacy anchor_frame derivation")
    ident = FrameIdentity(side=side, scenario=scenario, anchor=anchor,
                          anchor_occ=anchor_occ, anchor_frame=anchor_frame,
                          offset0=offset0, count=c.n_frames, present_first=present_first,
                          arm_offset=int(arm.get("offset", offset0)),
                          arm_count=int(arm.get("count", c.n_frames)),
                          anchors=anchors,
                          prov={"common": common_provenance(Path(trace_path)),
                                "side": side_provenance(side)})
    dest = entry_dir(scenario, cache_key(Path(trace_path), arm), side)
    store(dest, ident, src=src, call_trace_path=call_trace_path,
          kept_presents=kept_presents)
    return dest, ident


if __name__ == "__main__":
    import sys
    # quick inspector: print a cache entry's stored identity
    if len(sys.argv) < 2:
        raise SystemExit("usage: v3cache.py <cache-entry-dir>  — print stored identity")
    print(json.dumps(asdict(load_meta(Path(sys.argv[1]))), indent=1))
