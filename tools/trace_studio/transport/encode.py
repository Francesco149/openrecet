"""transport/encode.py — encode a frame dir into an ALL-INTRA scrub mp4.

Every frame a keyframe (keyint=1) → frame-exact browser seeking; yuv420p for
universal playback (the diff is grayscale so chroma subsampling is lossless for it).
"""
from __future__ import annotations

import subprocess
from pathlib import Path

VIDEO_FPS = 30                       # scrub video framerate (frame n ↔ time n/FPS)


def ffmpeg_encode(frames_dir: Path, out_mp4: Path, fps: int = VIDEO_FPS) -> bool:
    """Encode frame_*.png in `frames_dir` → all-intra h264 mp4. Returns success."""
    frames_dir = Path(frames_dir)
    frames = sorted(frames_dir.glob("frame_*.png"))
    if not frames:
        print(f"trace_studio: encode: no frames in {frames_dir}")
        return False
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-framerate", str(fps),
        "-pattern_type", "glob", "-i", "frame_*.png",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
        "-pix_fmt", "yuv420p",
        "-x264-params", "keyint=1:scenecut=0",
        "-movflags", "+faststart",
        str(out_mp4),
    ]
    r = subprocess.run(cmd, cwd=str(frames_dir), capture_output=True, text=True)
    if r.returncode != 0:
        print(f"trace_studio: encode FAILED ({Path(out_mp4).name}): "
              f"{r.stderr.strip()[:300]}")
        return False
    print(f"trace_studio: encoded {len(frames)} frames → {Path(out_mp4).name}")
    return True
