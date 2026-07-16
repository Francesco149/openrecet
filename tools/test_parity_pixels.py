#!/usr/bin/env python3
"""tools/test_parity_pixels.py — gate for the `pixels` pillar PRODUCER.

Proves the producer's truth-defining core (tools/parity/pixel_producer.py) without
any Windows/replay.exe/Frida dependency, by injecting fake per-index renderers:

  * FAITHFUL — identical port/retail frames → every `differ` == 0, and the doc feeds
    adapt_pixels to a PASS (producer output is consumable by the EP-04 adapter).
  * DISPROOF — one perturbed frame → that frame's differ > 0 and adapt_pixels FAILs,
    localizing exactly it (the metric is bit-exact: a single changed pixel counts).
  * FAIL CLOSED — a required frame absent from the identity join, a port/retail dim
    mismatch, and a missing rendered frame each raise (never a silent differ==0).
  * PROVENANCE — the doc carries `source` container hashes verbatim (EP-08 binding).
  * RAW I/O — read_raw_rgb round-trips the replay.exe --render-dump BGRA format.

Run: nix develop --command python3 tools/test_parity_pixels.py
"""
from __future__ import annotations

import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import adapt_pixels  # noqa: E402
from parity.observations import LogicalFrame  # noqa: E402
from parity.pixel_producer import (  # noqa: E402
    PixelProducerError,
    build_pixel_metrics,
    read_raw_rgb,
    wanted_and_map,
)

_checks = 0
_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


def raises(fn, exc, msg: str) -> None:
    try:
        fn()
    except exc:
        check(True, msg)
        return
    except Exception as e:  # wrong type
        check(False, f"{msg} (raised {type(e).__name__}, want {exc.__name__})")
        return
    check(False, f"{msg} (did not raise)")


# ── fixtures ─────────────────────────────────────────────────────────────────

def frame(val: int) -> np.ndarray:
    """A 4×4×3 uint8 frame filled with `val` (a distinct constant per index)."""
    return np.full((4, 4, 3), val & 0xFF, dtype=np.uint8)


def mk_pairs(n=5, anchor="HOUSE_FREEROAM", occ=1):
    """pairs_doc + required for n frames, offset i → port kept i, retail kept i+100
    (distinct index spaces so a port/retail render mix-up would show)."""
    keys = [[anchor, occ, i] for i in range(n)]
    pairs = {
        "port_entry": "/x/port", "retail_entry": "/x/retail",
        "pairs": [{"key": k, "port": i, "retail": i + 100} for i, k in enumerate(keys)],
    }
    required = [LogicalFrame(anchor, occ, i) for i in range(n)]
    return pairs, required


def renderers(port_vals: dict, retail_vals: dict):
    return (lambda i: port_vals.get(i), lambda i: retail_vals.get(i))


# ── tests ────────────────────────────────────────────────────────────────────

def test_wanted_and_map():
    pairs, required = mk_pairs(5)
    port, retail, sub = wanted_and_map(pairs, required)
    check(port == [0, 1, 2, 3, 4], "wanted: port kept indices")
    check(retail == [100, 101, 102, 103, 104], "wanted: retail kept indices (distinct space)")
    check(sub[LogicalFrame("HOUSE_FREEROAM", 1, 3)] == (3, 103), "wanted: lf→(port,retail) map")

    # a required frame the join never paired → fatal (can't prove an unpaired frame).
    extra = required + [LogicalFrame("HOUSE_FREEROAM", 1, 99)]
    raises(lambda: wanted_and_map(pairs, extra), PixelProducerError,
           "wanted: required frame absent from join raises")

    # dedup — two required frames sharing a kept index collapse in the render work-list.
    pd = {"port_entry": "/x/port", "retail_entry": "/x/retail",
          "pairs": [{"key": ["A", 1, 0], "port": 7, "retail": 7},
                    {"key": ["A", 1, 1], "port": 7, "retail": 8}]}
    port, retail, _ = wanted_and_map(pd, [LogicalFrame("A", 1, 0), LogicalFrame("A", 1, 1)])
    check(port == [7], "wanted: duplicate port index deduped")
    check(retail == [7, 8], "wanted: retail indices unique-sorted")


def test_faithful_all_identical():
    pairs, required = mk_pairs(5)
    # identical content on both sides (index-independent value) → bit-identical.
    pv = {i: frame(50) for i in range(5)}
    rv = {i + 100: frame(50) for i in range(5)}
    rp, rr = renderers(pv, rv)
    doc = build_pixel_metrics(pairs, required, rp, rr, source={"a": 1})

    check(doc["schema_version"] == 1 and doc["pillar"] == "pixels" and doc["mode"] == "exact",
          "faithful: doc header (schema/pillar/mode)")
    check(doc["source"] == {"a": 1}, "faithful: source stamped verbatim")
    check(len(doc["frames"]) == 5, "faithful: one frame per required")
    check(all(f["differ"] == 0 for f in doc["frames"]), "faithful: every differ == 0")
    check(all(f["total"] == 16 for f in doc["frames"]), "faithful: total = H*W (4*4)")
    check(all(f["meanabs"] == 0.0 for f in doc["frames"]), "faithful: meanabs 0")
    check([f["key"] for f in doc["frames"]] == [list(lf) for lf in required],
          "faithful: frames keyed + ordered by required")

    # the produced doc feeds the EP-04 adapter to a PASS.
    res = adapt_pixels_doc(doc, required)
    check(res.pillar["verdict"] == "PASS", "faithful: adapt_pixels PASS on identical frames")


def test_disproof_one_frame():
    pairs, required = mk_pairs(5)
    pv = {i: frame(50) for i in range(5)}
    rv = {i + 100: frame(50) for i in range(5)}
    # perturb ONE retail frame by a single pixel channel → bit-exact metric catches it.
    perturbed = frame(50)
    perturbed[2, 1, 0] = 51
    rv[102] = perturbed
    rp, rr = renderers(pv, rv)
    doc = build_pixel_metrics(pairs, required, rp, rr)

    diffs = {tuple(f["key"]): f["differ"] for f in doc["frames"]}
    check(diffs[("HOUSE_FREEROAM", 1, 2)] == 1, "disproof: exactly the perturbed frame has differ==1")
    check(sum(1 for f in doc["frames"] if f["differ"]) == 1, "disproof: only one frame differs")
    f2 = next(f for f in doc["frames"] if tuple(f["key"]) == ("HOUSE_FREEROAM", 1, 2))
    check(f2["meanabs"] > 0.0, "disproof: meanabs > 0 at the perturbed frame")

    res = adapt_pixels_doc(doc, required)
    check(res.pillar["verdict"] == "FAIL", "disproof: adapt_pixels FAIL")
    check(res.pillar["first_divergence"]["logical_frame"]["offset"] == 2,
          "disproof: adapt_pixels localizes the perturbed frame")


def test_fail_closed():
    pairs, required = mk_pairs(3)

    # a renderer that returns None for a frame (render produced nothing) → raise.
    pv = {0: frame(1), 1: frame(1), 2: frame(1)}
    rv = {100: frame(1), 101: None, 102: frame(1)}
    rp, rr = renderers(pv, rv)
    raises(lambda: build_pixel_metrics(pairs, required, rp, rr), PixelProducerError,
           "fail-closed: a missing rendered frame raises (never differ==0)")

    # a port/retail dim mismatch → not pixel-comparable → raise.
    pv2 = {0: frame(1), 1: frame(1), 2: frame(1)}
    rv2 = {100: frame(1), 101: np.zeros((8, 8, 3), np.uint8), 102: frame(1)}
    rp2, rr2 = renderers(pv2, rv2)
    raises(lambda: build_pixel_metrics(pairs, required, rp2, rr2), PixelProducerError,
           "fail-closed: a dim mismatch raises")


def test_read_raw_rgb_roundtrip(tmp: Path):
    # write the replay.exe --render-dump format: [w,h] u32 LE header + packed BGRA.
    w, h = 3, 2
    rgb = np.arange(w * h * 3, dtype=np.uint8).reshape(h, w, 3)
    bgra = np.dstack([rgb[:, :, 2], rgb[:, :, 1], rgb[:, :, 0], np.full((h, w), 255, np.uint8)])
    raw = tmp / "f00000.raw"
    raw.write_bytes(struct.pack("<II", w, h) + bgra.astype(np.uint8).tobytes())
    got = read_raw_rgb(raw)
    check(got.shape == (h, w, 3), "raw: shape (H,W,3)")
    check(np.array_equal(got, rgb), "raw: BGRA→RGB round-trips the original")

    # a truncated raw fails closed.
    bad = tmp / "bad.raw"
    bad.write_bytes(struct.pack("<II", w, h) + b"\x00\x00")
    raises(lambda: read_raw_rgb(bad), PixelProducerError, "raw: truncated body raises")


# adapt_pixels needs a file path — write the doc to a temp file, then adjudicate.
_ADAPT_TMP = Path(tempfile.mkdtemp(prefix="parity-px-adapt-"))
_adapt_n = 0


def adapt_pixels_doc(doc, required):
    global _adapt_n
    import json
    _adapt_n += 1
    p = _ADAPT_TMP / f"pm{_adapt_n}.json"
    p.write_text(json.dumps(doc))

    class _R:
        pass
    r = _R()
    res = adapt_pixels(p, required)
    r.observation, r.pillar = res.observation, res.pillar
    return r


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        test_wanted_and_map()
        test_faithful_all_identical()
        test_disproof_one_frame()
        test_fail_closed()
        test_read_raw_rgb_roundtrip(tmp)

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
