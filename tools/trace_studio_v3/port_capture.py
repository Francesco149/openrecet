#!/usr/bin/env python3
"""Trace Studio v3 — port full-extent capture + replay verification (P1 tail).

The companion to r2_retail_probe.py (retail side): drive the PORT through a
scenario's caprange WINDOW with the shared proxy d3d8.dll staged, capturing the
whole window into ONE multi-frame container, then replay EVERY kept frame and
assert each is bit-exact vs its proxy reference.

What it proves (and measures): multi-frame windowed capture + content-hash
resource dedup. A resource bound in every frame of the window is stored ONCE, so
the container stays ≈ one frame's resources + per-frame call deltas regardless of
window length — and each frame still re-renders bit-exactly from its own state
preamble. This is the "retail captured once, sliced forever" storage model on the
cheap-to-re-drive PORT side.

Mechanism (no Frida — the port is a local Windows exe):
  1. stage tools/trace_studio_v3/proxy/d3d8.dll next to build/openrecet.exe (the
     app-dir DLL search loads it before System32);
  2. run `scenario-test.py <scenario> --target openrecet`, which drives the port
     through the load + the scenario's {caprange} window with save-virtualization
     + phase/RNG pins. The port reads back each window frame (capture_backbuffer →
     GetBackBuffer), which the proxy uses as its per-frame keep trigger (MULTI
     mode: no v3proxy.cfg ⇒ capframe unset);
  3. the proxy writes %LOCALAPPDATA%\\openrecet\\v3\\{v3cap.bin, v3ref_NNN.raw};
  4. replay.exe renders each kept frame index and byte-compares to v3ref_NNN.raw.

The proxy is UNSTAGED on exit (a staged proxy adds per-call overhead to every
port run, incl. v2 scenario-test) unless --keep-proxy.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/port_capture.py \
      [scenario] [--no-verify] [--keep-proxy]
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT       = Path(__file__).resolve().parent.parent.parent
PROXY_DLL  = ROOT / "tools" / "trace_studio_v3" / "proxy" / "d3d8.dll"
REPLAY_EXE = ROOT / "tools" / "trace_studio_v3" / "replay" / "replay.exe"
PORT_EXE   = ROOT / "build" / "openrecet.exe"
STAGED_DLL = ROOT / "build" / "d3d8.dll"
SCENARIO_TEST = ROOT / "tools" / "scenario-test.py"
DEFAULT_SCENARIO = "house-loaded-display-pinned"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import v3cache                                                         # noqa: E402
import v3verify                                                        # noqa: E402


def caprange_of(scenario: str) -> tuple[int, int] | None:
    """(start, count) from the scenario's {caprange:[start,count]} line, or None.
    The caprange start IS the window's offset-since-anchor (offset0) for the stored
    identity — the same offset the retail arm uses ⇒ a matching join key."""
    import json
    trace = ROOT / "tests" / "scenarios" / scenario / "trace.jsonl"
    for raw in trace.read_text().splitlines():
        line = raw.strip()
        if line.startswith("{") and '"caprange"' in line:
            try:
                d = json.loads(line)
                if "caprange" in d:
                    return int(d["caprange"][0]), int(d["caprange"][1])
            except (ValueError, KeyError, IndexError):
                pass
    return None


def localappdata_v3() -> Path:
    """%LOCALAPPDATA%\\openrecet\\v3 as a WSL path (where the proxy writes)."""
    out = subprocess.run(["cmd.exe", "/c", "echo %LOCALAPPDATA%"],
                         capture_output=True, text=True, cwd="/mnt/c").stdout.strip()
    wsl = subprocess.run(["wslpath", "-u", out], capture_output=True, text=True,
                         check=True).stdout.strip()
    return Path(wsl) / "openrecet" / "v3"


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="v3 port full-extent capture + replay verify.")
    ap.add_argument("scenario", nargs="?", default=DEFAULT_SCENARIO,
                    help=f"scenario with a {{caprange}} window (default {DEFAULT_SCENARIO})")
    ap.add_argument("--no-verify", action="store_true",
                    help="capture only; skip the per-frame bit-exact replay check.")
    ap.add_argument("--keep-proxy", action="store_true",
                    help="leave build/d3d8.dll staged (default: unstage on exit).")
    ap.add_argument("--window", metavar="START:COUNT", default=None,
                    help="capture a present-count WINDOW [START, START+COUNT) via "
                         "v3proxy.cfg instead of the GetBackBuffer MULTI trigger — "
                         "the SAME present-window keep mode retail uses. Proves WINDOW "
                         "mode bit-exact on the local (fast) port. e.g. --window 944:44")
    ap.add_argument("--anchor", default="HOUSE_FREEROAM",
                    help="canonical join anchor for the stored identity — must match the "
                         "retail side so orv3_sync.py pairs them (default %(default)s; the "
                         "caprange's base anchor, e.g. HOUSE_FREEROAM == LOADING_END here).")
    ap.add_argument("--no-cache", action="store_true",
                    help="skip caching the capture + its stored identity (MULTI mode only).")
    ap.add_argument("--raw-refs", action="store_true",
                    help="write a full 3 MB v3ref_NNN.raw per kept frame (legacy). Default is "
                         "refhash=1: a fnv1a-64 line per frame in v3refs.txt + a raw every "
                         "500th — the only shape that scales to thousands-of-frames windows.")
    args = ap.parse_args()

    win_start = win_count = None
    if args.window:
        try:
            s, c = args.window.split(":")
            win_start, win_count = int(s), int(c)
        except ValueError:
            raise SystemExit(f"--window wants START:COUNT (got {args.window!r})")

    if not PROXY_DLL.exists():
        raise SystemExit(f"proxy not built: {PROXY_DLL} — `nix develop --command make` in proxy/")
    if not PORT_EXE.exists():
        raise SystemExit(f"port not built: {PORT_EXE} — `nix develop --command make -C src`")
    if not args.no_verify and not REPLAY_EXE.exists():
        raise SystemExit(f"replayer not built: {REPLAY_EXE} — `nix develop --command make` in replay/")

    v3 = localappdata_v3()
    v3.mkdir(parents=True, exist_ok=True)
    cap = v3 / "v3cap.bin"
    log = v3 / "v3proxy.log"

    # stage proxy + clear stale capture. A v3proxy.cfg's capframe selects WINDOW
    # mode (present-count keep, like retail); without one the port runs the
    # GetBackBuffer MULTI trigger. refhash (default) makes references fnv1a-64
    # lines in v3refs.txt instead of a 3 MB raw per frame.
    shutil.copy2(PROXY_DLL, STAGED_DLL)
    cfg = STAGED_DLL.parent / "v3proxy.cfg"
    cfg_lines = [] if args.raw_refs else ["refhash=1", "refraw_every=500"]
    if win_start is not None:
        cfg_lines += [f"capframe={win_start}", f"capcount={win_count}"]
        mode = f"WINDOW [{win_start},{win_start + win_count})"
    else:
        mode = "MULTI (GetBackBuffer)"
    if cfg_lines:
        cfg.write_text("".join(ln + "\n" for ln in cfg_lines))
    else:
        cfg.unlink(missing_ok=True)
    for f in [cap, log, *v3.glob("v3ref_*.raw"), v3 / "v3refs.txt", v3 / "v3replay_chk.raw"]:
        f.unlink(missing_ok=True)
    print(f"[stage] {PROXY_DLL.name} → {STAGED_DLL}  ({mode}, "
          f"refs={'raw' if args.raw_refs else 'hash'}, out={v3})")

    try:
        print(f"[run]   scenario-test {args.scenario} --target openrecet …")
        # ignore scenario-test's pass/fail (golden compare is irrelevant to v3 —
        # we only need the port to run the caprange window so the proxy captures).
        subprocess.run([sys.executable, str(SCENARIO_TEST), args.scenario,
                        "--target", "openrecet"], cwd=ROOT)
    finally:
        if not args.keep_proxy:
            STAGED_DLL.unlink(missing_ok=True)
            cfg.unlink(missing_ok=True)
            print(f"[stage] unstaged {STAGED_DLL.name}")

    if not cap.exists() or not log.exists():
        raise SystemExit(f"[fail] no capture produced at {v3} — check the run above")

    keeps = [ln for ln in log.read_text(errors="replace").splitlines() if ln.startswith("KEEP")]
    n = len(keeps)
    cap_mb = cap.stat().st_size / 1048576
    refs = sorted(v3.glob("v3ref_*.raw"))
    print(f"\n[cap]   {n} frame(s) kept · container {cap_mb:.1f} MB · {len(refs)} references")
    if refs:
        ref_mb = refs[0].stat().st_size / 1048576
        print(f"[cap]   dedup: {n} frames in {cap_mb:.1f} MB; {n}× raw pixels alone "
              f"would be {n*ref_mb:.0f} MB (resources stored once, frames ≈ free)")
    if n == 0:
        raise SystemExit("[fail] proxy loaded but kept 0 frames — does the scenario have a {caprange}?")

    # Cache the capture + its STORED identity (P2 sync-by-identity + slice cache):
    # MULTI (caprange) mode only — the window is anchor-relative (offset0 = the
    # caprange start), so kept frame k is (anchor#occ, offset0+k), the join key the
    # retail side shares. (--window is a raw present-count test mode with no anchor
    # identity ⇒ not cached.) orv3_sync.py pairs this with the retail entry.
    if win_start is None and not args.no_cache:
        cr = caprange_of(args.scenario)
        if cr is None:
            print(f"[cache] no {{caprange}} in {args.scenario} — skipping identity cache")
        else:
            offset0, cr_count = cr
            arm = {"anchor": args.anchor, "offset": offset0, "count": cr_count}
            trace = ROOT / "tests" / "scenarios" / args.scenario / "trace.jsonl"
            dest, ident = v3cache.preserve_live(args.scenario, "port", args.anchor,
                                                offset0, trace, arm)
            print(f"[cache] stored port → {dest}  (identity {ident.anchor}#{ident.anchor_occ}, "
                  f"offsets {ident.offset0}..{ident.offset0 + ident.count - 1}, "
                  f"present {ident.present_first}..{ident.present_first + ident.count - 1})")

    if args.no_verify:
        print("[skip] --no-verify: not replaying")
        return 0

    return v3verify.verify_dir(v3, n, label=f"{args.scenario} port capture")


if __name__ == "__main__":
    raise SystemExit(main())
