#!/usr/bin/env python3
"""tools/d3d_census.py — GX-00 D3D8 method census (CLI).

Print the capture-completeness census of the v3 proxy: how many D3D8 vtable methods
are RECORDED vs FORWARDED, and — the point — which forwarded methods are
`render_affecting_unsupported` (they CAN alter future pixels/resources/device state but
aren't captured, so a `pixels`/`render_program` pillar over a scene that calls one would
diverge silently). Cross-checks `proxy_generated.h` against the R3 census
`docs/schemas/d3d8-method-census-v1.json`; any drift (a method flipped recorded↔forwarded,
added, removed, or misclassified) is reported.

With `--dynamic <v3cap.census.json>` it instead runs the DYNAMIC census / GX-01 gate on a
proxy-emitted sidecar from a real drive: which forwarded methods were actually CALLED. A
render_affecting_unsupported method with a non-zero count means the capture is INCOMPLETE
for that scene (a pixels/render_program PASS over it would be unsound).

Exit: static — 0 census matches the proxy, 1 DRIFT, 2 fatal. dynamic — 0 SAFE (every risk
method 0-observed), 1 VIOLATION (a risk method fired), 2 INCONCLUSIVE/fatal.
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
    build_dynamic_report,
    load_and_report,
    load_census,
    load_dynamic,
    render_dynamic_text,
    render_text,
)

_DYNAMIC_EXIT = {"SAFE": 0, "VIOLATION": 1, "INCONCLUSIVE": 2}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="GX-00 D3D8 method census + drift guard")
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER,
                    help="the proxy's generated vtable header (default: proxy_generated.h)")
    ap.add_argument("--census", type=Path, default=DEFAULT_CENSUS,
                    help="the R3 census json (default: d3d8-method-census-v1.json)")
    ap.add_argument("--dynamic", type=Path, default=None, metavar="SIDECAR",
                    help="a proxy v3cap.census.json sidecar → run the DYNAMIC census "
                         "(GX-01 gate) instead of the static drift check: exit 0 SAFE "
                         "(every risk method 0-observed), 1 VIOLATION (a render-affecting-"
                         "unsupported method fired), 2 INCONCLUSIVE (sidecar incomplete)")
    ap.add_argument("--json", action="store_true", help="print the report as JSON")
    args = ap.parse_args(argv)

    if args.dynamic is not None:
        try:
            report = build_dynamic_report(load_census(args.census), load_dynamic(args.dynamic))
        except CensusError as exc:
            if args.json:
                print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
            else:
                print(f"d3d_census: {exc}", file=sys.stderr)
            return 2
        print(json.dumps(report, indent=1) if args.json else render_dynamic_text(report))
        return _DYNAMIC_EXIT[report["verdict"]]

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
