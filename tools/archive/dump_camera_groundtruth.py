#!/usr/bin/env python3
# tools/dump_camera_groundtruth.py — capture the scene-1 camera state
# (eye / lookat / VIEW matrix / PROJECTION matrix / FOV) from retail at a
# HOUSE frame, to diagnose the furniture-orientation/scale bug.
#
# Why: the Cf phase-2 furniture writer is ported + ground-truth-verified
# (positions match retail bit-for-bit), but the rendered furniture has
# wrong orientation + scale.  The view-matrix BUILD mechanism matches the
# engine (FUN_0040120c = LookAtRH(eye,lookat,(0,1,0)) × RotZ(zroll/2)),
# and the per-mesh WORLD chain is asm-verified, so the prime suspects are
# the camera POSE inputs (eye/lookat, full of BSS-zero pose_compute
# assumptions) and the FOV (DAT_073de3a0, port hardcodes 45.0°; real
# writer unported).  This dumper reads the engine's final camera globals
# after the HOUSE scene renders so we can diff against the port's
# scene1_camera output directly — no SetTransform trace needed.
#
# Engine VAs (FUN_0045bbf9 @ 0x45bbf9, FUN_0040120c @ 0x40120c):
#   VIEW  = DAT_073de29c (16f)   PROJECTION = DAT_073de2dc (16f)
#   eye   = DAT_073de31c (3f)    lookat     = DAT_073de328 (3f)
#   FOV   = DAT_073de3a0 (1f)    z_roll     = DAT_006051c4 (1f)
#
# Usage:
#   nix develop --command python3 tools/dump_camera_groundtruth.py \
#       [--remote cutestation.soy:27042] [--wait-frame 3300] \
#       [--duration-ms 90000]
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
VA_VIEW   = 0x073de29c   # D3DTS_VIEW matrix (16f)
VA_PROJ   = 0x073de2dc   # D3DTS_PROJECTION matrix (16f)
VA_EYE    = 0x073de31c   # camera eye (3f)
VA_LOOKAT = 0x073de328   # camera lookat (3f)
VA_FOV    = 0x073de3a0   # fov in degrees (1f)
VA_ZROLL  = 0x06051c4    # z_roll (1f)

# Phase-2 furniture count, to confirm we're past the writer + in HOUSE.
VA_PHASE2_COUNT = 0x0438bfb4

# Pose-input globals (validate scene1_camera's hardcodes / BSS-zero assumptions).
# Verified against FUN_00441c3e @ 0x441c3e (the pose helper):
#   uVar2 ("char_mode") = *(int*)(&DAT_045105a4 + save_slot*0x2dfc8)
#   scene_type/view_mode = (&DAT_068dd3fc)[stage_idx*0x6cf]
VA_STAGE_IDX    = 0x0438b4dc      # DAT_0438b4dc — current stage index
VA_SAVE_SLOT    = 0x0438b1e0      # DAT_0438b1e0 — current save-slot index
VA_SELECTOR_TBL = 0x068dd3fc      # DAT_068dd3fc — per-stage selector (scene_type)
SELECTOR_STRIDE = 0x6cf           # dwords
VA_CHARMODE_BASE = 0x045105a4     # &DAT_045105a4 — per-slot char_mode source
SAVEREC_STRIDE  = 0x2dfc8         # per-save-slot record stride
VA_STAGE_CLASS  = 0x0438b4e8      # DAT_0438b4e8 — port assumes 0 for HOUSE
VA_BIAS_X_SRC   = 0x056da1d8      # DAT_056da1d8 — port assumes 0
VA_BIAS_Z_SRC   = 0x056da1e0      # DAT_056da1e0 — port assumes 0
VA_FLOOR_INPUT  = 0x056da1dc      # DAT_056da1dc — port assumes 0
VA_YAW          = 0x073de39c      # _DAT_073de39c — g_scene1_camera_yaw
# compose-formula globals the port assumes BSS-zero (block E/G of 0x441c3e):
VA_RADIUS_ADD   = 0x0695ef70      # _DAT_0695ef70 — added to radius_xz
VA_EYEY_ADD     = 0x044e2c70      # _DAT_044e2c70 — added to eye.y
VA_LOOKY_ADD    = 0x069b2f78      # _DAT_069b2f78 — added to lookat.y
VA_SHAKE_Y      = 0x0438cc20      # _DAT_0438cc20 — added to eye.y + lookat.y
VA_FLOOR_BIAS   = 0x06a46f9c      # _DAT_06a46f9c — smoothed floor bias
VA_OFF_Y        = 0x0438b774      # _DAT_0438b774 — block-B radius base
VA_OFF_Z        = 0x0438b778      # _DAT_0438b778 — block-B eye.y delta
VA_OFF_X        = 0x0438b77c      # _DAT_0438b77c — block-B lookat.y base
VA_CINE_COUNTER = 0x0438be94      # DAT_0438be94 — cinematic ramp counter (int)
VA_FIRST_FRAME  = 0x0438cc68      # DAT_0438cc68 — first-frame snap flag (int)


def err(*a):
    print(*a, file=sys.stderr, flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--wait-frame", type=int, default=3300,
                    help="poll until engine frame exceeds this (writer fires "
                         "~3200 under auto-z-spam; meshes render ~3211)")
    ap.add_argument("--duration-ms", type=int, default=90_000)
    args = ap.parse_args(argv)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(RETAIL_EXE)
    win_cwd = fc.wslpath_w(ASSET_CWD)
    err(f"[cam] spawning {win_exe} (cwd {win_cwd}) via {args.remote}")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    def on_message(message, data):
        if message.get("type") != "send":
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
                err("[cam] get_frame failed:", e)
                time.sleep(0.1)
                continue
            if frame >= args.wait_frame:
                break
            time.sleep(0.05)
        else:
            err(f"[cam] timed out at frame {frame} (< {args.wait_frame})")

        def read_f32_array(va, n):
            hexstr = script.exports_sync.read_memory(va, n * 4)
            return list(struct.unpack(f"<{n}f", bytes.fromhex(hexstr)))

        def read_i32(va):
            hexstr = script.exports_sync.read_memory(va, 4)
            return struct.unpack("<i", bytes.fromhex(hexstr))[0]

        stage_idx = read_i32(VA_STAGE_IDX)
        save_slot = read_i32(VA_SAVE_SLOT)
        scene_type = read_i32(VA_SELECTOR_TBL + stage_idx * SELECTOR_STRIDE * 4)
        char_mode  = read_i32(VA_CHARMODE_BASE + save_slot * SAVEREC_STRIDE)

        out = {
            "frame_read":   frame,
            "phase2_count": read_i32(VA_PHASE2_COUNT),
            "eye":          read_f32_array(VA_EYE, 3),
            "lookat":       read_f32_array(VA_LOOKAT, 3),
            "fov_deg":      read_f32_array(VA_FOV, 1)[0],
            "z_roll":       read_f32_array(VA_ZROLL, 1)[0],
            "view":         read_f32_array(VA_VIEW, 16),
            "proj":         read_f32_array(VA_PROJ, 16),
            "pose_inputs": {
                "stage_idx":         stage_idx,
                "save_slot":         save_slot,
                "scene_type":        scene_type,   # port's stage_view_mode (assumes 0)
                "char_mode":         char_mode,     # port hardcodes 2 (PHC #11)
                "stage_class":       read_i32(VA_STAGE_CLASS),
                "bias_x_src":        read_f32_array(VA_BIAS_X_SRC, 1)[0],
                "bias_z_src":        read_f32_array(VA_BIAS_Z_SRC, 1)[0],
                "floor_input":       read_f32_array(VA_FLOOR_INPUT, 1)[0],
                "yaw":               read_f32_array(VA_YAW, 1)[0],
            },
            "compose_globals": {
                "radius_add":  read_f32_array(VA_RADIUS_ADD, 1)[0],  # _DAT_0695ef70
                "eyey_add":    read_f32_array(VA_EYEY_ADD, 1)[0],    # _DAT_044e2c70
                "looky_add":   read_f32_array(VA_LOOKY_ADD, 1)[0],   # _DAT_069b2f78
                "shake_y":     read_f32_array(VA_SHAKE_Y, 1)[0],     # _DAT_0438cc20
                "floor_bias":  read_f32_array(VA_FLOOR_BIAS, 1)[0],  # _DAT_06a46f9c
                "off_x":       read_f32_array(VA_OFF_X, 1)[0],       # _DAT_0438b77c
                "off_y":       read_f32_array(VA_OFF_Y, 1)[0],       # _DAT_0438b774
                "off_z":       read_f32_array(VA_OFF_Z, 1)[0],       # _DAT_0438b778
                "cine_counter": read_i32(VA_CINE_COUNTER),
                "first_frame":  read_i32(VA_FIRST_FRAME),
            },
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
