#!/usr/bin/env python3
"""tools/test_parity_save.py — gate for the `save` pillar PRODUCER + ST-00 map.

Proves the save-equality pillar's truth-defining core with NO drive dependency, on
synthetic 18 MB arenas:

  * STATE MAP — the ST-00 offset→region localizer resolves header/bank/array/byte
    fields against the REAL docs/schemas/state-map-v1.json (so a map edit that
    breaks an offset is caught here), and pins the real first-divergence locus
    (bank0/occupied_playtime @ byte 2840) the house-pause-save-commit drive found.
  * FAITHFUL — identical arenas → identical, ndiff 0, and adapt_save → PASS.
  * DISPROOF (M1 NEGATIVE TEST) — flip one byte at a known offset → first_divergence
    localizes to the exact region+offset, and adapt_save → FAIL with that path.
  * SUMMARY — a multi-element / multi-bank diff collapses to one region bucket that
    reports its byte count and bank span.
  * FAIL CLOSED — a wrong-size buffer / a missing save.dat RAISES (never identical).
  * ADAPTER — absent → NOT_CAPTURED; malformed / bad schema / stale source →
    INCONCLUSIVE.

Run: nix develop --command python3 tools/test_parity_save.py
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import adapt_save  # noqa: E402
from parity.fingerprint import sha256_file  # noqa: E402
from parity.observations import LogicalFrame  # noqa: E402
from parity.save_producer import (  # noqa: E402
    SAVE_ARENA_BYTES,
    SaveProducerError,
    compare_saves,
    produce,
    produce_from_run_dir,
)
from parity.state_map import StateMap, StateMapError  # noqa: E402

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


SM = StateMap.load()


def bank_off(bank: int, dword: int, byte_in_dword: int = 0) -> int:
    return HEADER + bank * BANK_STRIDE + dword * 4 + byte_in_dword


def arena() -> bytearray:
    return bytearray(SAVE_ARENA_BYTES)


# ── state map localizer ──────────────────────────────────────────────────────

def test_state_map():
    m = SM
    check(m.arena_bytes == SAVE_ARENA_BYTES, "map: arena bytes")

    h0 = m.locate(0)
    check(h0.scope == "header" and h0.region == "magic", "map: header byte 0 → magic")
    ls = m.locate(7 * 4)  # header dword 7
    check(ls.region == "last_slot_used" and ls.scope == "header",
          "map: header dword 7 → last_slot_used (not the stale dword-6 comment)")

    # the REAL first divergence house-pause-save-commit found — regression-lock it.
    pl = m.locate(2840)
    check(pl.scope == "bank" and pl.bank == 0 and pl.region == "occupied_playtime",
          f"map: byte 2840 → bank0/occupied_playtime (got {pl.path()})")
    check(pl.path() == "bank0/occupied_playtime", "map: playtime path")

    g = m.locate(bank_off(0, 3))
    check(g.region == "gold" and g.dword == 3, "map: bank0 dword 3 → gold")

    # array element index + bank
    cz = m.locate(bank_off(5, 46212, 12))  # closeness element 3 in bank 5
    check(cz.bank == 5 and cz.region == "closeness" and cz.element_index == 3,
          f"map: bank5 closeness[3] (got {cz.path()})")
    check(cz.path() == "bank5/closeness[3]", "map: array path with element index")

    rr = m.locate(bank_off(1, 40566 + 18))  # encyclopedia_discovery element 18 in bank 1
    check(rr.region == "encyclopedia_discovery" and rr.element_index == 18,
          f"map: bank1 encyclopedia_discovery[18] (got {rr.path()})")

    # byte-addressed field wins over the containing dword region
    ef = m.locate(bank_off(0, 0) + 179403)
    check(ef.region == "event_flag", f"map: byte 0x2bccb → event_flag (got {ef.region})")

    # unmapped gap → honest (unmapped), not a fabricated name
    um = m.locate(bank_off(0, 5))  # dword 5 (between field4 and item_slot_table)
    check(um.region == "(unmapped)" and um.field_class == "unknown",
          f"map: bank0 dword 5 → (unmapped) (got {um.region})")
    check(um.path() == "bank0/dword0x0005(unmapped)", "map: unmapped path")

    raises(lambda: m.locate(-1), StateMapError, "map: negative offset raises")
    raises(lambda: m.locate(SAVE_ARENA_BYTES), StateMapError, "map: past-arena offset raises")


# ── producer core ────────────────────────────────────────────────────────────

def adapt_save_doc(doc, tmp, **kw):
    p = Path(tmp) / f"sm{adapt_save_doc.n}.json"
    adapt_save_doc.n += 1
    p.write_text(json.dumps(doc))
    return adapt_save(p, **kw)


adapt_save_doc.n = 0


def test_faithful(tmp):
    a = arena()
    doc = compare_saves(bytes(a), bytes(a), SM,
                        source={"port_save_sha256": "a" * 64, "retail_save_sha256": "a" * 64})
    check(doc["identical"] is True and doc["ndiff"] == 0, "faithful: identical, ndiff 0")
    check(doc["first_divergence"] is None, "faithful: no first_divergence")
    r = adapt_save_doc(doc, tmp)
    check(r.pillar["verdict"] == "PASS", "faithful: adapt_save PASS")


def test_disproof_negative(tmp):
    """M1: a deliberate one-byte mutation localizes to the correct region+offset."""
    base = arena()
    off = bank_off(0, 3)  # bank0/gold
    mut = bytearray(base)
    mut[off] ^= 0xFF
    doc = compare_saves(bytes(base), bytes(mut), SM)
    check(doc["identical"] is False and doc["ndiff"] == 1, "disproof: ndiff 1")
    fd = doc["first_divergence"]
    check(fd["byte_off"] == off, f"disproof: first byte off {off} (got {fd['byte_off']})")
    check(fd["path"] == "bank0/gold", f"disproof: localized to bank0/gold (got {fd['path']})")
    check(fd["region"] == "gold" and fd["bank"] == 0, "disproof: region+bank")
    # port here is `base`, retail is `mut` — values reported per side
    check(fd["port_byte"] == base[off] and fd["retail_byte"] == mut[off],
          "disproof: reports each side's byte")
    r = adapt_save_doc(doc, tmp, nominal_frame=LogicalFrame("SAVE_PICKER_READY", 1, 0))
    check(r.pillar["verdict"] == "FAIL", "disproof: adapt_save FAIL")
    check(r.pillar["first_divergence"]["path"] == "bank0/gold", "disproof: adapter carries the path")
    check(r.pillar["first_divergence"]["logical_frame"]["anchor"] == "SAVE_PICKER_READY",
          "disproof: nominal frame = contract anchor")


def test_summary_collapse():
    """A diff across many elements/banks of one region → a single bucket with the
    right byte count and bank span (not one bucket per element)."""
    base = arena()
    mut = bytearray(base)
    # perturb encyclopedia_discovery[0] field-0 in banks 1..9 (one byte each)
    for b in range(1, 10):
        mut[bank_off(b, 40566)] ^= 0x01
    doc = compare_saves(bytes(base), bytes(mut), SM)
    rs = doc["region_summary"]
    enc = [r for r in rs if r["region"] == "encyclopedia_discovery"]
    check(len(enc) == 1, f"summary: encyclopedia_discovery collapses to 1 bucket (got {len(enc)})")
    check(enc[0]["ndiff"] == 9, "summary: 9 differing bytes counted")
    check(enc[0]["n_banks"] == 9 and enc[0]["bank_min"] == 1 and enc[0]["bank_max"] == 9,
          "summary: spans banks 1..9")


def test_fail_closed_size():
    a = arena()
    raises(lambda: compare_saves(bytes(a[:-1]), bytes(a), SM),
           SaveProducerError, "fail-closed: short port arena raises")
    raises(lambda: compare_saves(bytes(a), bytes(a) + b"\x00", SM),
           SaveProducerError, "fail-closed: oversize retail arena raises")


def test_produce_and_stamp(tmp):
    tmp = Path(tmp)
    p, r = tmp / "port.dat", tmp / "retail.dat"
    a = arena()
    p.write_bytes(bytes(a))
    b = bytearray(a)
    b[bank_off(0, 3)] = 0x7F  # bank0/gold
    r.write_bytes(bytes(b))
    doc, out = produce(p, r, tmp / "save-metrics.json")
    check(out.exists(), "produce: wrote save-metrics.json")
    check(doc["source"]["port_save_sha256"] == sha256_file(p), "produce: stamps port sha")
    check(doc["source"]["retail_save_sha256"] == sha256_file(r), "produce: stamps retail sha")
    check(doc["first_divergence"]["path"] == "bank0/gold", "produce: localizes the diff")
    # missing file → fail closed
    raises(lambda: produce_from_run_dir(tmp / "nope", tmp / "x.json"),
           SaveProducerError, "produce: missing run dir save.dat raises")


# ── adapter edge cases ───────────────────────────────────────────────────────

def test_adapter_edges(tmp):
    tmp = Path(tmp)
    # absent → NOT_CAPTURED
    r = adapt_save(tmp / "absent.json")
    check(r.pillar["verdict"] == "NOT_CAPTURED", "adapter: absent → NOT_CAPTURED")

    # not-identical but no first_divergence → INCONCLUSIVE
    bad = {"schema_version": 1, "pillar": "save", "arena_bytes": SAVE_ARENA_BYTES,
           "identical": False, "ndiff": 3}
    r = adapt_save_doc(bad, tmp)
    check(r.pillar["verdict"] == "INCONCLUSIVE", "adapter: no first_divergence → INCONCLUSIVE")

    # wrong schema major → INCONCLUSIVE
    r = adapt_save_doc({"schema_version": 999, "pillar": "save", "identical": True, "ndiff": 0}, tmp)
    check(r.pillar["verdict"] == "INCONCLUSIVE", "adapter: unknown schema → INCONCLUSIVE")

    # wrong pillar → INCONCLUSIVE
    r = adapt_save_doc({"schema_version": 1, "pillar": "pixels", "identical": True, "ndiff": 0}, tmp)
    check(r.pillar["verdict"] == "INCONCLUSIVE", "adapter: wrong pillar → INCONCLUSIVE")

    # stale source (expected_saves mismatch) → INCONCLUSIVE
    good = {"schema_version": 1, "pillar": "save", "arena_bytes": SAVE_ARENA_BYTES,
            "identical": True, "ndiff": 0, "source": {"port_save_sha256": "a" * 64}}
    r = adapt_save_doc(good, tmp, expected_saves={"port_save_sha256": "b" * 64})
    check(r.pillar["verdict"] == "INCONCLUSIVE", "adapter: stale source → INCONCLUSIVE")
    # matching source → PASS
    r = adapt_save_doc(good, tmp, expected_saves={"port_save_sha256": "a" * 64})
    check(r.pillar["verdict"] == "PASS", "adapter: matching source → PASS")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        test_state_map()
        test_faithful(tmp)
        test_disproof_negative(tmp)
        test_summary_collapse()
        test_fail_closed_size()
        test_produce_and_stamp(tmp)
        test_adapter_edges(tmp)

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
