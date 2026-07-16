#!/usr/bin/env python3
"""tools/test_parity_observations.py — EP-04 gate for the observation adapters.

Proves the acceptance criteria the roadmap fixes for EP-04 (§8 EP-04 + §15):

  * FAIL CLOSED — an absent evidence file is NOT_CAPTURED, never PASS; a required
    paired frame with no measurement is NOT_CAPTURED.
  * REORDERED / MISMATCHED IDENTITIES FAIL — a metrics doc whose frames are
    reordered, duplicated, or reference a frame outside the identity join is
    INCONCLUSIVE (not a silent pass); a stale source-container hash is INCONCLUSIVE.
  * EXACT PIXEL MODE COMPARES EVERY REQUIRED FRAME — dropping one required frame is
    NOT_CAPTURED; one differ>0 is FAIL at exactly that logical frame.
  * VERDICT MAP — draw ALIGNED/BATCHING → PASS (BATCHING noted); DIVERGENT → FAIL;
    join complete → identity PASS, honest gaps → identity FAIL.

The NEGATIVE tests (roadmap §15 "prove the gate catches a deliberate mismatch"):
mutate one differ 0→N, reorder the frames, drop a required frame, corrupt a source
hash, flip a draw verdict to DIVERGENT, bump the schema major — each must change
the verdict away from PASS in the specified direction.

The produced observation/pillar objects are also cross-validated against the EP-01
proof schema $defs (SKIPs if jsonschema is unavailable, like the git-backed
fingerprint checks).

Run: nix develop --command python3 tools/test_parity_observations.py
Exits non-zero on failure; prints OK on success.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import (  # noqa: E402
    FAIL,
    INCONCLUSIVE,
    NOT_CAPTURED,
    OBS_SCHEMA_VERSION,
    PASS,
    LogicalFrame,
    ObservationError,
    adapt_identity,
    adapt_pixels,
    adapt_render_program,
    load_required,
    render_metrics_from_view_json,
)

_checks = 0
_failures: list[str] = []
_skips: list[str] = []

A64 = "a" * 64  # placeholder container hashes
B64 = "b" * 64


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


def write_json(path: Path, doc) -> Path:
    path.write_text(json.dumps(doc))
    return path


# ── fixtures ─────────────────────────────────────────────────────────────────

def _keys():
    return [["HOUSE_FREEROAM", 1, 0], ["HOUSE_FREEROAM", 1, 1], ["HOUSE_FREEROAM", 1, 2]]


def pairs_doc(port_only=None, retail_only=None, join_verdict=None,
              omit_gap_arrays=False):
    d = {
        "anchor": "HOUSE_FREEROAM",
        "pairs": [{"key": k, "port": i, "retail": i} for i, k in enumerate(_keys())],
    }
    if join_verdict is not None:
        d["join_verdict"] = join_verdict
    if not omit_gap_arrays:
        d["port_only"] = [{"key": k} for k in (port_only or [])]
        d["retail_only"] = [{"key": k} for k in (retail_only or [])]
    return d


def pixel_metrics(frames=None, mode="exact", source=None):
    if frames is None:
        frames = [{"key": k, "differ": 0, "total": 100, "meanabs": 0.0} for k in _keys()]
    d = {"schema_version": OBS_SCHEMA_VERSION, "pillar": "pixels", "mode": mode, "frames": frames}
    if source is not None:
        d["source"] = source
    return d


def draw_metrics(frames=None, source=None):
    if frames is None:
        frames = [{"key": k, "draw_verdict": "ALIGNED", "port_tris": 5, "retail_tris": 5,
                   "divergent": []} for k in _keys()]
    d = {"schema_version": OBS_SCHEMA_VERSION, "pillar": "render_program", "frames": frames}
    if source is not None:
        d["source"] = source
    return d


# ── LogicalFrame ─────────────────────────────────────────────────────────────

def test_logical_frame():
    lf = LogicalFrame.from_key(["PAUSE_OPEN", 1, 123])
    check(lf == LogicalFrame("PAUSE_OPEN", 1, 123), "from_key basic")
    check(lf.label() == "PAUSE_OPEN#1+123", "label round-trips")
    check(LogicalFrame.from_label("PAUSE_OPEN#1+123") == lf, "from_label basic")
    check(LogicalFrame.from_label("A#2+-3") == LogicalFrame("A", 2, -3), "from_label neg offset")
    check(lf.as_dict() == {"anchor": "PAUSE_OPEN", "occurrence": 1, "offset": 123}, "as_dict shape")

    for bad in ([1, 2, 3], ["A", 2], ["", 1, 0], ["A", 0, 0], ["A", True, 0], ["A", 1, 1.5], ["A", 1, True]):
        try:
            LogicalFrame.from_key(bad)
            check(False, f"from_key should reject {bad!r}")
        except ObservationError:
            check(True, f"from_key rejects {bad!r}")
    for bad in ("no-hash", "A#x+0", "A#1", "A#1+z", 5):
        try:
            LogicalFrame.from_label(bad)
            check(False, f"from_label should reject {bad!r}")
        except ObservationError:
            check(True, f"from_label rejects {bad!r}")


# ── identity adapter ─────────────────────────────────────────────────────────

def test_identity(tmp: Path):
    absent = adapt_identity(tmp / "nope.json")
    check(absent.pillar["verdict"] == NOT_CAPTURED, "identity: absent → NOT_CAPTURED")
    check(absent.observation["captured"] is False, "identity: absent obs not captured")

    complete = write_json(tmp / "pairs_ok.json", pairs_doc())
    r = adapt_identity(complete)
    check(r.pillar["verdict"] == PASS, "identity: complete join → PASS")
    check(r.pillar["join"] == "JOIN_COMPLETE", "identity: join sub-status COMPLETE")

    partial = write_json(tmp / "pairs_gap.json",
                         pairs_doc(retail_only=[["HOUSE_FREEROAM", 1, 9]]))
    r = adapt_identity(partial)
    check(r.pillar["verdict"] == FAIL, "identity: honest gap → FAIL")
    check(r.pillar["join"] == "JOIN_PARTIAL", "identity: join sub-status PARTIAL")
    check(r.pillar["first_divergence"]["logical_frame"]["offset"] == 9,
          "identity: first_divergence localizes the gap frame")

    # a gap OUTSIDE the contract window must not fail the pillar inside it.
    win = ("HOUSE_FREEROAM", 1, 0, 2)
    r = adapt_identity(partial, window=win)
    check(r.pillar["verdict"] == PASS, "identity: gap outside window → PASS in window")

    # pre-EP-03 pairs.json: only a join_verdict string, no gap arrays.
    legacy = write_json(tmp / "pairs_legacy.json",
                        pairs_doc(join_verdict="JOIN_PARTIAL", omit_gap_arrays=True))
    r = adapt_identity(legacy)
    check(r.pillar["verdict"] == FAIL and r.pillar["join"] == "JOIN_PARTIAL",
          "identity: legacy join_verdict classified")

    # a duplicate join key = a broken/tampered join → INCONCLUSIVE.
    dup = pairs_doc()
    dup["pairs"].append({"key": ["HOUSE_FREEROAM", 1, 1], "port": 9, "retail": 9})
    r = adapt_identity(write_json(tmp / "pairs_dup.json", dup))
    check(r.pillar["verdict"] == INCONCLUSIVE, "identity: duplicate frame → INCONCLUSIVE")

    # corrupt JSON = present but untrustworthy → INCONCLUSIVE.
    corrupt = tmp / "pairs_corrupt.json"
    corrupt.write_text("{not json")
    check(adapt_identity(corrupt).pillar["verdict"] == INCONCLUSIVE,
          "identity: corrupt JSON → INCONCLUSIVE")


def test_load_required(tmp: Path):
    p = write_json(tmp / "pairs_lr.json", pairs_doc())
    req = load_required(p)
    check(req == [LogicalFrame.from_key(k) for k in _keys()], "load_required: all paired frames")
    req_w = load_required(p, window=("HOUSE_FREEROAM", 1, 1, 2))
    check(req_w == [LogicalFrame("HOUSE_FREEROAM", 1, 1), LogicalFrame("HOUSE_FREEROAM", 1, 2)],
          "load_required: window filters")
    try:
        load_required(tmp / "absent.json")
        check(False, "load_required should raise on absent pairs")
    except ObservationError:
        check(True, "load_required: absent → ObservationError")


# ── pixels adapter ───────────────────────────────────────────────────────────

def test_pixels(tmp: Path):
    req = [LogicalFrame.from_key(k) for k in _keys()]

    check(adapt_pixels(tmp / "nope.json", req).pillar["verdict"] == NOT_CAPTURED,
          "pixels: absent → NOT_CAPTURED")

    ok = write_json(tmp / "px_ok.json", pixel_metrics())
    check(adapt_pixels(ok, req).pillar["verdict"] == PASS, "pixels: all differ==0 → PASS")

    # NEGATIVE: mutate one differ 0→7 → FAIL at exactly that frame.
    bad_frames = [{"key": k, "differ": 0, "total": 100, "meanabs": 0.0} for k in _keys()]
    bad_frames[1]["differ"] = 7
    bad_frames[1]["meanabs"] = 0.3
    r = adapt_pixels(write_json(tmp / "px_bad.json", pixel_metrics(bad_frames)), req)
    check(r.pillar["verdict"] == FAIL, "pixels: a differ>0 → FAIL")
    check(r.pillar["first_divergence"]["logical_frame"]["offset"] == 1,
          "pixels: first_divergence at the mutated frame")
    check(r.pillar["first_divergence"]["port_value"]["differ"] == 7, "pixels: FAIL carries differ")

    # NEGATIVE: drop a required frame → NOT_CAPTURED (compares EVERY required frame).
    dropped = [f for f in pixel_metrics()["frames"] if f["key"] != _keys()[2]]
    r = adapt_pixels(write_json(tmp / "px_drop.json", pixel_metrics(dropped)), req)
    check(r.pillar["verdict"] == NOT_CAPTURED, "pixels: missing required frame → NOT_CAPTURED")

    # NEGATIVE: reorder the frames → INCONCLUSIVE.
    reordered = list(reversed(pixel_metrics()["frames"]))
    r = adapt_pixels(write_json(tmp / "px_reorder.json", pixel_metrics(reordered)), req)
    check(r.pillar["verdict"] == INCONCLUSIVE, "pixels: reordered frames → INCONCLUSIVE")

    # NEGATIVE: a foreign frame (outside the join) → INCONCLUSIVE.
    foreign = pixel_metrics()["frames"] + [{"key": ["OTHER", 1, 0], "differ": 0}]
    r = adapt_pixels(write_json(tmp / "px_foreign.json", pixel_metrics(foreign)), req)
    check(r.pillar["verdict"] == INCONCLUSIVE, "pixels: foreign frame → INCONCLUSIVE")

    # NEGATIVE: a required frame present but unmeasured (differ null) → NOT_CAPTURED.
    unmeasured = [{"key": k, "differ": 0} for k in _keys()]
    unmeasured[0] = {"key": _keys()[0]}
    r = adapt_pixels(write_json(tmp / "px_unmeasured.json", pixel_metrics(unmeasured)), req)
    check(r.pillar["verdict"] == NOT_CAPTURED, "pixels: unmeasured frame → NOT_CAPTURED")

    # NEGATIVE: source-container mismatch → INCONCLUSIVE (stale/swapped capture).
    src_ok = pixel_metrics(source={"port_container_sha256": A64, "retail_container_sha256": B64})
    check(adapt_pixels(write_json(tmp / "px_src_ok.json", src_ok), req,
                       expected_containers={"port_container_sha256": A64,
                                            "retail_container_sha256": B64}).pillar["verdict"] == PASS,
          "pixels: matching source → PASS")
    r = adapt_pixels(write_json(tmp / "px_src_bad.json", src_ok), req,
                     expected_containers={"port_container_sha256": ("c" * 64),
                                          "retail_container_sha256": B64})
    check(r.pillar["verdict"] == INCONCLUSIVE, "pixels: stale source hash → INCONCLUSIVE")
    # opting in with no recorded source at all also fails closed.
    r = adapt_pixels(ok, req, expected_containers={"port_container_sha256": A64})
    check(r.pillar["verdict"] == INCONCLUSIVE, "pixels: expected source but none recorded → INCONCLUSIVE")

    # NEGATIVE: unknown schema major → INCONCLUSIVE.
    bumped = pixel_metrics()
    bumped["schema_version"] = 999
    check(adapt_pixels(write_json(tmp / "px_ver.json", bumped), req).pillar["verdict"] == INCONCLUSIVE,
          "pixels: unknown schema major → INCONCLUSIVE")

    # empty required (window selected nothing) never reads as PASS.
    check(adapt_pixels(ok, []).pillar["verdict"] == INCONCLUSIVE, "pixels: empty required → INCONCLUSIVE")

    # a non-exact mode is not supported in v1 → INCONCLUSIVE, never a silent pass.
    check(adapt_pixels(ok, req, mode="fuzzy").pillar["verdict"] == INCONCLUSIVE,
          "pixels: unsupported mode → INCONCLUSIVE")


# ── render_program adapter ───────────────────────────────────────────────────

def test_render_program(tmp: Path):
    req = [LogicalFrame.from_key(k) for k in _keys()]

    check(adapt_render_program(tmp / "nope.json", req).pillar["verdict"] == NOT_CAPTURED,
          "render: absent → NOT_CAPTURED")

    ok = write_json(tmp / "dr_ok.json", draw_metrics())
    check(adapt_render_program(ok, req).pillar["verdict"] == PASS, "render: all ALIGNED → PASS")

    # BATCHING still PASSES but is noted (pixels expected equal).
    frames = draw_metrics()["frames"]
    frames[1]["draw_verdict"] = "BATCHING"
    r = adapt_render_program(write_json(tmp / "dr_batch.json", draw_metrics(frames)), req)
    check(r.pillar["verdict"] == PASS, "render: BATCHING → PASS")
    check("BATCHING" in r.pillar["detail"], "render: BATCHING noted in detail")

    # NEGATIVE: flip one verdict to DIVERGENT → FAIL, first_divergence = the tex.
    frames = draw_metrics()["frames"]
    frames[2] = {"key": _keys()[2], "draw_verdict": "DIVERGENT",
                 "divergent": [{"tex": "0" * 12 + "747d", "port_tris": 7, "retail_tris": 1}]}
    r = adapt_render_program(write_json(tmp / "dr_div.json", draw_metrics(frames)), req)
    check(r.pillar["verdict"] == FAIL, "render: DIVERGENT → FAIL")
    check(r.pillar["first_divergence"]["path"].endswith("747d"), "render: FAIL carries divergent tex")
    check(r.pillar["first_divergence"]["port_value"] == 7, "render: FAIL carries port_tris")

    # NEGATIVE: a required frame with no draw_verdict → NOT_CAPTURED.
    frames = [{"key": k, "draw_verdict": "ALIGNED"} for k in _keys()]
    frames[0] = {"key": _keys()[0]}
    r = adapt_render_program(write_json(tmp / "dr_novd.json", draw_metrics(frames)), req)
    check(r.pillar["verdict"] == NOT_CAPTURED, "render: missing draw_verdict → NOT_CAPTURED")

    # NEGATIVE: an unknown verdict token → INCONCLUSIVE.
    frames = draw_metrics()["frames"]
    frames[0]["draw_verdict"] = "MAYBE"
    r = adapt_render_program(write_json(tmp / "dr_unk.json", draw_metrics(frames)), req)
    check(r.pillar["verdict"] == INCONCLUSIVE, "render: unknown verdict → INCONCLUSIVE")


def test_from_view_json(tmp: Path):
    # A synthetic view.json: 1 gap row (skipped) + 3 paired rows, one DIVERGENT.
    view = {
        "scenario": "synthetic",
        "frames": [
            {"offset": 0, "label": "HOUSE_FREEROAM#1+0", "gap": "port", "draw_verdict": None},
            {"offset": 0, "label": "HOUSE_FREEROAM#1+0", "draw_verdict": "ALIGNED",
             "port_tris": 5, "retail_tris": 5, "divergent": []},
            {"offset": 1, "label": "HOUSE_FREEROAM#1+1", "draw_verdict": "ALIGNED",
             "port_tris": 5, "retail_tris": 5, "divergent": []},
            {"offset": 2, "label": "HOUSE_FREEROAM#1+2", "draw_verdict": "DIVERGENT",
             "divergent": [{"tex": "beef", "port_tris": 3, "retail_tris": 9}]},
        ],
    }
    doc = render_metrics_from_view_json(write_json(tmp / "view.json", view))
    check(len(doc["frames"]) == 3, "from_view_json: skips gap rows, keeps 3 paired")
    check(doc["frames"][0]["key"] == ["HOUSE_FREEROAM", 1, 0], "from_view_json: parses label→key")

    req = [LogicalFrame.from_key(k) for k in _keys()]
    metrics_path = write_json(tmp / "dr_from_view.json", doc)
    r = adapt_render_program(metrics_path, req)
    check(r.pillar["verdict"] == FAIL, "from_view_json: bridged DIVERGENT → render FAIL")
    check(r.pillar["first_divergence"]["logical_frame"]["offset"] == 2,
          "from_view_json: FAIL at the divergent frame")


# ── cross-validate against the EP-01 proof schema ────────────────────────────

def test_schema_shapes(tmp: Path):
    try:
        import jsonschema  # noqa: F401
    except Exception:
        _skips.append("proof-schema cross-validation (jsonschema unavailable)")
        return
    schema = json.loads((ROOT / "docs/schemas/parity-proof-v1.schema.json").read_text())
    proof = json.loads((ROOT / "docs/schemas/fixtures/proof-full.valid.json").read_text())

    req = [LogicalFrame.from_key(k) for k in _keys()]
    # a real FAIL result (pixels) + a PASS (identity) exercise the interesting shapes.
    px = adapt_pixels(write_json(tmp / "sc_px.json",
                      pixel_metrics([{"key": _keys()[0], "differ": 0},
                                     {"key": _keys()[1], "differ": 3, "meanabs": 0.1},
                                     {"key": _keys()[2], "differ": 0}])), req)
    ident = adapt_identity(write_json(tmp / "sc_pairs.json", pairs_doc()))

    for name, res in (("pixels", px), ("identity", ident)):
        proof["observations"][name] = res.observation
        proof["pillars"][name] = res.pillar
    try:
        jsonschema.validate(proof, schema)
        check(True, "produced observation/pillar objects validate against parity-proof-v1")
    except jsonschema.ValidationError as exc:
        check(False, f"schema validation failed: {exc.message} at {list(exc.absolute_path)}")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        test_logical_frame()
        test_identity(tmp)
        test_load_required(tmp)
        test_pixels(tmp)
        test_render_program(tmp)
        test_from_view_json(tmp)
        test_schema_shapes(tmp)

    for s in _skips:
        print(f"SKIP: {s}")
    if _failures:
        print("FAIL:")
        for f in _failures:
            print("  -", f)
        return 1
    print(f"OK ({_checks} checks, {len(_skips)} skipped)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
