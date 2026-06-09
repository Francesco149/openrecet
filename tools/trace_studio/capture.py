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


def _cached_retail_base(retail_dir: Path) -> int | None:
    """Best-effort recover a cached retail capture's rebase base from disk, for when
    the session manifest didn't record it (an interrupted/partial prior finalize, or
    a pre-base manifest).  An already-renumbered cache is label-named (min frame ==
    the window start — 0 for the common anchored window); an un-renumbered one yields
    its min absolute frame number.  Returns None only when there are no cached retail
    frames at all — so a `--only port` re-capture pairs against existing retail
    frames instead of falsely reporting 'retail 0 frames'."""
    nums = [int("".join(c for c in p.stem if c.isdigit()))
            for p in (retail_dir / "frames").glob("frame_*.png")
            if any(c.isdigit() for c in p.stem)]
    return min(nums) if nums else None


def _resolve_want_retail(target: str, only: str, has_cached_retail: bool) -> bool:
    """Whether the session should carry a retail side.

    `target == "both"` always wants retail.  The fast port-fix loop (`--only port`)
    re-runs ONLY the port and reuses the cached retail — the core studio loop (tweak
    the port, recapture, refresh the SAME session).  That loop must ALSO preserve a
    session's cached retail even when --target isn't "both": re-running only the port
    must never DROP the retail comparison the user is watching (the bug behind
    `--target openrecet --only port` silently losing videos/diff/anchors/base).  A
    genuinely port-only session has no cached retail → still no retail; a deliberate
    both→port-only conversion uses a full recapture (`--only both`), not the fast loop."""
    if target == "both":
        return True
    if only == "port" and has_cached_retail:
        return True
    return False


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
    working = sess_dir / "edit.trace.jsonl"  # want_retail resolved below (needs old_manifest)

    # The session manifest (loaded once). `source_trace` is the original recording;
    # on a re-capture `cfg.trace` is the WORKING trace, so the recording — and its
    # anchors — are found via the manifest, not `src`.
    old_manifest: dict = {}
    if (sess_dir / "session.json").exists():
        try:
            old_manifest = json.loads((sess_dir / "session.json").read_text())
        except Exception:                            # noqa: BLE001
            old_manifest = {}

    # Whether the session carries a retail side. `want_retail` was set above to
    # `target == "both"`, but the fast port-fix loop (`--only port`) must ALSO preserve
    # a session's cached retail even when --target isn't "both" — see
    # _resolve_want_retail. Without this, `--target openrecet --only port` (or any
    # port-only re-run of a both-session) dropped the cached retail (videos/diff/
    # anchors/base lost), silently breaking the comparison the user is watching.
    has_cached_retail = bool((old_manifest.get("videos") or {}).get("retail")) \
        or any((retail_dir / "frames").glob("frame_*.png"))
    want_retail = _resolve_want_retail(cfg.target, cfg.only, has_cached_retail)
    if want_retail and cfg.target != "both":
        _log("--only port: preserving the session's cached retail "
             "(re-running the port must not drop the retail comparison)")

    # ── anchoring (cfg.anchors None = AUTO) ──────────────────────────────────
    # A raw recording that carries {anchor} rows anchor-segments by default. FLAT
    # boot-syncing a load-bearing recording is wrong two ways: (1) the {caprange}
    # lands in the PRE-LOAD region (a Continue trace's window stops at the save-
    # picker), and (2) the INPUTS replay at boot-relative frames, so a walk recorded
    # at HF+51 fires during the port's (longer, non-deterministic) load and is lost —
    # the port never reproduces the movement. Anchor-segmenting syncs both the window
    # and the input replay to the recorded anchors. The recording is `src` on a fresh
    # capture, else the manifest's source_trace. Explicit --anchors/--no-anchors override.
    rec = src if hdr else None
    if rec is None:
        st = old_manifest.get("source_trace")
        if st and Path(st).exists() and ops.raw_header(Path(st)):
            rec = Path(st)
    rec_has_anchors = rec is not None and ops.raw_has_anchors(rec)
    anchored = cfg.anchors if cfg.anchors is not None else rec_has_anchors
    if cfg.anchors is None and rec_has_anchors:
        _log("auto-anchor: recording carries anchors → anchor-segmented distil "
             "(FLAT lands the window in the pre-load region AND desyncs input replay; "
             "--no-anchors to force)")

    # ── working trace: reuse (carries applied pins) unless --reset-trace ──────
    reuse = working.exists() and not cfg.reset_trace
    window_rebuilt = False
    if reuse and anchored and rec is not None \
            and not ops.window_at_freeroam(ops.load_ops(working)):
        # The working trace's window is STALE — FLAT (boot-synced; inputs desynced),
        # or anchored at a LATER scene the port can't reach (e.g. the town a shop-exit
        # leads to → port captures 0). Either was built before the free-roam-anchored
        # auto-window. Rebuild it anchor-segmented FROM THE RECORDING so the window
        # lands at the free-roam entry both targets reach. (Re-apply pins if any were
        # set; --no-anchors to keep the old placement.) This self-heals a session the
        # SPA re-capture would otherwise reuse verbatim (it passes the working trace).
        _log("re-capture: working trace window is STALE (FLAT or anchored past the "
             f"port-reachable free-roam) — rebuilding from {rec.name} at the free-roam "
             "entry (re-apply pins if needed; --no-anchors to keep it)")
        src, hdr = rec, ops.raw_header(rec)
        reuse = False
        window_rebuilt = True   # the window moved → any cached retail is now misaligned
    elif reuse and anchored and rec is None \
            and not any("wait" in o for o in ops.load_ops(working)):
        _log("re-capture: FLAT working trace + no source recording to rebuild from "
             "— reusing FLAT (re-capture from the recording to anchor)")
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
    # (old_manifest already loaded above for the anchoring decision)
    run_port = cfg.only in ("both", "port")
    run_retail = want_retail and cfg.only in ("both", "retail")
    if window_rebuilt and want_retail and not run_retail:
        # The window just moved (self-heal): a cached retail capture is from the OLD
        # window, so reusing it would pair the port's new window against retail's old
        # one (e.g. port=shop walk vs retail=town). Force a retail re-capture even on a
        # port-only re-capture — the stale cache can't be trusted.
        _log("re-capture: window rebuilt → forcing a retail re-capture too (the cached "
             "retail is from the old window; reusing it would misalign port↔retail)")
        run_retail = True
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
        retail_base = convert.renumber_retail(retail_dir, window_start=cr[0])
    elif keep_retail:                                # reuse the cached retail capture
        retail_base = (old_manifest.get("retail") or {}).get("base_abs")
        if retail_base is None:                      # manifest lost it → recover from disk
            retail_base = _cached_retail_base(retail_dir)
        _log(f"--only port: reusing cached retail (base {retail_base})")

    manifest: dict = {
        "schema": "trace-studio-v2",
        "session": sess,
        "trace": str(working),
        "working_trace": str(working),
        # the ORIGINAL recording (for the re-capture self-heal). NEVER clobber a known
        # recording with the working-trace path: on a re-capture `src` IS the working
        # trace, so prefer `rec` (resolved above), then the existing manifest value.
        "source_trace": str(rec) if rec is not None
        else (old_manifest.get("source_trace") or str(src)),
        "caprange": list(cr),
        "stride": stride,                # D3: 1 = dense; >1 = coarse OVERVIEW cadence
        "fps": encode.VIDEO_FPS,
        "amp": cfg.amp,
        "target": cfg.target,
        "suppress_loads": port_suppress,
        "port": {"base_abs": port_base},
        "retail": {"base_abs": retail_base, "error": result.get("retail_error")},
        # The coordinate contract (web/model.mjs): frame FILES on BOTH sides + the
        # diff are named by the anchor-relative LABEL (= window_start + k*stride for
        # viewer ordinal k); diff.per_frame[].frame and state.jsonl rows key as noted.
        "coords": {"naming": "label", "window_start": cr[0], "stride": stride,
                   "diff_keyed_by": "label", "state_keyed_by": "ordinal"},
        "videos": {},
        "anchors": {},
        "diff": None,
        "verdict": None,
        "call_trace": call_trace,
    }

    # Frame existence is the real "do we have retail" signal — NOT retail_base, which a
    # partial prior capture can leave None even with frames on disk (the bug that made
    # `--only port` falsely report "retail captured 0 frames" + skip the diff/verdict).
    have_retail_frames = ((run_retail or keep_retail)
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
        # retail anchor = abs of anchor-relative 0 (first kept frame is the window
        # start). port_base (global.json frame_base_abs) is already the anchor.
        retail_anchor = (retail_base - cr[0]) if retail_base is not None else 0
        state = state_mod.build_state(port_dir, retail_dir, port_base,
                                      retail_anchor, n_window,
                                      window_start=cr[0], stride=stride)
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
    # `--only port` with no cached retail is a DIFFERENT failure (nothing to reuse) than
    # a window the port never reached — don't blame the prologue in that case.
    errs: list[str] = []
    if n_port == 0:
        errs.append("port captured 0 frames")
    if keep_retail and not have_retail_frames:
        manifest["capture_error"] = (
            "--only port: no cached retail capture to reuse (this session has no "
            "retail/frames). Run a `capture`/`recapture` with --only both first to "
            "populate the retail side, then port-only re-captures will pair against it.")
        _log("CAPTURE ERROR: " + manifest["capture_error"])
    else:
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
