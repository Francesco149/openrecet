#!/usr/bin/env python3
# tools/dump_phase2_groundtruth.py — capture the FUN_00436f97 block-21
# ("alt-stage arm") phase-2 furniture-mesh state from retail, right after
# the writer fires on HOUSE entry.
#
# Why: Cf.minimal ported the writer as scene1_postload_walker_phase2_init()
# but left it unwired and dependent on three runtime inputs (scene_type,
# ivar8, stage_positions[10][2]).  E.1 call-trace proved FUN_00436f97 fires
# exactly once on new-game HOUSE entry (~frame 3200 under --auto-z-spam),
# 11 frames before the first scene1_render_meshes.  This dumper drives
# retail past that point and reads, via the agent's readMemory RPC:
#   - the writer OUTPUTS:  DAT_0438bfb4 count + the 5 phase-2 arrays
#   - the writer INPUTS:   stage index, save slot, scene_type selector,
#                          the 10 (x,z) stage-position source pairs
# so the wiring can be set + a host test can assert the port reproduces
# the retail arrays exactly.
#
# Usage:
#   nix develop --command python3 tools/dump_phase2_groundtruth.py \
#       [--remote cutestation.soy:27042] [--wait-frame 3300] \
#       [--duration-ms 60000]
# One JSON object to stdout (classifier-clean); diagnostics to stderr.

import argparse
import json
import struct
import sys
import time
from pathlib import Path

import frida

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida_capture as fc  # noqa: E402  — reuse spawn/path plumbing

ROOT       = fc.ROOT
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = fc.RETAIL_EXE
ASSET_CWD  = fc.ASSET_CWD
DEFAULT_REMOTE = fc.DEFAULT_REMOTE

# Engine VAs (Ghidra ImageBase 0x00400000).
VA_PHASE2_COUNT  = 0x0438bfb4   # DAT_0438bfb4 — phase-2 mesh count (output)
VA_PHASE1_COUNT  = 0x0438bfb0   # DAT_0438bfb0 — phase-1 mesh count
VA_MESH_TYPE     = 0x0438bfcc   # int32[20]
VA_ROT_Y         = 0x0438c01c   # float[20]
VA_POS_X         = 0x0438c06c   # float[20]
VA_POS_Y         = 0x0438c0bc   # float[20]
VA_POS_Z         = 0x0438c10c   # float[20]
VA_STAGE_IDX     = 0x0438b4dc   # DAT_0438b4dc — current stage index
VA_SAVE_SLOT     = 0x0438b1e0   # DAT_0438b1e0 — current save-slot index
VA_SELECTOR_TBL  = 0x068dd3fc   # DAT_068dd3fc — per-stage selector table
VA_SAVEREC_BASE  = 0x044e3798   # &DAT_044e3798 — save-record array base
SAVEREC_STRIDE   = 0x2dfc8      # per-save-slot record stride
STAGEPOS_OFF     = 0x2ce10      # x source at +0x2ce10, z at +0x2ce14, stride 8
SELECTOR_STRIDE  = 0x6cf        # DAT_068dd3fc index = stage_idx * 0x6cf (dwords)
N_PHASE2         = 20


def err(*a):
    print(*a, file=sys.stderr, flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--wait-frame", type=int, default=3300,
                    help="poll until engine frame exceeds this (writer fires "
                         "~3200 under auto-z-spam; values persist after)")
    ap.add_argument("--duration-ms", type=int, default=60_000)
    args = ap.parse_args(argv)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(RETAIL_EXE)
    win_cwd = fc.wslpath_w(ASSET_CWD)
    err(f"[dump] spawning {win_exe} (cwd {win_cwd}) via {args.remote}")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    msgs = []
    def on_message(message, data):
        if message.get("type") == "send":
            msgs.append(message["payload"])
        else:
            err("[agent-error]", message)
    script.on("message", on_message)
    script.load()

    script.exports_sync.init({
        "max_frames":   args.wait_frame + 2000,
        "hide_window":  True,
        "turbo":        True,
        "silent_audio": True,
        "auto_z_spam":  True,
    })
    device.resume(pid)

    deadline = time.monotonic() + args.duration_ms / 1000.0
    frame = 0
    try:
        while time.monotonic() < deadline:
            try:
                frame = script.exports_sync.get_frame()
            except Exception as e:
                err("[dump] get_frame failed:", e)
                time.sleep(0.1)
                continue
            if frame >= args.wait_frame:
                break
            time.sleep(0.05)
        else:
            err(f"[dump] timed out at frame {frame} (< {args.wait_frame})")

        def ru32(va):
            return int(script.exports_sync.read_u32(va)) & 0xffffffff

        def read_i32_array(va, n):
            hexstr = script.exports_sync.read_memory(va, n * 4)
            raw = bytes.fromhex(hexstr)
            return list(struct.unpack(f"<{n}i", raw))

        def read_f32_array(va, n):
            hexstr = script.exports_sync.read_memory(va, n * 4)
            raw = bytes.fromhex(hexstr)
            return list(struct.unpack(f"<{n}f", raw))

        stage_idx = ru32(VA_STAGE_IDX)
        save_slot = ru32(VA_SAVE_SLOT)
        # signed-interpret selector + counts
        def as_s32(u):
            return u - 0x100000000 if u >= 0x80000000 else u
        sel_va = VA_SELECTOR_TBL + stage_idx * SELECTOR_STRIDE * 4
        scene_type = as_s32(ru32(sel_va))

        saverec = VA_SAVEREC_BASE + save_slot * SAVEREC_STRIDE
        stagepos_va = saverec + STAGEPOS_OFF
        # 10 (x,z) pairs, stride 8 bytes (x at +0, z at +4)
        sp_hex = script.exports_sync.read_memory(stagepos_va, 10 * 8)
        sp_raw = bytes.fromhex(sp_hex)
        stage_positions = []
        for i in range(10):
            x, z = struct.unpack_from("<ii", sp_raw, i * 8)
            stage_positions.append([x, z])

        out = {
            "frame_read":      frame,
            "stage_idx":       stage_idx,
            "save_slot":       save_slot,
            "scene_type":      scene_type,
            "selector_va":     f"0x{sel_va:08x}",
            "saverec_base_va": f"0x{saverec:08x}",
            "stagepos_va":     f"0x{stagepos_va:08x}",
            "branch_2cdf4":    as_s32(ru32(saverec + 0x2cdf4)),
            "branch_2cde0":    as_s32(ru32(saverec + 0x2cde0)),
            "stage_positions": stage_positions,
            # outputs the port must reproduce:
            "phase2_count":    as_s32(ru32(VA_PHASE2_COUNT)),
            "phase1_count":    as_s32(ru32(VA_PHASE1_COUNT)),
            "mesh_type":       read_i32_array(VA_MESH_TYPE, N_PHASE2),
            "rot_y":           read_f32_array(VA_ROT_Y, N_PHASE2),
            "pos_x":           read_f32_array(VA_POS_X, N_PHASE2),
            "pos_y":           read_f32_array(VA_POS_Y, N_PHASE2),
            "pos_z":           read_f32_array(VA_POS_Z, N_PHASE2),
        }
        print(json.dumps(out, indent=2))
    finally:
        try:
            session.detach()
        except Exception:
            pass
        try:
            device.kill(pid)
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
