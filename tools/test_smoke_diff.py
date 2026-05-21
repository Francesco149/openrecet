#!/usr/bin/env python3
"""
tools/test_smoke_diff.py — sanity tests for `smoke-test.py::diff_runs`.

Run with `nix develop --command python3 tools/test_smoke_diff.py`. Exits
non-zero on failure; prints `OK` on success.

Three scenarios:
  1. Self-diff: a run versus itself → every overlay's red-mask is empty,
     the overlay pixels are byte-identical to the input.
  2. Synthetic diff: hand-modify a small rectangle in the "new" frame →
     the overlay's red mask matches that rectangle exactly.
  3. CLI smoke: run diff_runs against a tmpdir pair → diff-overlay.png
     and per-frame PNGs land on disk.
"""

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent.parent


def load_smoke_test():
    spec = importlib.util.spec_from_file_location(
        "smoke_test", ROOT / "tools" / "smoke-test.py"
    )
    mod = importlib.util.module_from_spec(spec)
    # dataclass decorator needs the module visible in sys.modules.
    sys.modules["smoke_test"] = mod
    spec.loader.exec_module(mod)
    return mod


def make_synthetic_run(parent: Path, name: str, n_frames: int = 3,
                       size=(64, 48)) -> Path:
    """Create a fake run directory with `n_frames` of solid-color BMPs."""
    run = parent / name
    frames = run / "frames"
    frames.mkdir(parents=True)
    for i in range(n_frames):
        # Use a deterministic gradient so the frames differ from each other.
        arr = np.zeros((size[1], size[0], 3), dtype=np.uint8)
        arr[:, :, 0] = (i * 30 + 64) & 0xff
        arr[:, :, 1] = (i * 17 + 32) & 0xff
        arr[:, :, 2] = (i * 53 + 96) & 0xff
        Image.fromarray(arr).save(frames / f"frame_{i:05d}.bmp")
    return run


def test_self_diff(smoke):
    print("test_self_diff ...", end=" ")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        run = make_synthetic_run(tmp, "run_a", n_frames=3)
        # Run diff against itself.
        smoke.diff_runs(run, run)
        overlay_dir = run / "diff"
        overlays = sorted(overlay_dir.glob("frame_*.png"))
        assert len(overlays) == 3, f"expected 3 overlays, got {len(overlays)}"
        # Self-diff: each overlay must equal its input frame exactly.
        for ov in overlays:
            orig = run / "frames" / (ov.stem + ".bmp")
            orig_arr = np.asarray(Image.open(orig).convert("RGB"))
            ov_arr = np.asarray(Image.open(ov).convert("RGB"))
            assert np.array_equal(ov_arr, orig_arr), (
                f"self-diff overlay {ov.name} differs from input"
            )
        # And the contact sheet exists.
        assert (run / "diff-overlay.png").exists(), "missing diff-overlay.png"
    print("OK")


def test_synthetic_diff(smoke):
    print("test_synthetic_diff ...", end=" ")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        gold = make_synthetic_run(tmp, "golden", n_frames=2)
        new = make_synthetic_run(tmp, "new", n_frames=2)
        # Modify a 4x4 block in new/frames/frame_00001.bmp
        target = new / "frames" / "frame_00001.bmp"
        arr = np.asarray(Image.open(target).convert("RGB")).copy()
        # The rectangle: rows 10..14, cols 20..24
        arr[10:14, 20:24, :] = [255, 255, 255]
        Image.fromarray(arr).save(target)

        smoke.diff_runs(new, gold)

        # The frame_00000 overlay should be byte-identical to its source
        # (no change between golden and new frame 0).
        ov0 = np.asarray(Image.open(new / "diff" / "frame_00000.png").convert("RGB"))
        src0 = np.asarray(Image.open(new / "frames" / "frame_00000.bmp").convert("RGB"))
        assert np.array_equal(ov0, src0), "frame_00000 overlay should be unchanged"

        # The frame_00001 overlay should have red-tinted pixels exactly on
        # the 4x4 modified rect, and only there.
        ov1 = np.asarray(Image.open(new / "diff" / "frame_00001.png").convert("RGB"))
        src1 = np.asarray(Image.open(new / "frames" / "frame_00001.bmp").convert("RGB"))
        diff_mask = np.any(ov1 != src1, axis=2)
        expected = np.zeros(diff_mask.shape, dtype=bool)
        expected[10:14, 20:24] = True
        assert np.array_equal(diff_mask, expected), (
            f"red mask mismatch: got {int(diff_mask.sum())} px, expected "
            f"{int(expected.sum())} px"
        )
        # And the tint must visibly bias red (R > average of G,B on those px).
        tinted = ov1[10:14, 20:24]
        assert (tinted[..., 0] > tinted[..., 1]).all(), (
            "modified region is not red-tinted"
        )
    print("OK")


def test_size_mismatch_safe(smoke):
    """Two runs with mismatched frame sizes still produce overlays
    without crashing; smaller dimensions are clipped to the overlap."""
    print("test_size_mismatch_safe ...", end=" ")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        gold = make_synthetic_run(tmp, "golden", n_frames=1, size=(64, 48))
        new = make_synthetic_run(tmp, "new", n_frames=1, size=(80, 60))
        smoke.diff_runs(new, gold)
        ovs = sorted((new / "diff").glob("frame_*.png"))
        assert ovs, "missing overlays for size-mismatch case"
        ov = np.asarray(Image.open(ovs[0]).convert("RGB"))
        # Overlay is sized to the intersection (48x64), not the larger input.
        assert ov.shape == (48, 64, 3), f"unexpected overlay shape {ov.shape}"
    print("OK")


def main() -> int:
    smoke = load_smoke_test()
    test_self_diff(smoke)
    test_synthetic_diff(smoke)
    test_size_mismatch_safe(smoke)
    print("all OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
