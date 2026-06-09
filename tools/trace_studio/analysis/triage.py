"""analysis/triage.py — one command from a captured session to a ranked divergence
report (docs/audits/2026-06-09-methodology-audit.md T1).

Composes what already exists — the stored pixel-diff curve, state.jsonl, the
phase/RNG verdict, flow_diff --field-timeline — into ONE machine-readable
triage.json + a short human summary, so the divergence hunt starts from a single
entry point instead of hand-chaining 4 tools.

Exit codes: 0 = clean (nothing over threshold, verdict ok) · 1 = divergence
found · 2 = session unusable for triage (no diff data).
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from ..paths import ROOT

# Defaults tuned for PINNED traces (the standing policy). The primary signal is
# `gt8` — pixels with any channel |Δ| > 8, the project's bit-clean criterion
# ("0 px >8/ch") — when the diff curve carries it (sessions captured after the
# stat landed); `differ` (ANY 1-LSB pixel) + meanabs are the fallback for older
# sessions and are noise-dominated on real captures. Unpinned traces should be
# pinned, not given a bigger floor.
DEFAULT_GT8_PX = 16
DEFAULT_DIFFER_PX = 64
DEFAULT_MEANABS = 0.02
FIELD_TIMELINE_TIMEOUT = 180


def _load_jsonl(p: Path) -> list[dict]:
    if not p.is_file():
        return []
    out = []
    for ln in p.read_text().splitlines():
        s = ln.strip()
        if s:
            try:
                out.append(json.loads(s))
            except json.JSONDecodeError:
                pass
    return out


def _field_timeline(port_dir: Path, retail_dir: Path) -> dict:
    rp, pp = retail_dir / "call_trace.jsonl", port_dir / "call_trace.jsonl"
    if not (rp.exists() and pp.exists()):
        return {"available": False}
    try:
        r = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "flow_diff.py"),
             "--retail", str(rp), "--port", str(pp), "--field-timeline"],
            capture_output=True, text=True, cwd=str(ROOT),
            timeout=FIELD_TIMELINE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"available": True, "timeout": FIELD_TIMELINE_TIMEOUT}
    return {"available": True, "exit_code": r.returncode,
            "text": r.stdout + (("\n[stderr]\n" + r.stderr) if r.stderr else "")}


def run_triage(sess_dir: Path, differ_px: int = DEFAULT_DIFFER_PX,
               meanabs: float = DEFAULT_MEANABS, gt8_px: int = DEFAULT_GT8_PX,
               skip: int = 0, field_timeline: bool = True) -> tuple[dict, int]:
    """Build the triage dict for a session. Returns (triage, exit_code)."""
    sess_dir = Path(sess_dir)
    mf = sess_dir / "session.json"
    if not mf.exists():
        return {"error": "no session.json"}, 2
    m = json.loads(mf.read_text())
    coords = m.get("coords") or {}
    window_start = int(coords.get("window_start",
                                  (m.get("caprange") or [0, 0])[0]))
    stride = max(1, int(coords.get("stride", m.get("stride") or 1)))

    t: dict = {
        "session": m.get("session"),
        "caprange": m.get("caprange"),
        "stride": stride,
        "n_frames": m.get("n_frames"),
        "n_frames_retail": m.get("n_frames_retail"),
        "thresholds": {"differ_px": differ_px, "meanabs": meanabs,
                       "gt8_px": gt8_px, "skip": skip},
        "problems": [],
    }
    if m.get("capture_error"):
        t["problems"].append({"kind": "capture_error", "msg": m["capture_error"]})
    if m.get("kept_count_mismatch"):
        t["problems"].append({"kind": "kept_count_mismatch",
                              **m["kept_count_mismatch"]})

    # ── pixel-diff curve → first/worst divergence ────────────────────────────
    per = (m.get("diff") or {}).get("per_frame") or []
    if not per:
        t["problems"].append({"kind": "no_diff_data",
                              "msg": "session has no retail-vs-port diff curve "
                                     "(port-only, or capture failed)"})
        return t, 2
    has_gt8 = any("gt8" in d for d in per)
    over = []
    first = worst = None

    def sev(d):                       # severity for the "worst" rank
        return d.get("gt8", d["differ"])

    for i, d in enumerate(per):
        if i < skip:
            continue
        hit = (d["gt8"] > gt8_px) if "gt8" in d \
            else (d["differ"] > differ_px or d["meanabs"] > meanabs)
        if hit:
            over.append(d)
            if first is None:
                first = {**d, "ordinal": i}
            if worst is None or sev(d) > sev(worst):
                worst = {**d, "ordinal": i}
    t["diff"] = {
        "frames": len(per),
        "metric": "gt8" if has_gt8 else "differ/meanabs (pre-gt8 session)",
        "over_threshold": len(over),
        "first": first,
        "worst": worst,
        "clean": first is None,
    }

    # ── state + nearest anchor at the first divergence ───────────────────────
    if first is not None:
        k = first["ordinal"]
        state = _load_jsonl(sess_dir / "state.jsonl")
        rows = [r for r in state if isinstance(r.get("frame"), int)
                and r["frame"] <= k]
        if rows:
            t["state_at_first"] = rows[-1]
        anchors = (m.get("anchors") or {}).get("retail") or []
        prior = [a for a in anchors
                 if a.get("frame") is not None and a["frame"] <= k * stride]
        if prior:
            t["anchor_before_first"] = prior[-1]

    # ── verdict (stored, else run if the call traces are on disk) ────────────
    v = m.get("verdict")
    if not v or v.get("available") is False:
        from . import verdict as verdict_mod
        v = verdict_mod.run_verdict(sess_dir / "port", sess_dir / "retail")
    t["verdict"] = v

    # ── field timeline (the WHICH-FIELD/WHEN drill) ──────────────────────────
    if field_timeline and first is not None:
        ft = _field_timeline(sess_dir / "port", sess_dir / "retail")
        if ft.get("text"):
            (sess_dir / "triage-field-timeline.txt").write_text(ft["text"])
            ft["file"] = "triage-field-timeline.txt"
            ft["tail"] = ft.pop("text")[-2000:]
        t["field_timeline"] = ft

    rc = 0 if (first is None and not t["problems"]
               and (not v or v.get("exit_code") in (0, None))) else 1
    return t, rc


def _fmt_state(row: dict) -> str:
    bits = []
    for side in ("port", "retail"):
        s = row.get(side) or {}
        keep = {k: s[k] for k in ("db054", "rng", "rngcalls", "poct", "coct",
                                  "anim", "px", "pz") if k in s}
        if keep:
            bits.append(f"{side}={keep}")
    return " · ".join(bits) or "(no state row)"


def print_summary(t: dict, rc: int) -> None:
    p = print
    p(f"triage: {t.get('session')}  window={t.get('caprange')} "
      f"stride={t.get('stride')}  frames={t.get('n_frames')}")
    for pr in t.get("problems", []):
        p(f"  PROBLEM [{pr.get('kind')}]: "
          f"{pr.get('msg', json.dumps({k: v for k, v in pr.items() if k != 'kind'}))}")
    d = t.get("diff")
    if d:
        def fmt(x):
            g = f" gt8={x['gt8']}px" if "gt8" in x else ""
            return (f"@ ordinal {x['ordinal']} (label {x['frame']}):"
                    f"{g} differ={x['differ']}px mean {x['meanabs']}")
        if d["clean"]:
            p(f"  pixel diff: CLEAN — 0/{d['frames']} frames over threshold "
              f"(metric: {d['metric']})")
        else:
            p(f"  pixel diff: {d['over_threshold']}/{d['frames']} frames over "
              f"threshold (metric: {d['metric']})")
            p(f"    first {fmt(d['first'])}")
            p(f"    worst {fmt(d['worst'])}")
        if any(pr.get("kind") == "kept_count_mismatch"
               for pr in t.get("problems", [])):
            p("    NOTE: kept-count mismatch — ordinal pairing (and so this "
              "curve) may be shifted; fix the seam alignment first")
    if t.get("anchor_before_first"):
        a = t["anchor_before_first"]
        p(f"  anchor ≤ first: {a.get('anchor')} @ {a.get('frame')}")
    if t.get("state_at_first"):
        p(f"  state @ first: {_fmt_state(t['state_at_first'])}")
    v = t.get("verdict") or {}
    if v.get("available") is False:
        p("  verdict: no call-trace on this session — re-capture with "
          "--call-trace for the phase/RNG verdict")
    elif "text" in v:
        head = [l for l in v["text"].splitlines() if l.strip()][:6]
        p(f"  verdict (exit {v.get('exit_code')}):")
        for l in head:
            p(f"    {l}")
    ft = t.get("field_timeline")
    if ft:
        if ft.get("file"):
            p(f"  field-timeline → {ft['file']} (exit {ft.get('exit_code')})")
        elif ft.get("timeout"):
            p(f"  field-timeline: timed out after {ft['timeout']}s (run "
              f"flow_diff --field-timeline by hand, filtered by --timeline-va)")
        elif ft.get("available") is False:
            p("  field-timeline: no call traces on disk")
    p(f"  → {'CLEAN' if rc == 0 else 'DIVERGENCE' if rc == 1 else 'UNUSABLE'}"
      f" (exit {rc}); triage.json written")
