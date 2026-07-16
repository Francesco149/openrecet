#!/usr/bin/env python3
"""tools/parity/pixel_producer.py — the `pixels` pillar PRODUCER.

The EP-04 `pixels` adapter (pixels.py) adjudicates a normalized `pixel-metrics.json`
doc but ships format-only: "a real headless producer that replays the v3 command
stream for each paired frame and emits this doc is wired in a later package." This
IS that package (roadmap rule 11 — build the consumer first, then the producer).

What it does, per the identity join a v3 window already computed (pairs.json):

  * for each identity-paired frame in the contract window, render the PORT side and
    the RETAIL side from their captured Trace Studio v3 command streams (v3cap.bin)
    at that side's kept-frame index — RT-correct, resident (replay.exe --render-dump);
  * measure the bit-exact differing-pixel count with the project's ONE canonical
    metric, tools/pixel_diff.amplified_diff (retail = A / ground truth, port = B);
  * emit the normalized doc pixels.adapt_pixels consumes, stamped with the two
    container SHA-256s as `source` so a downstream proof can BIND it to the exact
    capture the join came from (the EP-08 / HOLE-2 provenance check).

Split so the truth-defining core is testable with NO Windows/replay dependency:

  * build_pixel_metrics(...)      pure: (pairs, required, render_port, render_retail)
                                  → the doc. Injected renderers ⇒ unit-tested.
  * wanted_and_map(...)           pure: the per-side kept-frame index sets + lf→(p,r).
  * render_side_via_replay(...)   the Windows driver: replay.exe --render-dump → RGB.
  * produce_for_window(...)       orchestration: read a window dir, drive both sides,
                                  write <window_dir>/pixel-metrics.json.

FAIL CLOSED: a render that yields no frame, a dim mismatch, or a required frame that
the join never paired is an error here — the producer never invents a differ==0.
"""
from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from .fingerprint import sha256_file
from .observations import LogicalFrame, OBS_SCHEMA_VERSION

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
REPLAY_EXE = ROOT / "tools/trace_studio_v3/replay/replay.exe"


class PixelProducerError(Exception):
    """A fatal producer condition (no renderer output, dim mismatch, unpaired
    required frame, missing container). The CLI turns it into exit 2."""


# ── raw frame I/O (the replay.exe --render-dump / v3ref format) ───────────────

def read_raw_rgb(path: Path) -> np.ndarray:
    """Read an 8-byte [w,h] u32 header + tightly-packed BGRA raw into an (H,W,3)
    uint8 RGB array (the replayer writes BGRA; the diff wants RGB). Mirrors
    orv3_view.read_raw_rgb so a dumped frame reads identically to a v3ref."""
    b = Path(path).read_bytes()
    if len(b) < 8:
        raise PixelProducerError(f"truncated raw {path} ({len(b)} bytes)")
    w, h = int.from_bytes(b[0:4], "little"), int.from_bytes(b[4:8], "little")
    need = 8 + w * h * 4
    if len(b) < need:
        raise PixelProducerError(f"raw {path} short: {len(b)} < {need} ({w}x{h})")
    bgra = np.frombuffer(b, dtype=np.uint8, count=w * h * 4, offset=8).reshape(h, w, 4)
    return bgra[:, :, [2, 1, 0]].copy()


# ── pure core (unit-tested with injected renderers) ──────────────────────────

def _pairs_index(pairs_doc: dict) -> dict:
    """{LogicalFrame: (port_kept_idx, retail_kept_idx)} from a pairs.json's `pairs`
    array — the identity join's per-side render indices. Fail closed on a malformed
    row (a producer must never guess an index)."""
    pairs = pairs_doc.get("pairs")
    if not isinstance(pairs, list):
        raise PixelProducerError("pairs.json has no 'pairs' array")
    out = {}
    for row in pairs:
        lf = LogicalFrame.from_key(row.get("key"))
        p, r = row.get("port"), row.get("retail")
        if not isinstance(p, int) or isinstance(p, bool) or not isinstance(r, int) or isinstance(r, bool):
            raise PixelProducerError(f"pair {lf.label()} lacks int port/retail kept indices")
        out[lf] = (p, r)
    return out


def wanted_and_map(pairs_doc: dict, required: list):
    """(sorted-unique port kept idxs, sorted-unique retail kept idxs, {lf:(p,r)})
    for exactly the `required` frames — the render work-list each side must produce.
    A required frame absent from the join is fatal (the window can't prove it)."""
    idx = _pairs_index(pairs_doc)
    sub = {}
    for lf in required:
        if lf not in idx:
            raise PixelProducerError(
                f"required frame {lf.label()} is not in the identity join — "
                f"re-capture the window before producing pixels")
        sub[lf] = idx[lf]
    port = sorted({p for p, _ in sub.values()})
    retail = sorted({r for _, r in sub.values()})
    return port, retail, sub


def build_pixel_metrics(pairs_doc: dict, required: list, render_port, render_retail,
                        *, source: dict | None = None, mode: str = "exact") -> dict:
    """The normalized `pixel-metrics.json` doc for `required` (in order).
    `render_port`/`render_retail` map a kept-frame index → an (H,W,3) uint8 RGB
    array. `differ` is the bit-exact count from pixel_diff.amplified_diff (the same
    metric the whole project uses); `total` = H*W; `meanabs` the mean per-channel
    |Δ|. Pure — the Windows replay lives in render_side_via_replay."""
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    from pixel_diff import amplified_diff  # noqa: E402  (repo tools/)

    _, _, sub = wanted_and_map(pairs_doc, required)
    frames = []
    for lf in required:
        p_idx, r_idx = sub[lf]
        prgb = render_port(p_idx)
        rrgb = render_retail(r_idx)
        if prgb is None or rrgb is None:
            raise PixelProducerError(
                f"no rendered frame for {lf.label()} (port#{p_idx}={prgb is not None}, "
                f"retail#{r_idx}={rrgb is not None})")
        if prgb.shape != rrgb.shape:
            raise PixelProducerError(
                f"{lf.label()}: port {prgb.shape} != retail {rrgb.shape} dims — "
                f"captures are not pixel-comparable")
        # retail = A / ground truth, port = B (matches pixel_diff + orv3_view).
        _, differ, meanabs = amplified_diff(rrgb, prgb)
        total = int(prgb.shape[0] * prgb.shape[1])
        frames.append({"key": list(lf), "differ": int(differ),
                       "total": total, "meanabs": round(float(meanabs), 6)})
    doc = {"schema_version": OBS_SCHEMA_VERSION, "pillar": "pixels", "mode": mode,
           "frames": frames}
    if source is not None:
        doc["source"] = source
    return doc


# ── the Windows driver (replay.exe --render-dump) ────────────────────────────

def _winpath(p: Path) -> str:
    """WSL path → Windows path (replay.exe is a native Windows process)."""
    return subprocess.run(["wslpath", "-w", str(Path(p).resolve())],
                          capture_output=True, text=True, check=True).stdout.strip()


def render_side_via_replay(container: Path, wanted: list, out_dir: Path) -> dict:
    """Render every kept index in `wanted` from `container` (a v3cap.bin) via one
    resident replay.exe --render-dump, and return {idx: (H,W,3) RGB}. RT-correct
    (the resident core accumulates cross-frame render targets). Fail closed if the
    dump is missing any wanted frame."""
    container = Path(container)
    if not container.exists():
        raise PixelProducerError(f"no container at {container}")
    if not REPLAY_EXE.exists():
        raise PixelProducerError(
            f"replayer not built: {REPLAY_EXE} — `nix develop --command make` in replay/")
    if not wanted:
        return {}
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    listfile = out_dir / "wanted.txt"
    listfile.write_text("".join(f"{i}\n" for i in sorted(set(wanted))))
    r = subprocess.run(
        [str(REPLAY_EXE), _winpath(container), "--render-dump",
         _winpath(listfile), _winpath(out_dir)],
        cwd=str(REPLAY_EXE.parent), capture_output=True, text=True)
    out = {}
    missing = []
    for i in sorted(set(wanted)):
        raw = out_dir / f"f{i:05d}.raw"
        if not raw.exists():
            missing.append(i)
            continue
        out[i] = read_raw_rgb(raw)
    if missing:
        raise PixelProducerError(
            f"replay.exe --render-dump did not produce frames {missing[:8]} "
            f"(rc={r.returncode})\n{r.stdout}\n{r.stderr}")
    return out


# ── orchestration: a v3 window → pixel-metrics.json ──────────────────────────

def _container_of(entry: str | Path) -> Path:
    return Path(entry) / "v3cap.bin"


def produce_for_window(window_dir: Path, required: list, *, mode: str = "exact",
                       scratch: Path | None = None) -> tuple[dict, Path]:
    """Drive both sides of an existing v3 window dir (pairs.json → containers) and
    write <window_dir>/pixel-metrics.json for the `required` frames. Returns
    (doc, written_path). `source` is stamped with each v3cap.bin's SHA-256 so a
    proof binds the doc to the exact captures the join came from."""
    window_dir = Path(window_dir)
    pairs_path = window_dir / "pairs.json"
    if not pairs_path.exists():
        raise PixelProducerError(f"no pairs.json in {window_dir} (capture the window first)")
    pairs_doc = json.loads(pairs_path.read_text())
    if not required:
        raise PixelProducerError("no required frames (empty contract window ∩ join)")

    port_entry = pairs_doc.get("port_entry")
    retail_entry = pairs_doc.get("retail_entry")
    if not port_entry or not retail_entry:
        raise PixelProducerError("pairs.json lacks port_entry/retail_entry cache paths")
    port_cap, retail_cap = _container_of(port_entry), _container_of(retail_entry)

    port_wanted, retail_wanted, _ = wanted_and_map(pairs_doc, required)
    source = {"port_container_sha256": sha256_file(port_cap),
              "retail_container_sha256": sha256_file(retail_cap)}

    tmp = Path(scratch) if scratch else Path(tempfile.mkdtemp(prefix="parity-px-"))
    try:
        port_rgb = render_side_via_replay(port_cap, port_wanted, tmp / "port")
        retail_rgb = render_side_via_replay(retail_cap, retail_wanted, tmp / "retail")
        doc = build_pixel_metrics(
            pairs_doc, required,
            render_port=port_rgb.get, render_retail=retail_rgb.get,
            source=source, mode=mode)
    finally:
        if scratch is None:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)

    out = window_dir / "pixel-metrics.json"
    out.write_text(json.dumps(doc))
    return doc, out
