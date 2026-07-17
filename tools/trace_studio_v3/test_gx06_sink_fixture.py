#!/usr/bin/env python3
"""GX-06 kitchen-sink fixture acceptance (roadmap parity-evidence §9 GX-06).

Every NON-render-target recorded opcode the census tracks is captured in one controlled
frame AND that frame replays BIT-EXACT — the corpus's synthetic per-opcode plumbing proof
(the RT opcodes are test_gx06_rt_fixture's job; the real cached scenarios prove the
OBSERVED opcodes in situ, tools/gx_corpus.py).

SKIPs (exit 0) when the env can't run a D3D8 exe. Run:
  nix develop --command python3 tools/trace_studio_v3/test_gx06_sink_fixture.py
"""
from gx06_fixture_common import run_fixture

# the 22 non-RT recorded opcodes (orv3_format.h / the census recorded set)
SINK_OPS = [
    "DEV_PARAMS", "RES_TEX", "RES_VB", "RES_IB",
    "SetRenderState", "SetTextureStageState", "SetTransform", "SetMaterial",
    "SetTexture", "SetStreamSource", "SetIndices", "SetVertexShader",
    "DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP",
    "Clear", "SetLight", "LightEnable", "BeginScene", "EndScene", "Present",
]

if __name__ == "__main__":
    run_fixture("gx06_sink_fixture", "gx06_sink_fixture.exe", SINK_OPS)
