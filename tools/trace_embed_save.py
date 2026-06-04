#!/usr/bin/env python3
"""tools/trace_embed_save.py — embed a save file into one or more TAS traces.

Content-addresses + gzip-compresses the given raw save into the trace's ``_saves/``
store and inserts a ``{"savefile":"<ref>"}`` op at the top of each trace, so a replay
of that trace loads THIS save (via ``--save-override``) regardless of whatever
save.dat is on disk. See tools/trace_save.py for the storage model.

Usage:
    # embed a save into specific traces
    trace_embed_save.py SAVE.dat tests/scenarios/house-walk-left/trace.jsonl ...

    # embed into every scenario trace (the "embed current save into all traces" test)
    trace_embed_save.py SAVE.dat --all-scenarios

    # the live game's save (Carpe Fulgur EN Steam) lives in the install dir:
    trace_embed_save.py "/mnt/c/Program Files (x86)/Steam/steamapps/common/Recettear/save.dat" --all-scenarios
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_save

ROOT = Path(__file__).resolve().parent.parent
SCENARIOS = ROOT / "tests" / "scenarios"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("save", nargs="?",
                    help="raw save file to embed (e.g. the game's save.dat); "
                         "omit with --fresh")
    ap.add_argument("traces", nargs="*", help="trace.jsonl files to embed into")
    ap.add_argument("--fresh", action="store_true",
                    help="embed the @fresh sentinel (boot with no save → fresh "
                         "menu, no LOAD GAME) instead of a save file. For "
                         "'new game' scenarios that must be save-independent.")
    ap.add_argument("--all-scenarios", action="store_true",
                    help="embed into every tests/scenarios/*/trace.jsonl")
    ap.add_argument("--saves-dir",
                    help="override the content store dir (default: per-trace _saves/)")
    args = ap.parse_args(argv)

    traces = [Path(t) for t in args.traces]
    # In --fresh mode the first positional may actually be a trace, not a save.
    if args.fresh and args.save and args.save.endswith(".jsonl"):
        traces.insert(0, Path(args.save))
        args.save = None
    if args.all_scenarios:
        traces += sorted(SCENARIOS.glob("*/trace.jsonl"))
    if not traces:
        print("trace_embed_save: no traces given (pass paths or --all-scenarios)",
              file=sys.stderr)
        return 1

    if args.fresh:
        n = 0
        for tp in traces:
            if not tp.exists():
                print(f"  SKIP {tp} (not found)", file=sys.stderr)
                continue
            trace_save.embed_in_trace(tp, trace_save.FRESH_REF)
            print(f"  embedded @fresh → {tp}")
            n += 1
        print(f"trace_embed_save: marked {n} trace(s) @fresh (boot with no save)")
        return 0

    if not args.save:
        print("trace_embed_save: need a save file (or pass --fresh)", file=sys.stderr)
        return 1
    save = Path(args.save)
    if not save.exists():
        print(f"trace_embed_save: save not found: {save}", file=sys.stderr)
        return 1
    size = save.stat().st_size
    if size != trace_save.SAVE_ARENA_BYTES:
        print(f"trace_embed_save: WARNING — {save} is {size} bytes, expected "
              f"{trace_save.SAVE_ARENA_BYTES} (SAVE_BANK_ARENA_BYTES). Embedding "
              f"anyway; the port loads any size <= arena, but a wrong size usually "
              f"means the wrong file.", file=sys.stderr)

    sha = trace_save.sha256_file(save)
    print(f"trace_embed_save: save {save} sha256={sha} ({size} bytes)")
    n = 0
    for tp in traces:
        if not tp.exists():
            print(f"  SKIP {tp} (not found)", file=sys.stderr)
            continue
        store = (Path(args.saves_dir).resolve() if args.saves_dir
                 else trace_save.default_store_dir(tp))
        _, blob = trace_save.store_save(save, store, sha=sha)
        ref = trace_save._rel_ref(tp, blob)
        trace_save.embed_in_trace(tp, ref, sha=sha)
        print(f"  embedded → {tp}  (ref: {ref})")
        n += 1
    print(f"trace_embed_save: embedded into {n} trace(s); blob in content store as "
          f"{sha[:12]}…{trace_save.SAVE_SUFFIX}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
