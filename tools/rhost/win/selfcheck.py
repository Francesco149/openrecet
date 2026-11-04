"""selfcheck.py PIECE — a capture piece's recording vs its replay (selfcheck.ps1 ran the replay in PIECE\\check).
Writes PIECE\\check.json; on PASS deletes both state dumps (~200 MB each)."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rhs  # noqa: E402

BENIGN = ("ext-ptr(both)", "ext-ptr(stale)", "volatile-addr")   # rhost.py cmd_record's self-check


def result(log):
    r = {}
    if os.path.exists(log):
        for line in open(log, errors="replace"):
            if line.startswith("RESULT "):
                r = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
    return r


def main(d):
    c = os.path.join(d, "check")
    rec, rep = result(os.path.join(d, "rhost.log")), result(os.path.join(c, "rhost.log"))
    out = {"recorded": {k: rec.get(k) for k in ("frame", "rng", "reason", "state_sem")},
           "replayed": {k: rep.get(k) for k in ("frame", "rng", "reason", "state_sem")}}
    da, db = os.path.join(d, "state_end.rhs"), os.path.join(c, "state_end.rhs")
    real = None
    if os.path.exists(da) and os.path.exists(db):
        with open(os.path.join(c, "diff.txt"), "w") as f:
            rows = rhs.diff(da, db, show=40, out=f)
        real = [r for r in rows if r[3] not in BENIGN]
        out["dump_diffs"] = len(rows)
        out["real_diffs"] = len(real)
        out["first_real"] = [f"{va:08x} {a:08x} {b:08x} {k}" for va, a, b, k in real[:8]]
    sa, sb = os.path.join(d, "save.dat"), os.path.join(c, "save.dat")
    out["save_equal"] = (open(sa, "rb").read() == open(sb, "rb").read()) if os.path.exists(sa) and os.path.exists(sb) \
        else (os.path.exists(sa) == os.path.exists(sb))
    ok = bool(rec) and rec.get("frame") == rep.get("frame") and rec.get("rng") == rep.get("rng") and out["save_equal"] \
        and real is not None and not real
    out["status"] = "PASS" if ok else "FAIL"
    json.dump(out, open(os.path.join(d, "check.json"), "w"), indent=1)
    if ok:
        for p in (da, db):
            os.remove(p)
    print(out["status"], json.dumps(out))


if __name__ == "__main__":
    main(sys.argv[1])
