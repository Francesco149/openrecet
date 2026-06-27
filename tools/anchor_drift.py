#!/usr/bin/env python3
"""anchor_drift.py — port↔retail anchor-timeline DRIFT + per-frame state diff,
aligned at a chosen anchor occurrence.  Reads scenario-test run dirs (anchors.jsonl
+ call_trace.jsonl) OR v3-cache dirs (v3meta.json + call_trace.jsonl).

  anchor_drift.py <port_dir> <retail_dir> [--at ANCHOR[:OCC]] [--state LO HI FIELDS]

--at default CUSTOMER_SERVICE_ENTER:1.  Without --state: just the drift map.
With --state LO HI f1,f2,..: a per-offset side-by-side of those 0x48670f fields."""
import argparse, json, sys
from pathlib import Path

VA = 4744975  # 0x48670f

def load_anchors(d: Path):
    """-> list[(name, occ, frame)] in frame order."""
    aj = d / "anchors.jsonl"
    rows = []
    if aj.exists():
        for line in open(aj):
            line = line.strip()
            if not line: continue
            o = json.loads(line)
            rows.append((o["anchor"], o["frame"]))
    else:
        meta = json.loads((d / "v3meta.json").read_text())
        for a in meta["anchors"]:
            rows.append((a["name"], a["frame"]))
    occ = {}
    out = []
    for name, fr in rows:
        occ[name] = occ.get(name, 0) + 1
        out.append((name, occ[name], fr))
    return out

def align_frame(anchors, name, occ):
    for n, o, fr in anchors:
        if n == name and o == occ:
            return fr
    return None

def load_state(d: Path):
    st = {}
    p = d / "call_trace.jsonl"
    if not p.exists(): return st
    for line in open(p):
        if '4744975' not in line: continue
        try: o = json.loads(line)
        except json.JSONDecodeError: continue
        if o.get("va") == VA and "frame" in o:
            st[o["frame"]] = o["f"]
    return st

def fmt(v):
    if isinstance(v, float): return f"{v:.4g}"
    return str(v)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port_dir", type=Path)
    ap.add_argument("retail_dir", type=Path)
    ap.add_argument("--at", default="CUSTOMER_SERVICE_ENTER:1")
    ap.add_argument("--state", nargs=3, metavar=("LO","HI","FIELDS"))
    a = ap.parse_args()
    name, _, occ = a.at.partition(":")
    occ = int(occ or "1")
    pa = load_anchors(a.port_dir); ra = load_anchors(a.retail_dir)
    p0 = align_frame(pa, name, occ); r0 = align_frame(ra, name, occ)
    print(f"align at {name}:{occ}  port frame {p0} | retail frame {r0}")
    if p0 is None or r0 is None:
        print("!! alignment anchor missing on a side", file=sys.stderr); return 2

    # drift map over shared (name, occ)
    rmap = {(n,o): fr for n,o,fr in ra}
    print(f"\n{'anchor':<24} {'occ':>3} {'p_off':>6} {'r_off':>6} {'drift':>6}")
    print("-"*52)
    for n,o,fr in pa:
        if (n,o) not in rmap: continue
        po = fr - p0; ro = rmap[(n,o)] - r0
        d = po - ro
        if po < -5: continue
        mark = "" if d==0 else ("  <<<" if abs(d)>=2 else "  <")
        print(f"{n:<24} {o:>3} {po:>6} {ro:>6} {d:>+6}{mark}")

    if a.state:
        lo, hi, fields = int(a.state[0]), int(a.state[1]), a.state[2].split(",")
        ps = load_state(a.port_dir); rs = load_state(a.retail_dir)
        hdr = "off | " + " ".join(fields)
        print("\n=== PORT ===\n" + hdr)
        for off in range(lo,hi):
            p = ps.get(p0+off)
            if p: print(f"{off:>3} | " + " ".join(fmt(p.get(f)) for f in fields))
        print("\n=== RETAIL ===\n" + hdr)
        for off in range(lo,hi):
            r = rs.get(r0+off)
            if r: print(f"{off:>3} | " + " ".join(fmt(r.get(f)) for f in fields))

if __name__ == "__main__":
    raise SystemExit(main())
