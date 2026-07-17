#!/usr/bin/env python3
"""GX-06 render-target fixture acceptance (roadmap parity-evidence §9 GX-06).

The render-target opcodes gx06_sink does NOT emit — RES_RT_TEX, SetRenderTarget, CopyRects
— plus all four SURFREF kinds (NULL/BACKBUFFER/DEPTH/TEX), captured in one controlled frame
that renders into an RT, composites it, then CopyRects a corner onto the backbuffer, AND
replays BIT-EXACT. SetRenderTarget + the SURFREF kinds are OBSERVED in the pause backdrop
(a real proof exists); CopyRects is UNOBSERVED in every cached scene — this fixture is its
only capture.

SKIPs (exit 0) when the env can't run a D3D8 exe. Run:
  nix develop --command python3 tools/trace_studio_v3/test_gx06_rt_fixture.py
"""
from gx06_fixture_common import run_fixture

RT_OPS = [
    "DEV_PARAMS", "RES_RT_TEX", "SetRenderState", "SetTextureStageState", "SetTexture",
    "SetVertexShader", "DrawPrimitiveUP", "Clear", "BeginScene", "EndScene", "Present",
    "SetRenderTarget", "CopyRects",
]
RT_SURF = ["NULL", "BACKBUFFER", "DEPTH", "TEX"]

if __name__ == "__main__":
    run_fixture("gx06_rt_fixture", "gx06_rt_fixture.exe", RT_OPS, RT_SURF)
