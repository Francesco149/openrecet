#!/usr/bin/env python3
"""Join rhost corpus function coverage with the port ledger, IDA's function
table (FLIRT lib flag, size) and recettear-decomp names/match status.

usage: coverage_join.py <cov_dir_with_*/cov.txt> [--ida functions_ida.tsv]
       [--ledger docs/port-ledger.json] [--decomp ../recettear-decomp]

cov.txt lines: "<va hex> <first-hit frame>" (written by p3_run.exe COVERAGE_OUT).
"""
import argparse, glob, json, os, sys
from collections import defaultdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("covdir")
    ap.add_argument("--ida", default="/mnt/ephemeral/openrecet/decompiled-ida/functions_ida.tsv")
    ap.add_argument("--ledger", default="docs/port-ledger.json")
    ap.add_argument("--decomp", default="../recettear-decomp")
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()

    hits = defaultdict(set)                      # va -> scenarios
    for p in glob.glob(os.path.join(a.covdir, "*", "cov.txt")):
        scen = os.path.basename(os.path.dirname(p))
        for line in open(p):
            if line.strip():
                hits[int(line.split()[0], 16)].add(scen)

    ida = {}
    for i, line in enumerate(open(a.ida)):
        if i == 0:
            continue
        va, size, name, lib = line.rstrip("\n").split("\t")[:4]
        ida[int(va, 16)] = (int(size), name, lib == "1")

    led = {int(f["va"], 16): f for f in json.load(open(a.ledger))["functions"]}

    # statically linked d3dx8.lib / libc.lib sites (recettear-decomp vendorlib map):
    # IDA's FLIRT has no d3dx8 signatures, so merge these into the "lib" set.
    vtsv = os.path.join(a.decomp, "config", "vendorlib.tsv")
    nvend = 0
    if os.path.exists(vtsv):
        for line in open(vtsv):
            if line.startswith("#") or not line.strip():
                continue
            va = int(line.split("\t")[0], 16)
            if va in ida and not ida[va][2]:
                ida[va] = (ida[va][0], ida[va][1], True)
                nvend += 1

    dec_name, dec_matched = {}, set()
    ftsv = os.path.join(a.decomp, "config", "functions.tsv")
    if os.path.exists(ftsv):
        for line in open(ftsv):
            if line.startswith("#") or not line.strip():
                continue
            c = line.rstrip("\n").split("\t")
            dec_name[int(c[0], 16)] = c[2]
    fm = "/mnt/ephemeral/recettear-decomp/build/funmatch.json"
    if os.path.exists(fm):
        dec_matched = set(json.load(open(fm)).get("isolated_verified", []))

    def port_state(va):
        f = led.get(va)
        return f["inventory_state"] if f else "not-in-ledger"

    game = [va for va in ida if not ida[va][2]]
    hit_game = [va for va in game if va in hits]
    print(f"IDA functions: {len(ida)} ({len(game)} game = not FLIRT-lib and not in "
          f"decomp vendorlib; {nvend} extra lib via vendorlib)")
    print(f"corpus-hit: {len(hits)} total, {len(hit_game)} non-lib "
          f"({sum(ida[v][0] for v in hit_game)} bytes of "
          f"{sum(ida[v][0] for v in game)})")
    by = defaultdict(lambda: [0, 0])
    for va in hit_game:
        s = port_state(va)
        by[s][0] += 1
        by[s][1] += ida[va][0]
    print("port inventory state of corpus-hit game functions (count, bytes):")
    for s, (n, b) in sorted(by.items(), key=lambda x: -x[1][1]):
        print(f"  {s:22s} {n:5d} {b:8d}")
    gap = sorted((va for va in hit_game if port_state(va) in ("discovered", "not-in-ledger")),
                 key=lambda v: -ida[v][0])
    print(f"\nhit by corpus but NOT referenced by the port: {len(gap)} fns, "
          f"{sum(ida[v][0] for v in gap)} bytes. Largest {a.top}:")
    for va in gap[:a.top]:
        print(f"  {va:08x} {ida[va][0]:6d}B  scen={len(hits[va]):2d}  "
              f"decomp={dec_name.get(va, '-')}{' [MATCHED]' if va in dec_matched else ''}")


if __name__ == "__main__":
    sys.exit(main())
