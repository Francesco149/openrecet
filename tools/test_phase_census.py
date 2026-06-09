#!/usr/bin/env python3
"""Tests for phase_census pure parts (variant generation + differ + attribution).

Run: nix develop --command python3 tools/test_phase_census.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from phase_census import (  # noqa: E402
    diff_ranges, known_retail_cls, make_variants, nearest_sym)

PINNED = """\
{"savefile": "_saves/x.sav.gz"}
{"frame": 0, "buttons": "0x0000"}
{"frame": 105, "buttons": "0x0010"}
{"frame": 108, "buttons": "0x0000"}
{"wait": "LOADING_END"}
{"phasepin": 80}
{"rngseed": [80, 19937]}
{"caprange": [120, 48]}
{"calltrace": [120, 48]}
{"frame": 0, "buttons": "0x0000"}
"""


def ops(text):
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if s and not s.startswith("#"):
            out.append(json.loads(s))
    return out


def test_discovery_strips_pins_and_snaps_at_window():
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "_saves").mkdir()
        (Path(td) / "_saves" / "x.sav.gz").write_bytes(b"x")
        a, b, snap = make_variants(PINNED, "discovery", 37, 64, Path(td))
        assert snap == 120
        oa = ops(a)
        assert not any("phasepin" in o for o in oa)
        assert not any("rngseed" in o for o in oa)
        ms = [i for i, o in enumerate(oa) if "memsnap" in o]
        cr = [i for i, o in enumerate(oa) if "caprange" in o]
        assert ms and cr and ms[0] < cr[0]
        assert oa[ms[0]]["memsnap"] == 120
        # savefile absolutized
        sv = next(o for o in oa if "savefile" in o)
        assert Path(sv["savefile"]).is_absolute()


def test_pinned_ensures_pins_and_settles():
    with tempfile.TemporaryDirectory() as td:
        a, _, snap = make_variants(PINNED, "pinned", 37, 64, Path(td))
        assert snap == 80 + 64
        oa = ops(a)
        assert any("phasepin" in o for o in oa)        # pins kept
        assert any(o.get("memsnap") == 144 for o in oa)
        # an UNPINNED trace gets the canonical pins added at cr.start
        unpinned = PINNED.replace('{"phasepin": 80}\n', "") \
                         .replace('{"rngseed": [80, 19937]}\n', "")
        a2, _, snap2 = make_variants(unpinned, "pinned", 37, 64, Path(td))
        o2 = ops(a2)
        assert any(o.get("phasepin") == 120 for o in o2)
        assert any(o.get("rngseed") == [120, 19937] for o in o2)
        assert snap2 == 120 + 64


def test_variant_b_shifts_only_boot_segment():
    with tempfile.TemporaryDirectory() as td:
        a, b, _ = make_variants(PINNED, "discovery", 37, 64, Path(td))
        oa, ob = ops(a), ops(b)
        wa = next(i for i, o in enumerate(oa) if "wait" in o)
        # boot inputs shifted by +37
        for i in range(wa):
            if "buttons" in oa[i]:
                assert ob[i]["frame"] == oa[i]["frame"] + 37, (oa[i], ob[i])
        # post-anchor rows identical (incl. the memsnap + post-anchor input)
        assert oa[wa:] == ob[wa:]


def test_diff_ranges_groups_and_merges():
    a = bytearray(1000)
    b = bytearray(1000)
    b[10] = 1                      # lone byte
    b[100] = 2; b[103] = 3         # gap 3 < merge → one range
    b[500] = 4; b[600] = 5         # far apart → separate
    r = diff_ranges(bytes(a), bytes(b), merge_gap=8)
    assert r == [(10, 1), (100, 4), (500, 1), (600, 1)], r
    assert diff_ranges(bytes(a), bytes(a)) == []


def test_control_classifies_volatile():
    import phase_census as pc
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        a = bytearray(4096); b = bytearray(4096); c = bytearray(4096)
        b[0x100] = 1                   # timing signal (A==C, differs in B)
        b[0x800] = 2; c[0x800] = 3     # volatile (differs in B AND in control)
        for name, buf in (("a", a), ("b", b), ("ctl", c)):
            d = td / name / "frames"; d.mkdir(parents=True)
            (d / "memsnap_00120_x.bin").write_bytes(bytes(buf))
            (d / "memsnap_00120.json").write_text(json.dumps(
                {"frame": 120, "link_base": 0x400000, "sections": [
                    {"name": "x", "rva": 0x1000, "vsize": 4096,
                     "file": "memsnap_00120_x.bin"}]}))
        da = pc.find_dumps(td / "a", "port")
        db = pc.find_dumps(td / "b", "port")
        dc = pc.find_dumps(td / "ctl", "port")
        rep = pc.build_report(da, db, "port", None, dumps_ctl=dc)
        cls = {r["off"]: r["cls"] for r in rep["regions"][0]["ranges"]}
        assert cls[0x100] == "engine", cls
        assert cls[0x800] == "volatile", cls


def test_classification_patterns_and_known_retail():
    # the triage patterns + the retail KNOWN table seeded from the HOUSE census.
    import phase_census as pc
    assert pc.PORT_KNOWN_UNPINNED_PAT.match("g_scene1_overlay_slots")  # the mole
    assert not pc.PORT_KNOWN_UNPINNED_PAT.match("g_mystery")           # new → engine
    assert pc.PORT_HARNESS_PAT.match("g_sim_buttons")                  # input shift
    assert pc.PORT_HARNESS_PAT.match("g_singleton_mutex")
    assert pc.PORT_BENIGN_PAT.match("g_tab")                           # asset hash
    # retail: the sparkle slot region is known-unpinned; the DI region harness;
    # the one open byte is NOT covered (stays UNKNOWN → the gate flags it).
    assert pc.known_retail_cls(0x0064e91c, 24)[1] == "known-unpinned"
    assert pc.known_retail_cls(0x073dfd3c, 1)[1] == "harness"
    assert pc.known_retail_cls(0x09643518, 1)[1] == "clock"
    assert pc.known_retail_cls(0x0438b430, 1) is None                  # OPEN byte


def test_ptr_layout_vs_phase_counter():
    import phase_census as pc
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        a = bytearray(4096); b = bytearray(4096)
        # phase counter: small ints differ → engine (frame-affecting)
        a[0x40:0x44] = (64).to_bytes(4, "little")
        b[0x40:0x44] = (40).to_bytes(4, "little")
        # arena pointer relocated: null↔heapptr → ptr-layout (benign)
        b[0x80:0x84] = (0x12557ef0).to_bytes(4, "little")
        for name, buf in (("a", a), ("b", b)):
            d = td / name / "frames"; d.mkdir(parents=True)
            (d / "memsnap_00120_x.bin").write_bytes(bytes(buf))
            (d / "memsnap_00120.json").write_text(json.dumps(
                {"frame": 120, "link_base": 0x3a0000, "sections": [
                    {"name": "x", "rva": 0x90000, "vsize": 4096,
                     "file": "memsnap_00120_x.bin"}]}))
        rep = pc.build_report(pc.find_dumps(td / "a", "port"),
                              pc.find_dumps(td / "b", "port"), "port", None)
        cls = {r["off"]: r["cls"] for r in rep["regions"][0]["ranges"]}
        assert cls[0x40] == "engine", cls            # small-int phase
        assert cls[0x80] == "ptr-layout", cls         # relocated pointer
        # va uses the fixed image base, NOT the ASLR link_base in the json
        va40 = next(r["va"] for r in rep["regions"][0]["ranges"] if r["off"] == 0x40)
        assert va40 == f"0x{pc.IMAGE_BASE + 0x90000 + 0x40:08x}", va40


def test_nearest_sym_and_known_table():
    syms = [(0x1000, "g_alpha"), (0x2000, "g_beta")]
    assert nearest_sym(syms, 0x1004) == ("g_alpha", 4)
    assert nearest_sym(syms, 0x2000) == ("g_beta", 0)
    assert nearest_sym(syms, 0x500) == ("?", 0)
    # known retail: db054 hit, intersecting range counts, unknown VA → None
    assert known_retail_cls(0x056db054, 4) == (
        "db054 bob/sparkle counter", "pinned")
    assert known_retail_cls(0x056db052, 4) is not None     # straddles
    assert known_retail_cls(0x12345678, 4) is None
    assert known_retail_cls(0x073a3e0c, 4)[1] == "known-unpinned"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in tests:
        fn()
    print(f"OK: phase_census ({len(tests)} tests)")


if __name__ == "__main__":
    main()
