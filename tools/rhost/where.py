#!/usr/bin/env python3
"""where.py — which recording reaches a retail function / block (basic-block coverage of the corpus).

  where.py FUN_0043ae20 | 0x43ae20 | 43ae20        the function's blocks: hit (count, first scenario @ frame) / unhit
  where.py FUN_x --unhit                            only the unhit blocks (each with its first instruction)
  where.py --top N                                  the ported functions with the most unhit blocks
  where.py blocks OUT                               rebuild the block list (recursive descent over the game code)
  where.py index BATCH_DIR                          rebuild the index from a coverage corpus batch

Rebuild: `where.py blocks /tmp/bb.txt`, `rhost.py corpus --headless -j 8 --coverage /tmp/bb.txt`, then
`where.py index <the corpus batch dir>`. The INT3 first-hit coverage leaves retail's frames / RNG unchanged
(2026-11-19: 194 scenarios, frame + RNG equal to the plain corpus). Background: docs/findings/retail-coverage.md.
"""
import bisect
import collections
import glob
import gzip
import json
import os
import re
import subprocess
import sys

INDEX = "/mnt/ephemeral/openrecet/coverage/bb-latest.json.gz"
DECOMP = "/mnt/ephemeral/openrecet/decompiled-ida"
EXE = "/mnt/ephemeral/openrecet/staged_retail/recettear.unpacked.exe"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def game_functions():
    fns = []
    for line in open(os.path.join(DECOMP, "functions_ida.tsv")).read().splitlines()[1:]:
        c = line.split("\t")
        va = int(c[0], 16)
        if c[3] == "0" and va < 0x4a0000:
            fns.append((va, va + int(c[1]), c[2]))
    fns.sort()
    return fns


def disasm():
    out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", EXE], capture_output=True, text=True).stdout
    rx = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")
    ins, order = {}, []
    for line in out.splitlines():
        m = rx.match(line)
        if m:
            a = int(m.group(1), 16)
            ins[a] = (m.group(2), m.group(3).strip())
            order.append(a)
    return ins, {order[i]: order[i + 1] for i in range(len(order) - 1)}


def cmd_blocks(out):
    """block starts by recursive descent from each function entry (a linear sweep also decodes the switch
    tables inside function ranges — an INT3 there is read as data, never restored, and changes retail)"""
    ins, nxt = disasm()
    blocks, seen = set(), set()
    for fa, fb, _ in game_functions():
        if fa not in ins:
            continue
        blocks.add(fa)
        stack = [fa]
        while stack:
            a = stack.pop()
            while a is not None and fa <= a < fb and a in ins and a not in seen:
                seen.add(a)
                op, arg = ins[a]
                if op.startswith("ret") or op in ("int3", "hlt"):
                    break
                if op.startswith("j"):
                    t = re.match(r"0x([0-9a-f]+)$", arg) or re.match(r"([0-9a-f]+) <", arg)
                    if t:
                        ta = int(t.group(1), 16)
                        if fa <= ta < fb and ta in ins:
                            blocks.add(ta)
                            stack.append(ta)
                    if op == "jmp":
                        break
                    n = nxt.get(a)
                    if n is not None and fa <= n < fb:
                        blocks.add(n)
                a = nxt.get(a)
    open(out, "w").write("".join("%08x\n" % b for b in sorted(blocks)))
    print(f"{len(blocks)} blocks → {out}")


def cmd_index(batch, blocks_file=None):
    idx = {}
    for f in sorted(glob.glob(os.path.join(batch, "*", "coverage.txt"))):
        scen = os.path.basename(os.path.dirname(f))
        for line in open(f):
            va, fr = line.split()
            idx.setdefault(va, []).append([scen, int(fr)])
    blocks = [l.strip() for l in open(blocks_file)] if blocks_file else sorted(idx)
    os.makedirs(os.path.dirname(INDEX), exist_ok=True)
    json.dump({"_batch": batch, "blocks": blocks, "hits": idx}, gzip.open(INDEX, "wt"))
    print(f"{len(idx)} / {len(blocks)} blocks hit → {INDEX}")


def load():
    d = json.load(gzip.open(INDEX, "rt"))
    return [int(b, 16) for b in d["blocks"]], d["hits"]


def ledger():
    p = os.path.join(ROOT, "docs", "port-ledger.json")
    return {int(f["va"], 16): f for f in json.load(open(p))["functions"]}


def cmd_fun(arg, unhit_only):
    va = int(re.sub(r"^(FUN_|sub_|0x)", "", arg, flags=re.I), 16)
    fns = game_functions()
    st = [a for a, _, _ in fns]
    i = bisect.bisect_right(st, va) - 1
    if i < 0 or not (fns[i][0] <= va < fns[i][1]):
        raise SystemExit(f"{va:#x}: not in a game function")
    fa, fb, name = fns[i]
    blocks, hits = load()
    mine = [b for b in blocks if fa <= b < fb]
    ins, _ = disasm() if unhit_only else ({}, {})
    L = ledger().get(fa)
    h = [b for b in mine if "%08x" % b in hits]
    print(f"FUN_{fa:08x} ({name}, {fb - fa} B, port: {L['status'] if L else '?'} {', '.join(L['src'][:2]) if L else ''})"
          f" — {len(h)}/{len(mine)} blocks hit")
    for b in mine:
        e = hits.get("%08x" % b)
        if e and not unhit_only:
            first = min(e, key=lambda x: x[1])
            print(f"  {b:08x}  hit in {len(e):3d}  first {first[0]} @ f{first[1]}")
        elif not e:
            op = ins.get(b)
            print(f"  {b:08x}  UNHIT" + (f"   {op[0]} {op[1]}" if op else ""))


def cmd_top(n):
    blocks, hits = load()
    fns = game_functions()
    st = [a for a, _, _ in fns]
    per = collections.defaultdict(lambda: [0, 0])
    for b in blocks:
        fa = fns[bisect.bisect_right(st, b) - 1][0]
        per[fa][0] += 1
        per[fa][1] += ("%08x" % b) in hits
    led = ledger()
    rows = sorted(((t - k, fa, t, k) for fa, (t, k) in per.items() if k > 0 and led.get(fa, {}).get("status") in
                   ("ported", "verified", "stubbed")), reverse=True)[:n]
    tot = len(blocks)
    print(f"{sum(1 for b in blocks if ('%08x' % b) in hits)}/{tot} blocks hit; ported functions with unhit blocks:")
    for miss, fa, t, k in rows:
        L = led[fa]
        print(f"  {miss:4d} unhit / {t:4d}  FUN_{fa:08x}  {L['status']:8s} {', '.join(L['src'][:1])}")


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
        return
    if a[0] == "blocks":
        cmd_blocks(a[1])
    elif a[0] == "index":
        cmd_index(a[1], a[2] if len(a) > 2 else None)
    elif a[0] == "--top":
        cmd_top(int(a[1]) if len(a) > 1 else 30)
    else:
        cmd_fun(a[0], "--unhit" in a)


if __name__ == "__main__":
    main()
