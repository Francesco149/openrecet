#!/usr/bin/env python3
"""tools/parity/environment.py — EP-02 environment provenance group.

Assembles + validates the proof `environment` group (host/runtime facts that can
affect output). Fail closed: it VALIDATES supplied values and can DETECT a few
host defaults, but never fabricates the fields it cannot know (gpu/driver/…). The
real capture host (the Windows retail runner) supplies those; on the Linux dev box
`host_probe()` only offers what stdlib can honestly report.
"""
from __future__ import annotations

# The 8 fields the proof schema requires under `environment`, in canonical order.
REQUIRED = (
    "os_build",
    "locale",
    "codepage",
    "d3d_runtime",
    "gpu",
    "driver",
    "resolution",
    "display_mode",
)
DISPLAY_MODES = ("windowed", "fullscreen")


class EnvValidationError(ValueError):
    """A required environment field was missing/empty or display_mode was invalid."""


def host_probe() -> dict:
    """Best-effort, honest host facts from stdlib only. Returns ONLY the subset it
    can actually determine (never a fabricated gpu/driver/resolution). Suggestions
    for `collect_environment(detect=True)`, always overridable by the caller."""
    import locale as _locale
    import platform

    probe: dict = {"os_build": platform.platform()}
    try:
        loc, enc = _locale.getlocale()
    except (ValueError, TypeError):
        loc, enc = None, None
    if loc:
        probe["locale"] = loc
    if enc:
        probe["codepage"] = enc
    return {k: v for k, v in probe.items() if v}


def collect_environment(fields: dict | None = None, *, detect: bool = False, **overrides) -> dict:
    """Merge (host probe if detect) < fields < overrides, then validate and return
    exactly the 8 required keys in canonical order. Raises EnvValidationError if any
    required field is missing/empty or display_mode is not windowed|fullscreen."""
    env: dict = {}
    if detect:
        env.update(host_probe())
    if fields:
        env.update(fields)
    env.update(overrides)

    missing = [k for k in REQUIRED if not str(env.get(k, "")).strip()]
    if missing:
        raise EnvValidationError(f"missing/empty environment field(s): {missing}")
    if env["display_mode"] not in DISPLAY_MODES:
        raise EnvValidationError(
            f"display_mode must be one of {DISPLAY_MODES}, got {env['display_mode']!r}"
        )
    return {k: str(env[k]) for k in REQUIRED}
