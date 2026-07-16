#!/usr/bin/env python3
"""Validate current documentation entry points.

Historical audits/findings are intentionally snapshots; default checks target only
documents that claim to describe current policy or operation. External URLs are not
fetched. Run from anywhere inside/outside the repository.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[2]

CURRENT_ENTRY_AND_GUIDE_DOCS = (
    Path("README.md"),
    Path("CLAUDE.md"),
    Path("docs/PLAN.md"),
    Path("docs/AGENT-WORKFLOW.md"),
    Path("docs/DOCUMENTATION.md"),
    Path("docs/FRONT.md"),
    Path("docs/flow-trace-cheatsheet.md"),
    Path("docs/live-probe-harness.md"),
    Path("docs/render-depth-debugging.md"),
    Path("docs/trace-workflow.md"),
)

# Every non-archive plan claims to be live, even when it is a small completed-plan
# redirect. Discovering these files prevents a newly added plan from silently escaping
# the gate. Dated findings, audits, generated status, and archive trees remain excluded.
ACTIVE_PLAN_DOCS = tuple(
    path.relative_to(ROOT) for path in sorted((ROOT / "docs" / "plans").glob("*.md"))
)

AUTHORITATIVE_DOCS = CURRENT_ENTRY_AND_GUIDE_DOCS + ACTIVE_PLAN_DOCS

# Inline Markdown links/images. Reference-style links are not used by the current
# entry points. Keep the parser deliberately small and fail only on paths we can
# validate soundly.
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\n]+)\)")

FORBIDDEN = (
    (
        re.compile(r"\[\[[^\]\n]+\]\]"),
        "private wiki/auto-memory reference; link a repository document",
    ),
    (
        re.compile(r"\b(?:Opus|Sonnet|Haiku)\b"),
        "provider/model-specific workflow name; use R3/R2/R1",
    ),
    (
        re.compile(r"pixel-perfect rendering is not", re.IGNORECASE),
        "superseded fidelity policy",
    ),
)


@dataclass(frozen=True)
class Problem:
    path: Path
    line: int
    message: str

    def render(self) -> str:
        return f"{self.path.as_posix()}:{self.line}: {self.message}"


def _line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _target_path(source: Path, raw_target: str, root: Path) -> Path | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    # Optional Markdown title after a path is not used in current docs. Rejecting
    # spaces in actual paths would be wrong, so only strip a quoted title.
    target = re.sub(r'\s+["\'][^"\']*["\']\s*$', "", target)
    target = unquote(target.split("#", 1)[0])
    if not target:
        return None  # same-document anchor
    if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target):
        return None  # https:, mailto:, data:, etc.
    if target.startswith("//"):
        return None
    if target.startswith("/"):
        # Repository docs should not use host-absolute paths as Markdown links.
        return root / target.lstrip("/")
    return (source.parent / target).resolve()


def check_document(path: Path, root: Path = ROOT) -> tuple[list[Problem], int]:
    rel = path.relative_to(root)
    if not path.is_file():
        return [Problem(rel, 1, "authoritative document is missing")], 0

    text = path.read_text(encoding="utf-8")
    problems: list[Problem] = []
    checked_links = 0

    for pattern, message in FORBIDDEN:
        for match in pattern.finditer(text):
            problems.append(Problem(rel, _line_number(text, match.start()), message))

    for match in LINK_RE.finditer(text):
        target = _target_path(path, match.group(1), root)
        if target is None:
            continue
        checked_links += 1
        try:
            target.relative_to(root)
        except ValueError:
            problems.append(
                Problem(
                    rel,
                    _line_number(text, match.start()),
                    f"local link escapes repository: {match.group(1)!r}",
                )
            )
            continue
        if not target.exists():
            problems.append(
                Problem(
                    rel,
                    _line_number(text, match.start()),
                    f"broken local link: {match.group(1)!r}",
                )
            )

    return problems, checked_links


def run(paths: tuple[Path, ...] = AUTHORITATIVE_DOCS, root: Path = ROOT) -> int:
    problems: list[Problem] = []
    n_links = 0
    for rel in paths:
        found, links = check_document(root / rel, root)
        problems.extend(found)
        n_links += links

    if problems:
        for problem in sorted(problems, key=lambda p: (str(p.path), p.line, p.message)):
            print(problem.render())
        print(
            f"docs: FAILED ({len(problems)} problem(s), "
            f"{len(paths)} authoritative files, {n_links} local links)",
            file=sys.stderr,
        )
        return 1

    print(f"docs: OK ({len(paths)} authoritative files, {n_links} local links)")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="optional repository-relative Markdown paths (default: authoritative set)",
    )
    args = ap.parse_args(argv)
    paths = tuple(args.paths) if args.paths else AUTHORITATIVE_DOCS
    return run(paths)


if __name__ == "__main__":
    raise SystemExit(main())
