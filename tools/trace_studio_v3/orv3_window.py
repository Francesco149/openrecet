#!/usr/bin/env python3
"""Trace Studio v3 — the capture-once / slice-many WINDOW LOOP (P2 final).

ONE command to get an aligned port|retail pair for any sub-window of a scenario,
driving ONLY what's missing or stale. This is the auto-drive loop the P2 plan ends
on: "a driver flag that slices a cached full-extent instead of re-capturing when the
window is in-extent." It composes the proven P2 pieces —

    house_capture.py / port_capture.py   (drive + cache a full-extent, once)
    v3cache.find_extent                  (is the window already cached? — guarded)
    orv3_slice.slice_entry               (re-emit a sub-window, zero re-drive)
    orv3_sync.sync_entries               (the identity JOIN → pairs.json)

— into the loop a human actually runs while iterating. The full-extent is the
scenario's {caprange}; any sub-window inside it is served by SLICING the cache.

What each call does, per side (port, retail), independently:
  • RETAIL — find a cached full-extent for (scenario, anchor) that CONTAINS the
    requested window and was captured from the CURRENT trace (the dir-key re-hash
    guard in find_extent). HIT ⇒ slice it (instant, zero re-drive). MISS ⇒ drive the
    full caprange extent via house_capture.py (the slow load-stretch — paid ONCE),
    then slice. A port-side code change NEVER invalidates this (retail's key is
    trace+arm only) ⇒ the v2 "--only port" loop, now also immune to window changes.
  • PORT — same, plus a freshness check: a rebuilt build/openrecet.exe (mtime newer
    than the cached port container) means the cached PORT pixels are stale ⇒ re-drive
    the port (fast — no load-stretch). --reuse-port forces the cache; --force-port the
    drive.
Then JOIN the two sub-window slices by stored identity → pairs.json + an ALIGNED
verdict. A re-window or a port-fix loop that cost a full retail re-drive in v2 is now
a slice (+ at most a fast port drive).

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_window.py \
      house-loaded-display-pinned --window 130:20 \
      [--anchor HOUSE_FREEROAM] [--force-retail] [--force-port] [--reuse-port] \
      [--no-verify] [--max-frames N]
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3_slice      # noqa: E402
import orv3_sync       # noqa: E402
import orv3_view       # noqa: E402
import v3cache         # noqa: E402  (load_side — the parse-once handoff)

ROOT       = Path(__file__).resolve().parent.parent.parent
SCEN_DIR   = ROOT / "tests" / "scenarios"
PORT_EXE   = ROOT / "build" / "openrecet.exe"
HOUSE_DRV  = Path(__file__).resolve().parent / "house_capture.py"
PORT_DRV   = Path(__file__).resolve().parent / "port_capture.py"
VIEWER_EXE = Path(__file__).resolve().parent / "viewer" / "viewer.exe"
WIN_ROOT   = ROOT / "runs" / "studio-v3-windows"
# The "current working trace" pointer the desktop/Start-Menu "OpenRecet Trace
# Studio" shortcut opens. Rewritten on EVERY window build below, so the shortcut
# always opens the latest trace we drove — no manual updating. See open_studio.sh
# + CLAUDE.md "Trace Studio shortcut".
STUDIO_CURRENT = Path(__file__).resolve().parent / ".studio_current"


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(Path(p).resolve())],
                          capture_output=True, text=True, check=True).stdout.strip()


def write_current_pointer(view_path: Path, label: str) -> None:
    """Record the just-built window as the 'current working trace' for the shortcut
    (line 1 = the view.json path, line 2 = a human label). Best-effort; never fatal."""
    try:
        STUDIO_CURRENT.write_text(f"{view_path.resolve()}\n{label}\n")
    except OSError as e:
        print(f"[view]  (couldn't update the studio pointer: {e})")


def launch_viewer(view_path: Path) -> None:
    """Open the native viewer on a view.json, DETACHED (own session) so it outlives the
    loop process — the v3 equivalent of `trace_studio serve` opening the browser."""
    if not VIEWER_EXE.exists():
        print(f"[view]  viewer.exe not built ({VIEWER_EXE}) — "
              f"`nix develop --command make -C tools/trace_studio_v3/viewer`")
        return
    print(f"[view]  launching viewer: {VIEWER_EXE.name} {view_path.name}")
    subprocess.Popen([str(VIEWER_EXE), wslpath_w(view_path)], start_new_session=True)


def caprange_of(scenario: str) -> tuple[int, int]:
    """(start, count) of the scenario's {caprange} — the full-extent the sub-window
    slices from. A scenario without one can't drive a v3 full-extent window."""
    import json
    trace = SCEN_DIR / scenario / "trace.jsonl"
    for raw in trace.read_text().splitlines():
        line = raw.strip()
        if line.startswith("{") and '"caprange"' in line:
            try:
                d = json.loads(line)
                if "caprange" in d:
                    return int(d["caprange"][0]), int(d["caprange"][1])
            except (ValueError, KeyError, IndexError):
                pass
    raise SystemExit(f"{scenario!r} has no {{caprange}} — the v3 full-extent window needs one")


def port_stale(entry: Path) -> bool:
    """A rebuilt port exe (mtime newer than the cached container) means the cached
    PORT pixels predate the fix ⇒ stale, re-drive. Retail never goes stale this way."""
    if not PORT_EXE.exists():
        return False                       # can't tell — trust the cache
    return PORT_EXE.stat().st_mtime > (entry / "v3cap.bin").stat().st_mtime


def drive_retail(scenario: str, anchor: str, cr_start: int, cr_n: int,
                 max_frames: int | None, verify: bool, state: bool) -> int:
    """Drive + cache the retail full-extent. Returns the driver exit code (does NOT
    abort): a non-zero code is often a non-bit-exact replay VERIFY (e.g. an unported
    overlay like the pause menu) AFTER the container was already cached — the caller
    re-checks the cache and only fails on a true miss (no extent produced)."""
    cmd = [sys.executable, str(HOUSE_DRV), "--scenario", scenario, "--anchor", anchor,
           "--offset", str(cr_start), "--count", str(cr_n)]
    if max_frames is not None:
        cmd += ["--max-frames", str(max_frames)]
    if not verify:
        cmd += ["--no-verify"]
    if state:
        cmd += ["--state"]
    print(f"[loop]  RETAIL miss → driving full caprange extent [{cr_start},{cr_start + cr_n}) "
          f"(the load-stretch, paid ONCE): {' '.join(cmd[2:])}")
    return subprocess.run(cmd, cwd=ROOT).returncode


def drive_port(scenario: str, anchor: str, verify: bool, state: bool) -> int:
    """Drive + cache the port full-extent. Returns the driver exit code (see
    drive_retail — the caller re-checks the cache before treating it as fatal)."""
    cmd = [sys.executable, str(PORT_DRV), scenario, "--anchor", anchor]
    if not verify:
        cmd += ["--no-verify"]
    if state:
        cmd += ["--state"]
    print(f"[loop]  PORT drive (fast — no load-stretch): {' '.join(cmd[2:])}")
    return subprocess.run(cmd, cwd=ROOT).returncode


def ensure_side(side: str, scenario: str, anchor: str, req_off: int, req_n: int,
                cr_start: int, cr_n: int, trace_path: Path, *, force: bool,
                reuse: bool, max_frames: int | None, verify: bool,
                state: bool) -> tuple[Path, str]:
    """Return (full_extent_entry_dir, action) for `side`, driving only on a real miss
    (or, for the port, a rebuild; or, with --state, a cache that lacks engine state).
    action describes what happened, for the report."""
    entry = None if force else v3cache.find_extent(scenario, side, anchor, req_off, req_n, trace_path)
    action = "slice-cached"
    if entry and side == "port" and not reuse and port_stale(entry):
        print(f"[loop]  PORT cache is STALE (build/openrecet.exe rebuilt since capture) → re-drive")
        entry = None
    # --state but the cached extent was captured without it ⇒ re-drive WITH state
    # (state isn't in the cache key — it doesn't change pixels — so a same-key entry
    # may legitimately lack call_trace.jsonl; force the drive to add it).
    if entry is not None and state and not (entry / "call_trace.jsonl").exists():
        print(f"[loop]  {side.upper()} cache has no engine state (call_trace.jsonl) but "
              f"--state requested → re-drive with state")
        entry = None
    if entry is None:
        if side == "retail":
            rc = drive_retail(scenario, anchor, cr_start, cr_n, max_frames, verify, state)
        else:
            rc = drive_port(scenario, anchor, verify, state)
        entry = v3cache.find_extent(scenario, side, anchor, req_off, req_n, trace_path)
        if entry is None:
            raise SystemExit(f"[fail] {side} drove (exit {rc}) but produced no extent containing "
                             f"[{req_off},{req_off + req_n}) — check the driver output above")
        if rc != 0:
            # The driver cached the extent but exited non-zero — almost always a
            # non-bit-exact replay VERIFY (an unported/divergent overlay, e.g. the
            # pause menu), NOT a capture failure. Surface it, then proceed: the whole
            # point of the run is to INSPECT that divergence in the viewer.
            print(f"[warn] {side} driver exited {rc} but the extent IS cached → proceeding. "
                  f"Usually a non-bit-exact replay verify (a finding, not a drive failure); "
                  f"re-run with --no-verify to skip the check.")
        action = "drove"
    return entry, action


def materialize_window(entry: Path, req_off: int, req_n: int, out: Path,
                       verify: bool) -> tuple[Path, str]:
    """Get a standalone container for exactly [req_off, req_off+req_n). If the cached
    extent IS that window, use it directly (already verified at capture); else slice
    + (optionally) re-verify bit-exact. Returns (window_dir, note)."""
    meta = v3cache.load_meta(entry)
    if (meta.eff_arm_offset, meta.eff_arm_count) == (req_off, req_n):
        return entry, "full-extent (no slice)"
    _out, npass, nfail = orv3_slice.slice_entry(entry, req_off, req_n, out=out,
                                                verify=verify, quiet=True)
    if not verify:
        return out, f"sliced (unverified)"
    if nfail:
        raise SystemExit(f"[fail] slice {req_off}:{req_n} from {entry} NOT bit-exact "
                         f"({npass} ok / {nfail} bad) — the cached container is corrupt")
    return out, f"sliced, {npass}/{req_n} bit-exact"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="v3 capture-once/slice-many window loop: slice a cached full-extent "
                    "(zero re-drive) when the requested window is in-extent, drive only "
                    "what's missing or stale, then JOIN port↔retail by identity.")
    ap.add_argument("scenario", help="scenario with a {caprange} full-extent")
    ap.add_argument("--window", metavar="OFFSET:COUNT", required=True,
                    help="requested sub-window in anchor-relative offset space (e.g. 130:20). "
                         "Must lie within the scenario's {caprange}.")
    ap.add_argument("--anchor", default="HOUSE_FREEROAM",
                    help="join anchor (default %(default)s; the caprange base — "
                         "HOUSE_FREEROAM == LOADING_END for the house scenario).")
    ap.add_argument("--force-retail", action="store_true", help="re-drive retail even if cached.")
    ap.add_argument("--force-port", action="store_true", help="re-drive the port even if cached/fresh.")
    ap.add_argument("--reuse-port", action="store_true",
                    help="slice the cached port even if the exe was rebuilt (skip the freshness check).")
    ap.add_argument("--state", action="store_true",
                    help="ALSO capture+show the once-per-frame engine state (the viewer's "
                         "game-state panel: rng / player+companion px/py/anim / menu / dialogue). "
                         "Drives BOTH sides with --state; a cached extent without state is re-driven "
                         "to add it (state isn't in the cache key). Negligible capture cost — the "
                         "probes are window-gated. Off by default (keeps the d3d capture lean).")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the per-frame bit-exact replay checks (drive + slice).")
    ap.add_argument("--max-frames", type=int, default=None,
                    help="engine frame budget for a retail drive (must exceed anchor+offset+count).")
    ap.add_argument("--view", action="store_true",
                    help="after the join, emit view.json (the native viewer's manifest) into the window dir.")
    ap.add_argument("--launch", action="store_true",
                    help="emit view.json AND open the native viewer on it (implies --view). "
                         "The one-command loop: drive/slice/sync → view → viewer.")
    args = ap.parse_args()

    try:
        req_off, req_n = (int(x) for x in args.window.split(":"))
    except ValueError:
        raise SystemExit(f"--window wants OFFSET:COUNT (got {args.window!r})")
    if req_n <= 0:
        raise SystemExit("--window COUNT must be > 0")

    trace_path = SCEN_DIR / args.scenario / "trace.jsonl"
    if not trace_path.exists():
        raise SystemExit(f"no scenario trace: {trace_path}")
    cr_start, cr_n = caprange_of(args.scenario)
    if not (cr_start <= req_off and req_off + req_n <= cr_start + cr_n):
        raise SystemExit(f"window [{req_off},{req_off + req_n}) is outside the scenario's full-extent "
                         f"(caprange [{cr_start},{cr_start + cr_n})) — widen the caprange to capture more.")

    verify = not args.no_verify
    print(f"=== v3 window loop: {args.scenario}  {args.anchor}+{req_off}:{req_n}  "
          f"(full-extent caprange [{cr_start},{cr_start + cr_n})) ===")

    # Per side: cache-hit ⇒ slice; miss/stale ⇒ drive (only what's needed), then slice.
    retail_entry, r_act = ensure_side(
        "retail", args.scenario, args.anchor, req_off, req_n, cr_start, cr_n, trace_path,
        force=args.force_retail, reuse=False, max_frames=args.max_frames, verify=verify,
        state=args.state)
    port_entry, p_act = ensure_side(
        "port", args.scenario, args.anchor, req_off, req_n, cr_start, cr_n, trace_path,
        force=args.force_port, reuse=args.reuse_port, max_frames=args.max_frames, verify=verify,
        state=args.state)

    win_dir = WIN_ROOT / args.scenario / f"win-{req_off}-{req_n}"
    retail_win, r_note = materialize_window(retail_entry, req_off, req_n, win_dir / "retail", verify)
    port_win, p_note = materialize_window(port_entry, req_off, req_n, win_dir / "port", verify)

    print(f"\n--- materialized window {args.anchor}+{req_off}:{req_n} ---")
    print(f"  retail : {r_act:12s} → {r_note}")
    print(f"  port   : {p_act:12s} → {p_note}")

    # Parse each window side's container ONCE here, then thread the SAME LoadedSide
    # through sync AND view (view re-calls sync internally) — the parse-once handoff, so
    # the 91+58 MB containers parse once per loop instead of ~3× per side per phase.
    pside = v3cache.load_side(port_win)
    rside = v3cache.load_side(retail_win)

    # JOIN the two sub-window slices by stored identity → pairs.json.
    print(f"\n--- sync-by-identity ---")
    win_dir.mkdir(parents=True, exist_ok=True)
    res = orv3_sync.sync_entries(pside, rside, write_pairs=True,
                                 pairs_path=win_dir / "pairs.json")

    # one-command tail: emit the native viewer's view.json (+ optionally open it).
    if args.view or args.launch:
        view_path = win_dir / "view.json"
        vm = orv3_view.write_view_json(pside, rside, view_path)
        nd = sum(1 for f in vm["frames"] if f.get("draw_verdict") and f["draw_verdict"] != "ALIGNED")
        print(f"\n--- view ---")
        print(f"  wrote {view_path}  ({vm['count']} columns, {vm['n_gaps']} gaps, "
              f"{nd} draw-divergent)")
        # point the "OpenRecet Trace Studio" shortcut at this freshly-built window.
        write_current_pointer(view_path, f"{args.scenario}  {args.anchor}+{args.window}")
        if args.launch:
            launch_viewer(view_path)

    drove = [s for s, a in (("retail", r_act), ("port", p_act)) if a == "drove"]
    saved = "nothing re-driven (pure cache slice)" if not drove else f"drove only: {', '.join(drove)}"
    print(f"\n=== LOOP DONE — {res['verdict']} · {saved} ===")
    print(f"    window dir: {win_dir}  (port/ retail/ pairs.json"
          f"{', view.json' if (args.view or args.launch) else ''})")
    return 0 if res["verdict"] == "ALIGNED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
