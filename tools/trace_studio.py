#!/usr/bin/env python3
"""tools/trace_studio.py — thin launcher for the trace_studio package.

The studio's logic lives in the `tools/trace_studio/` PACKAGE (model / drive /
transport / analysis / edits / record / server + cli). This file is kept so the
documented command `python3 tools/trace_studio.py {capture,serve,apply}` and the
server's capture subprocess spawn keep working verbatim.

NB the .py FILE and the trace_studio/ PACKAGE coexist on purpose: running this file
by path executes it as __main__, while `import trace_studio` resolves to the package
(a directory shadows a same-named .py module for imports). See docs/trace-workflow.md.

Run under the dev shell (needs Pillow/numpy/ffmpeg):
    nix develop --command python3 tools/trace_studio.py capture <trace|scn> ...
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # tools/ on path → the package
from trace_studio.cli import main                            # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
