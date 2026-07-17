#!/usr/bin/env python3
"""GX-05 acceptance — forced hash collision remains DISTINCT (roadmap parity-evidence §9 GX-05).

The proxy's resource dedup is BYTE-COMPARE, not hash-trust: the FNV-64 hash only buckets, and
a hash match is confirmed by memcmp of the full retained body (+ type+len) before dedup'ing. So
a collision can NEVER alias two distinct resources into one id — provable, not probabilistic.

A real FNV-64 collision is infeasible to construct, so gx05_fixture.exe sets the in-process seam
OPENRECET_V3_TEST_FORCE_COLLISION=1 ⇒ the proxy's fnv1a returns a constant ⇒ EVERY resource lands
in one hash bucket, forcing dedup to rely SOLELY on the byte-compare. The fixture drives one VB
with four binds A,B,A,C. A correct capture stores exactly THREE distinct RES_VB and binds resids
[0,1,0,2] (split A|B, dedup the re-bind of A, split C past BOTH hash-equal entries). The OLD
hash-only dedup under this forced collision would store ONE record for all four ⇒ ids [0,0,0,0],
aliasing B and C onto A.

Two independent guards against a VACUOUS pass (normal hashing would also give 3 distinct VB):
  (1) v3proxy.log must contain "forcing hash collisions" — the seam was actually honored;
  (2) the log must record >=2 "FNV-64 COLLISION" lines — byte-compare rejected the forced
      hash-matches (normal hashing gives ZERO, since distinct content → distinct hashes).

SKIPs (exit 0) when the env can't run a D3D8 exe (no WSL / no device).

Run: nix develop --command python3 tools/trace_studio_v3/test_gx05_fixture.py
"""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROXY = HERE / "proxy"
INSPECT = HERE / "inspect_cap.py"


def skip(msg: str):
    print(f"SKIP gx05_fixture: {msg}")
    sys.exit(0)


def die(msg: str):
    print(f"FAIL gx05_fixture: {msg}")
    sys.exit(1)


def main():
    if not shutil.which("wslpath"):
        skip("no wslpath — a live D3D8 exe (WSL interop) is required for the byte-compare path")
    for tgt in ("d3d8.dll", "gx05_fixture.exe"):     # build the CURRENT proxy + the fixture
        r = subprocess.run(["make", tgt], cwd=PROXY, capture_output=True, text=True)
        if r.returncode != 0:
            die(f"build {tgt} failed:\n{r.stdout[-800:]}\n{r.stderr[-800:]}")

    with tempfile.TemporaryDirectory() as td:
        run = Path(td)
        out = run / "out"
        out.mkdir()
        shutil.copy(PROXY / "d3d8.dll", run)         # staged next to the exe → Direct3DCreate8 loads the proxy
        shutil.copy(PROXY / "gx05_fixture.exe", run)
        outw = subprocess.run(["wslpath", "-w", str(out)],
                              capture_output=True, text=True, check=True).stdout.strip()
        r = subprocess.run([str(run / "gx05_fixture.exe"), outw],
                           cwd=run, capture_output=True, text=True)
        combined = (r.stdout + r.stderr).strip()
        if "FAIL CreateDevice" in combined or "FAIL Direct3DCreate8" in combined:
            skip(f"no D3D8 device in this env ({combined[:140]})")
        cap = out / "v3cap.bin"
        if r.returncode != 0 or not cap.exists():
            die(f"fixture exit {r.returncode}, cap exists={cap.exists()}:\n{combined[-600:]}")

        rep = json.loads(subprocess.run([sys.executable, str(INSPECT), str(cap)],
                                        capture_output=True, text=True, check=True).stdout)
        binds = rep["stream_binds_resids"]
        vb = rep["resources"]["VB"]
        # the proxy's dedup-soundness health block (in the census, co-located with the capture —
        # NOT the log, which opens at DllMain before OPENRECET_V3_OUT is set, so lands elsewhere).
        dd = json.loads((out / "v3cap.census.json").read_text()).get("dedup", {})
        seam_on = dd.get("force_collision") == 1
        n_collide = dd.get("collisions", 0)

        checks: list[tuple[bool, str]] = []
        def check(cond, msg): checks.append((bool(cond), msg))

        # the seam MUST have been active, else the test is vacuous (normal hashing also splits)
        check(seam_on, f"the forced-collision seam was ACTIVE (census dedup.force_collision=1, got {dd!r})")
        check(n_collide >= 2, f"byte-compare REJECTED >=2 forced hash-collisions (got {n_collide}; "
                              f"normal hashing would give 0 — double-confirms the seam)")
        check(vb == 3, f"three distinct RES_VB despite one hash bucket (A,B,C; got {vb})")
        check(dd.get("distinct") == 3, f"census dedup.distinct agrees: 3 distinct resources (got {dd.get('distinct')})")
        check(len(binds) == 4, f"four SetStreamSource binds captured (got {len(binds)})")
        if len(binds) == 4:
            check(binds == [0, 1, 0, 2],
                  f"resids [0,1,0,2] — split A|B, DEDUP re-bind A, split C past both entries (got {binds})")

        fails = [m for ok, m in checks if not ok]
        for ok, m in checks:
            print(f"  {'ok ' if ok else '✗  '}{m}")
        if fails:
            die(f"{len(fails)}/{len(checks)} checks failed")
        print(f"ok — gx05_fixture: {len(checks)} checks passed "
              f"(byte-compare dedup: forced collision stays distinct, {n_collide} collisions rejected)")


if __name__ == "__main__":
    main()
