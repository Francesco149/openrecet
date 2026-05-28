#!/usr/bin/env python3
"""Guard: the count of stubbed-but-wired ports must not silently grow.

A stubbed body can pass call-count parity, masquerading as a real port
(see memory feedback_mark_stubbed_ports). We count distinct
CALL_TRACE_ENTER_STUB hex-VA probes under src/ and compare against a
committed baseline. Exit nonzero if the live count EXCEEDS the baseline;
note (but pass) if it drops, suggesting the baseline be lowered.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
BASELINE_FILE = Path(__file__).resolve().parent / "stub_count_baseline.txt"
PROBE_RE = re.compile(r"CALL_TRACE_ENTER_STUB\(0x[0-9a-fA-F]+")


def main() -> int:
    vas = set()
    for path in SRC.rglob("*"):
        if path.is_file():
            try:
                text = path.read_text(errors="ignore")
            except OSError:
                continue
            vas.update(PROBE_RE.findall(text))
    live = len(vas)
    baseline = int(BASELINE_FILE.read_text().split()[0])

    if live > baseline:
        print(f"FAIL: stubbed-but-wired port count grew {baseline} -> {live}")
        print("A new CALL_TRACE_ENTER_STUB probe was added. Finish the port,")
        print(f"or intentionally bump the baseline in {BASELINE_FILE}.")
        return 1
    if live < baseline:
        print(f"NOTE: stub count dropped {baseline} -> {live}; "
              f"lower the baseline in {BASELINE_FILE}.")
        return 0
    print(f"OK: stubbed-but-wired port count at baseline ({live}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
