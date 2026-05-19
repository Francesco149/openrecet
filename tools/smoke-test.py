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


def diff_runs(new_run: Path, golden_run: Path) -> Path:
    """Pairwise SSIM between matching frame numbers; write a markdown report."""
    from skimage.metrics import structural_similarity as ssim
    from PIL import Image
    import numpy as np

    def frames_in(d: Path) -> list[Path]:
        return sorted(list(d.glob("*.bmp")) + list(d.glob("*.png")))

    nframes = frames_in(new_run / "frames")
    gframes = frames_in(golden_run / "frames")
    pairs = list(zip(gframes, nframes))
    lines = [
        f"# diff: {new_run.name} vs {golden_run.name}",
        "",
        f"- golden frames: {len(gframes)}",
        f"- new frames:    {len(nframes)}",
        f"- compared:      {len(pairs)}",
        "",
        "| frame | ssim | size match |",
        "|------:|-----:|:----------:|",
    ]
    avg = 0.0
    for g, n in pairs:
        gi = np.asarray(Image.open(g).convert("L"))
        ni = np.asarray(Image.open(n).convert("L"))
        size_match = gi.shape == ni.shape
        if not size_match:
            h = min(gi.shape[0], ni.shape[0])
            w = min(gi.shape[1], ni.shape[1])
            gi, ni = gi[:h, :w], ni[:h, :w]
        s = ssim(gi, ni, data_range=255)
        avg += s
        lines.append(f"| {n.stem} | {s:.4f} | {'✓' if size_match else '✗'} |")
    if pairs:
        avg /= len(pairs)
    lines += ["", f"**mean SSIM: {avg:.4f}**", ""]

    report = new_run / "diff.md"
    report.write_text("\n".join(lines))
    print(f"diff: {report.relative_to(ROOT)}  mean SSIM={avg:.4f}")
    return report


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

    return 0


if __name__ == "__main__":
    sys.exit(main())
