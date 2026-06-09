#!/usr/bin/env python3
"""
tools/freeroam_reach.py — static call-graph reachability for the HOUSE
free-roam structural-parity survey.

Parses FUN_xxxxxxxx call edges out of the per-function decompile
(docs/decompiled/by-address/<va>.c), does a transitive closure from a
set of free-roam spine roots, and classifies every reached node against
docs/port-ledger.json.  The unported/stubbed nodes reachable from the
spine are the structural-parity work list.

Output: JSON to stdout (or --out) with the reached set, per-node status,
shortest-path depth from a root, and the direct callers inside the
reached set (so clustering can follow the call structure).
"""
import json, re, sys, argparse
from pathlib import Path
from collections import deque, defaultdict

ROOT = Path(__file__).resolve().parent.parent
LEDGER = ROOT / "docs/port-ledger.json"
BYADDR = ROOT / "docs/decompiled/by-address"

FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{8})")

# Free-roam spine roots (engine VAs).  Each is a per-frame entry the HOUSE
# free-roam state actually drives.  Keep this list auditable + documented.
DEFAULT_ROOTS = {
    "0048670f": "player controller tick (FUN_0048670f, scene1_player_ctrl_tick)",
    "00442cef": "ingame sim default arm (FUN_00442cef)",
    "004547ab": "render-thread top-level (FUN_004547ab)",
}


def name_to_va(name):
    return name[4:].lower() if name.startswith("FUN_") else name.lower()


def load_ledger():
    d = json.load(open(LEDGER))
    by_va = {}
    for f in d["functions"]:
        va = f["va"].lower().replace("0x", "").zfill(8)
        by_va[va] = f
    return by_va


def callees(va):
    """Set of callee VAs (8-hex lowercase) referenced in <va>'s decomp."""
    p = BYADDR / f"{va}.c"
    if not p.exists():
        # filenames strip a leading 00 sometimes; try both
        alt = BYADDR / f"{va.lstrip('0')}.c"
        p = alt if alt.exists() else p
    if not p.exists():
        return set(), False
    txt = p.read_text(errors="replace")
    out = set()
    for m in FUN_RE.finditer(txt):
        c = m.group(1).lower()
        if c != va:
            out.add(c)
    return out, True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--roots", nargs="*", help="extra root VAs (8-hex)")
    ap.add_argument("--max-depth", type=int, default=99)
    args = ap.parse_args()

    ledger = load_ledger()
    roots = dict(DEFAULT_ROOTS)
    for r in args.roots or []:
        roots[r.lower().replace("0x", "").zfill(8)] = "(cli root)"

    depth = {}
    parents = defaultdict(set)   # va -> direct callers within reached set
    edges_cache = {}
    no_decomp = set()

    dq = deque()
    for r in roots:
        depth[r] = 0
        dq.append(r)

    while dq:
        va = dq.popleft()
        d = depth[va]
        if d >= args.max_depth:
            continue
        cs, ok = edges_cache.get(va, (None, None))
        if cs is None:
            cs, ok = callees(va)
            edges_cache[va] = (cs, ok)
        if not ok:
            no_decomp.add(va)
        for c in cs:
            parents[c].add(va)
            if c not in depth:
                depth[c] = d + 1
                dq.append(c)

    # Classify reached nodes
    rows = []
    for va, d in depth.items():
        f = ledger.get(va)
        status = f["status"] if f else "UNKNOWN(not-in-table)"
        if f and f.get("is_thunk"):
            continue
        rows.append({
            "va": va,
            "name": f["name"] if f else f"FUN_{va}",
            "status": status,
            "size": f["size"] if f else 0,
            "depth": d,
            "callers": sorted(parents.get(va, [])),
            "is_root": va in roots,
        })

    rows.sort(key=lambda r: (-r["size"]))
    from collections import Counter
    status_counts = Counter(r["status"] for r in rows)
    work = [r for r in rows if r["status"] in ("unported", "stubbed")]
    work_bytes = sum(r["size"] for r in work)

    result = {
        "roots": roots,
        "reached_total": len(rows),
        "status_counts": dict(status_counts),
        "work_count": len(work),
        "work_bytes": work_bytes,
        "no_decomp_count": len(no_decomp),
        "rows": rows,
    }
    out = json.dumps(result, indent=1)
    if args.out:
        Path(args.out).write_text(out)
        print(f"reached={len(rows)} work(unported+stubbed)={len(work)} "
              f"work_bytes={work_bytes} statuses={dict(status_counts)} → {args.out}")
    else:
        print(out)


if __name__ == "__main__":
    main()
