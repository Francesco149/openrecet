#!/usr/bin/env python3
"""
tools/smoke-test.py — run an exe via WSLInterop and capture artifacts.

Usage:
    smoke-test.py --target original   --scenario boot
    smoke-test.py --target openrecet  --scenario boot

Targets:
    original   → vendor/unpacked/recettear.unpacked.exe (assets in vendor/original/)
    openrecet  → build/openrecet.exe

WSLInterop runs the exe natively on the host Windows — no wine, no Xvfb.
The trade-off: the runtime (Windows + drivers + .NET) isn't pinnable, so
each "original vs ours" diff must run on the SAME machine in the same
session. That's fine for development; for CI we'd revisit.

Frame capture is currently external (not implemented yet). For now this
harness captures: exit code, process lifetime, stdout/stderr, and a
sha256 of the exe. Once openrecet.exe gains `--capture-to <dir>` it'll
self-emit frames as 32-bit BMPs that this harness collects.

Output:
    runs/<scenario>/<target>-<run-id>/
        stdout.log      (captured exe stdout)
        stderr.log      (captured exe stderr)
        run.json        (target, sha256, duration, exit code, …)
        frames/         (populated once exe gains --capture-to)
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parent.parent


# ─── targets ──────────────────────────────────────────────────────────────


TARGETS = {
    "original": {
        "exe": ROOT / "vendor/unpacked/recettear.unpacked.exe",
        "cwd": ROOT / "vendor/original",
    },
    "openrecet": {
        "exe": ROOT / "build/openrecet.exe",
        "cwd": ROOT / "vendor/original",
    },
}


# ─── helpers ──────────────────────────────────────────────────────────────


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def run_id() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def wslpath_w(p: Path) -> str:
    """Translate a Linux path to its Windows form for argv to a Windows exe."""
    r = subprocess.run(
        ["wslpath", "-w", str(p)],
        capture_output=True, text=True, check=True,
    )
    return r.stdout.strip()


def taskkill(image_name: str) -> None:
    """Force-kill a Windows process by image name. Idempotent — no error if absent."""
    subprocess.run(
        ["/mnt/c/WINDOWS/system32/taskkill.exe", "/F", "/IM", image_name],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


@dataclass
class Scenario:
    name: str
    duration_s: float
    args: list[str]
    env: dict[str, str]

    @classmethod
    def load(cls, path: Path) -> "Scenario":
        data = yaml.safe_load(path.read_text())
        return cls(
            name=data["name"],
            duration_s=float(data.get("duration_s", 5)),
            args=list(data.get("args", [])),
            env=dict(data.get("env", {})),
        )


def builtin_scenario(name: str) -> Scenario:
    presets = {
        "boot":   Scenario(name="boot",   duration_s=3, args=[], env={}),
        "title":  Scenario(name="title",  duration_s=8, args=[], env={}),
        "config": Scenario(name="config", duration_s=4, args=[], env={}),
    }
    if name not in presets:
        raise SystemExit(
            f"unknown scenario '{name}'. Builtin: {list(presets)}; "
            f"or provide tests/scenarios/{name}.yaml"
        )
    return presets[name]


# ─── run ──────────────────────────────────────────────────────────────────


def run(target: str, scenario: Scenario, args: argparse.Namespace) -> Path:
    spec = TARGETS[target]
    exe: Path = spec["exe"]
    cwd: Path = spec["cwd"]

    if not exe.exists():
        raise SystemExit(
            f"{target} exe missing: {exe}\n"
            + ("Run ./tools/setup.sh." if target == "original"
               else "Build openrecet first: `make -C src`.")
        )
    if not cwd.exists():
        raise SystemExit(f"asset cwd missing: {cwd} (run ./tools/setup.sh)")

    rid = run_id()
    run_dir = ROOT / "runs" / scenario.name / f"{target}-{rid}"
    frames = run_dir / "frames"
    frames.mkdir(parents=True)

    metadata = {
        "target": target,
        "exe": str(exe),
        "exe_sha256": sha256(exe),
        "scenario": asdict(scenario),
        "started_utc": rid,
        "via": "WSLInterop",
    }

    stdout_path = run_dir / "stdout.log"
    stderr_path = run_dir / "stderr.log"

    env = os.environ.copy()
    env.update(scenario.env)

    # Hand WSLInterop the exe and let it dispatch to Windows. Pass through
    # any user-supplied args, plus (once supported by our exe) a Windows-
    # form path to the frame-capture directory.
    cmd = [str(exe), *scenario.args]
    if args.capture and target == "openrecet":
        cmd += ["--capture-to", wslpath_w(frames),
                "--capture-every-ms", str(args.capture_every_ms)]
    if args.audio_trace and target == "openrecet":
        # Append-mode log of BGM swaps (and later SE/fade events) lands
        # at <run_dir>/audio-trace.jsonl. Cross-readable from the Linux
        # side after the run finishes.
        trace_path = run_dir / "audio-trace.jsonl"
        cmd += ["--audio-trace", wslpath_w(trace_path)]
    if target == "openrecet":
        # Self-terminate via SetTimer → WM_TIMER → DestroyWindow instead of
        # relying on SIGTERM/taskkill — that path leaves orphan windows on
        # the host because the modal MessageBox or paused-window state can
        # swallow the signal. Set the in-exe limit shorter than proc.wait's
        # deadline so the graceful path wins the race; the TimeoutExpired
        # fallback still catches anything that goes wrong.
        graceful_ms = max(500, int(scenario.duration_s * 1000) - 500)
        cmd += ["--max-duration-ms", str(graceful_ms)]

    t0 = time.time()
    with stdout_path.open("wb") as so, stderr_path.open("wb") as se:
        proc = subprocess.Popen(
            cmd, cwd=str(cwd), env=env, stdout=so, stderr=se,
            preexec_fn=os.setsid,
        )
        try:
            proc.wait(timeout=scenario.duration_s)
            ran_to_completion = True
        except subprocess.TimeoutExpired:
            ran_to_completion = False
            # The Linux-side process is a WSLInterop stub; the real exe is
            # a Windows process. SIGTERM the stub then taskkill on Windows
            # to be sure.
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
            taskkill(exe.name)
        finally:
            metadata["exit_code"] = proc.returncode
            metadata["duration_s"] = round(time.time() - t0, 3)
            metadata["ran_to_completion"] = ran_to_completion

    metadata["frames"] = len(list(frames.glob("*.bmp"))) + len(list(frames.glob("*.png")))
    (run_dir / "run.json").write_text(json.dumps(metadata, indent=2))
    print(
        f"run: {run_dir.relative_to(ROOT)}  "
        f"(exit={metadata['exit_code']}, {metadata['duration_s']}s, "
        f"{metadata['frames']} frames)"
    )
    return run_dir


# ─── diff (frame-based) ───────────────────────────────────────────────────


def _rel(p: Path) -> str:
    """Display path relative to the project root if possible, else absolute.
    The repo-relative form is friendlier in normal runs; tests use tmpdirs
    outside the tree and need the absolute fallback.
    """
    try:
        return str(p.relative_to(ROOT))
    except ValueError:
        return str(p)


def _diff_overlay(golden_rgb, new_rgb, threshold: int = 4):
    """Return a new RGB image with pixels where |new-golden| ≥ threshold
    (across any RGB channel) blended 50/50 with pure red. Inputs are
    H×W×3 uint8 numpy arrays of identical shape. Used by diff_runs.
    """
    import numpy as np

    diff = np.abs(new_rgb.astype(np.int16) - golden_rgb.astype(np.int16))
    mask = (diff.max(axis=2) >= threshold)  # H×W bool

    out = new_rgb.copy()
    if mask.any():
        red = np.array([255, 0, 0], dtype=np.int16)
        blended = (out[mask].astype(np.int16) + red) // 2
        out[mask] = blended.astype(np.uint8)
    return out, mask


def diff_runs(new_run: Path, golden_run: Path) -> Path:
    """Pairwise SSIM + per-pixel diff overlay between matching frames.

    Writes:
      - <new_run>/diff.md (markdown SSIM report)
      - <new_run>/diff/frame_NNNNN.png (per-frame red-tinted overlay)
      - <new_run>/diff-overlay.png (contact-sheet of the overlays)
    """
    from skimage.metrics import structural_similarity as ssim
    from PIL import Image
    import numpy as np

    def frames_in(d: Path) -> list[Path]:
        return sorted(list(d.glob("*.bmp")) + list(d.glob("*.png")))

    nframes = frames_in(new_run / "frames")
    gframes = frames_in(golden_run / "frames")
    pairs = list(zip(gframes, nframes))

    overlay_dir = new_run / "diff"
    overlay_dir.mkdir(exist_ok=True)
    # Wipe any stale overlays from a previous diff run so the contact sheet
    # only reflects the current pair set.
    for p in overlay_dir.glob("frame_*.png"):
        p.unlink()

    lines = [
        f"# diff: {new_run.name} vs {golden_run.name}",
        "",
        f"- golden frames: {len(gframes)}",
        f"- new frames:    {len(nframes)}",
        f"- compared:      {len(pairs)}",
        "",
        "| frame | ssim | diff px | size match |",
        "|------:|-----:|--------:|:----------:|",
    ]
    avg = 0.0
    total_diff_px = 0
    for g, n in pairs:
        g_img = Image.open(g).convert("RGB")
        n_img = Image.open(n).convert("RGB")
        gi_rgb = np.asarray(g_img)
        ni_rgb = np.asarray(n_img)
        size_match = gi_rgb.shape == ni_rgb.shape
        if not size_match:
            h = min(gi_rgb.shape[0], ni_rgb.shape[0])
            w = min(gi_rgb.shape[1], ni_rgb.shape[1])
            gi_rgb, ni_rgb = gi_rgb[:h, :w, :], ni_rgb[:h, :w, :]

        gi_l = np.asarray(Image.fromarray(gi_rgb).convert("L"))
        ni_l = np.asarray(Image.fromarray(ni_rgb).convert("L"))
        s = ssim(gi_l, ni_l, data_range=255)
        avg += s

        overlay, mask = _diff_overlay(gi_rgb, ni_rgb)
        diff_px = int(mask.sum())
        total_diff_px += diff_px
        Image.fromarray(overlay).save(overlay_dir / f"{n.stem}.png")

        lines.append(
            f"| {n.stem} | {s:.4f} | {diff_px} | "
            f"{'✓' if size_match else '✗'} |"
        )
    if pairs:
        avg /= len(pairs)
    lines += [
        "",
        f"**mean SSIM: {avg:.4f}**",
        f"**total diff pixels (threshold=4): {total_diff_px}**",
        "",
    ]

    report = new_run / "diff.md"
    report.write_text("\n".join(lines))
    print(f"diff: {_rel(report)}  mean SSIM={avg:.4f}")

    overlays = sorted(overlay_dir.glob("frame_*.png"))
    if overlays:
        out_overlay = new_run / "diff-overlay.png"
        cmd = [
            sys.executable, str(CONTACT_SHEET),
            "--src", str(overlay_dir),
            "--out", str(out_overlay),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(
                f"diff-overlay sheet failed: {r.stderr.strip()}",
                file=sys.stderr,
            )
        else:
            print(f"diff-overlay: {_rel(out_overlay)}")

    return report


# ─── contact-sheet integration ────────────────────────────────────────────


CONTACT_SHEET = ROOT / "tools" / "contact-sheet.py"


def _frames_in(d: Path) -> list[Path]:
    if not d.is_dir():
        return []
    return sorted(list(d.glob("*.bmp")) + list(d.glob("*.png")))


def make_contact_sheets(run_dir: Path, diff_against: Path | None) -> None:
    """Compose contact sheets from a smoke run's frames.

    - Always emits <run_dir>/contact.png when frames exist (a single-source
      grid of the new run).
    - When `diff_against` is given, additionally emits
      <run_dir>/diff-contact.png as a side-by-side grid (golden | new).

    The PNG output is what makes a smoke run multimodally inspectable —
    assistant `Read`s the PNG and forms an actual picture of the boot
    sequence instead of relying on the SSIM number.

    Failures here are non-fatal; the run itself already succeeded.
    """
    frames = run_dir / "frames"
    new_frames = _frames_in(frames)
    if not new_frames:
        print("contact-sheet: skipped — no frames captured", file=sys.stderr)
        return

    out_single = run_dir / "contact.png"
    cmd = [
        sys.executable, str(CONTACT_SHEET),
        "--src", str(frames),
        "--out", str(out_single),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(
            f"contact-sheet (single) failed: {r.stderr.strip()}",
            file=sys.stderr,
        )
    else:
        print(f"contact: {out_single.relative_to(ROOT)}")

    if diff_against is not None:
        golden_frames = diff_against / "frames"
        if not _frames_in(golden_frames):
            print(
                "contact-sheet (diff): skipped — golden run has no frames",
                file=sys.stderr,
            )
            return
        out_diff = run_dir / "diff-contact.png"
        cmd = [
            sys.executable, str(CONTACT_SHEET),
            "--left", str(golden_frames),
            "--right", str(frames),
            "--out", str(out_diff),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(
                f"contact-sheet (diff) failed: {r.stderr.strip()}",
                file=sys.stderr,
            )
        else:
            print(f"diff-contact: {out_diff.relative_to(ROOT)}")


# ─── cli ──────────────────────────────────────────────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", choices=list(TARGETS), required=True)
    ap.add_argument("--scenario", default="boot")
    ap.add_argument(
        "--duration", type=float, default=None,
        help="override scenario duration_s",
    )
    ap.add_argument(
        "--capture", action="store_true",
        help="pass --capture-to to the exe (openrecet only)",
    )
    ap.add_argument(
        "--capture-every-ms", type=int, default=1000,
        help="milliseconds between captures (default 1000 = 1 fps)",
    )
    ap.add_argument(
        "--diff-against", type=Path, default=None,
        help="path to a previous run dir to SSIM-diff against",
    )
    ap.add_argument(
        "--no-contact-sheet", action="store_true",
        help="skip the auto contact.png composition after --capture",
    )
    ap.add_argument(
        "--audio-trace", action="store_true",
        help="forward --audio-trace <run_dir>/audio-trace.jsonl to the exe",
    )
    args = ap.parse_args()

    yaml_path = ROOT / "tests/scenarios" / f"{args.scenario}.yaml"
    if yaml_path.exists():
        scenario = Scenario.load(yaml_path)
    else:
        scenario = builtin_scenario(args.scenario)

    if args.duration is not None:
        scenario.duration_s = args.duration

    run_dir = run(args.target, scenario, args)

    if args.diff_against:
        if not args.diff_against.exists():
            raise SystemExit(f"--diff-against not found: {args.diff_against}")
        diff_runs(run_dir, args.diff_against)

    if args.capture and not args.no_contact_sheet:
        make_contact_sheets(run_dir, args.diff_against)

    return 0


if __name__ == "__main__":
    sys.exit(main())
