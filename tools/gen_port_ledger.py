#!/usr/bin/env python3
"""Generate a derived port ledger for OpenRecet (EP-06 lifecycle model).

The ledger maps every engine function (from the Ghidra export
``docs/decompiled/functions.csv``) to a **two-axis lifecycle**, derived purely
from artifacts already in the tree — it is regenerated, never hand-maintained,
so it cannot drift.  The two axes are kept SEPARATE (EP-06: "do not
collapse to one strongest global label"); a function can be retail-executed yet
unimplemented, so a single label would lie.

  INVENTORY axis  (source-derived — NO runtime claim)
  ---------------  source of truth
  discovered        non-thunk in functions.csv (the universe floor)
  source-referenced a bare FUN_<va> appears in src/ (a mention/provenance —
                    NOT a port claim; a comment cannot claim implementation)
  implemented       a PORT-OF(0xVA) attestation OR a CALL_TRACE_ENTER(_STUB)
                    probe (you cannot instrument a function you did not port)
  instrumented      a CALL_TRACE_ENTER(0xVA) probe wired (runtime-DIFFABLE, not
                    runtime-PROVEN).  _STUB → instrumented + a "stub" flag.

  RUNTIME axis  (needs runtime evidence — none is bound today)
  ------------
  ``runtime_state`` is null for every function.  The 2026-07 proof-bundle
  binding (parity_prove + docs/parity-proof-index.json) was retired on
  2026-10-13 with the methodology reset; runtime evidence is to come from the
  in-process retail host (docs/findings/in-process-retail-host.md).  The field
  stays in the JSON so consumers keep a stable shape.

A DEPRECATED legacy ``status`` (verified/stubbed/ported/unported) is preserved
per function for existing consumers (mem_watch.py) and human continuity.

Outputs (all under docs/, all git-tracked — no vendor bytes):
  docs/port-ledger.json   full per-VA lifecycle map (machine-readable)
  docs/port-ledger.md     human-readable summary + per-state VA lists
  docs/STATUS.md          60-second headline: inventory ladder + runtime deficit

Run from the repo root:  python3 tools/gen_port_ledger.py
Add --check to fail (exit 3) if the on-disk ledger is stale (for a pre-commit
hook): it regenerates into memory and diffs against what's committed.

Design history: commit 2f9bad71 (EP-06), simplified 2026-10-13.
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

# The active front is the ONE hand-edited status block.  It lives in docs/FRONT.md
# (between the FRONT:BEGIN/FRONT:END markers) and is injected here verbatim, so STATUS
# can never drift from reality.  Everything else in STATUS is derived from code.
FRONT_MD = REPO / "docs" / "FRONT.md"

# --- inventory ladder (EP-06) ---------------------------------------------
INVENTORY_LADDER = ["discovered", "source-referenced", "implemented", "instrumented"]


def read_front() -> str:
    """Return the hand-edited 'current front' block from docs/FRONT.md (the text
    between the FRONT:BEGIN / FRONT:END markers).  Falls back to a stub if missing."""
    try:
        text = FRONT_MD.read_text(encoding="utf-8")
    except FileNotFoundError:
        return "- (docs/FRONT.md missing — add it; see gen_port_ledger.py read_front)"
    begin = text.find("<!-- FRONT:BEGIN -->")
    end = text.find("<!-- FRONT:END -->")
    if begin == -1 or end == -1:
        return text.strip()
    body = text[begin + len("<!-- FRONT:BEGIN -->"):end].strip()
    return body

PROBE_FULL_RE = re.compile(r"CALL_TRACE_ENTER\(\s*0x([0-9a-fA-F]+)u?\s*\)")
PROBE_STUB_RE = re.compile(r"CALL_TRACE_ENTER_STUB\(\s*0x([0-9a-fA-F]+)u?\s*\)")
FUN_RE = re.compile(r"FUN_([0-9a-fA-F]{6,8})")
# PORT-OF(0xVA) — an opt-in, comment-only author attestation that the enclosing
# src function faithfully ports engine FUN_<va>.  INVENTORY-level (source
# fidelity, like objdump-exact), NOT a runtime claim.
PORT_OF_RE = re.compile(r"PORT-OF\(\s*0x([0-9a-fA-F]+)u?\s*\)")
# PORT-DEBT(...) markers — MVP/synthetic shortcuts inside ported code. The full
# registry is tools/gen_port_debt.py → docs/port-debt.{md,json}; here we only
# count them (own scan, no inter-tool dependency) for the STATUS headline.
PORT_DEBT_RE = re.compile(r"PORT-DEBT\(\s*[a-z-]+\s*,")


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


def scan_src():
    """Return (verified, stubbed, ported, port_of) VA -> [src files] maps."""
    verified: dict[int, set[str]] = {}
    stubbed: dict[int, set[str]] = {}
    ported: dict[int, set[str]] = {}
    port_of: dict[int, set[str]] = {}
    for path in sorted(SRC.rglob("*.c")) + sorted(SRC.rglob("*.h")):
        rel = str(path.relative_to(REPO))
        text = path.read_text(errors="replace")
        for m in PROBE_FULL_RE.finditer(text):
            verified.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in PROBE_STUB_RE.finditer(text):
            stubbed.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in FUN_RE.finditer(text):
            ported.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in PORT_OF_RE.finditer(text):
            port_of.setdefault(int(m.group(1), 16), set()).add(rel)
    sort = lambda d: {va: sorted(files) for va, files in d.items()}
    return sort(verified), sort(stubbed), sort(ported), sort(port_of)


def count_port_debt() -> int:
    """Count PORT-DEBT(...) markers in src/ (the registry's headline number).

    Independent re-derivation of tools/gen_port_debt.py's total so STATUS stays
    self-contained — no read of the derived port-debt.json, no tool ordering
    dependency. Both scans are pure functions of src/, so they agree."""
    n = 0
    for path in sorted(SRC.rglob("*.c")) + sorted(SRC.rglob("*.h")):
        n += len(PORT_DEBT_RE.findall(path.read_text(errors="replace")))
    return n


def inventory_state(has_probe, has_stub, has_port_of, has_fun) -> str:
    """Furthest INVENTORY rung with source evidence (monotonic)."""
    if has_probe or has_stub:
        return "instrumented"
    if has_port_of:
        return "implemented"
    if has_fun:
        return "source-referenced"
    return "discovered"


def legacy_status(has_probe, has_stub, has_ref) -> str:
    """DEPRECATED per-function alias — kept byte-identical for mem_watch.py and
    human continuity.  verified=full probe, stubbed=stub probe, ported=any src
    reference (FUN_ or PORT-OF), unported=none."""
    if has_probe:
        return "verified"
    if has_stub:
        return "stubbed"
    if has_ref:
        return "ported"
    return "unported"


def classify(funcs, call_targets, verified, stubbed, ported, port_of):
    """Assign the two-axis lifecycle per engine VA, plus collect 'referenced but
    not an engine function' VAs (indirect/vtable-target or sub-helper hints)."""
    entries: dict[int, dict] = {}
    known = set(funcs)
    for va, info in funcs.items():
        has_probe = va in verified
        has_stub = va in stubbed
        has_port_of = va in port_of
        has_fun = va in ported
        has_ref = has_fun or has_port_of

        inv = inventory_state(has_probe, has_stub, has_port_of, has_fun)
        quality = ["stub"] if (has_stub and not has_probe) else []

        # src files that reference this VA (probe / FUN_ / PORT-OF), deduped.
        src = sorted(set(verified.get(va, [])) | set(stubbed.get(va, []))
                     | set(ported.get(va, [])) | set(port_of.get(va, [])))

        evidence = {
            "discovered": True,
            "source_referenced": has_ref,
            "implemented": has_probe or has_port_of,
            "instrumented": has_probe or has_stub,
            "instrumented_stub": has_stub and not has_probe,
        }

        entries[va] = {
            "va": f"0x{va:06x}",
            "name": info["name"],
            "size": info["size"],
            "is_thunk": info["is_thunk"],
            "is_call_target": va in call_targets,
            "inventory_state": inv,
            "runtime_state": None,        # no runtime evidence bound (see docstring)
            "evidence": evidence,
            "quality_flags": quality,
            "src": src,
            "status": legacy_status(has_probe, has_stub, has_ref),  # DEPRECATED alias
        }
    # Probed/ported/attested VAs the function table doesn't know (indirect
    # targets, or VAs naming sub-helpers inside a larger Ghidra function).
    orphans = sorted(
        (set(verified) | set(stubbed) | set(ported) | set(port_of)) - known)
    return entries, orphans


def summarize(entries, funcs, orphans, port_debt=0):
    real = {va: e for va, e in entries.items() if not e["is_thunk"]}
    by_inv = lambda s: sum(1 for e in real.values() if e["inventory_state"] == s)
    instrumented_stub = sum(1 for e in real.values()
                            if e["inventory_state"] == "instrumented" and "stub" in e["quality_flags"])
    instrumented = by_inv("instrumented")
    implemented_only = by_inv("implemented")
    source_referenced_only = by_inv("source-referenced")
    discovered_only = by_inv("discovered")
    runtime_proven = sum(1 for e in real.values() if e["runtime_state"] is not None)

    non_thunk = len(real)
    # "referenced-or-better" = anything with a src marker (the old "touched").
    referenced_plus = instrumented + implemented_only + source_referenced_only

    counts = {
        "engine_functions_total": len(funcs),
        "non_thunk_functions": non_thunk,
        # --- INVENTORY axis (furthest rung) ---
        "inv_instrumented": instrumented,
        "inv_instrumented_full": instrumented - instrumented_stub,
        "inv_instrumented_stub": instrumented_stub,
        "inv_implemented": implemented_only,
        "inv_source_referenced": source_referenced_only,
        "inv_discovered": discovered_only,
        "referenced_or_better": referenced_plus,
        "pct_referenced_or_better": round(100.0 * referenced_plus / max(1, non_thunk), 1),
        # --- RUNTIME axis (no runtime evidence bound yet) ---
        "runtime_proven": runtime_proven,
        "pct_runtime_proven": round(100.0 * runtime_proven / max(1, non_thunk), 1),
        "orphan_refs_not_in_function_table": len(orphans),
        "port_debt": port_debt,
        # --- DEPRECATED legacy aliases (kept for existing consumers) ---
        "verified": instrumented - instrumented_stub,
        "stubbed": instrumented_stub,
        "ported": source_referenced_only + implemented_only,
        "unported": discovered_only,
        "touched": referenced_plus,
        "pct_touched": round(100.0 * referenced_plus / max(1, non_thunk), 1),
        "pct_verified": round(100.0 * runtime_proven / max(1, non_thunk), 1),
    }
    return counts


def render_json(entries, orphans, counts) -> str:
    payload = {
        "_generated_by": "tools/gen_port_ledger.py",
        "_note": "DERIVED FILE — do not edit by hand; run the generator. "
                 "inventory_state = source markers (NOT runtime proof); "
                 "runtime_state = null for every function (no runtime evidence "
                 "bound since the 2026-10-13 methodology reset). 'status' is a "
                 "DEPRECATED legacy alias. Output is a pure function of src/ + "
                 "functions.csv + the VAs JSON, so it is idempotent (safe for a "
                 "pre-commit --check).",
        "counts": counts,
        "orphan_refs": [f"0x{va:06x}" for va in orphans],
        "functions": [entries[va] for va in sorted(entries)],
    }
    return json.dumps(payload, indent=1) + "\n"


def render_status(counts) -> str:
    c = counts
    bar_n = round(c["pct_referenced_or_better"] / 5)  # 20-cell bar
    bar = "█" * bar_n + "░" * (20 - bar_n)
    return f"""# OpenRecet — status at a glance

> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`
> (also auto-regenerated by the pre-commit hook). Headline numbers only;
> narrative lives in `PROGRESS.md`, durable RE in `findings/`, full map in
> `port-ledger.md`.

## Port INVENTORY (source markers — NOT runtime proof)

```
{bar}  {c['pct_referenced_or_better']}% referenced-or-better   ({c['inv_instrumented']} instrumented)
```

| inventory state    | count | evidence (a `src/` marker only — no runtime) |
|--------------------|------:|----------------------------------------------|
| instrumented       | {c['inv_instrumented']:>5} | `CALL_TRACE_ENTER(_STUB)` probe wired ({c['inv_instrumented_full']} full + {c['inv_instrumented_stub']} stub) |
| implemented        | {c['inv_implemented']:>5} | `PORT-OF(0xVA)` author attestation, no probe |
| source-referenced  | {c['inv_source_referenced']:>5} | a `FUN_<va>` appears in `src/` (mention/provenance — **not** a port claim) |
| discovered         | {c['inv_discovered']:>5} | exists in the engine, no `src/` reference    |
| **total non-thunk**| **{c['non_thunk_functions']:>3}** | of {c['engine_functions_total']} incl. thunks |

## Port RUNTIME proof (cross-target)

```
{c['runtime_proven']} function{'' if c['runtime_proven'] == 1 else 's'} runtime-proven   ({c['pct_runtime_proven']}%)
```

**INVENTORY ≠ PARITY**: an instrumented probe is a *diffable* point, not proof a
function ran or matched retail. No runtime evidence is bound today: the
2026-07 proof-bundle binding was retired on 2026-10-13 (methodology reset,
`audits/2026-10-13-methodology-reset.md`); runtime evidence is to come from the
in-process retail host (`findings/in-process-retail-host.md`). Prior human
attestations in `findings/confirmed-parity-ledger.md` are UNVERIFIED pending
re-verification.

{c['orphan_refs_not_in_function_table']} VAs are referenced in src/ but absent from the function table
(indirect/vtable targets or sub-helpers) — see `port-ledger.json` `orphan_refs`.

**Port debt:** {c['port_debt']} `PORT-DEBT(...)` markers — MVP/synthetic shortcuts
inside code with a `src/` marker (they silently cap structural parity).
Registry: `port-debt.md` / `.json`; retirement plan: `plans/un-mvp-structural-parity.md`.

## Current front

> Hand-edited in `docs/FRONT.md` (the one status block); injected here verbatim.

{read_front()}

## Where to read next

- `STATUS.md` (this file) — 60-second orientation.
- `port-ledger.md` / `.json` — per-function lifecycle (derived).
- `port-debt.md` / `.json` — MVP/synthetic shortcuts inside ported code (derived).
- `PROGRESS.md` — dated narrative changelog.
- `findings/INDEX.md` — map of subsystem RE writeups.
- `audits/2026-10-13-methodology-reset.md` — current methodology decision record.
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
        "Per-engine-function lifecycle, two independent axes: an **INVENTORY** rung",
        "derived from `src/` markers (`FUN_` / `PORT-OF` / `CALL_TRACE_ENTER`) and a",
        "**RUNTIME** rung that needs runtime evidence (none bound since the",
        "2026-10-13 methodology reset). A `src/` marker is INVENTORY, never parity.",
        "",
        "## Summary",
        "",
        f"- non-thunk engine functions: **{c['non_thunk_functions']}** "
        f"(of {c['engine_functions_total']} incl. thunks)",
        f"- **inventory:** instrumented **{c['inv_instrumented']}** "
        f"({c['inv_instrumented_full']} full + {c['inv_instrumented_stub']} stub), "
        f"implemented {c['inv_implemented']}, "
        f"source-referenced {c['inv_source_referenced']}, "
        f"discovered {c['inv_discovered']} "
        f"→ {c['referenced_or_better']} referenced-or-better ({c['pct_referenced_or_better']}%)",
        f"- **runtime:** {c['runtime_proven']} proven ({c['pct_runtime_proven']}%) "
        f"— no runtime evidence bound",
        f"- orphan refs (in src/, not in function table): {c['orphan_refs_not_in_function_table']}",
        "",
    ]
    real = [entries[va] for va in sorted(entries) if not entries[va]["is_thunk"]]
    for state, blurb in (
        ("instrumented", "CALL_TRACE_ENTER(_STUB) probe wired (INVENTORY — not runtime-proven)"),
        ("implemented", "PORT-OF(0xVA) attestation, no probe (INVENTORY)"),
        ("source-referenced", "a FUN_<va> mention in src/ — NOT a port claim"),
    ):
        rows = [e for e in real if e["inventory_state"] == state]
        lines.append(f"## {state} ({len(rows)}) — {blurb}")
        lines.append("")
        lines.append("| VA | name | size | call-target | flags | src |")
        lines.append("|----|------|-----:|:-----------:|-------|-----|")
        for e in rows:
            ct = "✓" if e["is_call_target"] else ""
            flags = ",".join(e["quality_flags"])
            src = ", ".join(Path(s).name for s in e["src"][:3])
            if len(e["src"]) > 3:
                src += f" (+{len(e['src']) - 3})"
            lines.append(
                f"| {e['va']} | {e['name']} | {e['size']} | {ct} | {flags} | {src} |"
            )
        lines.append("")
    # Runtime-proven functions (empty today) — an explicit section so the axis is
    # visible even at zero.
    proven = [e for e in real if e["runtime_state"] is not None]
    lines.append(f"## runtime-proven ({len(proven)})")
    lines.append("")
    lines.append("_None — no runtime evidence is bound (INVENTORY ≠ PARITY). The "
                 "2026-07 proof-bundle bindings were retired on 2026-10-13; re-verify "
                 "under the in-process retail host._")
    lines.append("")
    return "\n".join(lines) + "\n"


def build():
    """Pure builder: returns (entries, orphans, counts). Used by tests."""
    funcs = load_engine_functions()
    call_targets = load_call_targets()
    verified, stubbed, ported, port_of = scan_src()
    entries, orphans = classify(
        funcs, call_targets, verified, stubbed, ported, port_of)
    counts = summarize(entries, funcs, orphans, count_port_debt())
    return entries, orphans, counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="exit 3 if on-disk ledger differs from a fresh gen")
    args = ap.parse_args()

    entries, orphans, counts = build()

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
    print(f"  inventory: {counts['referenced_or_better']}/{counts['non_thunk_functions']} "
          f"referenced-or-better ({counts['pct_referenced_or_better']}%), "
          f"{counts['inv_instrumented']} instrumented; "
          f"runtime: {counts['runtime_proven']} proven")
    return 0


if __name__ == "__main__":
    sys.exit(main())
