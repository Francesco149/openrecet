#!/usr/bin/env python3
"""
tools/flow_diff.py — execution + dataflow diff (the divergence drill-in).

Reads two call_trace.jsonl traces carrying DECLARED PAYLOADS (port emits them
via CALL_TRACE_BEGIN/FIELD/END; retail via the Frida reader + tools/flow/
retail_fields.json), aligns the per-frame call CHAIN by execution-order `seq`,
and names the FIRST call — in execution order — whose:

  • call diverges        ([chain]  retail called X here, port called Y / nothing)
  • inputs/state differ  ([data]   aligned call, field F: retail A vs port B)

This is the complement to render_diff.py --explain: --explain names the wrong
*draw*; flow_diff names the *logic cascade* that produced the wrong state. Unlike
call_trace_diff.py (per-frame Counter — call set/count only, data-blind,
order-blind), flow_diff walks the chain in order and compares the data moved.

TWO axes:
  • default (per-frame chain walk) — "in THIS frame, which call's data first
    diverged?" Best for a known-bad frame.
  • --field-timeline — the orthogonal axis: "across ALL frames, which STATE
    FIELD of a once-per-frame stub first stopped tracking retail, and when?"
    Scans every common frame per declared field and names the first divergent
    (frame, field) with a context window. The one-command localizer for stuck-
    counter / wrong-flag bugs (e.g. the LOAD GAME X-back soft-lock = select_phase
    pinned at 0xf). Benign-marked fields surface as ⚠ accepted (with reason),
    not silently dropped. Draw VAs (>1×/frame) are deferred to render_diff.

Frames: retail and port frame numbers differ (boot timing). Either pass an
explicit pair (--retail-frame R --port-frame P) or, for segtrace-synced captures
where the numbers align, diff the common frames (default).

Float fields compare within --eps; int/hex exact. Fields marked "benign" in the
spec (memory-layout pointers, phase-origin counters, RNG seed origin) are
compared for presence only — see docs/plans/execution-flow-trace.md.

Exit: 0 = no divergence, 1 = divergence found, 2 = structural/input error.
"""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


# ── load ───────────────────────────────────────────────────────────────────


def load_trace(path: Path) -> dict[int, list[dict]]:
    """frame -> events sorted by seq (execution order)."""
    by_frame: dict[int, list[dict]] = {}
    with path.open() as f:
        for lineno, raw in enumerate(f, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                e = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{lineno}: malformed JSON: {exc}")
            if "va" not in e or "frame" not in e:
                raise SystemExit(f"{path}:{lineno}: missing va/frame: {e!r}")
            by_frame.setdefault(int(e["frame"]), []).append(e)
    for evts in by_frame.values():
        evts.sort(key=lambda e: e.get("seq", 0))
    return by_frame


def load_names(spec: dict, csv_path: Path | None) -> dict[int, str]:
    """va -> short name. Spec names win; functions.csv fills the rest."""
    names: dict[int, str] = {}
    if csv_path and csv_path.exists():
        for line in csv_path.read_text().splitlines():
            parts = line.split(",")
            if len(parts) >= 2:
                try:
                    names[int(parts[0], 0)] = parts[1].strip()
                except ValueError:
                    continue
    for va_s, entry in spec.get("fields", {}).items():
        if isinstance(entry, dict) and entry.get("name"):
            names[int(va_s, 0) if isinstance(va_s, str) else int(va_s)] = \
                entry["name"]
    return names


def load_benign(spec: dict) -> set[tuple[int, str]]:
    """Set of (va, field-name) marked benign — compared for presence only."""
    out: set[tuple[int, str]] = set()
    for va_s, entry in spec.get("fields", {}).items():
        va = int(va_s, 0) if isinstance(va_s, str) else int(va_s)
        for fld in (entry.get("fields", []) if isinstance(entry, dict) else []):
            if fld.get("benign"):
                out.add((va, fld["name"]))
    return out


def load_field_reasons(spec: dict) -> dict[tuple[int, str], str]:
    """(va, field-name) -> human reason, for fields marked benign with a
    `"reason"`. Surfaced by --field-timeline so a benign divergence reads as
    *accepted-with-cause* instead of being silently dropped."""
    out: dict[tuple[int, str], str] = {}
    for va_s, entry in spec.get("fields", {}).items():
        if not isinstance(entry, dict):
            continue
        va = int(va_s, 0) if isinstance(va_s, str) else int(va_s)
        for fld in entry.get("fields", []):
            if fld.get("benign") and fld.get("reason"):
                out[(va, fld["name"])] = fld["reason"]
    return out


def load_field_order(spec: dict) -> dict[int, list[str]]:
    """va -> declared field-name order (for stable timeline rows)."""
    out: dict[int, list[str]] = {}
    for va_s, entry in spec.get("fields", {}).items():
        if not isinstance(entry, dict):
            continue
        va = int(va_s, 0) if isinstance(va_s, str) else int(va_s)
        out[va] = [f["name"] for f in entry.get("fields", []) if "name" in f]
    return out


def load_chain_benign(spec: dict) -> set[int]:
    """VAs whose *position* in the call chain is benign — excluded from chain
    alignment so a legitimate floating call (a clock read, an order-irrelevant
    helper) can't masquerade as a [chain] divergence and hide the real one.
    Marked `"chain_benign": true` at the entry level in retail_fields.json."""
    out: set[int] = set()
    for va_s, entry in spec.get("fields", {}).items():
        if isinstance(entry, dict) and entry.get("chain_benign"):
            out.add(int(va_s, 0) if isinstance(va_s, str) else int(va_s))
    return out


# ── compare ────────────────────────────────────────────────────────────────


@dataclass
class Divergence:
    frame_r: int
    frame_p: int
    kind:    str          # "chain" | "data" | "payload"
    va:      int
    detail:  str
    seq_r:   int = -1
    seq_p:   int = -1


def _field_diverges(a, b, eps: float) -> bool:
    if isinstance(a, bool) or isinstance(b, bool):
        return a != b
    if isinstance(a, float) or isinstance(b, float):
        try:
            return abs(float(a) - float(b)) > max(eps, eps * max(abs(float(a)),
                                                                 abs(float(b))))
        except (TypeError, ValueError):
            return a != b
    return a != b          # ints / hex strings: exact


def _compare_payload(va: int, rev: dict, pev: dict, eps: float,
                     benign: set[tuple[int, str]]) -> tuple[str, object, object] | None:
    """First divergent field (in declared order), or None."""
    rf = rev.get("f")
    pf = pev.get("f")
    if (rf is None) != (pf is None):
        return ("<payload>", "present" if rf is not None else "absent",
                "present" if pf is not None else "absent")
    if rf is None:
        return None
    for name in rf:                       # dict order = declared field order
        if (va, name) in benign:
            continue
        a, b = rf[name], pf.get(name)
        if name not in pf or _field_diverges(a, b, eps):
            return (name, a, b)
    return None


def diff_frame(fr: int, fp: int, retail: list[dict], port: list[dict],
               eps: float, names: dict[int, str],
               benign: set[tuple[int, str]]) -> Divergence | None:
    """First divergence in execution order for one aligned frame pair."""
    r_vas = [e["va"] for e in retail]
    p_vas = [e["va"] for e in port]
    sm = difflib.SequenceMatcher(a=r_vas, b=p_vas, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for i, j in zip(range(i1, i2), range(j1, j2)):
                d = _compare_payload(retail[i]["va"], retail[i], port[j],
                                     eps, benign)
                if d:
                    name, a, b = d
                    va = retail[i]["va"]
                    return Divergence(
                        fr, fp, "data", va,
                        f'field "{name}": retail={a} port={b}',
                        retail[i].get("seq", -1), port[j].get("seq", -1))
        else:
            # chain divergence: a call present on one side only, here.
            if i2 > i1:
                e = retail[i1]
                return Divergence(
                    fr, fp, "chain", e["va"],
                    f"retail called {fmt_va(e['va'], names)} that the port "
                    f"did not (tag={tag})", seq_r=e.get("seq", -1))
            e = port[j1]
            return Divergence(
                fr, fp, "chain", e["va"],
                f"port called {fmt_va(e['va'], names)} that retail did not "
                f"(tag={tag})", seq_p=e.get("seq", -1))
    return None


def fmt_va(va: int, names: dict[int, str]) -> str:
    n = names.get(va)
    return f"{va:#x}({n})" if n else f"{va:#x}"


# ── field timeline (the per-field state-divergence localizer) ───────────────
#
# diff_frame answers "first call whose data diverged in THIS frame" — great for
# a known-bad frame, but a flat per-frame walk. For a *state-machine stub* logged
# once per frame (a CALL_TRACE_BEGIN_STUB dump of N globals), the question is the
# other axis: "across ALL frames, which FIELD first stopped tracking retail, and
# when?". --field-timeline scans every common frame per declared field and names
# the first divergent (frame, field), with a context window — the one-command
# localizer for stuck-counter / wrong-state-flag bugs (e.g. the LOAD GAME X-back
# soft-lock: select_phase pinned at 0xf, surfaced here as the first ✗ field).


@dataclass
class FieldTrack:
    va:      int
    field:   str
    benign:  bool
    reason:  str
    samples: list                       # [(frame, occ, r_val, p_val)] compared
    first:   tuple | None = None        # (frame, occ, r_val, p_val) first ✗
    n_div:   int = 0


def _va_occurrences(evts: list[dict], va: int) -> list[dict]:
    return [e for e in evts if e.get("va") == va and e.get("f") is not None]


def build_field_timeline(va: int, retail: dict[int, list[dict]],
                         port: dict[int, list[dict]], common: list[int],
                         eps: float, benign: set[tuple[int, str]],
                         reasons: dict[tuple[int, str], str],
                         field_order: list[str]) -> tuple[list[FieldTrack], int]:
    """Per-field tracks for one VA over the common frames. Returns (tracks,
    max_occ_per_frame). Aligns the i-th retail occurrence with the i-th port
    occurrence (so once-per-frame stubs are the clean occ==0 case)."""
    # discover field set (declared order first, then any extras seen).
    seen: list[str] = list(field_order)
    for fr in common:
        for e in _va_occurrences(retail.get(fr, []), va):
            for k in e["f"]:
                if k not in seen:
                    seen.append(k)
    tracks = {name: FieldTrack(va, name, (va, name) in benign,
                               reasons.get((va, name), ""), [])
              for name in seen}
    max_occ = 0
    for fr in common:
        rocc = _va_occurrences(retail.get(fr, []), va)
        pocc = _va_occurrences(port.get(fr, []), va)
        max_occ = max(max_occ, len(rocc), len(pocc))
        for occ, (re, pe) in enumerate(zip(rocc, pocc)):
            rf, pf = re["f"], pe["f"]
            for name, tr in tracks.items():
                if name not in rf or name not in pf:
                    continue
                a, b = rf[name], pf[name]
                tr.samples.append((fr, occ, a, b))
                if _field_diverges(a, b, eps):
                    tr.n_div += 1
                    if tr.first is None:
                        tr.first = (fr, occ, a, b)
    ordered = [tracks[n] for n in seen]
    return ordered, max_occ


def print_field_timeline(va: int, names: dict[int, str], tracks: list[FieldTrack],
                         n_frames: int, frame_lo: int, frame_hi: int,
                         max_occ: int, window: int, full: bool) -> bool:
    """Print one VA's timeline. Returns True if a REAL (non-benign) field
    diverged."""
    print("═" * 78)
    occ_note = f"   occurrences/frame: up to {max_occ}" if max_occ > 1 else ""
    print(f"field timeline: {fmt_va(va, names)}   "
          f"{n_frames} common frames [{frame_lo}..{frame_hi}]{occ_note}")
    if not tracks:
        print("  (no field payloads for this va in both traces)")
        return False
    namew = max(len(t.field) for t in tracks)
    real_div = False
    detail_for: list[FieldTrack] = []
    for t in tracks:
        if not t.samples:
            status, extra = "· no overlap", ""
        elif t.first is None:
            status, extra = "✓ aligned", ""
        elif t.benign:
            fr, occ, a, b = t.first
            status = "⚠ benign-accepted"
            extra = f"@{fr}  {a}→{b}" + (f"  ({t.reason})" if t.reason else "")
        else:
            fr, occ, a, b = t.first
            status = "✗ DIVERGES"
            extra = f"first @{fr}  retail={a} port={b}  ({t.n_div} frame(s))"
            real_div = True
            detail_for.append(t)
        print(f"  {t.field:<{namew}}  {status:<18} {extra}")

    # context window beneath the table for each real divergence (and, with
    # --timeline-full, for every field).
    show = tracks if full else detail_for
    for t in show:
        if not t.samples:
            continue
        print(f"  ── {t.field} ──")
        if full and t.first is None:
            rows = t.samples
        else:
            fr0 = t.first[0] if t.first else t.samples[0][0]
            rows = [s for s in t.samples if abs(s[0] - fr0) <= window]
        occ_col = max_occ > 1
        hdr = "      frame  " + ("occ  " if occ_col else "") + "retail   port"
        print(hdr)
        for fr, occ, a, b in rows:
            mark = "  ←" if (t.first and (fr, occ) == (t.first[0], t.first[1])) else ""
            occ_s = f"{occ:<4} " if occ_col else ""
            print(f"      {fr:<6} {occ_s}{str(a):<8} {str(b)}{mark}")
    return real_div


def _max_occ_per_frame(va: int, retail: dict[int, list[dict]],
                       port: dict[int, list[dict]], common: list[int]) -> int:
    m = 0
    for fr in common:
        m = max(m, len(_va_occurrences(retail.get(fr, []), va)),
                len(_va_occurrences(port.get(fr, []), va)))
    return m


def run_field_timeline(args, retail, port, names, benign, reasons,
                       field_order: dict[int, list[str]],
                       spec_vas: list[int]) -> int:
    if args.retail_frame is not None:
        common = [args.retail_frame]            # single explicit frame
    else:
        common = sorted(set(retail) & set(port))
    if not common:
        raise SystemExit("no common frames to build a timeline")

    def has_payload(tr, va):
        return any(_va_occurrences(evts, va) for evts in tr.values())

    if args.timeline_va is not None:
        va = int(args.timeline_va, 0)
        vas = [va]
        # A draw/helper VA logged many times per frame can't be aligned by
        # occurrence index (the streams reorder) — that's render_diff's job.
        # Warn but proceed; the position-zip is still a usable rough cut.
        if _max_occ_per_frame(va, retail, port, common) > 1:
            print(f"  ! {fmt_va(va, names)} fires >1×/frame — field-timeline "
                  f"aligns by occurrence INDEX (streams may reorder). For draw "
                  f"streams use render_diff.py --explain instead.\n")
    else:
        # Auto: every spec'd field-bearing VA present in BOTH — but only the
        # once-per-frame state stubs (the localizer's domain). Multi-occurrence
        # VAs (draw helpers) are skipped with a pointer to render_diff.
        vas, skipped = [], []
        for va in spec_vas:
            if not (has_payload(retail, va) and has_payload(port, va)):
                continue
            (vas if _max_occ_per_frame(va, retail, port, common) <= 1
             else skipped).append(va)
        if skipped:
            print("  (skipped >1×/frame draw VAs — use render_diff.py: "
                  + ", ".join(fmt_va(v, names) for v in skipped) + ")\n")
        if not vas:
            raise SystemExit("no once-per-frame field-bearing VA carries "
                             "payloads in both traces; pass --timeline-va "
                             "explicitly, or check coverage")

    any_real = False
    benign_hit = False
    for va in vas:
        tracks, max_occ = build_field_timeline(
            va, retail, port, common, args.eps, benign, reasons,
            field_order.get(va, []))
        real = print_field_timeline(
            va, names, tracks, len(common), common[0], common[-1],
            max_occ, args.timeline_window, args.timeline_full)
        any_real = any_real or real
        benign_hit = benign_hit or any(t.benign and t.first for t in tracks)
    print("═" * 78)
    if any_real:
        print("verdict: ✗ field divergence — see ✗ rows above (first frame/field)")
    elif benign_hit:
        print("verdict: ✓ all fields aligned (benign-accepted divergences only)")
    else:
        print("verdict: ✓ all fields aligned")
    return 1 if any_real else 0


# ── main ───────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--retail", required=True, type=Path)
    ap.add_argument("--port", required=True, type=Path)
    ap.add_argument("--retail-frame", type=int, default=None,
                    help="diff this single retail frame against --port-frame")
    ap.add_argument("--port-frame", type=int, default=None)
    ap.add_argument("--eps", type=float, default=1e-4,
                    help="float field tolerance (abs+relative, default %(default)g)")
    ap.add_argument("--spec", type=Path,
                    default=ROOT / "tools" / "flow" / "retail_fields.json",
                    help="field spec (for names + benign-field marks)")
    ap.add_argument("--names-csv", type=Path,
                    default=ROOT / "docs" / "decompiled" / "functions.csv",
                    help="optional va,name CSV to label un-spec'd calls")
    ap.add_argument("--all", action="store_true",
                    help="report every diverging frame, not stop at the first")
    ap.add_argument("--mapped-only", action="store_true",
                    help="restrict the chain comparison to VAs the PORT emits "
                         "(its probed/mapped set). Use this while port call-"
                         "coverage < retail: an un-probed retail call is a "
                         "coverage gap (track via call_trace_diff), not a "
                         "divergence. Without it the full chain is compared.")
    ap.add_argument("--field-timeline", action="store_true",
                    help="per-field state-divergence localizer: scan ALL common "
                         "frames per declared field of each field-bearing stub "
                         "and name the first divergent (frame, field) with a "
                         "context window. The one-command answer to 'which state "
                         "field stopped tracking retail, and when?'. Benign-"
                         "marked fields surface as ⚠ accepted (with reason), not "
                         "dropped. Complements the default per-frame chain walk.")
    ap.add_argument("--timeline-va", default=None,
                    help="restrict --field-timeline to this VA (e.g. 0x49a59e); "
                         "default = every field-bearing spec VA present in both.")
    ap.add_argument("--timeline-window", type=int, default=2,
                    help="frames of context to print on each side of a "
                         "divergence (--field-timeline; default %(default)d)")
    ap.add_argument("--timeline-full", action="store_true",
                    help="--field-timeline: dump every frame's values per field, "
                         "not just a window around the first divergence")
    args = ap.parse_args(argv)

    for p in (args.retail, args.port):
        if not p.exists():
            raise SystemExit(f"trace not found: {p}")

    spec = json.loads(args.spec.read_text()) if args.spec.exists() else {}
    names = load_names(spec, args.names_csv)
    benign = load_benign(spec)
    chain_benign = load_chain_benign(spec)
    reasons = load_field_reasons(spec)
    field_order = load_field_order(spec)

    retail = load_trace(args.retail)
    port = load_trace(args.port)

    if args.field_timeline:
        # Timeline mode reads payloads by VA directly; it does not walk the
        # call chain, so the chain-only filters (mapped-only, chain_benign)
        # don't apply. Validate the explicit-frame flag pairing reuses the
        # same rule as the chain path below.
        if (args.retail_frame is None) != (args.port_frame is None):
            raise SystemExit("--retail-frame and --port-frame must be given together")
        spec_vas = list(field_order.keys())
        return run_field_timeline(args, retail, port, names, benign, reasons,
                                  field_order, spec_vas)

    if args.mapped_only:
        mapped = {e["va"] for evts in port.values() for e in evts}
        retail = {f: [e for e in evts if e["va"] in mapped]
                  for f, evts in retail.items()}
        port = {f: [e for e in evts if e["va"] in mapped]
                for f, evts in port.items()}

    # Drop position-benign VAs (clock reads etc.) from BOTH sides so a benign
    # reorder never wins the "first divergence" race over a real one.
    if chain_benign:
        retail = {f: [e for e in evts if e["va"] not in chain_benign]
                  for f, evts in retail.items()}
        port = {f: [e for e in evts if e["va"] not in chain_benign]
                for f, evts in port.items()}

    if (args.retail_frame is None) != (args.port_frame is None):
        raise SystemExit("--retail-frame and --port-frame must be given together")
    if args.retail_frame is not None:
        pairs = [(args.retail_frame, args.port_frame)]
    else:
        common = sorted(set(retail) & set(port))
        if not common:
            raise SystemExit(
                "no common frame numbers; pass --retail-frame/--port-frame "
                f"(retail has {sorted(retail)[:8]}…, port has {sorted(port)[:8]}…)")
        pairs = [(f, f) for f in common]

    found = False
    for fr, fp in pairs:
        if fr not in retail:
            print(f"  ! retail frame {fr} absent"); continue
        if fp not in port:
            print(f"  ! port frame {fp} absent"); continue
        d = diff_frame(fr, fp, retail[fr], port[fp], args.eps, names, benign)
        print("=" * 78)
        if d is None:
            print(f"FRAME retail={fr} port={fp}: ✓ chain + data aligned "
                  f"({len(retail[fr])} vs {len(port[fp])} calls)")
            continue
        found = True
        print(f"FRAME retail={fr} port={fp}: ✗ first divergence")
        loc = []
        if d.seq_r >= 0:
            loc.append(f"r.seq={d.seq_r}")
        if d.seq_p >= 0:
            loc.append(f"p.seq={d.seq_p}")
        print(f"  [{d.kind}] {fmt_va(d.va, names)}  ({', '.join(loc)})")
        print(f"      {d.detail}")
        if not args.all:
            break

    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
