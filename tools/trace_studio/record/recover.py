"""record/recover.py — reconstruct a `.raw.jsonl` from a killed recorder's live-
streamed run-dir (lifted verbatim from trace_studio_serve._recover_raw)."""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


def recover_raw(out: Path, run_dir: Path) -> bool:
    """Reconstruct a `.raw.jsonl` from frida_capture's LIVE-STREAMED run-dir when
    its finalize was interrupted before it could assemble the file. The streamed
    pieces survive a kill: run_dir/trace.jsonl (sparse input change-points,
    line-buffered) + run_dir/agent.log ([anchor]/[esc_record]/[save_capture]
    lines). We rebuild the exact same format finalize writes (header + DENSE
    sticky-filled inputs + anchors + esc + {savefile}/{save_write}), including the
    18 MB boot-save blob that was streamed next to the raw. Returns True on write."""
    trace_jsonl = run_dir / "trace.jsonl"
    agent_log = run_dir / "agent.log"
    if not trace_jsonl.exists():
        return False
    inputs: dict[int, int] = {}
    for ln in trace_jsonl.read_text().splitlines():
        ln = ln.strip()
        if not ln:
            continue
        try:
            o = json.loads(ln)
        except json.JSONDecodeError:
            continue
        if "frame" in o and "buttons" in o:
            inputs[int(o["frame"])] = int(str(o["buttons"]), 16)
    if not inputs:
        return False
    anchors: list[dict] = []
    escs: list[int] = []
    saves: list[dict] = []
    maxf = max(inputs)
    if agent_log.exists():
        for ln in agent_log.read_text(errors="replace").splitlines():
            m = re.match(r"\[anchor\] (\S+) @ frame=(\d+) gframe=(\d+) rng=(\d+)", ln)
            if m:
                fr = int(m.group(2))
                anchors.append({"anchor": m.group(1), "frame": fr,
                                "gframe": int(m.group(3)), "rng": int(m.group(4))})
                maxf = max(maxf, fr)
                continue
            m = re.match(r"\[esc_record\] frame=(\d+)", ln)
            if m:
                fr = int(m.group(1)); escs.append(fr); maxf = max(maxf, fr)
                continue
            m = re.match(r"\[save_capture\] (boot|write) #(\d+) @frame=(\d+) .*→ (\S+)", ln)
            if m:
                saves.append({"which": m.group(1), "index": int(m.group(2)),
                              "frame": int(m.group(3)), "file": m.group(4)})
    n = maxf + 1
    seed = next((a["rng"] for a in sorted(anchors, key=lambda a: a["frame"])), None)
    lines = [json.dumps({"_rec": "openrecet-tas-raw-v1", "frames": n,
                         "start_abs": 0, "rng_seed_at_start": seed,
                         "_recovered": True})]
    sticky = 0
    for i in range(n):
        sticky = inputs.get(i, sticky)
        lines.append(json.dumps({"frame": i, "buttons": f"0x{sticky:04x}"}))
    for a in anchors:
        lines.append(json.dumps({"anchor": a["anchor"], "frame": a["frame"],
                                 "gframe": a["gframe"], "rng": a["rng"]}))
    for ef in escs:
        lines.append(json.dumps({"esc": ef}))
    # Re-emit save rows (recompute sha/size from the streamed .bin next to the raw).
    for sv in saves:
        blob = out.parent / sv["file"]
        if not blob.exists():
            continue
        data = blob.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        if sv["which"] == "boot":
            lines.append(json.dumps({"savefile": sv["file"], "sha256": sha,
                                     "size": len(data)}))
        else:
            lines.append(json.dumps({"save_write": {
                "index": sv["index"], "frame": sv["frame"], "file": sv["file"],
                "sha256": sha, "size": len(data)}}))
    out.write_text("\n".join(lines) + "\n")
    return True
