#!/usr/bin/env python3
"""GX-05 acceptance — the v3 container READER fails safely on truncated/corrupt input
(roadmap parity-evidence §9 GX-05: "truncated/corrupt containers fail safely").

Builds + runs replay/corrupt_fuzz.exe, which #includes the AUTHORITATIVE replay_core.c
and drives its step() with do_res=0, do_calls=0 (pure parse, NO device) over crafted
truncated / corrupt-length / integer-overflow / unknown-opcode records + 40000
deterministic fuzz inputs, asserting the cursor never escapes the buffer and every walk
terminates. The fuzzer self-asserts and exits 1 on any escape/hang; this wrapper checks
exit 0 + the OK line. (Verified decisive: it FAILS against the pre-GX-05 unhardened
reader — cursor escaped on RES_IB/CopyRects/SetTransform/SetMaterial + the fuzz.)

Runs headless (no D3D device) ⇒ never SKIPs for lack of a device — only if mingw/WSL
interop is unavailable.

Run: nix develop --command python3 tools/trace_studio_v3/test_corrupt_reader.py
"""
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPLAY = HERE / "replay"


def skip(msg: str):
    print(f"SKIP corrupt_reader: {msg}")
    sys.exit(0)


def die(msg: str):
    print(f"FAIL corrupt_reader: {msg}")
    sys.exit(1)


def main():
    if not shutil.which("wslpath"):
        skip("no wslpath — a mingw exe (WSL interop) is required to run the fuzzer")
    r = subprocess.run(["make", "corrupt_fuzz.exe"], cwd=REPLAY, capture_output=True, text=True)
    if r.returncode != 0:
        die(f"build corrupt_fuzz.exe failed:\n{r.stdout[-800:]}\n{r.stderr[-800:]}")

    r = subprocess.run([str(REPLAY / "corrupt_fuzz.exe")], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print(out)
    if r.returncode != 0 or "corrupt_fuzz: OK" not in out:
        die(f"the reader did NOT fail safely (exit {r.returncode}) — a cursor escaped or a walk hung")
    print("ok — corrupt_reader: the v3 reader fails safely on truncated/corrupt containers")


if __name__ == "__main__":
    main()
