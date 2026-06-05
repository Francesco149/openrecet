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
    args = ap.parse_args(argv)

    for p in (args.retail, args.port):
        if not p.exists():
            raise SystemExit(f"trace not found: {p}")

    spec = json.loads(args.spec.read_text()) if args.spec.exists() else {}
    names = load_names(spec, args.names_csv)
    benign = load_benign(spec)
    chain_benign = load_chain_benign(spec)

    retail = load_trace(args.retail)
    port = load_trace(args.port)

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
