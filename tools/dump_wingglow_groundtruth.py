#!/usr/bin/env python3
# tools/dump_wingglow_groundtruth.py — recover the Tear wing-glow (records-A
# type-0x1f) draw recipe from retail at a HOUSE freeroam frame.
#
# Why: porting FUN_004176ff's records-A type-0x1f arm (decompile L3818-3921,
# draw ret_va 0x41e165).  Two facts can't be read statically:
#   1. The 4-vertex billboard template at BSS &DAT_0064b548 (xyz + diffuse +
#      uv, stride 0x18) — never written in the decompile, so its geometry +
#      effect.bmp atlas UVs are unknown.  We read its 96 bytes live.
#   2. The blend/alpha render-state envelope active at the 0x41e165 draw —
#      the Ghidra preamble arg-aliasing makes SRCBLEND/DESTBLEND unreliable.
#      We d3d-trace a freeroam window and replay state up to the draw.
#
# Output: one JSON object to stdout (classifier-clean); the raw d3d-trace
# batch stream is written to <run-dir>/d3d_trace.jsonl; diagnostics to stderr.
#
# Usage:
#   nix develop --command python3 tools/dump_wingglow_groundtruth.py \
#       --run-dir runs/wingglow-gt [--remote cutestation.soy:27042] \
#       [--trace-frames 3400-3404] [--wait-frame 3402] [--duration-ms 120000]

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

VA_VBUF        = 0x0064b548   # records-A type-0x1f billboard template (4 verts × 0x18)
VBUF_BYTES     = 4 * 0x18     # 96
VA_RECA_COUNT  = 0x0076b960   # g_scene1_records_a_count
VA_RECA_BASE   = 0x069b2f80   # records-A slot 0
RECA_STRIDE_B  = 0x25 * 4     # 148 bytes
OFF_TYPE       = 12           # dword
OFF_AGE        = 13
OFF_SCALE      = 14


def err(*a):
    print(*a, file=sys.stderr, flush=True)


def parse_frames(s):
    out = set()
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--run-dir", type=Path, required=True)
    ap.add_argument("--trace-frames", default="3400-3404")
    ap.add_argument("--wait-frame", type=int, default=3402)
    ap.add_argument("--duration-ms", type=int, default=120_000)
    args = ap.parse_args(argv)

    args.run_dir.mkdir(parents=True, exist_ok=True)
    d3d_out = open(args.run_dir / "d3d_trace.jsonl", "w")
    trace_frames = parse_frames(args.trace_frames)

    device = frida.get_device_manager().add_remote_device(args.remote)
    win_exe = fc.wslpath_w(RETAIL_EXE)
    win_cwd = fc.wslpath_w(ASSET_CWD)
    err(f"[wg] spawning {win_exe} via {args.remote}")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    n_batches = [0]

    def on_message(message, data):
        if message.get("type") != "send":
            err("[agent-error]", message)
            return
        p = message["payload"]
        if isinstance(p, dict) and p.get("kind") == "d3d_trace_batch":
            for ev in p.get("events", []):
                ev["frame"] = p.get("frame")
                d3d_out.write(json.dumps(ev) + "\n")
            n_batches[0] += 1
    script.on("message", on_message)
    script.load()

    script.exports_sync.init({
        "max_frames":       args.wait_frame + 3000,
        "hide_window":      True,
        "turbo":            True,
        "silent_audio":     True,
        "auto_z_spam":      True,
        "d3d_trace":        True,
        "d3d_trace_frames": trace_frames,
    })
    device.resume(pid)

    deadline = time.monotonic() + args.duration_ms / 1000.0
    frame = 0
    try:
        last_trace = max(trace_frames)
        while time.monotonic() < deadline:
            try:
                frame = script.exports_sync.get_frame()
            except Exception as e:
                err("[wg] get_frame failed:", e)
                time.sleep(0.1)
                continue
            if frame > last_trace + 2:
                break
            time.sleep(0.05)
        else:
            err(f"[wg] timed out at frame {frame}")

        def read_hex(va, n):
            return bytes.fromhex(script.exports_sync.read_memory(va, n))

        def read_i32(va):
            return struct.unpack("<i", read_hex(va, 4))[0]

        # vbuf: 4 verts × {x,y,z(f), diffuse(u32), u,v(f)}
        vb = read_hex(VA_VBUF, VBUF_BYTES)
        verts = []
        for i in range(4):
            x, y, z, diff, u, v = struct.unpack_from("<fffIff", vb, i * 0x18)
            verts.append({"pos": [x, y, z], "diffuse": f"0x{diff:08x}",
                          "uv": [u, v]})

        # records-A: confirm 0x1f live + capture a sample slot's fields
        count_a = read_i32(VA_RECA_COUNT)
        live_1f = []
        for slot in range(min(count_a, 4096)):
            base = VA_RECA_BASE + slot * RECA_STRIDE_B
            t = read_i32(base + OFF_TYPE * 4)
            if t == 0x1f:
                age = read_i32(base + OFF_AGE * 4)
                scale = struct.unpack("<f", read_hex(base + OFF_SCALE * 4, 4))[0]
                rec = read_hex(base, RECA_STRIDE_B)
                fields = struct.unpack("<37f", rec)
                live_1f.append({"slot": slot, "age": age, "scale": scale,
                                "pos": list(fields[0:3]),
                                "rot": list(fields[6:9]),
                                "scale_field_f": fields[OFF_SCALE]})
            if len(live_1f) >= 12:
                break

        out = {
            "frame_read": frame,
            "d3d_batches": n_batches[0],
            "vbuf_va": f"0x{VA_VBUF:08x}",
            "vbuf_verts": verts,
            "records_a_count": count_a,
            "live_0x1f": live_1f,
        }
        print(json.dumps(out, indent=2))
    finally:
        d3d_out.close()
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
