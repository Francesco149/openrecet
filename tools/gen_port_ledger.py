#!/usr/bin/env python3
"""Generate a derived port ledger for OpenRecet.

The ledger maps every engine function (from the Ghidra export
``docs/decompiled/functions.csv``) to its port status, derived purely from
artifacts already in the tree — it is regenerated, never hand-maintained, so
it cannot drift:

  status        source of truth
  ------        ---------------
  verified      a CALL_TRACE_ENTER(0xVA) probe in src/  (runtime-diffed vs retail)
  stubbed       a CALL_TRACE_ENTER_STUB(0xVA) probe in src/
  ported        the VA appears as FUN_<va> in a src/ comment/provenance header
                but carries no call-trace probe
  unported      present in the engine but never referenced from src/

Outputs (all under docs/, all git-tracked — they contain no vendor bytes):
  docs/port-ledger.json   full per-VA map (machine-readable; feeds E.3 diff)
  docs/port-ledger.md     human-readable summary table + per-status VA lists
  docs/STATUS.md          60-second headline: counts, %, current phase, blocker

Run from the repo root:  python3 tools/gen_port_ledger.py
Add --check to fail (exit 3) if the on-disk ledger is stale (for a pre-commit
hook): it regenerates into memory and diffs against what's committed.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"
FUNCTIONS_CSV = REPO / "docs" / "decompiled" / "functions.csv"
CALL_TARGETS_JSON = REPO / "tools" / "ttd" / "data" / "engine_function_vas.json"

LEDGER_JSON = REPO / "docs" / "port-ledger.json"
LEDGER_MD = REPO / "docs" / "port-ledger.md"
STATUS_MD = REPO / "docs" / "STATUS.md"

# These are kept out of band so STATUS stays a 60-second read.  Edit the two
# lines below when the active front moves; everything else is derived.
CURRENT_PHASE = "Phase E — leaf-first execution parity (harness-roadmap.md §E)"
CURRENT_BLOCKER = (
    "Cf.* HOUSE shop_table writer chunk (no-decompile-writer region) — "
    "blocks visible HOUSE pixels; D.7 mem_watch would unblock"
)

PROBE_FULL_RE = re.compile(r"CALL_TRACE_ENTER\(\s*0x([0-9a-fA-F]+)u?\s*\)")
PROBE_STUB_RE = re.compile(r"CALL_TRACE_ENTER_STUB\(\s*0x([0-9a-fA-F]+)u?\s*\)")
FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{6,8})")


def load_engine_functions() -> dict[int, dict]:
    """All engine functions from the Ghidra export, keyed by entry VA."""
    funcs: dict[int, dict] = {}
    with FUNCTIONS_CSV.open() as fh:
        for row in csv.DictReader(fh):
            try:
                va = int(row["entry"], 16)
            except (KeyError, ValueError):
                continue
            funcs[va] = {
                "name": row.get("name", ""),
                "size": int(row.get("size") or 0),
                "is_thunk": (row.get("is_thunk") == "true"),
            }
    return funcs


def load_call_targets() -> set[int]:
    data = json.loads(CALL_TARGETS_JSON.read_text())
    return set(data.get("vas", []))


def scan_src() -> tuple[dict[int, list[str]], dict[int, list[str]], dict[int, list[str]]]:
    """Return (verified, stubbed, ported) VA -> [src files] maps."""
    verified: dict[int, set[str]] = {}
    stubbed: dict[int, set[str]] = {}
    ported: dict[int, set[str]] = {}
    for path in sorted(SRC.rglob("*.c")) + sorted(SRC.rglob("*.h")):
        rel = str(path.relative_to(REPO))
        text = path.read_text(errors="replace")
        for m in PROBE_FULL_RE.finditer(text):
            verified.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in PROBE_STUB_RE.finditer(text):
            stubbed.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in FUN_RE.finditer(text):
            ported.setdefault(int(m.group(1), 16), set()).add(rel)
    sort = lambda d: {va: sorted(files) for va, files in d.items()}
    return sort(verified), sort(stubbed), sort(ported)


def classify(funcs, call_targets, verified, stubbed, ported):
    """Assign one status per engine VA, plus collect 'ported but not an engine
    function' VAs (the indirect/vtable-target hint)."""
    entries: dict[int, dict] = {}
    known = set(funcs)
    for va, info in funcs.items():
        if va in verified:
            status, files = "verified", verified[va]
        elif va in stubbed:
            status, files = "stubbed", stubbed[va]
        elif va in ported:
            status, files = "ported", ported[va]
        else:
            status, files = "unported", []
        entries[va] = {
            "va": f"0x{va:06x}",
            "name": info["name"],
            "size": info["size"],
            "is_thunk": info["is_thunk"],
            "is_call_target": va in call_targets,
            "status": status,
            "src": files,
        }
    # Probed/ported VAs the function table doesn't know (indirect targets,
    # or VAs naming sub-helpers inside a larger Ghidra function).
    orphans = sorted((set(verified) | set(stubbed) | set(ported)) - known)
    return entries, orphans


def summarize(entries, funcs, orphans):
    real = {va: e for va, e in entries.items() if not e["is_thunk"]}
    by = lambda s: sum(1 for e in real.values() if e["status"] == s)
    counts = {
        "engine_functions_total": len(funcs),
        "non_thunk_functions": len(real),
        "verified": by("verified"),
        "stubbed": by("stubbed"),
        "ported": by("ported"),
        "unported": by("unported"),
        "orphan_refs_not_in_function_table": len(orphans),
    }
    touched = counts["verified"] + counts["stubbed"] + counts["ported"]
    counts["touched"] = touched
    counts["pct_touched"] = round(100.0 * touched / max(1, counts["non_thunk_functions"]), 1)
    counts["pct_verified"] = round(100.0 * counts["verified"] / max(1, counts["non_thunk_functions"]), 1)
    return counts


def render_json(entries, orphans, counts) -> str:
    payload = {
        "_generated_by": "tools/gen_port_ledger.py",
        "_note": "DERIVED FILE — do not edit by hand; run the generator. "
                 "Output is a pure function of src/ + functions.csv + the VAs "
                 "JSON, so it is idempotent (safe for a pre-commit --check).",
        "counts": counts,
        "orphan_refs": [f"0x{va:06x}" for va in orphans],
        "functions": [entries[va] for va in sorted(entries)],
    }
    return json.dumps(payload, indent=1) + "\n"


def render_status(counts) -> str:
    c = counts
    bar_n = round(c["pct_touched"] / 5)  # 20-cell bar
    bar = "█" * bar_n + "░" * (20 - bar_n)
    return f"""# OpenRecet — status at a glance

> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`
> (also auto-regenerated by the pre-commit hook). Headline numbers only;
> narrative lives in `PROGRESS.md`, durable RE in `findings/`, full map in
> `port-ledger.md`.

## Port coverage (non-thunk engine functions)

```
{bar}  {c['pct_touched']}% touched   ({c['pct_verified']}% runtime-verified)
```

| status    | count | what it means                                            |
|-----------|------:|----------------------------------------------------------|
| verified  | {c['verified']:>5} | CALL_TRACE_ENTER probe, runtime-diffed vs retail         |
| stubbed   | {c['stubbed']:>5} | CALL_TRACE_ENTER_STUB — wired but body incomplete        |
| ported    | {c['ported']:>5} | reimplemented in src/, no runtime probe yet              |
| **touched** | **{c['touched']:>3}** | verified + stubbed + ported                         |
| unported  | {c['unported']:>5} | exists in engine, never referenced from src/             |
| **total** | **{c['non_thunk_functions']:>3}** | non-thunk engine functions (of {c['engine_functions_total']} incl. thunks) |

{c['orphan_refs_not_in_function_table']} VAs are referenced in src/ but absent from the function table
(indirect/vtable targets or sub-helpers) — see `port-ledger.json` `orphan_refs`.

## Current front

- **Phase:** {CURRENT_PHASE}
- **Top blocker:** {CURRENT_BLOCKER}

## Where to read next

- `STATUS.md` (this file) — 60-second orientation.
- `port-ledger.md` / `.json` — per-function port status (derived).
- `PROGRESS.md` — dated narrative changelog.
- `findings/INDEX.md` — map of subsystem RE writeups.
- `harness-roadmap.md` — verification/tooling phases (A–E).
- `AGENT-WORKFLOW.md` — how to work on this repo.
"""


def render_md(entries, orphans, counts) -> str:
    c = counts
    lines = [
        "# OpenRecet — port ledger",
        "",
        "> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`.",
        "> See `STATUS.md` for the headline.",
        "",
        "Per-engine-function port status, derived from `functions.csv` (universe),",
        "`CALL_TRACE_ENTER(_STUB)` probes (verified/stubbed), and `FUN_` references",
        "in `src/` (ported). This is the answer to *\"is FUN_x done?\"* at a glance.",
        "",
        "## Summary",
        "",
        f"- non-thunk engine functions: **{c['non_thunk_functions']}** "
        f"(of {c['engine_functions_total']} incl. thunks)",
        f"- touched: **{c['touched']}** ({c['pct_touched']}%) — "
        f"verified {c['verified']}, stubbed {c['stubbed']}, ported {c['ported']}",
        f"- unported: **{c['unported']}**",
        f"- orphan refs (in src/, not in function table): {c['orphan_refs_not_in_function_table']}",
        "",
    ]
    real = [entries[va] for va in sorted(entries) if not entries[va]["is_thunk"]]
    for status, blurb in (
        ("verified", "runtime-diffed vs retail"),
        ("stubbed", "wired, body incomplete"),
        ("ported", "reimplemented, no probe yet"),
    ):
        rows = [e for e in real if e["status"] == status]
        lines.append(f"## {status} ({len(rows)}) — {blurb}")
        lines.append("")
        lines.append("| VA | name | size | call-target | src |")
        lines.append("|----|------|-----:|:-----------:|-----|")
        for e in rows:
            ct = "✓" if e["is_call_target"] else ""
            src = ", ".join(Path(s).name for s in e["src"][:3])
            if len(e["src"]) > 3:
                src += f" (+{len(e['src']) - 3})"
            lines.append(
                f"| {e['va']} | {e['name']} | {e['size']} | {ct} | {src} |"
            )
        lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="exit 3 if on-disk ledger differs from a fresh gen")
    args = ap.parse_args()

    funcs = load_engine_functions()
    call_targets = load_call_targets()
    verified, stubbed, ported = scan_src()
    entries, orphans = classify(funcs, call_targets, verified, stubbed, ported)
    counts = summarize(entries, funcs, orphans)

    out = {
        LEDGER_JSON: render_json(entries, orphans, counts),
        LEDGER_MD: render_md(entries, orphans, counts),
        STATUS_MD: render_status(counts),
    }

    if args.check:
        stale = [p.name for p, txt in out.items()
                 if not p.exists() or p.read_text() != txt]
        if stale:
            print(f"port ledger stale: {', '.join(stale)} "
                  f"(run: python3 tools/gen_port_ledger.py)", file=sys.stderr)
            return 3
        print("port ledger up to date")
        return 0

    for path, txt in out.items():
        path.write_text(txt)
    print(f"wrote {LEDGER_JSON.name}, {LEDGER_MD.name}, {STATUS_MD.name}")
    print(f"  {counts['touched']}/{counts['non_thunk_functions']} touched "
          f"({counts['pct_touched']}%), {counts['verified']} verified, "
          f"{counts['unported']} unported")
    return 0


if __name__ == "__main__":
    sys.exit(main())
