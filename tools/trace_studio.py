#!/usr/bin/env python3
"""
tools/trace_studio.py — the human-facing TAS trace iteration studio.

ONE tool to record-tweak-verify a long trace until it plays end-to-end 1:1 on
BOTH the port and retail. Three subcommands:

  capture   Drive port + retail through a trace's anchor-relative {caprange}
            window (turbo, concurrent), align the two frame sets, build a
            white-diff, and encode each into an all-intra scrub VIDEO (so the
            viewer scales to thousands of 60fps frames — one ranged stream
            instead of N×1MB PNG fetches). With --call-trace it also captures
            the flow-trace on both sides and stores the phase/RNG VERDICT
            (ALIGNED / CONST-OFFSET / DRIFT + rngcalls) + a per-frame state
            stream for the viewer overlay.

  serve     A local http server + single-page viewer: port|retail|diff videos
            seeking in lockstep, a per-frame state overlay that HIGHLIGHTS the
            fields that differ port↔retail (so you SEE where phase/RNG drift),
            an anchor track, the verdict panel, and mark/edit buttons that POST
            pin-phase / pin-RNG / add-anchor / feature / note marks to edits.jsonl.

  apply     Read edits.jsonl and (a) auto-insert {phasepin}/{rngseed} ops into
            the trace at the marked anchor-relative frames, (b) --auto-pin
            propose pins straight from the stored verdict (db054 CONST-OFFSET →
            {phasepin}; rngcalls DESYNC → {rngseed}), and (c) emit a worklist.md
            for the anchor/feature/note marks (frame + state + crop context) so
            Claude can implement the missing path, then re-`capture` to re-verify.

The iterate loop:  capture → serve (spot divergence, pin / mark) → apply →
implement / re-capture the window → verdict goes PHASE-CLEAN / diff goes black.

Reuses the existing tools as libraries (no logic reinvented): export_trace
(port drive + 0-based anchor-relative renumber), frida_capture.run_capture
(retail), trace_save.resolve_save, pixel_diff.amplified_diff, flow_diff
(--verdict), frame_io. See docs/trace-workflow.md.

Run under the dev shell (needs Pillow/numpy/ffmpeg):
    nix develop --command python3 tools/trace_studio.py capture <trace|scn> ...
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

SESS_ROOT = ROOT / "runs" / "trace-studio"
DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")
VIDEO_FPS = 30                       # scrub video framerate (frame n ↔ time n/FPS)
WEB_DIR = ROOT / "tools" / "trace_studio_web"

# Once-per-frame flow-trace VAs whose declared fields we surface in the viewer
# state overlay (see docs/flow-trace-cheatsheet.md "Standard once-per-frame anchors").
STATE_VAS = {
    0x47be92: "sched",      # tick_scheduler — rng / rngcalls
    0x48670f: "house",      # house_update   — player+companion poct/px/anim/...
    0x49a59e: "title",      # scene_title_sim
    0x46c320: "dlg",        # dialogue_tick
}


# ─── shared helpers ──────────────────────────────────────────────────────────

def _log(msg: str) -> None:
    print(f"trace_studio: {msg}", file=sys.stderr)


def resolve_trace(arg: str) -> Path:
    """A trace file path, or a scenario name → tests/scenarios/<name>/trace.jsonl."""
    p = Path(arg)
    if p.exists():
        return p.resolve()
    scn = ROOT / "tests" / "scenarios" / arg / "trace.jsonl"
    if scn.exists():
        return scn.resolve()
    raise SystemExit(f"trace_studio: no trace file or scenario named {arg!r}")


def load_ops(path: Path) -> list[dict]:
    ops = []
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            ops.append(json.loads(s))
        except json.JSONDecodeError:
            pass
    return ops


def extract_caprange(ops: list[dict]) -> tuple[int, int] | None:
    for o in ops:
        if isinstance(o, dict) and "caprange" in o:
            cr = o["caprange"]
            return int(cr[0]), int(cr[1])
    return None


def raw_header(path: Path) -> dict | None:
    """If `path` is a frida/F2 raw recording, return its header dict, else None."""
    try:
        first = next(l for l in path.read_text().splitlines() if l.strip())
        o = json.loads(first)
    except (StopIteration, OSError, json.JSONDecodeError):
        return None
    return o if isinstance(o, dict) and str(o.get("_rec", "")).startswith(
        "openrecet-tas-raw") else None


def raw_default_window(path: Path) -> tuple[int, int] | None:
    """Default capture window for a raw recording: from the LAST anchor (the base
    export_trace's {caprange} resolves against) through the end of the recording
    + a little margin — i.e. the post-intro free-roam span the user actually played."""
    anchors, frames = [], []
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if "anchor" in o and "frame" in o:
            anchors.append(int(o["frame"]))
        elif "frame" in o and "buttons" in o:
            frames.append(int(o["frame"]))
    if not frames:
        return None
    last_anchor = max(anchors) if anchors else 0
    return (0, max(1, max(frames) - last_anchor + 90))


def extract_calltrace(ops: list[dict]) -> tuple[int, int] | None:
    for o in ops:
        if isinstance(o, dict) and "calltrace" in o:
            ct = o["calltrace"]
            if isinstance(ct, list):
                return int(ct[0]), int(ct[1])
            return int(ct), 0
    return None


def load_png_rgb(path: Path):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(path).convert("RGB"))


def save_png(arr, path: Path) -> None:
    from PIL import Image
    Image.fromarray(arr).save(path)


def ffmpeg_encode(frames_dir: Path, out_mp4: Path, fps: int = VIDEO_FPS) -> bool:
    """Encode frame_*.png in `frames_dir` into an ALL-INTRA h264 mp4 (every frame a
    keyframe → frame-exact browser seeking). yuv420p for universal playback; the
    diff is grayscale so chroma subsampling is lossless for it. Returns success."""
    frames = sorted(frames_dir.glob("frame_*.png"))
    if not frames:
        _log(f"encode: no frames in {frames_dir}")
        return False
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-framerate", str(fps),
        "-pattern_type", "glob", "-i", "frame_*.png",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
        "-pix_fmt", "yuv420p",
        "-x264-params", "keyint=1:scenecut=0",
        "-movflags", "+faststart",
        str(out_mp4),
    ]
    r = subprocess.run(cmd, cwd=str(frames_dir),
                       capture_output=True, text=True)
    if r.returncode != 0:
        _log(f"encode FAILED ({out_mp4.name}): {r.stderr.strip()[:300]}")
        return False
    _log(f"encoded {len(frames)} frames → {out_mp4.relative_to(SESS_ROOT.parent)}")
    return True


def read_anchors(path: Path, base: int) -> list[dict]:
    """anchors.jsonl ({anchor, frame}) rebased to anchor-relative index."""
    out = []
    if not path.exists():
        return out
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if "anchor" in o and "frame" in o:
            out.append({"anchor": o["anchor"], "frame": int(o["frame"]) - base})
    return out


# ─── capture ─────────────────────────────────────────────────────────────────

def _capture_port(trace: Path, port_dir: Path, cr: tuple[int, int],
                  max_frames: int, call_trace: bool, ct: tuple[int, int] | None,
                  result: dict) -> None:
    """Drive the port via export_trace.main (also drops anchors.jsonl)."""
    import export_trace
    argv = [
        str(trace),
        "--caprange", f"{cr[0]},{cr[1]}",
        "--run-dir", str(port_dir),
        "--max-frames", str(max_frames),
        "--name", "trace-studio",
        "--anchor-record",        # port anchor stream → port_dir/anchors.jsonl
    ]
    if call_trace:
        argv.append("--call-trace")
    # export_trace doesn't pass --anchor-trace-record; the port still drops its
    # final_anchor into global.json which we use as the rebase base. (Port anchor
    # ticks on the timeline come from global.json; retail carries anchors.jsonl.)
    rc = export_trace.main(argv)
    result["port_rc"] = rc


def _capture_retail(trace_work: Path, orig_trace: Path, retail_dir: Path,
                    max_frames: int, rng_seed: int | None, call_trace: bool,
                    remote: str, result: dict) -> None:
    """Drive retail via frida_capture.run_capture over the SAME work trace.
    The {savefile} ref is relative to the ORIGINAL trace's dir (not the session
    work copy), so resolve the save against orig_trace."""
    import types
    import frida_capture
    import trace_save
    retail_dir.mkdir(parents=True, exist_ok=True)
    scen = types.SimpleNamespace(
        name="trace-studio",
        capture_frames=[],
        max_frames=int(max_frames),
        duration_ceiling_ms=600_000,
        rng_seed=rng_seed,
    )
    try:
        meta = frida_capture.run_capture(
            scen, retail_dir,
            remote=remote,
            input_segtrace_path=trace_work,
            hide_window=True, turbo=True, silent_audio=True,
            force_resolution=(1024, 768),
            rng_seed=rng_seed,
            save_ref=trace_save.resolve_save(orig_trace),
            call_trace=call_trace,
            anchor_trace=True,        # → retail anchors.jsonl for the studio timeline
        )
        result["retail_meta"] = meta
    except Exception as e:                       # noqa: BLE001 — surface, don't crash port
        result["retail_error"] = repr(e)
        _log(f"retail capture FAILED: {e!r}")


def renumber_retail(retail_dir: Path) -> int | None:
    """Retail writes frame_<absolute>.png; mirror export_trace's 0-based renumber
    so frame_NNNNN aligns with the port. Returns the rebase base, or None."""
    from frame_io import frame_glob
    frames_dir = retail_dir / "frames"
    frames = frame_glob(frames_dir)
    if not frames:
        return None
    nums = []
    for p in frames:
        digits = "".join(c for c in p.stem if c.isdigit())
        if digits:
            nums.append((int(digits), p))
    if not nums:
        return None
    nums.sort()
    base = nums[0][0]
    if base == 0:
        return 0
    # rel < abs everywhere, so ascending rename never collides.
    for n, p in nums:
        tgt = p.with_name(f"frame_{n - base:05d}{p.suffix}")
        if tgt != p:
            p.rename(tgt)
    return base


def build_diff(port_dir: Path, retail_dir: Path, diff_dir: Path,
               amp: float) -> dict:
    """Per anchor-relative index present on BOTH sides, write the white-diff PNG.
    Returns a summary {n, per_frame:[{frame,differ,meanabs}]}."""
    from pixel_diff import amplified_diff
    diff_dir.mkdir(parents=True, exist_ok=True)
    pf = {int("".join(c for c in p.stem if c.isdigit())): p
          for p in (port_dir / "frames").glob("frame_*.png")}
    rf = {int("".join(c for c in p.stem if c.isdigit())): p
          for p in (retail_dir / "frames").glob("frame_*.png")}
    common = sorted(set(pf) & set(rf))
    per = []
    for n in common:
        a = load_png_rgb(rf[n])              # retail = ground truth (left/A)
        b = load_png_rgb(pf[n])              # port
        if a.shape != b.shape:
            continue
        d, differ, meanabs = amplified_diff(a, b, amp)
        save_png(d, diff_dir / f"frame_{n:05d}.png")
        per.append({"frame": n, "differ": differ, "meanabs": round(meanabs, 4)})
    return {"n": len(per), "per_frame": per}


def build_state(port_dir: Path, retail_dir: Path, port_base: int,
                retail_base: int, nframes: int) -> list[dict]:
    """Merge the once-per-frame flow-trace fields from both call_trace.jsonl into
    anchor-relative rows: [{frame, port:{...}, retail:{...}}], clipped to the
    capture window [0, nframes). (The lightweight once-per-frame probes emit from
    boot regardless of the {calltrace} window, so we clip to the captured span.)"""
    from flow_diff import load_trace
    vaset = set(STATE_VAS)

    def collect(path: Path, base: int) -> dict[int, dict]:
        if not path.exists():
            return {}
        by_frame = load_trace(path, va_filter=vaset)
        out: dict[int, dict] = {}
        for fr, evts in by_frame.items():
            rel = fr - base
            if rel < 0 or rel >= nframes:
                continue
            merged: dict = {}
            for e in evts:
                f = e.get("f")
                if isinstance(f, dict):
                    merged.update(f)
            if merged:
                out[rel] = merged
        return out

    p = collect(port_dir / "call_trace.jsonl", port_base)
    r = collect(retail_dir / "call_trace.jsonl", retail_base)
    frames = sorted(set(p) | set(r))
    return [{"frame": n, "port": p.get(n, {}), "retail": r.get(n, {})}
            for n in frames]


def run_verdict(port_dir: Path, retail_dir: Path,
                align_field: str = "db054") -> dict:
    """Run flow_diff --verdict over the two call traces; capture text + exit code.
    Port and retail frames are ABSOLUTE (port ~600, retail ~14500 under turbo
    load-stretch), so we pair them by --align-field db054 (the shared phase
    clock), not raw frame number — without it there are no common frames."""
    rp = retail_dir / "call_trace.jsonl"
    pp = port_dir / "call_trace.jsonl"
    if not (rp.exists() and pp.exists()):
        return {"available": False}
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "flow_diff.py"),
         "--retail", str(rp), "--port", str(pp), "--verdict",
         "--align-field", align_field],
        capture_output=True, text=True, cwd=str(ROOT))
    return {"available": True, "exit_code": r.returncode,
            "align_field": align_field,
            "text": r.stdout + (("\n[stderr]\n" + r.stderr) if r.stderr else "")}


def _distill_raw(raw: Path, out: Path) -> None:
    """Distil a frida/F2 raw recording → an anchor-segmented replayable trace
    (folds the embedded save into <out_dir>/_saves/ so the session is self-contained)."""
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "distill_trace.py"), str(raw),
         "--anchor-segments", "-o", str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0 or not out.exists():
        raise SystemExit(f"trace_studio: distil failed for {raw}:\n{r.stderr[-800:]}")


def _localize_save(src: Path, text: str, sess_dir: Path) -> str:
    """Copy a non-raw trace's save blob into the session's _saves/ and rewrite the
    {savefile} op to point there, so the working trace is self-contained (its save
    resolves relative to the SESSION dir, not the original scenario dir)."""
    import trace_save
    ref = trace_save.read_ref(src)
    if not ref or ref == trace_save.FRESH_REF:
        return text
    blob = (src.resolve().parent / ref).resolve()
    if not blob.exists():
        return text
    dst_dir = sess_dir / "_saves"
    dst_dir.mkdir(exist_ok=True)
    dst = dst_dir / blob.name
    if not dst.exists():
        import shutil
        shutil.copy2(blob, dst)
    new_ref = f"_saves/{blob.name}"
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        o = None
        if s and not s.startswith("#"):
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                o = None
        if isinstance(o, dict) and "savefile" in o:
            o["savefile"] = new_ref
            out.append(json.dumps(o))
        else:
            out.append(ln)
    return "\n".join(out) + "\n"


def _ensure_window_ops(text: str, cr: tuple[int, int], call_trace: bool) -> str:
    """Insert {caprange} (+ {calltrace} if requested) after the final {wait},
    replacing any pre-existing window ops. Pins (phasepin/rngseed) the user applies
    sit just after the {wait} too and are preserved."""
    keep = []
    for ln in text.splitlines():
        s = ln.strip()
        if s and not s.startswith("#"):
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                o = None
            if isinstance(o, dict) and ("caprange" in o or "calltrace" in o):
                continue
        keep.append(ln)
    li = -1
    for i, ln in enumerate(keep):
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "wait" in o:
            li = i
    ins = [json.dumps({"caprange": [cr[0], cr[1]]})]
    if call_trace:
        ins.append(json.dumps({"calltrace": [cr[0], cr[1]]}))
    at = li + 1 if li >= 0 else len(keep)
    keep[at:at] = ins
    return "\n".join(keep) + "\n"


def _build_working_trace(src: Path, sess_dir: Path, working: Path,
                         cr: tuple[int, int], call_trace: bool) -> None:
    """Create the session's editable WORKING trace from a source (raw recording or
    a .jsonl): distil if raw, localize its save, and inject the window ops. This is
    the artifact the user's pins land on (via `apply`) and that re-captures reuse."""
    if raw_header(src):
        base = sess_dir / "recording.trace.jsonl"
        _distill_raw(src, base)
        text = base.read_text()
    else:
        text = _localize_save(src, src.read_text(), sess_dir)
    working.write_text(_ensure_window_ops(text, cr, call_trace))


def cmd_capture(args) -> int:
    src = resolve_trace(args.trace)
    hdr = raw_header(src)

    sess = args.session or (
        (src.name[:-len(".raw.jsonl")] if src.name.endswith(".raw.jsonl")
         else src.parent.name)
        + f"-{dt.datetime.now():%Y%m%d-%H%M%S}")
    sess_dir = SESS_ROOT / sess
    port_dir = sess_dir / "port"
    retail_dir = sess_dir / "retail"
    diff_dir = sess_dir / "diff" / "frames"
    sess_dir.mkdir(parents=True, exist_ok=True)
    want_retail = args.target == "both"
    working = sess_dir / "edit.trace.jsonl"

    # Establish the WORKING trace. On a fresh capture, build it from the source +
    # window ops; on a re-capture, REUSE it (it carries the user's applied pins) —
    # unless --reset-trace. This is what makes the in-studio pin→apply→re-capture
    # loop work: apply edits edit.trace.jsonl, re-capture drives it.
    if working.exists() and not args.reset_trace:
        _log(f"re-capture: reusing working trace {working.name} (with applied pins)")
    else:
        if args.caprange:
            s, c = args.caprange.split(",")
            cr0 = (int(s), int(c))
        else:
            cr0 = extract_caprange(load_ops(src))
            if not cr0 and hdr:
                cr0 = raw_default_window(src)
                _log(f"raw recording → auto window caprange={cr0} "
                     f"(last anchor → end of recording)")
        if not cr0:
            raise SystemExit("trace_studio: no --caprange given and none in the trace")
        _build_working_trace(src, sess_dir, working, cr0, bool(args.call_trace))

    trace = working
    ops = load_ops(trace)
    cr = extract_caprange(ops)
    if not cr:
        raise SystemExit(f"trace_studio: working trace {working} has no caprange")
    ct = extract_calltrace(ops)
    call_trace = bool(args.call_trace and ct)
    if args.call_trace and not ct:
        _log("--call-trace requested but the working trace has no {calltrace} op "
             "(rebuild with --reset-trace --call-trace)")

    # Clear prior capture artifacts so a re-capture into the same session can't
    # accumulate stale frames (which corrupt the anchor-relative renumber base).
    # Preserve the user's marks (edits.jsonl / worklist.md) — same anchor-relative
    # window ⇒ the indices stay valid across a re-capture.
    import shutil
    for sub in ("port", "retail", "diff"):
        if (sess_dir / sub).exists():
            shutil.rmtree(sess_dir / sub)
    for stale in ("port.mp4", "retail.mp4", "diff.mp4", "state.jsonl",
                  "session.json"):
        (sess_dir / stale).unlink(missing_ok=True)

    _log(f"session {sess}  caprange={cr}  call_trace={call_trace}  "
         f"target={args.target}")

    # ── drive: port (always) + retail (concurrent) ──────────────────────────
    result: dict = {}
    threads = []
    tp = threading.Thread(target=_capture_port,
                          args=(trace, port_dir, cr, args.port_max_frames,
                                call_trace, ct, result))
    tp.start(); threads.append(tp)

    # Retail needs the work trace export_trace writes; wait for it to exist.
    if want_retail:
        work = port_dir / "trace.work.jsonl"
        import time
        for _ in range(600):                     # ≤60s for the port to write it
            if work.exists():
                break
            time.sleep(0.1)
        rng_seed = None
        gp = port_dir / "global.json"
        # global.json appears only at port end; fall back to the trace's seed.
        seed_from_trace = next((o.get("rngseed", [None, None])[1]
                                for o in ops if "rngseed" in o), None)
        rng_seed = seed_from_trace
        if work.exists():
            tr = threading.Thread(
                target=_capture_retail,
                args=(work, trace, retail_dir, args.retail_max_frames, rng_seed,
                      call_trace, args.remote, result))
            tr.start(); threads.append(tr)
        else:
            _log("port never wrote trace.work.jsonl — skipping retail")
            want_retail = False

    for t in threads:
        t.join()

    if result.get("port_rc", 1) != 0:
        _log("port capture reported a non-zero rc; continuing with what landed")

    # ── post: bases, renumber retail, diff, encode, state, verdict ──────────
    gp = port_dir / "global.json"
    port_base = None
    if gp.exists():
        port_base = json.loads(gp.read_text()).get("frame_base_abs")
    port_anchors = []  # port has no anchors.jsonl; final_anchor only (in global)

    retail_base = None
    if want_retail and "retail_error" not in result:
        retail_base = renumber_retail(retail_dir)

    manifest = {
        "schema": "trace-studio-v1",
        "session": sess,
        "trace": str(working),
        "working_trace": str(working),
        "source_trace": str(src),
        "caprange": list(cr),
        "fps": VIDEO_FPS,
        "amp": args.amp,
        "target": args.target,
        "port": {"base_abs": port_base},
        "retail": {"base_abs": retail_base,
                   "error": result.get("retail_error")},
        "videos": {},
        "anchors": {},
        "diff": None,
        "verdict": None,
        "call_trace": call_trace,
    }

    # diff frames (needs both sides aligned)
    have_retail_frames = (want_retail and retail_base is not None
                          and any((retail_dir / "frames").glob("frame_*.png")))
    if have_retail_frames:
        manifest["diff"] = build_diff(port_dir, retail_dir,
                                      sess_dir / "diff" / "frames", args.amp)

    # encode videos
    if ffmpeg_encode(port_dir / "frames", sess_dir / "port.mp4"):
        manifest["videos"]["port"] = "port.mp4"
    if have_retail_frames:
        if ffmpeg_encode(retail_dir / "frames", sess_dir / "retail.mp4"):
            manifest["videos"]["retail"] = "retail.mp4"
        if ffmpeg_encode(sess_dir / "diff" / "frames", sess_dir / "diff.mp4"):
            manifest["videos"]["diff"] = "diff.mp4"

    # anchor track (retail anchors.jsonl rebased; port from global.final_anchor)
    if want_retail and retail_base is not None:
        manifest["anchors"]["retail"] = read_anchors(
            retail_dir / "anchors.jsonl", retail_base)

    # Copy BOTH sides' raw anchor streams (absolute engine frames) into the session
    # for the timeline editor's sync-anchor alignment (tools/trace_studio_web/align.mjs).
    for side, sd in (("port", port_dir), ("retail", retail_dir)):
        a = sd / "anchors.jsonl"
        if a.exists():
            shutil.copy2(a, sess_dir / f"anchors.{side}.jsonl")
            manifest.setdefault("anchor_files", {})[side] = f"anchors.{side}.jsonl"

    # flow-trace state + verdict
    n_window = len(list((port_dir / "frames").glob("frame_*.png")))
    if call_trace and port_base is not None:
        rb = retail_base if retail_base is not None else 0
        state = build_state(port_dir, retail_dir, port_base, rb, n_window)
        (sess_dir / "state.jsonl").write_text(
            "".join(json.dumps(r) + "\n" for r in state))
        manifest["state"] = "state.jsonl"
        manifest["verdict"] = run_verdict(port_dir, retail_dir)

    # frame count (anchor-relative range present on the port)
    n_port = len(list((port_dir / "frames").glob("frame_*.png")))
    manifest["n_frames"] = n_port
    nums = sorted(int("".join(c for c in p.stem if c.isdigit()))
                  for p in (port_dir / "frames").glob("frame_*.png"))
    manifest["frame_range"] = [nums[0], nums[-1]] if nums else [0, 0]

    # Surface a clear error when a side captured 0 frames — the window was never
    # reached (typically a cross-target prologue divergence: the port can't replay
    # a retail-recorded new-game prologue to the window). The ANCHOR streams are
    # still captured, so the timeline shows the divergence even with no video — that
    # is exactly what the tool is for.
    errs = []
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

    if args.prune_frames:
        for d in (port_dir / "frames", retail_dir / "frames",
                  sess_dir / "diff" / "frames"):
            if d.exists():
                for p in d.glob("frame_*.png"):
                    p.unlink()
        _log("pruned bulk PNG frames (videos retained)")

    (sess_dir / "session.json").write_text(json.dumps(manifest, indent=2) + "\n")
    _log(f"session.json written → {sess_dir}")
    _log(f"DONE: {n_port} frames, videos={list(manifest['videos'])}"
         + (f", verdict exit={manifest['verdict'].get('exit_code')}"
            if manifest.get("verdict") else ""))
    print(f"\nview it:  nix develop --command python3 tools/trace_studio.py "
          f"serve --session {sess}\n")
    return 0


# ─── serve ───────────────────────────────────────────────────────────────────

def cmd_serve(args) -> int:
    from trace_studio_serve import serve
    sess_dir = SESS_ROOT / args.session if args.session else None
    if sess_dir and not sess_dir.exists():
        raise SystemExit(f"trace_studio: no session {args.session} under {SESS_ROOT}")
    serve(SESS_ROOT, WEB_DIR, host=args.host, port=args.port,
          default_session=args.session, remote=args.remote)
    return 0


# ─── apply ───────────────────────────────────────────────────────────────────

def cmd_apply(args) -> int:
    from trace_studio_apply import apply
    sess_dir = SESS_ROOT / args.session
    if not sess_dir.exists():
        raise SystemExit(f"trace_studio: no session {args.session}")
    apply(sess_dir, trace_override=Path(args.trace) if args.trace else None,
          auto_pin=args.auto_pin, dry_run=args.dry_run)
    return 0


# ─── cli ─────────────────────────────────────────────────────────────────────

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="drive port+retail → aligned scrub videos")
    c.add_argument("trace", help="trace.jsonl path OR a scenario name")
    c.add_argument("--caprange", help="START,COUNT (default: from the trace)")
    c.add_argument("--target", choices=("both", "openrecet"), default="both")
    c.add_argument("--session", help="session name (default: <trace>-<ts>)")
    c.add_argument("--call-trace", action="store_true",
                   help="capture the flow-trace + store the phase/RNG verdict "
                        "(needs a {calltrace} op in the trace)")
    c.add_argument("--amp", type=float, default=6.0, help="white-diff amplification")
    c.add_argument("--port-max-frames", type=int, default=4000)
    c.add_argument("--retail-max-frames", type=int, default=22000,
                   help="retail turbo load-stretches anchors late; ≥22000 for HOUSE")
    c.add_argument("--remote", default=DEFAULT_REMOTE)
    c.add_argument("--prune-frames", action="store_true",
                   help="drop bulk PNGs after encoding (long traces)")
    c.add_argument("--reset-trace", action="store_true",
                   help="rebuild the session's working trace from the source "
                        "(discards applied pins)")
    c.set_defaults(func=cmd_capture)

    s = sub.add_parser("serve", help="open the scrubbing editor")
    s.add_argument("--session", help="session to open by default")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=8778)
    s.add_argument("--remote", default=DEFAULT_REMOTE,
                   help="frida host:port the record panel attaches to")
    s.set_defaults(func=cmd_serve)

    a = sub.add_parser("apply", help="apply edits.jsonl pins + emit worklist")
    a.add_argument("session")
    a.add_argument("--trace", help="trace to edit (default: the session's trace)")
    a.add_argument("--auto-pin", action="store_true",
                   help="also propose pins from the stored verdict")
    a.add_argument("--dry-run", action="store_true")
    a.set_defaults(func=cmd_apply)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
