#!/usr/bin/env python3
"""GX-06 graphics-capture regression corpus — coverage gate CLI (roadmap
parity-evidence-roadmap.md §9 GX-06).

Default (FAST gate): load the manifest + census, print the opcode×{fixture,real-proof}
coverage matrix, exit 0 iff COMPLETE — every recorded opcode is fixture-covered, every
OBSERVED opcode has a bit-exact real proof, and the opcode↔census map has no drift. Touches
no caches and no replay.exe, so it runs in the host suite on any checkout.

  --verify   drive-capable: re-parse each real-proof container (opcodes/SURFREF kinds) + re-run
             v3verify (bit-exact) + run the fixture acceptance tests, RECONCILING each against
             the manifest attestation (reports any drift). Needs the v3 caches + replay.exe.
  --write    with --verify: re-STAMP the manifest real_proofs (opcodes/surfrefs/verify/frames/
             verified_at) from what was measured — keeps the attestation fresh after a cache
             re-drive re-keys a scenario dir.
  --json     emit the machine-readable report to stdout instead of the table.

Run:  nix develop --command python3 tools/gx_corpus.py [--verify [--write]] [--json]
"""
from __future__ import annotations

import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "trace_studio_v3"))

from parity import gx_corpus  # noqa: E402
from parity.d3d_census import load_census  # noqa: E402

CACHE_ROOT = ROOT / "runs" / "studio-v3-cache"


def _measure_real_proof(rp: dict) -> dict:
    """Re-derive a real proof's actual opcodes/SURFREFs (parse the cached container) + verify
    verdict (v3verify). Returns {opcodes, surfrefs, verify, frames, present?} or {error}."""
    import orv3
    from v3verify import verify_counts
    d = CACHE_ROOT / rp["cache_key"] / rp["side"]
    cap = d / "v3cap.bin"
    if not cap.exists():
        return {"error": f"cache absent: {cap}"}
    c = orv3.Container.load(cap)
    opcodes = sorted(k for k in c.opcode_counts(by_name=True) if k != "EOF")
    surfrefs = sorted(c.surfref_counts(by_name=True))
    npass, nfail, total = verify_counts(d, quiet=True)
    verify = "REPLAY_EXACT" if (nfail == 0 and npass == total and total > 0) else "REPLAY_DIVERGENT"
    return {"opcodes": opcodes, "surfrefs": surfrefs, "verify": verify, "frames": f"{npass}/{total}"}


def _reconcile(manifest: dict, write: bool) -> tuple[int, list[str]]:
    """--verify: re-measure each real proof + run the fixture tests; report drift vs the
    manifest. Returns (nonzero on any drift/failure, log lines)."""
    log: list[str] = []
    bad = 0
    for rp in manifest["real_proofs"]:
        m = _measure_real_proof(rp)
        tag = f"{rp['scenario']} [{rp['cache_key']}/{rp['side']}]"
        if "error" in m:
            log.append(f"  ? {tag}: {m['error']} (cannot verify)")
            bad += 1
            continue
        diffs = []
        if set(m["opcodes"]) != set(rp.get("opcodes", [])):
            miss = sorted(set(rp.get("opcodes", [])) - set(m["opcodes"]))
            extra = sorted(set(m["opcodes"]) - set(rp.get("opcodes", [])))
            diffs.append(f"opcodes drift (missing {miss}, new {extra})")
        if set(m["surfrefs"]) != set(rp.get("surfrefs", [])):
            diffs.append(f"surfrefs drift (was {rp.get('surfrefs', [])}, now {m['surfrefs']})")
        if m["verify"] != rp.get("verify"):
            diffs.append(f"verify {rp.get('verify')} → {m['verify']}")
        if diffs:
            log.append(f"  ✗ {tag}: " + "; ".join(diffs))
            bad += 1
            if write:
                rp["opcodes"] = m["opcodes"]; rp["surfrefs"] = m["surfrefs"]
                rp["verify"] = m["verify"]; rp["frames"] = m["frames"]
                rp["verified_at"] = datetime.date.today().isoformat()
        else:
            log.append(f"  ok {tag}: {m['verify']} {m['frames']}, {len(m['opcodes'])} opcodes"
                       + (f", {len(m['surfrefs'])} SURFREF" if m["surfrefs"] else ""))
    # run the fixture acceptance tests (each SKIPs cleanly without a D3D8 device)
    for fx in manifest["fixtures"]:
        t = ROOT / fx["test"]
        r = subprocess.run([sys.executable, str(t)], capture_output=True, text=True)
        last = (r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else ""
        if r.returncode != 0:
            log.append(f"  ✗ {fx['name']} test FAILED: {last[:120]}")
            bad += 1
        else:
            log.append(f"  ok {fx['name']}: {last[:100]}")
    return bad, log


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", type=Path, default=gx_corpus.DEFAULT_MANIFEST)
    ap.add_argument("--census", type=Path, default=gx_corpus.DEFAULT_CENSUS)
    ap.add_argument("--verify", action="store_true", help="re-measure real proofs + run fixture tests")
    ap.add_argument("--write", action="store_true", help="with --verify: re-stamp the manifest attestations")
    ap.add_argument("--json", action="store_true", help="machine-readable report")
    args = ap.parse_args(argv)

    manifest = gx_corpus.load_manifest(args.manifest)
    census = load_census(args.census)
    report = gx_corpus.build_report(manifest, census)

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(gx_corpus.format_report(report))

    rc = gx_corpus.gate(report)

    if args.verify:
        print("\n=== --verify: re-measuring real proofs + running fixture tests ===")
        bad, log = _reconcile(manifest, args.write)
        for ln in log:
            print(ln)
        if args.write and bad:
            args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
            print(f"  → re-stamped {args.manifest}")
            # a --write pass reconciles the manifest; re-gate against the fresh attestation
            report = gx_corpus.build_report(gx_corpus.load_manifest(args.manifest), census)
            rc = gx_corpus.gate(report)
        rc = rc or (1 if bad else 0)

    print(f"\nVERDICT: {report['verdict']}  (exit {rc})")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
