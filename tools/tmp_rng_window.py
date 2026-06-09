#!/usr/bin/env python3
"""One-off: where does the rngcalls delta accumulate on item-display-2?

Aligns port/retail by db054 (house_update rows), prints per-db054-frame
rngcalls delta around jumps, and aggregates which VAs run during frames
where house_update does NOT run (menu/pause windows).
"""
import json, sys
from collections import defaultdict

HOUSE_VA = 4744975  # 0x48670f house_update

def load(path):
    frames = defaultdict(lambda: {"db054": None, "rngcalls": None, "vas": []})
    with open(path) as f:
        for line in f:
            r = json.loads(line)
            fr = r["frame"]
            d = frames[fr]
            d["vas"].append(r["va"])
            fl = r.get("f") or {}
            if r["va"] == HOUSE_VA and "db054" in fl:
                d["db054"] = fl["db054"]
            if "rngcalls" in fl:
                d["rngcalls"] = fl["rngcalls"]
    return dict(sorted(frames.items()))

def db054_to_rngcalls(frames):
    """db054 value -> rngcalls at the LAST raw frame having that db054
    (so the delta to the next db054 includes any pause in between)."""
    out = {}
    last_db = None
    for fr, d in frames.items():
        if d["db054"] is not None:
            last_db = d["db054"]
        if last_db is not None and d["rngcalls"] is not None:
            out[last_db] = (fr, d["rngcalls"])
    return out

port = load(sys.argv[1])
retail = load(sys.argv[2])
p_map = db054_to_rngcalls(port)
r_map = db054_to_rngcalls(retail)

common = sorted(set(p_map) & set(r_map))
print(f"common db054 frames: {len(common)}  range [{common[0]}..{common[-1]}]")

# normalize: rngcalls offset at first common frame
p0 = p_map[common[0]][1]; r0 = r_map[common[0]][1]
prev_delta = 0
print("\ndb054 frames where the port-retail rngcalls delta CHANGES:")
print(f"{'db054':>6} {'p_raw':>6} {'r_raw':>7} {'p_rng':>7} {'r_rng':>7} {'delta':>6} {'step':>5}")
for db in common:
    pf, pc = p_map[db]; rf, rc = r_map[db]
    delta = (pc - p0) - (rc - r0)
    if delta != prev_delta:
        print(f"{db:>6} {pf:>6} {rf:>7} {pc-p0:>7} {rc-r0:>7} {delta:>6} {delta-prev_delta:>+5}")
    prev_delta = delta

# pause windows: port raw frames with no house row but other activity
print("\nport raw-frame gaps between consecutive db054 ticks > 1 frame:")
hframes = [(fr, d["db054"]) for fr, d in port.items() if d["db054"] is not None]
for (f1, d1), (f2, d2) in zip(hframes, hframes[1:]):
    if f2 - f1 > 1:
        # aggregate VAs in the gap (exclusive of house frames)
        agg = defaultdict(int)
        rng_in_gap = 0
        prev_rc = port[f1]["rngcalls"]
        for fr in range(f1 + 1, f2):
            if fr in port:
                for va in port[fr]["vas"]:
                    agg[va] += 1
                rc = port[fr]["rngcalls"]
                if rc is not None and prev_rc is not None:
                    pass
        # rng consumed across gap = rngcalls@f2 - rngcalls@f1
        rc2 = port[f2]["rngcalls"]; rc1 = port[f1]["rngcalls"]
        gap_rng = (rc2 - rc1) if (rc2 is not None and rc1 is not None) else None
        top = sorted(agg.items(), key=lambda kv: -kv[1])[:8]
        tops = " ".join(f"{va:#x}×{n}" for va, n in top)
        print(f"  labels {f1}->{f2} ({f2-f1-1} gap frames, db054 {d1}->{d2}) rng+={gap_rng}  {tops}")
