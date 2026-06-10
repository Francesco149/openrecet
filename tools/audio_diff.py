#!/usr/bin/env python3
"""
tools/audio_diff.py — port↔retail sound-trigger divergence detector.

Reads two `audio.jsonl` traces (retail emitted by the Frida agent's audio
hooks via tools/frida_capture.py; port emitted by --audio-trace / src/audio.c)
and reports where the port's SOUND TRIGGERS diverge from retail's — so a sound
gap (the item-display interaction is silent in the port today) shows up from
the traces alone, no need to boot the port and listen.

What it surfaces:

  MISSING-IN-PORT  retail plays a sound the port never does  ← the common gap
  EXTRA-IN-PORT    port plays a sound retail doesn't
  TIMING           a matched sound fires >--frame-tol frames apart

Each side is the ordered sequence of sound triggers the engine fired:

  bgm_swap  → BGM track change            identity = ("bgm", track)
  se_play   → resource SE (slot index)    identity = ("se", slot)
  se_play   → filename/voice SE (slot=-1) identity = ("se_file", path)

`fade_start` events are volume-apply side effects (port-only) and are ignored
unless --include-fades.

Both sides carry the engine `frame` index — the SAME counter the d3d/frame
capture aligns on (port: g_tick.frame_count via audio_trace_set_frame; retail:
the agent's manual frame counter). So a sound's (identity, frame) locates it
exactly. Sounds are matched PER IDENTITY by nearest frame within --frame-tol,
which keeps a recurring SE (cursor tick, page-advance) and a small constant
phase offset from manufacturing false divergences.

CLI:
    nix develop --command tools/audio_diff.py \\
        --retail tests/scenarios/<s>/out/retail/audio.jsonl \\
        --port   tests/scenarios/<s>/out/port/audio.jsonl

    # tighter timing window + machine-readable summary for trace_studio:
    tools/audio_diff.py --retail R --port P --frame-tol 1 --summary-json out.json

Exit code: 0 if aligned, 1 on any divergence, 2 on a structural error
(missing/unparseable input).

Schema + design notes: docs/findings/audio-trace-diff.md.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path


# 21 BGM filenames (src/audio.c audio_bgm_filenames[]) — display labels only,
# so a retail-only bgm_swap reads as "town.wav" not a bare track index. These
# are filenames, not assets; safe to carry here.
BGM_NAMES = [
    "retitle2010.wav", "town.wav", "sougen.wav", "cave.wav", "forest.wav",
    "ruins.wav", "boss.wav", "over.wav", "open.wav", "close.wav",
    "treasure.wav", "fanfare.wav", "ed.wav", "clear.wav", "night02.wav",
    "rival.wav", "lastboss02.wav", "lastd01.wav", "feaver.wav", "staff.wav",
    "water.wav",
]


def bgm_label(track: int) -> str:
    if 0 <= track < len(BGM_NAMES):
        return f"bgm track {track} ({BGM_NAMES[track]})"
    return f"bgm track {track}"


# ── event model ───────────────────────────────────────────────────────────


@dataclass
class SoundEvent:
    frame: int
    kind:  str            # "bgm_swap" | "se_play"
    ident: tuple          # ("bgm", track) | ("se", slot) | ("se_file", path)
    label: str
    t_ms:  int
    lineno: int


_TRIGGER_KINDS = ("bgm_swap", "se_play")


def load_events(path: Path, include_fades: bool) -> list[SoundEvent]:
    """Parse an audio.jsonl into ordered sound-trigger events. Raises
    SystemExit on a malformed row or a missing required field."""
    out: list[SoundEvent] = []
    with path.open() as f:
        for lineno, raw in enumerate(f, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                evt = json.loads(raw)
            except json.JSONDecodeError as e:
                raise SystemExit(f"{path}:{lineno}: malformed JSON: {e}")
            kind = evt.get("kind")
            if kind == "fade_start" and not include_fades:
                continue
            if kind not in _TRIGGER_KINDS:
                continue
            if "frame" not in evt:
                raise SystemExit(
                    f"{path}:{lineno}: audio event missing `frame` "
                    f"(re-capture after the frame-stamp change): {evt!r}")
            frame = int(evt["frame"])
            t_ms  = int(evt.get("t_ms", 0))
            if kind == "bgm_swap":
                track = int(evt["track"])
                ident: tuple = ("bgm", track)
                label = bgm_label(track)
            else:  # se_play
                slot = int(evt["slot"])
                name = evt.get("name")
                if slot == -1:
                    ident = ("se_file", name or "")
                    label = name or "(voice/file SE)"
                else:
                    ident = ("se", slot)
                    label = name or f"se slot {slot}"
            out.append(SoundEvent(frame, kind, ident, label, t_ms, lineno))
    return out


# ── per-identity frame alignment ───────────────────────────────────────────


@dataclass
class Pair:
    ident:   tuple
    label:   str
    r_frame: int
    p_frame: int
    delta:   int          # p_frame - r_frame


@dataclass
class Unmatched:
    ident: tuple
    label: str
    frame: int


@dataclass
class DiffResult:
    pairs:   list[Pair]      = field(default_factory=list)  # matched within tol
    missing: list[Unmatched] = field(default_factory=list)  # retail-only
    extra:   list[Unmatched] = field(default_factory=list)  # port-only
    n_retail: int = 0
    n_port:   int = 0

    @property
    def timing(self) -> list[Pair]:
        return [p for p in self.pairs if p.delta != 0]

    @property
    def diverged(self) -> bool:
        return bool(self.missing or self.extra)


def _ident_label(ident: tuple, r: list[SoundEvent],
                 p: list[SoundEvent]) -> str:
    """Prefer a retail-side label; fall back to the port's. Both carry the
    same `se_NNN_idXXXX` / path name now, so either works — retail first keeps
    output stable when only one side has the event."""
    if r:
        return r[0].label
    if p:
        return p[0].label
    if ident[0] == "bgm":
        return bgm_label(int(ident[1]))
    return str(ident)


def diff_events(retail: list[SoundEvent], port: list[SoundEvent],
                tol: int) -> DiffResult:
    """Align retail vs port per sound identity. Within an identity, sorted
    frame lists are merged: two occurrences match when |Δframe| <= tol,
    otherwise the earlier one is unmatched (missing if retail, extra if
    port). Robust to recurring SEs and a small constant phase offset."""
    res = DiffResult(n_retail=len(retail), n_port=len(port))

    r_by: dict[tuple, list[SoundEvent]] = {}
    p_by: dict[tuple, list[SoundEvent]] = {}
    for e in retail:
        r_by.setdefault(e.ident, []).append(e)
    for e in port:
        p_by.setdefault(e.ident, []).append(e)

    # Stable identity order: by earliest frame seen on either side, then ident.
    idents = set(r_by) | set(p_by)

    def first_frame(ident: tuple) -> int:
        fs = [e.frame for e in r_by.get(ident, [])] + \
             [e.frame for e in p_by.get(ident, [])]
        return min(fs) if fs else 0

    for ident in sorted(idents, key=lambda i: (first_frame(i), str(i))):
        R = sorted(r_by.get(ident, []), key=lambda e: e.frame)
        P = sorted(p_by.get(ident, []), key=lambda e: e.frame)
        label = _ident_label(ident, R, P)
        i = j = 0
        while i < len(R) and j < len(P):
            d = P[j].frame - R[i].frame
            if abs(d) <= tol:
                res.pairs.append(Pair(ident, label, R[i].frame,
                                      P[j].frame, d))
                i += 1
                j += 1
            elif R[i].frame < P[j].frame:
                res.missing.append(Unmatched(ident, label, R[i].frame))
                i += 1
            else:
                res.extra.append(Unmatched(ident, label, P[j].frame))
                j += 1
        while i < len(R):
            res.missing.append(Unmatched(ident, label, R[i].frame))
            i += 1
        while j < len(P):
            res.extra.append(Unmatched(ident, label, P[j].frame))
            j += 1

    res.missing.sort(key=lambda u: u.frame)
    res.extra.sort(key=lambda u: u.frame)
    return res


# ── report ──────────────────────────────────────────────────────────────


def verdict_str(res: DiffResult, tol: int) -> str:
    if not res.diverged:
        n = len(res.pairs)
        off = len(res.timing)
        tail = (f" ({n - off} frame-exact, {off} within ±{tol}f)"
                if off else " (frame-exact)")
        return f"ALIGNED — all {n} sound(s) present{tail}"
    return (f"DIVERGE — {len(res.missing)} missing-in-port, "
            f"{len(res.extra)} extra-in-port, {len(res.timing)} timing")


def print_report(res: DiffResult, tol: int, label: str | None,
                 max_list: int) -> None:
    if label:
        print(f"═══ audio divergence: {label} ═══")
    print(f"retail: {res.n_retail} sound event(s)   "
          f"port: {res.n_port} sound event(s)   (frame-tol=±{tol})")
    print(f"  ✓ matched {len(res.pairs)}   ⚠ timing {len(res.timing)}   "
          f"✗ missing-in-port {len(res.missing)}   "
          f"✗ extra-in-port {len(res.extra)}")

    def _list(title: str, rows: list[str]) -> None:
        if not rows:
            return
        print()
        print(title)
        for s in rows[:max_list]:
            print(f"  {s}")
        if len(rows) > max_list:
            print(f"  … +{len(rows) - max_list} more")

    _list("MISSING IN PORT — retail plays these, port is silent:",
          [f"frame {u.frame:>6}  {u.label}" for u in res.missing])
    _list("EXTRA IN PORT — port plays these, retail does not:",
          [f"frame {u.frame:>6}  {u.label}" for u in res.extra])
    _list(f"TIMING — matched (within ±{tol}f) but not frame-exact:",
          [f"retail f{p.r_frame} / port f{p.p_frame}  (Δ{p.delta:+d})  {p.label}"
           for p in sorted(res.timing, key=lambda p: abs(p.delta),
                           reverse=True)])

    print()
    print(f"VERDICT: {verdict_str(res, tol)}")


def summary_obj(res: DiffResult, tol: int) -> dict:
    return {
        "retail_events": res.n_retail,
        "port_events":   res.n_port,
        "frame_tol":     tol,
        "n_matched":     len(res.pairs),
        "n_timing":      len(res.timing),
        "n_missing":     len(res.missing),
        "n_extra":       len(res.extra),
        "verdict":       "ALIGNED" if not res.diverged else "DIVERGE",
        "missing": [{"frame": u.frame, "ident": list(u.ident),
                     "label": u.label} for u in res.missing],
        "extra":   [{"frame": u.frame, "ident": list(u.ident),
                     "label": u.label} for u in res.extra],
        "timing":  [{"ident": list(p.ident), "label": p.label,
                     "r_frame": p.r_frame, "p_frame": p.p_frame,
                     "delta": p.delta} for p in res.timing],
    }


# ── main ──────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--retail", required=True, type=Path,
                    help="retail-side audio.jsonl (frida_capture audio hooks)")
    ap.add_argument("--port", required=True, type=Path,
                    help="port-side audio.jsonl (--audio-trace / src/audio.c)")
    ap.add_argument("--frame-tol", type=int, default=2,
                    help="max |Δframe| for a matched sound before it counts "
                         "as a TIMING divergence (default %(default)d)")
    ap.add_argument("--include-fades", action="store_true",
                    help="also diff fade_start (volume-apply) events; off by "
                         "default — they're port-only side effects.")
    ap.add_argument("--label", default=None,
                    help="header label (e.g. the session/scenario name)")
    ap.add_argument("--max-list", type=int, default=80,
                    help="cap rows printed per section (default %(default)d)")
    ap.add_argument("--summary-json", type=Path, default=None,
                    help="also write the machine-readable summary here")
    ap.add_argument("--quiet", action="store_true",
                    help="print only the VERDICT line")
    args = ap.parse_args(argv)

    for side, p in (("retail", args.retail), ("port", args.port)):
        if not p.exists():
            print(f"audio_diff: {side} trace not found: {p}", file=sys.stderr)
            return 2

    retail = load_events(args.retail, args.include_fades)
    port   = load_events(args.port,   args.include_fades)
    res = diff_events(retail, port, args.frame_tol)

    if args.summary_json:
        args.summary_json.write_text(json.dumps(summary_obj(res, args.frame_tol),
                                                indent=2))

    if args.quiet:
        print(f"VERDICT: {verdict_str(res, args.frame_tol)}")
    else:
        print_report(res, args.frame_tol, args.label, args.max_list)

    return 1 if res.diverged else 0


if __name__ == "__main__":
    sys.exit(main())
