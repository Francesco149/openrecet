#!/usr/bin/env python3
"""Shared scaffolding for the GX-06 corpus fixture acceptance tests (roadmap
parity-evidence §9 GX-06): build the CURRENT proxy + the named fixture, run it under the
staged proxy, parse the captured container, and prove (a) every expected opcode/SURFREF
kind was captured and (b) the single kept frame replays BIT-EXACT vs the proxy's reference.

A fixture proves the record→replay plumbing for its opcodes IN ISOLATION (a controlled
synthetic frame); the corpus's other axis — the real cached scenarios — proves each
OBSERVED opcode in situ (tools/gx_corpus.py). SKIPs (exit 0) when the env can't run a
D3D8 exe (no WSL interop / no device), like test_gx04/gx05_fixture."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import orv3  # noqa: E402

PROXY = HERE / "proxy"
REPLAY = HERE / "replay" / "replay.exe"


def _wslw(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def run_fixture(tag: str, exe: str, expected_ops, expected_surf=()) -> None:
    if not shutil.which("wslpath"):
        print(f"SKIP {tag}: no wslpath — a live D3D8 exe (WSL interop) is required")
        sys.exit(0)
    for tgt in ("d3d8.dll", exe):                      # build the CURRENT proxy + the fixture
        r = subprocess.run(["make", tgt], cwd=PROXY, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"FAIL {tag}: build {tgt}:\n{r.stdout[-800:]}\n{r.stderr[-800:]}")
            sys.exit(1)
    if not REPLAY.exists():
        print(f"SKIP {tag}: replay.exe not built — `nix develop --command make` in replay/")
        sys.exit(0)

    with tempfile.TemporaryDirectory() as td:
        run = Path(td); out = run / "out"; out.mkdir()
        shutil.copy(PROXY / "d3d8.dll", run)           # staged next to the exe → the proxy loads
        shutil.copy(PROXY / exe, run)
        r = subprocess.run([str(run / exe), _wslw(out)], cwd=run, capture_output=True, text=True)
        combined = (r.stdout + r.stderr).strip()
        if "FAIL CreateDevice" in combined or "FAIL Direct3DCreate8" in combined:
            print(f"SKIP {tag}: no D3D8 device in this env ({combined[:140]})")
            sys.exit(0)
        cap = out / "v3cap.bin"; ref = out / "v3ref_000.raw"
        if r.returncode != 0 or not cap.exists() or not ref.exists():
            print(f"FAIL {tag}: exit {r.returncode} cap={cap.exists()} ref={ref.exists()}\n{combined[-600:]}")
            sys.exit(1)

        c = orv3.Container.load(cap)
        ops = set(c.opcode_counts(by_name=True))
        surf = set(c.surfref_counts(by_name=True))
        checks: list[tuple[bool, str]] = []
        for op in expected_ops:
            checks.append((op in ops, f"opcode {op} captured"))
        for k in expected_surf:
            checks.append((k in surf, f"SURFREF {k} captured"))

        # bit-exact replay of the single kept frame vs the proxy's own reference readback
        chk = out / "chk.raw"
        rr = subprocess.run([str(REPLAY), _wslw(cap), _wslw(ref), "0", _wslw(chk)],
                            capture_output=True, text=True)
        m = re.search(r"differing bytes\s*:\s*(\d+)", rr.stdout + rr.stderr)
        db = m.group(1) if m else None
        checks.append((db == "0", f"replay bit-exact (differing bytes={db})"))

        fails = [msg for ok, msg in checks if not ok]
        for ok, msg in checks:
            print(f"  {'ok ' if ok else 'X  '}{msg}")
        if fails:
            print(f"FAIL {tag}: {len(fails)}/{len(checks)} checks failed")
            sys.exit(1)
        extra = f" + {len(expected_surf)} SURFREF kinds" if expected_surf else ""
        print(f"ok — {tag}: {len(checks)} checks passed "
              f"({len(expected_ops)} opcodes{extra}, replay bit-exact)")
