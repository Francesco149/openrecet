"""drive/caps.py — EngineCaps: detect what the running exe / harness supports.

The plan wants the Python rewrite to ship and work BEFORE (or independently of)
each engine change, degrading gracefully. So instead of assuming the D1/D2 flags
exist, we probe the built exe binary for the flag TOKEN STRINGS it compares against
(`lstrcmpA(tok, "--capture-suppress-loads")` embeds the literal). This is
side-effect-free (no game launch), accurate (the exe literally contains the token),
and a pre-D1 exe correctly reports unsupported.

Retail-side suppression is a Python capability (frida_capture.run_capture's
`suppress_loads` param), so it's reported from the function signature, not the exe.

D2 capture-local is NOT an exe flag — it's pure Python staging (point --capture-to
at a Windows-LOCAL NTFS dir, then frame_io.copyback_convert). So its capability is
"a local stage root resolves" (a host/env property), probed via
frame_io.local_stage_root(), not an exe-token search.
"""
from __future__ import annotations

import inspect
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class EngineCaps:
    exe: Path | None
    supports_suppress_loads: bool      # D1 port: --capture-suppress-loads (exe flag)
    supports_capture_local: bool       # D2: local stage root resolves (host/env, not exe)
    supports_capstride: bool           # D3 port: {capstride} segtrace op (exe token)
    retail_supports_suppress_loads: bool

    def summary(self) -> str:
        f = lambda b: "yes" if b else "no"   # noqa: E731
        return (f"engine caps (exe={self.exe.name if self.exe else 'MISSING'}): "
                f"suppress_loads={f(self.supports_suppress_loads)} "
                f"capture_local={f(self.supports_capture_local)} "
                f"capstride={f(self.supports_capstride)} "
                f"retail_suppress={f(self.retail_supports_suppress_loads)}")


def _find_exe(root: Path) -> Path | None:
    for name in ("openrecet.exe", "openrecet-debug.exe"):
        p = Path(root) / "build" / name
        if p.exists():
            return p
    return None


def _exe_has(data: bytes, token: str) -> bool:
    return token.encode() in data


def _retail_supports_suppress() -> bool:
    """frida_capture.run_capture grew a `suppress_loads` kwarg in Phase 1."""
    try:
        import frida_capture
        return "suppress_loads" in inspect.signature(
            frida_capture.run_capture).parameters
    except Exception:                                   # noqa: BLE001
        return False


def _local_stage_available() -> bool:
    """D2 capture-local is a host/env capability: can we derive a Windows-local
    NTFS staging dir under %LOCALAPPDATA% (reached via /mnt/c)? frame_io caches
    the cmd.exe probe, so this also warms it for the later export_trace call."""
    try:
        import frame_io
        return frame_io.local_stage_root() is not None
    except Exception:                                   # noqa: BLE001
        return False


def probe(root: Path) -> EngineCaps:
    exe = _find_exe(root)
    data = exe.read_bytes() if exe else b""
    return EngineCaps(
        exe=exe,
        supports_suppress_loads=_exe_has(data, "--capture-suppress-loads"),
        supports_capture_local=_local_stage_available(),
        supports_capstride=_exe_has(data, "capstride"),
        retail_supports_suppress_loads=_retail_supports_suppress(),
    )
