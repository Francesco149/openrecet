#!/usr/bin/env python3
"""Trace Studio v3 — RETAIL HOUSE-drive full-extent capture (P1 tail FINAL).

Drive the real retail exe to the HOUSE (save-virtualized + input-segtrace replay)
and arm the capture proxy at HOUSE_FREEROAM+OFFSET for a real post-load 3D
free-roam present-window. This is the retail full-extent deliverable that combines
two already-proven paths into one: the PORT's 3D multi-frame bit-exact window
(`da5f601`, 48/48) and R2's RETAIL single-frame capture (`fe3722a`, 0 px).

Why a separate driver from retail_capture.py: that one is a bare Frida script with
a FIXED or export-armed present-window — enough for the deterministic-early TITLE.
The HOUSE drive needs the FULL v2 capture machinery — save virtualization, the
anchor-segmented input segtrace, turbo, resolution pinning, RNG pins — so it goes
through frida_capture.run_capture (the SAME path scenario-test's retail target
uses) with the new `v3_arm` hook. The agent arms the proxy IN-PROCESS the first
time the anchor fires (zero IPC latency ⇒ armed well before the window starts).

The proxy is staged with `armwait=1`: through the long pre-anchor load g_capframe
is unset, so the GetBackBuffer MULTI keep-trigger would otherwise mis-keep a stray
readback (retail's own, or the agent's v2 caprange captures) as a bogus load
frame. armwait suppresses that trigger entirely ⇒ the proxy keeps NOTHING until
the in-process arm sets the present-window; only the armed window survives. (The
segtrace's own {caprange} captures still run as harmless v2 readbacks — armwait
makes them non-keeping — so they need no stripping.)

Mechanism:
  1. load the scenario (a segtrace: {savefile} + walk-to-house inputs + pins);
  2. resolve {savefile} → save_ref (sandboxed; never touches the real save);
  3. stage tools/trace_studio_v3/proxy/d3d8.dll + v3proxy.cfg(armwait=1) next to
     the unpacked retail exe;
  4. frida_capture.run_capture(... v3_arm={anchor,offset,count} ...) spawns retail
     under Frida (turbo, hidden, silent, resolution-pinned), replays the segtrace,
     and the agent arms OrV3ArmWindowAt(anchor_frame+offset, count) when the
     anchor fires; the proxy keeps that present-window into %LOCALAPPDATA%\\
     openrecet\\v3\\{v3cap.bin, v3ref_NNN.raw} + finalizes (EOF) after the last;
  5. replay.exe renders each kept frame index + byte-compares to its reference.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/house_capture.py \
      [--scenario house-loaded-display-pinned] [--anchor HOUSE_FREEROAM] \
      [--offset 120] [--count 48] [--max-frames N] [--no-verify] [--keep-proxy]
"""
import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

# Reuse the retail-side helpers verbatim (replay verify, %LOCALAPPDATA% resolver,
# port resolution, wslpath). retail_capture imports frida at module top — fine
# under the nix devshell this driver runs in.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import retail_capture as rc                                            # noqa: E402
import v3cache                                                         # noqa: E402

ROOT      = Path(__file__).resolve().parent.parent.parent
PROXY_SRC = ROOT / "tools" / "trace_studio_v3" / "proxy" / "d3d8.dll"
PROXY_DLL = ROOT / "vendor" / "unpacked" / "d3d8.dll"
SCEN_DIR  = ROOT / "tests" / "scenarios"


def load_scenario(name: str):
    """Load tools/scenario-test.py's Scenario for `name` (the hyphenated filename
    forces importlib; the module's heavy imports — frida_capture — are lazy)."""
    sys.path.insert(0, str(ROOT / "tools"))
    spec = importlib.util.spec_from_file_location(
        "scenario_test", ROOT / "tools" / "scenario-test.py")
    mod = importlib.util.module_from_spec(spec)
    # Register before exec: @dataclass's KW_ONLY probe resolves cls.__module__ via
    # sys.modules, which is None (→ AttributeError) for an unregistered module.
    sys.modules["scenario_test"] = mod
    spec.loader.exec_module(mod)
    return mod.Scenario.load(SCEN_DIR / name)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="v3 retail HOUSE-drive full-extent capture + replay verify.")
    ap.add_argument("--scenario", default="house-loaded-display-pinned",
                    help="segtrace scenario that loads a save + walks to the house "
                         "(default %(default)s).")
    ap.add_argument("--anchor", default="HOUSE_FREEROAM",
                    help="anchor to arm the present-window relative to. HOUSE_FREEROAM "
                         "fires ONCE, on the same frame as the final LOADING_END (proven), "
                         "so it robustly targets the post-load free-roam window despite "
                         "the nondeterministic load-stretch (default %(default)s).")
    ap.add_argument("--offset", type=int, default=120,
                    help="frames after the anchor to start the window (default %(default)s; "
                         "HOUSE_FREEROAM+120 = the port's LOADING_END+120..168 window).")
    ap.add_argument("--count", type=int, default=48,
                    help="present-window length in frames (default %(default)s; matches the "
                         "port's proven 48-frame HOUSE window).")
    ap.add_argument("--max-frames", type=int, default=None,
                    help="override the scenario's engine-side frame budget. Retail load-"
                         "stretches, so the budget must exceed the anchor frame + offset + "
                         "count. Default: the scenario's max_frames.")
    ap.add_argument("--duration-ms", type=int, default=None,
                    help="override the scenario's wall-clock ceiling (ms).")
    ap.add_argument("--no-verify", action="store_true",
                    help="capture only; skip the per-frame bit-exact replay check.")
    ap.add_argument("--keep-proxy", action="store_true",
                    help="leave d3d8.dll + v3proxy.cfg staged in vendor/unpacked/ "
                         "(default: unstage on exit — a staged proxy would load into "
                         "v2 scenario-test --target retail runs).")
    ap.add_argument("--frida-remote", default=rc.DEFAULT_REMOTE)
    ap.add_argument("--run-dir", type=Path, default=None,
                    help="where run_capture writes its v2 artifacts (anchors.jsonl, "
                         "agent.log, frames). Default: runs/studio-v3-experiments/...")
    args = ap.parse_args()

    if not PROXY_SRC.exists():
        raise SystemExit(f"proxy not built: {PROXY_SRC} — `nix develop --command make` in proxy/")
    if not args.no_verify and not rc.REPLAY_EXE.exists():
        raise SystemExit(f"replayer not built: {rc.REPLAY_EXE} — `nix develop --command make` in replay/")

    # Late imports (need the nix devshell): the heavy capture/orchestration deps.
    sys.path.insert(0, str(ROOT / "tools"))
    import frida_capture                                               # noqa: E402
    import trace_save                                                  # noqa: E402

    scen = load_scenario(args.scenario)
    if not scen.is_segtrace:
        raise SystemExit(f"{args.scenario!r} is not a segtrace — the house drive needs the "
                         "anchor-segmented input trace ({wait}/{caprange} ops) to reach the house")
    if args.max_frames is not None:
        scen.max_frames = int(args.max_frames)
    if args.duration_ms is not None:
        scen.duration_ceiling_ms = int(args.duration_ms)

    trace_path = scen.path / "trace.jsonl"
    # Resolve {savefile} → sandboxed save_ref (the replay never touches the real save).
    save_ref = trace_save.resolve_save(trace_path)

    # Stage proxy + armwait cfg. NO capframe: the agent arms the present-window
    # live at the anchor (config.v3_arm → OrV3ArmWindowAt). armwait makes the proxy
    # idle (keep nothing) until then.
    shutil.copy2(PROXY_SRC, PROXY_DLL)
    cfg_path = PROXY_DLL.parent / "v3proxy.cfg"
    cfg_path.write_text("armwait=1\n")
    print(f"[stage] {PROXY_DLL.name} + v3proxy.cfg(armwait=1) → ARM "
          f"{args.anchor}+{args.offset}:{args.count} (agent in-process)")

    # Clear stale capture in the proxy's output dir.
    v3 = rc.localappdata_v3()
    v3.mkdir(parents=True, exist_ok=True)
    cap = v3 / "v3cap.bin"
    log = v3 / "v3proxy.log"
    for f in [cap, log, *v3.glob("v3ref_*.raw"), v3 / "v3replay_chk.raw"]:
        f.unlink(missing_ok=True)

    run_dir = args.run_dir or (ROOT / "runs" / "studio-v3-experiments" /
                               f"house-drive-{args.scenario}")
    run_dir.mkdir(parents=True, exist_ok=True)

    res_w, res_h = rc.openrecet_screen_dims()
    v3_arm = {"anchor": args.anchor, "offset": int(args.offset), "count": int(args.count)}
    print(f"[run]   retail drive: save_ref={save_ref!r} force_res={res_w}x{res_h} "
          f"max_frames={scen.max_frames} → run_capture(v3_arm={v3_arm})")
    try:
        frida_capture.run_capture(
            scen, run_dir,
            remote=args.frida_remote,
            input_segtrace_path=trace_path,
            save_ref=save_ref,
            hide_window=True, turbo=True, silent_audio=True,
            force_resolution=(res_w, res_h),
            rng_seed=scen.rng_seed,
            suppress_loads=scen.suppress_loads,
            v3_arm=v3_arm,
        )
    finally:
        # Unstage the proxy so it can't load into a later v2 scenario-test retail run.
        if not args.keep_proxy:
            PROXY_DLL.unlink(missing_ok=True)
            cfg_path.unlink(missing_ok=True)
            print(f"[stage] unstaged {PROXY_DLL.name}")

    # ── pull + report + verify ──
    if not cap.exists() or not log.exists():
        raise SystemExit(f"[fail] no capture produced at {v3} — the proxy never opened a "
                         f"container. Check {run_dir}/agent.log (did retail spawn? did d3d8.dll load?).")
    log_txt = log.read_text(errors="replace")
    keeps = [ln for ln in log_txt.splitlines() if ln.startswith("KEEP")]
    n = len(keeps)
    cap_mb = cap.stat().st_size / 1048576
    refs = sorted(v3.glob("v3ref_*.raw"))
    armed = [ln for ln in log_txt.splitlines() if ln.startswith("ARM ")]
    final = [ln for ln in log_txt.splitlines() if "FINALIZE" in ln]
    print(f"\n[cap]   {n} frame(s) kept · container {cap_mb:.1f} MB · {len(refs)} references")
    if armed:
        print(f"[cap]   {armed[-1].strip()}")
    if final:
        print(f"[cap]   {final[-1].strip()}")
    if refs and n:
        ref_mb = refs[0].stat().st_size / 1048576
        print(f"[cap]   dedup: {n} frames in {cap_mb:.1f} MB; {n}× raw pixels alone would be "
              f"{n*ref_mb:.0f} MB (resources stored once, frames ≈ free)")
    if n == 0:
        # The most common cause: the anchor never fired (budget too low) or the
        # window started past the run end.
        anchors = run_dir / "anchors.jsonl"
        hint = ""
        if anchors.exists():
            fired = [ln.strip() for ln in anchors.read_text(errors="replace").splitlines()
                     if args.anchor in ln]
            hint = (f" anchors.jsonl: {args.anchor} fired {len(fired)}× "
                    f"({fired[-1] if fired else 'NEVER'})")
        print("--- v3proxy.log tail ---")
        for ln in log_txt.splitlines()[-12:]:
            print("  " + ln)
        raise SystemExit(f"[fail] proxy kept 0 frames — anchor didn't arm in time or the "
                         f"window ran past the budget. Raise --max-frames / lower --offset.{hint}")

    rc_code = 0
    if args.no_verify:
        print("[skip] --no-verify: not replaying")
    else:
        rc_code = rc.replay_verify(v3, n)

    # Cache the capture under a content key + its STORED identity (P2 sync-by-
    # identity + slice cache): retail kept frame k is (anchor#occ, offset+k). The
    # port side (port_capture.py) caches the matching window under the same key;
    # orv3_sync.py JOINs them by identity — load-stretch-immune (E3).
    arm = {"anchor": args.anchor, "offset": args.offset, "count": args.count}
    dest, ident = v3cache.preserve_live(args.scenario, "retail", args.anchor,
                                        args.offset, trace_path, arm, src=v3)
    print(f"[cache] stored retail → {dest}  (identity {ident.anchor}#{ident.anchor_occ}, "
          f"offsets {ident.offset0}..{ident.offset0 + ident.count - 1}, "
          f"present {ident.present_first}..{ident.present_first + ident.count - 1})")
    return rc_code


if __name__ == "__main__":
    raise SystemExit(main())
