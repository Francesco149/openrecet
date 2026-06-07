"""capture.py — the capture orchestrator (the decomposed cmd_capture god-function).

Ties together model + drive + transport + analysis + trace_build into one flow:
resolve trace → build/reuse the working trace → drive port(+retail) concurrently →
convert/renumber/diff/encode/state/verdict → build the v2 segmented session.json
(v1 superset + schema_version:2 + timeline with loads as zero-frame seams) → write.

Behaviour matches the v1 monolith; the only additions are EngineCaps-gated D1
load-suppression and the v2 timeline.
"""
from __future__ import annotations

import datetime as dt
import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from .analysis import pixeldiff, state as state_mod, verdict as verdict_mod
from .drive import caps as caps_mod, runner
from .model import ops, session as sess_mod, timeline as tl_mod
from .paths import DEFAULT_REMOTE, SESS_ROOT
from .transport import convert, encode
from . import trace_build


def _log(msg: str) -> None:
    print(f"trace_studio: {msg}")


@dataclass
class CaptureConfig:
    trace: str
    session: str | None = None
    target: str = "both"                 # both | openrecet
    call_trace: bool = False
    amp: float = 6.0
    caprange: str | None = None
    port_max_frames: int = 4000
    retail_max_frames: int = 22000
    remote: str = DEFAULT_REMOTE
    prune_frames: bool = False
    reset_trace: bool = False
    only: str = "both"                   # both | port
    anchors: bool | None = None          # None=auto (anchor iff the recording carries
                                         # anchors); True/False force on/off
    suppress_loads: bool = True          # D1: collapse loads to zero-frame seams
    capture_local: bool = True           # D2: local-disk staging (degrades if unsupported)
    capstride: int = 1                   # D3: capture every Nth frame (OVERVIEW); 1 = dense


def run_capture(cfg: CaptureConfig) -> int:
    from .paths import ROOT
    src = ops.resolve_trace(cfg.trace, ROOT)
    hdr = ops.raw_header(src)

    sess = cfg.session or (
        (src.name[:-len(".raw.jsonl")] if src.name.endswith(".raw.jsonl")
         else src.parent.name)
        + f"-{dt.datetime.now():%Y%m%d-%H%M%S}")
    sess_dir = SESS_ROOT / sess
    port_dir = sess_dir / "port"
    retail_dir = sess_dir / "retail"
    sess_dir.mkdir(parents=True, exist_ok=True)
    want_retail = cfg.target == "both"
    working = sess_dir / "edit.trace.jsonl"

    # ── anchoring (cfg.anchors None = AUTO) ──────────────────────────────────
    # A raw recording that carries {anchor} rows anchor-segments by default: FLAT
    # boot-syncing a load-bearing recording lands the {caprange} in the PRE-LOAD
    # region (e.g. a Continue trace's window stops at the save-picker instead of
    # reaching the loaded scene). The recorder logged anchors precisely so the
    # window can target the loaded content; honour them. Explicit --anchors /
    # --no-anchors override.
    rec_has_anchors = bool(hdr) and ops.raw_has_anchors(src)
    anchored = cfg.anchors if cfg.anchors is not None else rec_has_anchors
    if cfg.anchors is None and rec_has_anchors:
        _log("auto-anchor: recording carries anchors → anchor-segmented distil "
             "(FLAT would land the window in the pre-load region; --no-anchors to force)")

    # ── working trace: reuse (carries applied pins) unless --reset-trace ──────
    reuse = working.exists() and not cfg.reset_trace
    if reuse and anchored and not any("wait" in o for o in ops.load_ops(working)):
        # A stale FLAT working trace (no {wait}) for an anchored recording has its
        # window in the wrong place — rebuild rather than silently re-capture the
        # pre-load. (Re-apply pins if any were set.)
        _log("re-capture: working trace is FLAT but the recording carries anchors — "
             "rebuilding anchor-segmented (re-apply pins if needed; --no-anchors to keep FLAT)")
        reuse = False
    if reuse:
        _log(f"re-capture: reusing working trace {working.name} (with applied pins)")
    else:
        if cfg.caprange:
            s, c = cfg.caprange.split(",")
            cr0: tuple[int, int] | None = (int(s), int(c))
        else:
            cr0 = ops.extract_caprange(ops.load_ops(src))
            if not cr0 and hdr:
                cr0 = ops.raw_default_window(src, anchored=anchored)
                _log(f"raw recording → {'anchor-segmented' if anchored else 'FLAT (no anchoring, boot-synced)'} "
                     f"distil, auto window caprange={cr0}")
        if not cr0:
            raise SystemExit("trace_studio: no --caprange given and none in the trace")
        trace_build.build_working_trace(src, sess_dir, working, cr0,
                                        bool(cfg.call_trace), anchored=anchored,
                                        capstride=cfg.capstride)

    trace = working
    op_list = ops.load_ops(trace)
    cr = ops.extract_caprange(op_list)
    if not cr:
        raise SystemExit(f"trace_studio: working trace {working} has no caprange")
    # Stride is read back from the WORKING trace (source of truth — a reused trace
    # keeps its own {capstride}; a fresh build just injected --capstride above).
    stride = ops.extract_capstride(op_list)
    ct = ops.extract_calltrace(op_list)
    call_trace = bool(cfg.call_trace and ct)
    if cfg.call_trace and not ct:
        _log("--call-trace requested but the working trace has no {calltrace} op "
             "(rebuild with --reset-trace --call-trace)")

    # ── engine caps: gate D1/D2; degrade gracefully ──────────────────────────
    caps = caps_mod.probe(ROOT)
    _log(caps.summary())
    port_suppress = bool(cfg.suppress_loads and caps.supports_suppress_loads)
    retail_suppress = bool(cfg.suppress_loads and caps.retail_supports_suppress_loads)
    port_capture_local = bool(cfg.capture_local and caps.supports_capture_local)
    if cfg.suppress_loads and not caps.supports_suppress_loads:
        _log("suppress-loads requested but the exe lacks --capture-suppress-loads "
             "(pre-D1 build) — loads will capture frames; seams still reconstruct "
             "from anchors")
    if cfg.capture_local and not caps.supports_capture_local:
        _log("capture-local requested but no local stage root resolved (non-WSL / no "
             "cmd.exe) — capturing straight over the 9p mount")

    # ── per-side recapture (--only port reuses cached retail) ────────────────
    old_manifest: dict = {}
    if (sess_dir / "session.json").exists():
        try:
            old_manifest = json.loads((sess_dir / "session.json").read_text())
        except Exception:                            # noqa: BLE001
            old_manifest = {}
    run_port = cfg.only in ("both", "port")
    run_retail = want_retail and cfg.only in ("both", "retail")
    keep_retail = want_retail and not run_retail     # reuse existing retail outputs

    # Clear only the side(s) being re-run (+ diff, always rebuilt). Preserve marks.
    clear = (["port"] if run_port else []) + (["retail"] if run_retail else []) + ["diff"]
    for sub in clear:
        if (sess_dir / sub).exists():
            shutil.rmtree(sess_dir / sub)
    for stale in ((["port.mp4"] if run_port else []) +
                  (["retail.mp4"] if run_retail else []) +
                  ["diff.mp4", "state.jsonl", "session.json"]):
        (sess_dir / stale).unlink(missing_ok=True)

    _log(f"session {sess}  caprange={cr}  call_trace={call_trace}  "
         f"target={cfg.target}  only={cfg.only}  suppress_loads={port_suppress}  "
         f"capture_local={port_capture_local}")

    # ── drive port + retail (concurrent, per --only) ─────────────────────────
    result = runner.drive_both(
        working_trace=trace, orig_trace=src, ops=op_list,
        port_dir=port_dir, retail_dir=retail_dir, cr=cr, call_trace=call_trace,
        run_port=run_port, run_retail=run_retail,
        port_max_frames=cfg.port_max_frames, retail_max_frames=cfg.retail_max_frames,
        remote=cfg.remote, port_suppress=port_suppress, retail_suppress=retail_suppress,
        port_capture_local=port_capture_local)
    if result.get("retail_skipped"):
        want_retail = False
    if result.get("port_rc", 1) != 0:
        _log("port capture reported a non-zero rc; continuing with what landed")

    # BMP→PNG (idempotent; run-openrecet can miss a large capture).
    for sd in (port_dir, retail_dir):
        n_conv = convert.convert_to_png(sd / "frames")
        if n_conv:
            _log(f"converted {n_conv} BMP→PNG in {sd.name}/frames")

    # ── post: bases, renumber retail ─────────────────────────────────────────
    gp = port_dir / "global.json"
    port_base = None
    if gp.exists():
        port_base = json.loads(gp.read_text()).get("frame_base_abs")

    retail_base = None
    if run_retail and "retail_error" not in result:
        retail_base = convert.renumber_retail(retail_dir)
    elif keep_retail:                                # reuse the cached retail capture
        retail_base = (old_manifest.get("retail") or {}).get("base_abs")
        _log(f"--only port: reusing cached retail (base {retail_base})")

    manifest: dict = {
        "schema": "trace-studio-v2",
        "session": sess,
        "trace": str(working),
        "working_trace": str(working),
        "source_trace": str(src),
        "caprange": list(cr),
        "stride": stride,                # D3: 1 = dense; >1 = coarse OVERVIEW cadence
        "fps": encode.VIDEO_FPS,
        "amp": cfg.amp,
        "target": cfg.target,
        "suppress_loads": port_suppress,
        "port": {"base_abs": port_base},
        "retail": {"base_abs": retail_base, "error": result.get("retail_error")},
        "videos": {},
        "anchors": {},
        "diff": None,
        "verdict": None,
        "call_trace": call_trace,
    }

    have_retail_frames = ((run_retail or keep_retail) and retail_base is not None
                          and any((retail_dir / "frames").glob("frame_*.png")))
    if have_retail_frames:
        manifest["diff"] = pixeldiff.build_diff(
            port_dir, retail_dir, sess_dir / "diff" / "frames", cfg.amp)

    # encode videos: (re)encode the side(s) we ran; keep the cached one otherwise
    if run_port and encode.ffmpeg_encode(port_dir / "frames", sess_dir / "port.mp4"):
        manifest["videos"]["port"] = "port.mp4"
    elif (sess_dir / "port.mp4").exists():
        manifest["videos"]["port"] = "port.mp4"
    if have_retail_frames:
        if run_retail:
            if encode.ffmpeg_encode(retail_dir / "frames", sess_dir / "retail.mp4"):
                manifest["videos"]["retail"] = "retail.mp4"
        elif (sess_dir / "retail.mp4").exists():     # cached retail video
            manifest["videos"]["retail"] = "retail.mp4"
        if encode.ffmpeg_encode(sess_dir / "diff" / "frames", sess_dir / "diff.mp4"):
            manifest["videos"]["diff"] = "diff.mp4"

    # anchor track (retail anchors.jsonl rebased) + copy BOTH raw streams in.
    if (run_retail or keep_retail) and retail_base is not None \
            and (retail_dir / "anchors.jsonl").exists():
        manifest["anchors"]["retail"] = ops.read_anchors(
            retail_dir / "anchors.jsonl", retail_base)
    for side, sd in (("port", port_dir), ("retail", retail_dir)):
        a = sd / "anchors.jsonl"
        if a.exists():
            shutil.copy2(a, sess_dir / f"anchors.{side}.jsonl")
            manifest.setdefault("anchor_files", {})[side] = f"anchors.{side}.jsonl"

    # Snapshot the trace that was DRIVEN (read-only "emitted inputs" view).
    if trace.exists():
        shutil.copy2(trace, sess_dir / "captured.trace.jsonl")
        manifest["captured_trace"] = "captured.trace.jsonl"

    # flow-trace state + verdict
    n_window = len(list((port_dir / "frames").glob("frame_*.png")))
    if call_trace and port_base is not None:
        rb = retail_base if retail_base is not None else 0
        state = state_mod.build_state(port_dir, retail_dir, port_base, rb, n_window)
        (sess_dir / "state.jsonl").write_text(
            "".join(json.dumps(r) + "\n" for r in state))
        manifest["state"] = "state.jsonl"
        manifest["verdict"] = verdict_mod.run_verdict(port_dir, retail_dir)

    # frame count + range (anchor-relative, present on the port)
    n_port = len(list((port_dir / "frames").glob("frame_*.png")))
    manifest["n_frames"] = n_port
    nums = sorted(int("".join(c for c in p.stem if c.isdigit()))
                  for p in (port_dir / "frames").glob("frame_*.png"))
    manifest["frame_range"] = [nums[0], nums[-1]] if nums else [0, 0]

    # Ordinal-pairing guard (the studio's port↔retail comparator pairs Nth-left vs
    # Nth-right, not by absolute frame): both sides MUST keep the same kept-count.
    # D1 suppression + LOADING_END-anchored windows + identical {capstride} give
    # this for free; a mismatch means the stride/suppression desynced — surface it.
    n_retail = len(list((retail_dir / "frames").glob("frame_*.png")))
    manifest["n_frames_retail"] = n_retail
    if have_retail_frames and n_retail != n_port:
        manifest["kept_count_mismatch"] = {"port": n_port, "retail": n_retail}
        _log(f"WARNING kept-count MISMATCH port={n_port} retail={n_retail} — "
             f"ordinal pairing is unreliable (check the {{capstride}}/load-suppress "
             f"seam alignment; overview windows must anchor at LOADING_END or later)")
    elif have_retail_frames:
        _log(f"kept-count parity OK: port == retail == {n_port}"
             + (f" (stride {stride})" if stride > 1 else ""))

    # Surface a clear error when a side captured 0 frames (window never reached).
    errs: list[str] = []
    if n_port == 0:
        errs.append("port captured 0 frames")
    if want_retail and not have_retail_frames and "retail_error" not in result:
        errs.append("retail captured 0 frames")
    if result.get("retail_error"):
        errs.append(f"retail: {result['retail_error']}")
    if errs:
        manifest["capture_error"] = (
            "; ".join(errs) + ". The capture window was never reached — the port "
            "likely diverged in the prologue before the window's anchor. The anchor "
            "timelines are still captured (use them to see/work around the "
            "divergence); for full video+state replay record a Continue/Load trace "
            "(which skips the prologue) or adjust the window.")
        _log("CAPTURE ERROR: " + manifest["capture_error"])

    if cfg.prune_frames:
        for d in (port_dir / "frames", retail_dir / "frames",
                  sess_dir / "diff" / "frames"):
            if d.exists():
                for p in d.glob("frame_*.png"):
                    p.unlink()
        _log("pruned bulk PNG frames (videos retained)")

    # ── v2 timeline: loads as zero-frame seams (from the anchor streams) ──────
    port_firings = ops.read_anchor_stream(port_dir / "anchors.jsonl")
    retail_firings = ops.read_anchor_stream(retail_dir / "anchors.jsonl")
    timeline = tl_mod.build_timeline(
        port_firings=port_firings, retail_firings=retail_firings,
        n_frames=manifest["n_frames"], frame_range=manifest["frame_range"],
        videos=manifest["videos"], verdict=manifest.get("verdict"),
        state=manifest.get("state"), call_trace=manifest["call_trace"],
        stride=stride)
    manifest = sess_mod.make_v2_manifest(manifest, timeline)

    sess_mod.write_session(sess_dir, manifest)
    n_seams = sum(1 for e in timeline if e.get("kind") == "load_seam")
    _log(f"session.json written → {sess_dir}")
    _log(f"DONE: {n_port} frames"
         + (f" @ stride {stride} (OVERVIEW)" if stride > 1 else "")
         + f", {n_seams} load-seam(s), videos={list(manifest['videos'])}"
         + (f", verdict exit={manifest['verdict'].get('exit_code')}"
            if manifest.get("verdict") else ""))
    print(f"\nview it:  nix develop --command python3 tools/trace_studio.py "
          f"serve --session {sess}\n")
    return 0
