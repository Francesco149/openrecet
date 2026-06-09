#!/usr/bin/env python3
# tools/dump_phase1_groundtruth.py — capture the PII.3c "draw loop A"
# (wall/floor/jutan) state from retail on new-game HOUSE entry.
#
# Why: PII.3b (furniture, draw loop B) renders end-to-end.  The shop
# interior background (walls/floor/carpet) is FUN_00457714's draw loop A,
# which iterates DAT_0438bfb0 (=2 for HOUSE) phase-1 instances.  Each
# instance indexes a mesh in the DAT_068dcca0[] array (stride 0x28) via a
# per-instance mesh-index array DAT_0438bfb8[].  The open question static
# analysis can't settle: does HOUSE actually LOAD wall/floor meshes into
# DAT_068dcca0 (FUN_00474681, gated on per-stage mesh count DAT_068ded24)?
# This dumper reads, at the HOUSE furniture frame:
#   - phase-1 count + the mesh-index array DAT_0438bfb8[]
#   - the DAT_068dcca0[] mesh-array slots (ptr + face count per 0x28 slot)
#   - the per-stage mesh count DAT_068ded24[stage]
#   - the distance-cull threshold (*DAT_068dd2f0 + 0x1a78)
#   - the phase-1 transform fields (tail of the shared 20-entry arrays)
#
# Usage:
#   nix develop --command python3 tools/dump_phase1_groundtruth.py \
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
import frida_capture as fc  # noqa: E402

ROOT       = fc.ROOT
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = fc.RETAIL_EXE
ASSET_CWD  = fc.ASSET_CWD
DEFAULT_REMOTE = fc.DEFAULT_REMOTE

# Engine VAs (Ghidra ImageBase 0x00400000).
VA_PHASE2_COUNT  = 0x0438bfb4   # DAT_0438bfb4 — phase-2 mesh count
VA_PHASE1_COUNT  = 0x0438bfb0   # DAT_0438bfb0 — phase-1 mesh count
VA_PHASE1_IDX    = 0x0438bfb8   # DAT_0438bfb8 — per-phase-1 mesh-index array
VA_MESH_TYPE     = 0x0438bfcc   # int32[20]   (shared head; phase-1 at tail)
VA_ROT_Y         = 0x0438c01c   # float[20]
VA_POS_X         = 0x0438c06c   # float[20]
VA_POS_Y         = 0x0438c0bc   # float[20]
VA_POS_Z         = 0x0438c10c   # float[20]
VA_STAGE_IDX     = 0x0438b4dc   # DAT_0438b4dc — current stage index
VA_SAVE_SLOT     = 0x0438b1e0   # DAT_0438b1e0 — current save-slot index
VA_MESH_ARRAY    = 0x068dcca0   # DAT_068dcca0 — wall/floor/jutan mesh array
VA_MESH_COUNT    = 0x068ded24   # DAT_068ded24 — per-stage loaded-mesh count
VA_STAGE_PAL_PTR = 0x068dd2f0   # DAT_068dd2f0 — current stage-palette pointer
STAGE_STRIDE     = 0x1b3c       # per-stage record stride (DAT_068ded24 etc.)
MESH_SLOT_STRIDE = 0x28         # DAT_068dcca0 per-mesh entry stride
N_MESH_SLOTS     = 20           # (DAT_068dcfc0 - DAT_068dcca0) / 0x28
N_ENTRIES        = 20


def err(*a):
    print(*a, file=sys.stderr, flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--wait-frame", type=int, default=3300)
    ap.add_argument("--duration-ms", type=int, default=60_000)
    args = ap.parse_args(argv)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(RETAIL_EXE)
    win_cwd = fc.wslpath_w(ASSET_CWD)
    err(f"[dump] spawning {win_exe} (cwd {win_cwd}) via {args.remote}")
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

        def as_s32(u):
            return u - 0x100000000 if u >= 0x80000000 else u

        def read_i32_array(va, n):
            raw = bytes.fromhex(script.exports_sync.read_memory(va, n * 4))
            return list(struct.unpack(f"<{n}i", raw))

        def read_f32_array(va, n):
            raw = bytes.fromhex(script.exports_sync.read_memory(va, n * 4))
            return list(struct.unpack(f"<{n}f", raw))

        stage_idx = ru32(VA_STAGE_IDX)
        save_slot = ru32(VA_SAVE_SLOT)
        phase1_count = as_s32(ru32(VA_PHASE1_COUNT))

        # per-stage loaded-mesh count (FUN_00474681 gate)
        mesh_count_va = VA_MESH_COUNT + stage_idx * STAGE_STRIDE
        mesh_count = as_s32(ru32(mesh_count_va))

        # distance-cull threshold: *(int*)(*DAT_068dd2f0 + 0x1a78)
        stage_pal = ru32(VA_STAGE_PAL_PTR)
        cull_thresh = as_s32(ru32(stage_pal + 0x1a78)) if stage_pal else None

        # DAT_068dcca0[] mesh array — for each of 20 slots read the mesh
        # ptr (+0), the face-slot-array ptr (+4), the face-base (+8) and
        # the face/subset count (+0x10).
        mesh_slots = []
        for s in range(N_MESH_SLOTS):
            base = VA_MESH_ARRAY + s * MESH_SLOT_STRIDE
            mesh_slots.append({
                "slot":       s,
                "mesh_ptr":   f"0x{ru32(base + 0x00):08x}",
                "faceslot_p": f"0x{ru32(base + 0x04):08x}",
                "face_base":  f"0x{ru32(base + 0x08):08x}",
                "face_count": as_s32(ru32(base + 0x10)),
            })

        out = {
            "frame_read":   frame,
            "stage_idx":    stage_idx,
            "save_slot":    save_slot,
            "phase1_count": phase1_count,
            "phase2_count": as_s32(ru32(VA_PHASE2_COUNT)),
            # FUN_00474681 mesh-load gate + result:
            "mesh_count_va":   f"0x{mesh_count_va:08x}",
            "stage_mesh_count": mesh_count,
            "stage_pal_ptr":   f"0x{stage_pal:08x}",
            "cull_threshold":  cull_thresh,
            # draw loop A inputs:
            "phase1_mesh_index": read_i32_array(VA_PHASE1_IDX, 8),
            "mesh_slots":        mesh_slots,
            # shared transform arrays (phase-1 lives at the tail):
            "mesh_type":  read_i32_array(VA_MESH_TYPE, N_ENTRIES),
            "rot_y":      read_f32_array(VA_ROT_Y, N_ENTRIES),
            "pos_x":      read_f32_array(VA_POS_X, N_ENTRIES),
            "pos_y":      read_f32_array(VA_POS_Y, N_ENTRIES),
            "pos_z":      read_f32_array(VA_POS_Z, N_ENTRIES),
        }
        print(json.dumps(out, indent=2))
    finally:
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    main()
