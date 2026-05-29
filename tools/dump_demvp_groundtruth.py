#!/usr/bin/env python3
# tools/dump_demvp_groundtruth.py — resolve the two open questions blocking
# the HOUSE-input de-MVP (see PROGRESS 2026-05-29 "De-MVP HOUSE inputs"):
#
#   Q1. cam-adds scope.  _DAT_0695ef70 (radius+14) / _DAT_044e2c70 (eye.y+21)
#       / _DAT_069b2f78 (lookat.y-1.8) are BSS-zero in the image AND have
#       ZERO writers in all 2620 decompiled functions, yet retail holds them
#       at 14/21/-1.8 at the HOUSE frame.  Are they global constants written
#       at process/data init (→ already non-zero at an EARLY pre-HOUSE frame)
#       or scene-specific (→ zero early, 14/21/-1.8 only after HOUSE entry)?
#       We snapshot them at an early frame AND the HOUSE frame to decide.
#
#   Q2. furniture-position offset.  The Cf reader (FUN_00436f97 L34803) walks
#       pairs from record+0x2ce10; the new-game seeder FUN_0049d36d L102214
#       writes furniture at record+0x2ce20.  Dump the raw record byte window
#       +0x2ce00..+0x2ce48 at the HOUSE frame so we can see exactly where the
#       3 live furniture (x,z) pairs (3,3)/(1,0)/(0,1) sit + read char_mode
#       (+0x2ce0c) + the scene_type selector + bias sources.
#
# One JSON object to stdout (classifier-clean); diagnostics to stderr.
#
# Usage:
#   nix develop --command python3 tools/dump_demvp_groundtruth.py \
#       [--remote cutestation.soy:27042] [--early-frame 250] \
#       [--house-frame 3300] [--duration-ms 120000]

import argparse
import json
import struct
import sys
import time
from pathlib import Path

import frida

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida_capture as fc  # noqa: E402

ROOT       = fc.ROOT
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = fc.RETAIL_EXE
ASSET_CWD  = fc.ASSET_CWD
DEFAULT_REMOTE = fc.DEFAULT_REMOTE

# Engine VAs (Ghidra ImageBase 0x00400000).
VA_STAGE_IDX     = 0x0438b4dc      # DAT_0438b4dc — current stage index
VA_SAVE_SLOT     = 0x0438b1e0      # DAT_0438b1e0 — current save-slot index
VA_SELECTOR_TBL  = 0x068dd3fc      # DAT_068dd3fc — per-stage selector (scene_type)
SELECTOR_STRIDE  = 0x6cf           # dwords
VA_SAVEREC_BASE  = 0x044e3798      # &DAT_044e3798 — per-save-slot record base
SAVEREC_STRIDE   = 0x2dfc8         # per-save-slot record stride (bytes)
REC_CHARMODE_OFF = 0x2ce0c         # char_mode  (= 0x045105a4 - base)
REC_FURN_WIN_OFF = 0x2ce00         # start of the byte window we dump
REC_FURN_WIN_LEN = 0x50           # 80 bytes = 20 dwords = 10 (x,z) pairs +hdr

# cam-adds (no writer in decompile; read at both frames for Q1).
VA_RADIUS_ADD    = 0x0695ef70      # _DAT_0695ef70 — radius_xz += this
VA_EYEY_ADD      = 0x044e2c70      # _DAT_044e2c70 — eye.y   += this
VA_LOOKY_ADD     = 0x069b2f78      # _DAT_069b2f78 — lookat.y+= this
# bias + stage-default-pos sources (feed bias_x/z_src in pose_compute).
VA_BIAS_X_SRC    = 0x056da1d8      # DAT_056da1d8
VA_BIAS_Z_SRC    = 0x056da1e0      # DAT_056da1e0
VA_FLOOR_INPUT   = 0x056da1dc      # DAT_056da1dc
VA_STAGE_DEF_X   = 0x0438b1ec      # _DAT_0438b1ec — feeds bias_x_src
VA_STAGE_DEF_Y   = 0x0438b1f0      # _DAT_0438b1f0
VA_STAGE_DEF_Z   = 0x0438b1f4      # _DAT_0438b1f4 — feeds bias_z_src
VA_YAW           = 0x073de39c      # _DAT_073de39c — camera yaw
VA_PHASE2_COUNT  = 0x0438bfb4      # DAT_0438bfb4 — phase-2 furniture count


def err(*a):
    print(*a, file=sys.stderr, flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--early-frame", type=int, default=250,
                    help="first snapshot frame (pre-HOUSE: title/menu)")
    ap.add_argument("--house-frame", type=int, default=3300,
                    help="second snapshot frame (writer fires ~3200)")
    ap.add_argument("--duration-ms", type=int, default=120_000)
    ap.add_argument("--no-auto-z-spam", action="store_true",
                    help="don't drive into HOUSE — for a true pre-scene read")
    ap.add_argument("--early-only", action="store_true",
                    help="take only the early snapshot, then exit")
    args = ap.parse_args(argv)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(RETAIL_EXE)
    win_cwd = fc.wslpath_w(ASSET_CWD)
    err(f"[demvp] spawning {win_exe} (cwd {win_cwd}) via {args.remote}")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    def on_message(message, data):
        if message.get("type") != "send":
            err("[agent-error]", message)
    script.on("message", on_message)
    script.load()

    script.exports_sync.init({
        "max_frames":   args.house_frame + 2000,
        "hide_window":  True,
        "turbo":        True,
        "silent_audio": True,
        "auto_z_spam":  not args.no_auto_z_spam,
    })
    device.resume(pid)

    def read_bytes(va, n):
        return bytes.fromhex(script.exports_sync.read_memory(va, n))

    def read_f32(va):
        return struct.unpack("<f", read_bytes(va, 4))[0]

    def read_i32(va):
        return struct.unpack("<i", read_bytes(va, 4))[0]

    def wait_for(frame_target):
        deadline = time.monotonic() + args.duration_ms / 1000.0
        frame = 0
        while time.monotonic() < deadline:
            try:
                frame = script.exports_sync.get_frame()
            except Exception as e:
                err("[demvp] get_frame failed:", e)
                time.sleep(0.1)
                continue
            if frame >= frame_target:
                return frame
            time.sleep(0.03)
        err(f"[demvp] timed out at frame {frame} (< {frame_target})")
        return frame

    def snapshot(label):
        stage_idx = read_i32(VA_STAGE_IDX)
        save_slot = read_i32(VA_SAVE_SLOT)
        rec = VA_SAVEREC_BASE + save_slot * SAVEREC_STRIDE
        win = read_bytes(rec + REC_FURN_WIN_OFF, REC_FURN_WIN_LEN)
        win_dwords = list(struct.unpack(f"<{REC_FURN_WIN_LEN // 4}i", win))
        return {
            "label":        label,
            "stage_idx":    stage_idx,
            "save_slot":    save_slot,
            "phase2_count": read_i32(VA_PHASE2_COUNT),
            "scene_type":   read_i32(VA_SELECTOR_TBL + stage_idx * SELECTOR_STRIDE * 4),
            "char_mode":    read_i32(rec + REC_CHARMODE_OFF),
            "cam_adds": {
                "radius_add": read_f32(VA_RADIUS_ADD),
                "eyey_add":   read_f32(VA_EYEY_ADD),
                "looky_add":  read_f32(VA_LOOKY_ADD),
            },
            "bias": {
                "bias_x_src":  read_f32(VA_BIAS_X_SRC),
                "bias_z_src":  read_f32(VA_BIAS_Z_SRC),
                "floor_input": read_f32(VA_FLOOR_INPUT),
                "stage_def_x": read_f32(VA_STAGE_DEF_X),
                "stage_def_y": read_f32(VA_STAGE_DEF_Y),
                "stage_def_z": read_f32(VA_STAGE_DEF_Z),
            },
            "yaw": read_f32(VA_YAW),
            # record window as dwords, annotated by byte-offset for Q2.
            "rec_window_off": REC_FURN_WIN_OFF,
            "rec_window_dwords": {
                f"+0x{REC_FURN_WIN_OFF + i*4:x}": v
                for i, v in enumerate(win_dwords)
            },
        }

    try:
        f_early = wait_for(args.early_frame)
        early = snapshot(f"early@{f_early}")
        if args.early_only:
            print(json.dumps({"early": early}, indent=2))
        else:
            f_house = wait_for(args.house_frame)
            house = snapshot(f"house@{f_house}")
            print(json.dumps({"early": early, "house": house}, indent=2))
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
