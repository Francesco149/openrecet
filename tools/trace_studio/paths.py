"""trace_studio.paths — shared locations + defaults (was module-level in the monolith)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

# tools/trace_studio/paths.py → parents[2] = repo root
ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
SESS_ROOT = ROOT / "runs" / "trace-studio"
WEB_DIR = ROOT / "tools" / "trace_studio_web"
DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")

# The studio wraps sibling tools (export_trace / frida_capture / flow_diff /
# pixel_diff / trace_save / frame_io) as libraries — like the old monolith, ensure
# tools/ is importable whenever the package is loaded (not just via the launcher).
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
