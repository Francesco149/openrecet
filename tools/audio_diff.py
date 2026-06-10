#!/usr/bin/env python3
"""
tools/audio_diff.py — port↔retail sound-trigger divergence detector.

Reads two `audio.jsonl` traces (retail emitted by the Frida agent's audio
hooks via tools/frida_capture.py; port emitted by --audio-trace / src/audio.c)
and reports where the port's SOUND TRIGGERS diverge from retail's — so a sound
gap (the item-display interaction is silent in the port today) shows up from
the traces alone, no need to boot the port and listen.

What it surfaces, per distinct sound:

  MISSING-IN-PORT  retail triggers a sound the port triggers fewer/zero times
  EXTRA-IN-PORT    port triggers a sound retail triggers fewer/zero times
  MATCHED          same trigger count on both sides

A "sound" is identified by WHAT plays, not when:

  bgm_swap  → BGM track change            identity = ("bgm", track)
  se_play   → resource SE (slot index)    identity = ("se", slot)
  se_play   → filename/voice SE (slot=-1) identity = ("se_file", path)

`fade_start` events are volume-apply side effects (port-only) and are ignored
unless --include-fades.

WHY identity+count, not frame alignment: both sides stamp the engine `frame`,
but the absolute frame ORIGINS differ and the offset is NOT constant across a
trace that spans a load — retail plays an intro/load the port skips, so the
post-load offset (~thousands of frames) differs from the pre-load one. Matching
on identity+count is immune to that phase/load skew and answers the real
question ("which sounds is the port missing, and how many times") exactly.
Frames are reported only as context (raw, per-side). Precise per-event timing
alignment is a separate, label-space concern (the trace-studio coordinate
transform); see docs/findings/audio-trace-diff.md.

CLI:
    nix develop --command tools/audio_diff.py \\
        --retail runs/trace-studio/<s>/retail/audio.jsonl \\
        --port   runs/trace-studio/<s>/port/audio.jsonl

    # convenience: resolve both sides from a trace-studio session by name
    tools/audio_diff.py --session item-display-2

    # machine-readable summary (e.g. for trace_studio triage):
    tools/audio_diff.py --session item-display-2 --summary-json out.json

Exit code: 0 if aligned (no missing/extra), 1 on any divergence, 2 on a
structural error (missing/unparseable input).
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SESSIONS_ROOT = ROOT / "runs" / "trace-studio"


# 21 BGM filenames (src/audio.c audio_bgm_filenames[]) — display labels only,
# so a bgm_swap reads as "town.wav" not a bare track index. Filenames, not
# assets; safe to carry here.
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
            frame = int(evt.get("frame", -1))
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


# ── identity + count diff ───────────────────────────────────────────────


@dataclass
class IdentDiff:
    ident:    tuple
    label:    str
    r_frames: list[int] = field(default_factory=list)   # retail trigger frames
    p_frames: list[int] = field(default_factory=list)   # port trigger frames

    @property
    def r_count(self) -> int: return len(self.r_frames)

    @property
    def p_count(self) -> int: return len(self.p_frames)

    @property
    def missing(self) -> int: return max(0, self.r_count - self.p_count)

    @property
    def extra(self) -> int: return max(0, self.p_count - self.r_count)

    @property
    def status(self) -> str:
        if self.missing: return "missing"
        if self.extra:   return "extra"
        return "matched"


@dataclass
class DiffResult:
    idents:   list[IdentDiff] = field(default_factory=list)
    n_retail: int = 0
    n_port:   int = 0

    @property
    def missing_idents(self) -> list[IdentDiff]:
        return [d for d in self.idents if d.status == "missing"]

    @property
    def extra_idents(self) -> list[IdentDiff]:
        return [d for d in self.idents if d.status == "extra"]

    @property
    def matched_idents(self) -> list[IdentDiff]:
        return [d for d in self.idents if d.status == "matched"]

    @property
    def total_missing(self) -> int:
        return sum(d.missing for d in self.idents)

    @property
    def total_extra(self) -> int:
        return sum(d.extra for d in self.idents)

    @property
    def diverged(self) -> bool:
        return self.total_missing > 0 or self.total_extra > 0


def diff_events(retail: list[SoundEvent],
                port: list[SoundEvent]) -> DiffResult:
    """Group both sides by sound identity and compare trigger COUNTS. Frame
    origins / load skew don't matter — a missing sound is a count deficit on
    the port side. Idents are ordered by earliest retail frame, then port."""
    res = DiffResult(n_retail=len(retail), n_port=len(port))

    by: dict[tuple, IdentDiff] = {}

    def get(ident: tuple, label: str) -> IdentDiff:
        d = by.get(ident)
        if d is None:
            d = IdentDiff(ident, label)
            by[ident] = d
        return d

    for e in retail:
        get(e.ident, e.label).r_frames.append(e.frame)
    for e in port:
        get(e.ident, e.label).p_frames.append(e.frame)

    for d in by.values():
        d.r_frames.sort()
        d.p_frames.sort()

    def first(d: IdentDiff) -> int:
        fs = d.r_frames or d.p_frames
        return fs[0] if fs else 0

    res.idents = sorted(by.values(), key=lambda d: (first(d), str(d.ident)))
    return res


# ── report ──────────────────────────────────────────────────────────────


def _frames_str(frames: list[int], cap: int) -> str:
    if not frames:
        return "—"
    shown = ",".join(str(f) for f in frames[:cap])
    if len(frames) > cap:
        shown += f",+{len(frames) - cap}"
    return shown


def verdict_str(res: DiffResult) -> str:
    if not res.diverged:
        return (f"ALIGNED — every sound retail triggers, the port triggers "
                f"the same number of times ({len(res.matched_idents)} sound(s))")
    return (f"DIVERGE — port is missing {res.total_missing} trigger(s) across "
            f"{len(res.missing_idents)} sound(s); "
            f"{res.total_extra} extra across {len(res.extra_idents)} sound(s)")


def print_report(res: DiffResult, label: str | None, show_frames: int) -> None:
    if label:
        print(f"═══ sound-trigger divergence: {label} ═══")
    print(f"retail: {res.n_retail} trigger(s)   port: {res.n_port} trigger(s)")
    print(f"  ✗ missing-in-port {res.total_missing} (over "
          f"{len(res.missing_idents)} sound(s))   "
          f"✗ extra-in-port {res.total_extra} (over "
          f"{len(res.extra_idents)} sound(s))   "
          f"✓ matched {len(res.matched_idents)}")

    def _section(title: str, rows: list[IdentDiff], who: str) -> None:
        if not rows:
            return
        print()
        print(title)
        for d in rows:
            delta = d.missing if who == "missing" else d.extra
            print(f"  {d.label:<34} retail ×{d.r_count}  port ×{d.p_count}"
                  f"   → {delta} {who}")
            print(f"      retail frames: {_frames_str(d.r_frames, show_frames)}")
            print(f"      port   frames: {_frames_str(d.p_frames, show_frames)}")

    _section("MISSING IN PORT — retail triggers these, the port triggers fewer:",
             res.missing_idents, "missing")
    _section("EXTRA IN PORT — port triggers these, retail triggers fewer:",
             res.extra_idents, "extra")

    if res.matched_idents:
        print()
        print("MATCHED — same trigger count on both sides:")
        for d in res.matched_idents:
            print(f"  {d.label:<34} ×{d.r_count}")

    print()
    print(f"VERDICT: {verdict_str(res)}")


def summary_obj(res: DiffResult) -> dict:
    def row(d: IdentDiff) -> dict:
        return {"ident": list(d.ident), "label": d.label,
                "retail_count": d.r_count, "port_count": d.p_count,
                "missing": d.missing, "extra": d.extra,
                "retail_frames": d.r_frames, "port_frames": d.p_frames}
    return {
        "retail_events":   res.n_retail,
        "port_events":     res.n_port,
        "total_missing":   res.total_missing,
        "total_extra":     res.total_extra,
        "n_missing_sounds": len(res.missing_idents),
        "n_extra_sounds":   len(res.extra_idents),
        "n_matched_sounds": len(res.matched_idents),
        "verdict":         "ALIGNED" if not res.diverged else "DIVERGE",
        "missing": [row(d) for d in res.missing_idents],
        "extra":   [row(d) for d in res.extra_idents],
        "matched": [row(d) for d in res.matched_idents],
    }


# ── main ──────────────────────────────────────────────────────────────────


def _resolve_paths(args) -> tuple[Path, Path]:
    if args.session:
        sess = SESSIONS_ROOT / args.session
        return sess / "retail" / "audio.jsonl", sess / "port" / "audio.jsonl"
    if not (args.retail and args.port):
        raise SystemExit("audio_diff: give --session NAME, or both "
                         "--retail and --port")
    return args.retail, args.port


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--session", default=None,
                    help="trace-studio session name; resolves retail/ + port/ "
                         "audio.jsonl under runs/trace-studio/<name>/")
    ap.add_argument("--retail", type=Path, default=None,
                    help="retail-side audio.jsonl (frida_capture audio hooks)")
    ap.add_argument("--port", type=Path, default=None,
                    help="port-side audio.jsonl (--audio-trace / src/audio.c)")
    ap.add_argument("--include-fades", action="store_true",
                    help="also count fade_start (volume-apply) events; off by "
                         "default — they're port-only side effects.")
    ap.add_argument("--label", default=None,
                    help="header label (defaults to the session name)")
    ap.add_argument("--show-frames", type=int, default=12,
                    help="cap trigger frames listed per sound (default %(default)d)")
    ap.add_argument("--summary-json", type=Path, default=None,
                    help="also write the machine-readable summary here")
    ap.add_argument("--quiet", action="store_true",
                    help="print only the VERDICT line")
    args = ap.parse_args(argv)

    retail_path, port_path = _resolve_paths(args)
    for side, p in (("retail", retail_path), ("port", port_path)):
        if not p.exists():
            print(f"audio_diff: {side} trace not found: {p}", file=sys.stderr)
            return 2

    retail = load_events(retail_path, args.include_fades)
    port   = load_events(port_path,   args.include_fades)
    res = diff_events(retail, port)

    if args.summary_json:
        args.summary_json.write_text(json.dumps(summary_obj(res), indent=2))

    label = args.label or args.session
    if args.quiet:
        print(f"VERDICT: {verdict_str(res)}")
    else:
        print_report(res, label, args.show_frames)

    return 1 if res.diverged else 0


if __name__ == "__main__":
    sys.exit(main())
