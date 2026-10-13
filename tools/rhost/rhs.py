"""rhs.py — read rhost state dumps (.rhs, "RHS2") and diff them.

Layout (little-endian): "RHS2" frame x87cw nranges, nranges×(lo,hi), the bytes of
each range in order, a 128 KB bitmap (1 bit per 4 KB page: value points into a
non-deterministic committed mapping at dump time), nvol, nvol×volatile address.
"""
import struct
import sys
from array import array
from collections import Counter

RT_DATA_LO, RT_DATA_HI = 0x528000, 0x964F8E0


class Dump:
    def __init__(self, path):
        b = open(path, "rb").read()
        magic, self.frame, self.cw, n = struct.unpack_from("<4I", b, 0)
        if magic != 0x32534852:
            raise ValueError(f"{path}: not an RHS2 dump")
        off = 16
        self.ranges = [struct.unpack_from("<2I", b, off + 8 * i) for i in range(n)]
        off += 8 * n
        self.words = {}
        for lo, hi in self.ranges:
            a = array("I")
            a.frombytes(b[off:off + (hi - lo)])
            self.words[lo] = a
            off += hi - lo
        self.ext = b[off:off + (1 << 17)]
        off += 1 << 17
        (nv,) = struct.unpack_from("<I", b, off)
        self.vol = set(struct.unpack_from(f"<{nv}I", b, off + 4))

    def is_ext(self, v):
        return v >= 0x10000 and (self.ext[v >> 15] >> ((v >> 12) & 7)) & 1

    def region(self, va):
        if RT_DATA_LO <= va < RT_DATA_HI:
            return "data"
        return "arena"


def classify(da, db, va, a, b):
    if va in da.vol:
        return "volatile-addr"
    ea, eb = da.is_ext(a), db.is_ext(b)
    if ea and eb:
        return "ext-ptr(both)"
    if ea or eb:
        return "ext-ptr(one)"
    if is_det(a) or is_det(b):
        return "det-ptr"
    if (a | b) & 3 == 0 and a >= 0x10000 and b >= 0x10000 and (a ^ b) < 0x1000000:
        return "ext-ptr(stale)"   # aligned pointers into memory unmapped at dump time (e.g. old D3D lock ptr)
    return "value"


def is_det(v):
    return (0x200000 <= v < 0x9955000) or (0x20000000 <= v < 0x59800000)


def diff(pa, pb, show=40, out=sys.stdout):
    da, db = Dump(pa), Dump(pb)
    if da.ranges != db.ranges:
        print(f"range layout differs: {len(da.ranges)} vs {len(db.ranges)} ranges", file=out)
        sa, sb = set(da.ranges), set(db.ranges)
        for r in sorted(sa ^ sb)[:20]:
            print(f"  {'A' if r in sa else 'B'} only {r[0]:08x}..{r[1]:08x}", file=out)
    rows = []
    for lo, hi in da.ranges:
        if lo not in db.words or db.words[lo] is None:
            continue
        A, B = da.words[lo], db.words[lo]
        n = min(len(A), len(B))
        if A[:n] == B[:n]:
            continue
        for i in range(n):
            if A[i] != B[i]:
                va = lo + 4 * i
                rows.append((va, A[i], B[i], classify(da, db, va, A[i], B[i])))
    c = Counter(r[3] for r in rows)
    print(f"frames {da.frame}/{db.frame}  differing dwords: {len(rows)}  " + "  ".join(f"{k}={v}" for k, v in c.most_common()), file=out)
    real = [r for r in rows if r[3] in ("value", "det-ptr", "ext-ptr(one)", "ext-ptr(stale)")]
    for va, a, b, k in real[:show]:
        fa = struct.unpack("<f", struct.pack("<I", a))[0]
        fb = struct.unpack("<f", struct.pack("<I", b))[0]
        print(f"  {va:08x} {da.region(va):5s} {a:08x} {b:08x}  {k:13s} f={fa:.6g}/{fb:.6g}", file=out)
    if len(real) > show:
        print(f"  ... {len(real) - show} more", file=out)
    return rows


if __name__ == "__main__":
    diff(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 40)
