#!/usr/bin/env python3
"""Trace Studio v3 — sync-by-identity JOIN (the v3 alignment authority).

The v2 sync bug class (pains #2/#3): frames are paired by a reconstructed GLOBAL
label (window_start + cumulative kept-index). A non-deterministic load makes the
two sides keep different counts, the index drifts, and everything after the seam
mispairs — patched over by anchor-rebase / honest-holes / per-panel-seek in 4+
places. Identity is implied by a filename, never stored, so any input drift makes
the filenames silently lie.

v3 dissolves it (E3-proven): every kept frame carries a STORED identity
`(anchor#occ, offset-since-anchor)`, IDENTICAL on both sides for the same logical
moment no matter how far the load stretched the absolute present-count. Pairing is
a JOIN on that key — computed ONCE here, written to pairs.json, read by the diff /
seek / state table / marks alike. Loads live BETWEEN anchors and may differ freely;
each gameplay segment re-syncs at its anchor by construction. Where the join
genuinely can't pair a frame, it's an EXPLICIT, named gap — never a silent mispair.

This joins two v3cache entries (port + retail), each a container + its stored
v3meta.json identity. It also CONTRASTS with naive absolute-present pairing to show
the win: when retail load-stretches ~13k frames past the port, the two windows
share ZERO absolute presents yet the identity join pairs them frame-for-frame.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/orv3_sync.py \
      <port-entry-dir> <retail-entry-dir> [--write-pairs]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3       # noqa: E402
import v3cache    # noqa: E402


def identities(entry: Path):
    """[(index, key=(anchor,occ,offset), present)] for every kept frame, from the
    container's frame count + the entry's stored identity."""
    meta = v3cache.load_meta(entry)
    c = orv3.Container.load(entry / "v3cap.bin")
    rows = []
    for f in c.frames:
        rows.append({"index": f.index, "key": list(meta.key_of(f.index)),
                     "present": f.present})
    return meta, rows


def join(port_rows, retail_rows):
    """JOIN on the stored identity key. Returns paired (port_index, retail_index)
    plus the honest, named gaps on each side."""
    rby = {tuple(r["key"]): r for r in retail_rows}
    pby = {tuple(p["key"]): p for p in port_rows}
    pairs, port_only = [], []
    for p in port_rows:
        r = rby.get(tuple(p["key"]))
        if r:
            pairs.append({"key": p["key"], "port": p["index"], "retail": r["index"],
                          "port_present": p["present"], "retail_present": r["present"]})
        else:
            port_only.append(p)
    retail_only = [r for r in retail_rows if tuple(r["key"]) not in pby]
    return pairs, port_only, retail_only


def naive_absolute_pairs(port_rows, retail_rows):
    """Contrast: pair iff the two frames share the SAME absolute present-count. This
    is what any frame-number-based scheme reduces to once the load stretch moves the
    presents — it pairs ZERO frames when retail loads thousands of frames behind."""
    rpres = {r["present"] for r in retail_rows}
    return sum(1 for p in port_rows if p["present"] in rpres)


def main() -> int:
    ap = argparse.ArgumentParser(description="v3 sync-by-identity join (port vs retail).")
    ap.add_argument("port_entry", type=Path, help="port cache entry dir (v3cap.bin + v3meta.json)")
    ap.add_argument("retail_entry", type=Path, help="retail cache entry dir")
    ap.add_argument("--write-pairs", action="store_true",
                    help="write pairs.json (the stored join) next to the port entry")
    args = ap.parse_args()

    pmeta, prows = identities(args.port_entry)
    rmeta, rrows = identities(args.retail_entry)

    print(f"port   : {pmeta.scenario}  {pmeta.anchor}#{pmeta.anchor_occ}  "
          f"offsets {pmeta.offset0}..{pmeta.offset0 + pmeta.count - 1}  "
          f"present {prows[0]['present']}..{prows[-1]['present']}  ({len(prows)} frames)")
    print(f"retail : {rmeta.scenario}  {rmeta.anchor}#{rmeta.anchor_occ}  "
          f"offsets {rmeta.offset0}..{rmeta.offset0 + rmeta.count - 1}  "
          f"present {rrows[0]['present']}..{rrows[-1]['present']}  ({len(rrows)} frames)")
    load_stretch = rmeta.anchor_frame - pmeta.anchor_frame
    print(f"load stretch (retail anchor − port anchor present-count): {load_stretch:+d} frames")

    pairs, port_only, retail_only = join(prows, rrows)
    naive = naive_absolute_pairs(prows, rrows)

    print(f"\nv3 identity-join (anchor#occ, offset-since-anchor):")
    print(f"  paired           : {len(pairs)} / {min(len(prows), len(rrows))}")
    print(f"  port-only  (gaps): {len(port_only)}"
          + (f"  e.g. {[p['key'] for p in port_only[:3]]}" if port_only else ""))
    print(f"  retail-only(gaps): {len(retail_only)}"
          + (f"  e.g. {[r['key'] for r in retail_only[:3]]}" if retail_only else ""))
    if pairs:
        e = pairs[0]
        print(f"  e.g. {tuple(e['key'])}: port#{e['port']}(present {e['port_present']}) "
              f"== retail#{e['retail']}(present {e['retail_present']})  "
              f"— {e['retail_present'] - e['port_present']:+d} absolute, SAME moment")

    naive_msg = ("load stretch ⇒ zero shared presents — frame-number pairing is hopeless"
                 if naive == 0 else "windows happen to overlap in present space")
    print(f"\ncontrast — naive absolute-present pairing (same present-count):")
    print(f"  paired           : {naive} / {min(len(prows), len(rrows))}   ({naive_msg})")

    verdict = "ALIGNED" if (not port_only and not retail_only) else \
              f"PARTIAL ({len(pairs)} paired, {len(port_only)}+{len(retail_only)} honest gaps)"
    print(f"\nVERDICT: {verdict}")

    if args.write_pairs:
        out = args.port_entry / "pairs.json"
        out.write_text(json.dumps({
            "port_entry": str(args.port_entry), "retail_entry": str(args.retail_entry),
            "anchor": pmeta.anchor, "verdict": verdict,
            "pairs": pairs, "port_only": port_only, "retail_only": retail_only,
        }, indent=1))
        print(f"wrote {out} ({len(pairs)} pairs)")

    # A report tool: honest gaps are a real structural fact (one side genuinely has a
    # frame the other doesn't), not a tool failure — so always exit 0. The VERDICT
    # line + pairs.json carry the alignment state for a caller that wants it.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
