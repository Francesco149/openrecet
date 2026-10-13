#!/usr/bin/env python3
"""GX-04 acceptance — the POSITIVE mutation proof (roadmap parity-evidence §9 GX-03/GX-04).

The proxy FREEZES each VB/IB bind's content, so a same-frame re-mutation is captured as
TWO distinct RES_VB versions (and an identical re-bind DEDUPS). The arrprobe re-drive
proves the wrapper is TRANSPARENT on static buffers (80/80 bit-exact, RES_VB stays 5) but
its buffers never mutate mid-frame; this drives the mutation path the old frame-end
snapshot got WRONG.

Builds + runs the standalone gx04_fixture.exe under the proxy: one VB, three binds with
contents A, B, A. A correct capture stores exactly TWO RES_VB (split A!=B) and binds
resids [A, B, A] (dedup the re-bind). The old code stored ONE record (end-of-frame
content) for all three ⇒ the B-draw would replay as A.

SKIPs (exit 0) when the env can't run a D3D8 exe (no WSL / no device) — a live device is
needed for the positive path; the arrprobe re-drive covers transparent-on-static.

Run: nix develop --command python3 tools/trace_studio_v3/test_gx04_fixture.py
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
    print(f"SKIP gx04_fixture: {msg}")
    sys.exit(0)


def die(msg: str):
    print(f"FAIL gx04_fixture: {msg}")
    sys.exit(1)


def main():
    if not shutil.which("wslpath"):
        skip("no wslpath — a live D3D8 exe (WSL interop) is required for the positive path")
    for tgt in ("d3d8.dll", "gx04_fixture.exe"):     # build the CURRENT proxy + the fixture
        r = subprocess.run(["make", tgt], cwd=PROXY, capture_output=True, text=True)
        if r.returncode != 0:
            die(f"build {tgt} failed:\n{r.stdout[-800:]}\n{r.stderr[-800:]}")

    with tempfile.TemporaryDirectory() as td:
        run = Path(td)
        out = run / "out"
        out.mkdir()
        shutil.copy(PROXY / "d3d8.dll", run)         # staged next to the exe → Direct3DCreate8 loads the proxy
        shutil.copy(PROXY / "gx04_fixture.exe", run)
        outw = subprocess.run(["wslpath", "-w", str(out)],
                              capture_output=True, text=True, check=True).stdout.strip()
        r = subprocess.run([str(run / "gx04_fixture.exe"), outw],
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
        sc = json.loads((out / "v3cap.census.json").read_text())["resource_binds"]

        checks: list[tuple[bool, str]] = []
        def check(cond, msg): checks.append((bool(cond), msg))

        check(vb == 2, f"two distinct RES_VB versions from the A,B,A mutation (got {vb})")
        check(len(binds) == 3, f"three SetStreamSource binds captured (got {len(binds)})")
        if len(binds) == 3:
            check(binds[0] != binds[1], f"SPLIT: bind A (id {binds[0]}) != bind B (id {binds[1]}) — same-frame mutation → two versions")
            check(binds[0] == binds[2], f"DEDUP: bind A (id {binds[0]}) == re-bind A (id {binds[2]}) — identical content → one record")
        check(sc["vb_fallback"] == 0, f"every VB bind used freeze-at-bind (0 fallback, got {sc['vb_fallback']})")
        check(sc["vb_multibind"] >= 1, f"the buffer was bound >1×/frame — the reuse the freeze handles (multibind {sc['vb_multibind']})")

        fails = [m for ok, m in checks if not ok]
        for ok, m in checks:
            print(f"  {'ok ' if ok else '✗  '}{m}")
        if fails:
            die(f"{len(fails)}/{len(checks)} checks failed")
        print(f"ok — gx04_fixture: {len(checks)} checks passed (VB mutation SPLIT + DEDUP verified)")


if __name__ == "__main__":
    main()
