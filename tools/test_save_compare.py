#!/usr/bin/env python3
"""tools/test_save_compare.py — gate for tools/save_compare.py (synthetic arenas).

  * MAP — the offset→region localizer resolves header/bank/array/byte fields
    against the REAL tools/save_arena_map.json, and pins the real first-divergence
    locus the house-pause-save-commit drive found (bank0/occupied_playtime @ 2840).
  * IDENTICAL — equal arenas → identical, ndiff 0.
  * ONE-BYTE MUTATION — localizes to the exact region + offset, per-side values.
  * SUMMARY — a multi-bank diff of one region collapses to one bucket with span.
  * FAIL CLOSED — wrong-size buffers / missing files raise; CLI exit codes 0/1/2.

Run: nix develop --command python3 tools/test_save_compare.py
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import save_compare as S  # noqa: E402

HEADER = 2832
BANK_STRIDE = 188360

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
    except Exception as e:  # noqa: BLE001
        check(False, f"{msg} (raised {type(e).__name__}, want {exc.__name__})")
    else:
        check(False, f"{msg} (did not raise)")


AM = S.ArenaMap.load()


def bank_off(bank: int, dword: int, byte_in_dword: int = 0) -> int:
    return HEADER + bank * BANK_STRIDE + dword * 4 + byte_in_dword


def arena() -> bytearray:
    return bytearray(S.SAVE_ARENA_BYTES)


def test_map():
    m = AM
    check(m.arena_bytes == S.SAVE_ARENA_BYTES, "map: arena bytes")
    h0 = m.locate(0)
    check(h0.scope == "header" and h0.region == "magic", "map: header byte 0 → magic")
    ls = m.locate(7 * 4)
    check(ls.region == "last_slot_used" and ls.scope == "header",
          "map: header dword 7 → last_slot_used")
    pl = m.locate(2840)
    check(pl.path() == "bank0/occupied_playtime",
          f"map: byte 2840 → bank0/occupied_playtime (got {pl.path()})")
    g = m.locate(bank_off(0, 3))
    check(g.region == "gold" and g.dword == 3, "map: bank0 dword 3 → gold")
    cz = m.locate(bank_off(5, 46212, 12))
    check(cz.path() == "bank5/closeness[3]", f"map: bank5 closeness[3] (got {cz.path()})")
    rr = m.locate(bank_off(1, 40566 + 18))
    check(rr.region == "encyclopedia_discovery" and rr.element_index == 18,
          f"map: bank1 encyclopedia_discovery[18] (got {rr.path()})")
    ef = m.locate(bank_off(0, 0) + 179403)
    check(ef.region == "event_flag", f"map: byte field wins over dword region (got {ef.region})")
    um = m.locate(bank_off(0, 5))
    check(um.path() == "bank0/dword0x0005(unmapped)" and um.field_class == "unknown",
          f"map: unmapped gap is honest (got {um.path()})")
    raises(lambda: m.locate(-1), S.SaveCompareError, "map: negative offset raises")
    raises(lambda: m.locate(S.SAVE_ARENA_BYTES), S.SaveCompareError,
           "map: past-arena offset raises")


def test_identical():
    a = bytes(arena())
    doc = S.compare_saves(a, a, AM)
    check(doc["identical"] is True and doc["ndiff"] == 0, "identical: ndiff 0")
    check(doc["first_divergence"] is None and doc["region_summary"] == [],
          "identical: no divergence, empty summary")


def test_one_byte_mutation():
    base = arena()
    off = bank_off(0, 3)          # bank0/gold
    mut = bytearray(base)
    mut[off] ^= 0xFF
    doc = S.compare_saves(bytes(base), bytes(mut), AM)
    check(doc["identical"] is False and doc["ndiff"] == 1, "mutation: ndiff 1")
    fd = doc["first_divergence"]
    check(fd["byte_off"] == off and fd["path"] == "bank0/gold",
          f"mutation: localized to bank0/gold @ {off} (got {fd['path']} @ {fd['byte_off']})")
    check(fd["port_byte"] == base[off] and fd["retail_byte"] == mut[off],
          "mutation: reports each side's byte")
    # a diff in the last block of the arena is found too
    tail = bytearray(base)
    tail[S.SAVE_ARENA_BYTES - 1] = 1
    d2 = S.compare_saves(bytes(base), bytes(tail), AM)
    check(d2["ndiff"] == 1 and d2["first_divergence"]["byte_off"] == S.SAVE_ARENA_BYTES - 1,
          "mutation: last byte of the arena detected")


def test_summary_collapse():
    base = arena()
    mut = bytearray(base)
    for b in range(1, 10):
        mut[bank_off(b, 40566)] ^= 0x01
    mut[2840] = 7                 # plus one header/bank0 playtime byte, first
    doc = S.compare_saves(bytes(base), bytes(mut), AM)
    check(doc["ndiff"] == 10, f"summary: ndiff 10 (got {doc['ndiff']})")
    check(doc["first_divergence"]["path"] == "bank0/occupied_playtime",
          "summary: first divergence is the lowest offset")
    enc = [r for r in doc["region_summary"] if r["region"] == "encyclopedia_discovery"]
    check(len(enc) == 1 and enc[0]["ndiff"] == 9, "summary: one encyclopedia bucket, 9 bytes")
    check(enc[0]["n_banks"] == 9 and enc[0]["bank_min"] == 1 and enc[0]["bank_max"] == 9,
          "summary: bucket spans banks 1..9")
    check([r["first_byte_off"] for r in doc["region_summary"]]
          == sorted(r["first_byte_off"] for r in doc["region_summary"]),
          "summary: sorted by first byte")
    check("DIFFER" in S.format_report(doc) and "encyclopedia_discovery" in S.format_report(doc),
          "summary: text report names the region")


def test_fail_closed_and_cli():
    a = bytes(arena())
    raises(lambda: S.compare_saves(a[:-1], a, AM), S.SaveCompareError,
           "fail-closed: short port arena raises")
    raises(lambda: S.compare_saves(a, a + b"\x00", AM), S.SaveCompareError,
           "fail-closed: oversize retail arena raises")
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        run = tmp / "scen-both-20260101"
        (run / "openrecet" / "saveout").mkdir(parents=True)
        (run / "retail" / "saveout").mkdir(parents=True)
        p, r = run / "openrecet/saveout/save.dat", run / "retail/saveout/save.dat"
        p.write_bytes(a)
        r.write_bytes(a)
        check(S.main([str(p), str(r)]) == 0, "cli: identical → exit 0")
        b = bytearray(a)
        b[bank_off(0, 3)] = 0x7F
        r.write_bytes(bytes(b))
        out = tmp / "rep.json"
        check(S.main(["--run-dir", str(run), "--json", str(out)]) == 1, "cli: differ → exit 1")
        check(out.exists() and '"bank0/gold"' in out.read_text(), "cli: JSON report written")
        doc = S.compare_files(p, r)
        check(len(doc["source"]["port_save_sha256"]) == 64, "cli: stamps sha256 of inputs")
        check(S.latest_both_run("scen", runs=tmp) == run, "cli: newest both-run located")
        check(S.main([str(p), str(tmp / "missing.dat")]) == 2, "cli: missing file → exit 2")
        raises(lambda: S.saveout_pair(tmp / "nope"), S.SaveCompareError,
               "fail-closed: run dir without save.dat raises")


def main() -> int:
    test_map()
    test_identical()
    test_one_byte_mutation()
    test_summary_collapse()
    test_fail_closed_and_cli()
    if _failures:
        print("FAIL:")
        for f in _failures:
            print("  -", f)
        return 1
    print(f"OK ({_checks} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
