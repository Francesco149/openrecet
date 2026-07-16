#!/usr/bin/env python3
"""Run the repository's executable Python tool tests with one command.

Most historical `tools/test_*.py` files are self-contained programs rather than pytest
modules. Invoking pytest over the glob misinterprets their explicit module arguments as
fixtures. This runner preserves each test's intended process isolation and exit contract.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def discover() -> list[Path]:
    tests = sorted((ROOT / "tools").glob("test_*.py"))
    tests.append(ROOT / "tools" / "trace_studio_v3" / "test_orv3.py")
    return tests


def run_one(path: Path) -> tuple[bool, str]:
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    proc = subprocess.run(
        [sys.executable, str(path)],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output = proc.stdout.rstrip()
    return proc.returncode == 0, output


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "tests",
        nargs="*",
        type=Path,
        help="optional test paths, relative to repository root",
    )
    args = ap.parse_args(argv)

    tests = [ROOT / p for p in args.tests] if args.tests else discover()
    missing = [p for p in tests if not p.is_file()]
    if missing:
        for path in missing:
            print(f"MISSING {path.relative_to(ROOT)}", file=sys.stderr)
        return 2

    failures: list[Path] = []
    for path in tests:
        rel = path.relative_to(ROOT)
        ok, output = run_one(path)
        if ok:
            summary = output.splitlines()[-1] if output else "exit 0"
            print(f"PASS {rel}: {summary}")
        else:
            failures.append(path)
            print(f"FAIL {rel}", file=sys.stderr)
            if output:
                print(output, file=sys.stderr)

    if failures:
        print(
            f"python tools: FAILED {len(failures)}/{len(tests)}",
            file=sys.stderr,
        )
        return 1
    print(f"python tools: OK {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
