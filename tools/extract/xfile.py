#!/usr/bin/env python3
"""
tools/extract/xfile.py — DirectX .x text-format model summarizer.

The DirectX retained-mode `.x` file format is fully documented in the legacy
DirectX SDK. Format header: `xof 0303txt 0032` (version 3.3, text encoding,
32-bit floats). It's structured templates (Mesh, Frame, Material, …) with
nested braces.

This tool does NOT yet build a full parser — that's phase 2 work, when we
need to verify our loader matches the engine's interpretation. For now it
walks the templates at the top level, counts them, and dumps a JSON summary.
That's enough to validate the extractor pipeline and detect any unexpected
format quirks Recettear's files might have (e.g., custom templates).

Usage:
    extract/xfile.py path/to/model.x [model2.x ...]
    extract/xfile.py --scan vendor/original/xfile/         # walk a tree
    extract/xfile.py --scan vendor/original/xfile/ --out scan.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path


XFILE_HEADER_RE = re.compile(rb"xof (\d{4})(txt|bin|tzip|bzip) (\d{4})")


def header_info(data: bytes) -> dict:
    m = XFILE_HEADER_RE.match(data[:16])
    if not m:
        return {"valid": False, "raw_header": data[:16].hex()}
    return {
        "valid": True,
        "version": m.group(1).decode(),
        "encoding": m.group(2).decode(),
        "float_size": int(m.group(3)),
    }


def template_counts(text: str) -> dict[str, int]:
    """Count top-level template invocations.

    A `.x` file is a sequence of `TemplateName <optional-uuid> { ... }` blocks.
    Within a block, sub-templates may nest. We track every template name we
    see at any depth — gives us a histogram of what's used.
    """
    counter: Counter[str] = Counter()
    # Tokenize loosely: an identifier followed by `{` (possibly with stuff
    # between, like a name + UUID). This matches both top-level and nested.
    for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\b[^{};]*\{", text):
        name = m.group(1)
        # Filter obvious non-template tokens (primitives, keywords).
        if name in {"FLOAT", "DWORD", "WORD", "CHAR", "STRING", "ARRAY",
                    "template", "void", "binary"}:
            continue
        counter[name] += 1
    return dict(counter.most_common())


def summarize(path: Path) -> dict:
    data = path.read_bytes()
    info: dict = {"path": str(path), "size": len(data)}
    info.update(header_info(data))
    if not info.get("valid"):
        return info
    try:
        text = data.decode("latin-1")  # `.x` text is ASCII; latin-1 is safe.
    except Exception as e:
        info["decode_error"] = str(e)
        return info
    info["templates"] = template_counts(text)
    return info


def scan(root: Path) -> list[dict]:
    results: list[dict] = []
    for p in sorted(root.rglob("*.x")):
        results.append(summarize(p))
    return results


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", type=Path,
                    help=".x files to summarize")
    ap.add_argument("--scan", type=Path, default=None,
                    help="recursively scan a directory tree of .x files")
    ap.add_argument("--out", type=Path, default=None,
                    help="write JSON results to this path (default: stdout)")
    ap.add_argument("--aggregate", action="store_true",
                    help="with --scan, also emit an aggregate template histogram")
    args = ap.parse_args()

    if not args.paths and not args.scan:
        ap.error("provide one or more .x paths, or --scan DIR")

    results: list[dict] = []
    if args.scan:
        results.extend(scan(args.scan))
    for p in args.paths:
        results.append(summarize(p))

    output: dict = {"files": results}

    if args.aggregate:
        agg: Counter[str] = Counter()
        for r in results:
            for k, v in (r.get("templates") or {}).items():
                agg[k] += v
        output["aggregate_templates"] = dict(agg.most_common())
        output["file_count"] = len(results)
        output["total_bytes"] = sum(r.get("size", 0) for r in results)

    text = json.dumps(output, indent=2, ensure_ascii=False)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
        print(f"wrote {args.out} ({len(results)} files)")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
