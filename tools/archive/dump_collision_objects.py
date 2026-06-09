#!/usr/bin/env python3
# tools/dump_collision_objects.py — capture the HOUSE collision OBJECT table
# (the per-object mesh index + world origin that FUN_00432e50 loops over and
# subtracts) from retail at HOUSE free-roam.  These are the objects the port's
# single-object (shop_1st.x at origin 0) collision is missing — see
# engine-quirks §67.
#
# Arrays (all parallel, 20 slots, zeroed at FUN_00436f97 LAB_004371ff):
#   DAT_0438bfb0  phase-1 count          DAT_0438bfb4  phase-2 count
#   DAT_0438bfb8  phase-1 mesh idx i32[20]
#   DAT_0438bfcc  phase-2 mesh idx i32[20]
#   DAT_0438c008  per-object rot/scale f32[20]
#   DAT_0438c058  origin X  f32[20]
#   DAT_0438c0a8  origin Y  f32[20]
#   DAT_0438c0f8  origin Z  f32[20]
# Slot map (FUN_00432e50 L106-114): phase-1 → slots 0..count1-1; phase-2 →
# slots (i-count1)+5 for i in count1..count1+count2-1.
#
# Usage:
#   nix develop --command python3 tools/dump_collision_objects.py \
#       [--remote cutestation.soy:27042] [--wait-frame 3300]
# One JSON object to stdout (classifier-clean); diagnostics to stderr.
import argparse, json, struct, sys, time
from pathlib import Path
import frida
sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida_capture as fc  # noqa: E402

ROOT = fc.ROOT
AGENT_JS = ROOT / "tools" / "frida" / "openrecet-agent.js"

VA = {
    "phase1_count": 0x0438bfb0, "phase2_count": 0x0438bfb4,
    "phase1_mesh":  0x0438bfb8, "phase2_mesh":  0x0438bfcc,
    "rot":          0x0438c008, "ox": 0x0438c058, "oy": 0x0438c0a8, "oz": 0x0438c0f8,
    "stage_idx":    0x0438b4dc,
}
N = 20

def err(*a): print(*a, file=sys.stderr, flush=True)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE)
    ap.add_argument("--wait-frame", type=int, default=3300)
    ap.add_argument("--duration-ms", type=int, default=60_000)
    args = ap.parse_args(argv)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(fc.RETAIL_EXE); win_cwd = fc.wslpath_w(fc.ASSET_CWD)
    err(f"[dump] spawning {win_exe} via {args.remote}")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())
    script.on("message", lambda m, d: err("[agent-error]", m) if m.get("type") != "send" else None)
    script.load()
    script.exports_sync.init({"max_frames": args.wait_frame + 2000, "hide_window": True,
                              "turbo": True, "silent_audio": True, "auto_z_spam": True})
    device.resume(pid)

    deadline = time.monotonic() + args.duration_ms / 1000.0
    frame = 0
    while time.monotonic() < deadline:
        try: frame = script.exports_sync.get_frame()
        except Exception as e: err("[dump] get_frame:", e); time.sleep(0.1); continue
        if frame >= args.wait_frame: break
        time.sleep(0.05)
    else:
        err(f"[dump] timed out at frame {frame}")

    def rmem(va, nbytes): return bytes.fromhex(script.exports_sync.read_memory(va, nbytes))
    def ru32(va): return int(script.exports_sync.read_u32(va)) & 0xffffffff
    def as_s32(u): return u - 0x100000000 if u >= 0x80000000 else u
    def i32a(va): return list(struct.unpack(f"<{N}i", rmem(va, N*4)))
    def f32a(va): return list(struct.unpack(f"<{N}f", rmem(va, N*4)))

    out = {
        "frame_read": frame,
        "stage_idx": as_s32(ru32(VA["stage_idx"])),
        "phase1_count": as_s32(ru32(VA["phase1_count"])),
        "phase2_count": as_s32(ru32(VA["phase2_count"])),
        "phase1_mesh": i32a(VA["phase1_mesh"]),
        "phase2_mesh": i32a(VA["phase2_mesh"]),
        "rot":   f32a(VA["rot"]),
        "origin_x": f32a(VA["ox"]),
        "origin_y": f32a(VA["oy"]),
        "origin_z": f32a(VA["oz"]),
    }
    # derive the active object slots + their (mesh, origin)
    c1, c2 = out["phase1_count"], out["phase2_count"]
    objs = []
    for i in range(max(0, c1)):
        slot = i
        objs.append({"slot": slot, "phase": 1, "mesh": out["phase1_mesh"][slot] if slot < N else None,
                     "origin": [out["origin_x"][slot], out["origin_y"][slot], out["origin_z"][slot]]})
    for i in range(c1, c1 + max(0, c2)):
        slot = (i - c1) + 5
        if slot >= N: continue
        objs.append({"slot": slot, "phase": 2, "mesh": out["phase2_mesh"][i - c1] if (i-c1) < N else None,
                     "origin": [out["origin_x"][slot], out["origin_y"][slot], out["origin_z"][slot]]})
    out["objects"] = objs
    print(json.dumps(out, indent=2))
    try: device.kill(pid)
    except Exception: pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
