#!/usr/bin/env python3
"""tools/d3d_census.py — GX-00 D3D8 method census (CLI).

Print the capture-completeness census of the v3 proxy: how many D3D8 vtable methods
are RECORDED vs FORWARDED, and — the point — which forwarded methods are
`render_affecting_unsupported` (they CAN alter future pixels/resources/device state but
aren't captured, so a `pixels`/`render_program` pillar over a scene that calls one would
diverge silently). Cross-checks `proxy_generated.h` against the R3 census
`docs/schemas/d3d8-method-census-v1.json`; any drift (a method flipped recorded↔forwarded,
added, removed, or misclassified) is reported.

Exit: 0 census matches the proxy, 1 DRIFT detected, 2 fatal (unparseable header/census).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity.d3d_census import (  # noqa: E402
    DEFAULT_CENSUS,
    DEFAULT_HEADER,
    CensusError,
    load_and_report,
    render_text,
)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="GX-00 D3D8 method census + drift guard")
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER,
                    help="the proxy's generated vtable header (default: proxy_generated.h)")
    ap.add_argument("--census", type=Path, default=DEFAULT_CENSUS,
                    help="the R3 census json (default: d3d8-method-census-v1.json)")
    ap.add_argument("--json", action="store_true", help="print the report as JSON")
    args = ap.parse_args(argv)
    try:
        report = load_and_report(args.header, args.census)
    except CensusError as exc:
        if args.json:
            print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
        else:
            print(f"d3d_census: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=1))
    else:
        print(render_text(report))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
