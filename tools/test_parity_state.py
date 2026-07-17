#!/usr/bin/env python3
"""tools/test_parity_state.py — gate for the `state` (volatile) pillar: the ST-02
canonical encoder + Merkle roots and the ST-03 producer/adapter.

Proves the volatile-state pillar's truth-defining core with NO drive dependency,
on synthetic captured field dicts + a synthetic view.json:

  * SCHEMA — the real docs/schemas/state-volatile-v1.json loads, resolves every
    field's type from retail_fields.json (a grouping/spec drift is caught here),
    and DROPS the benign field (title/submenu_state).
  * CODEC — i32/u32/hex/f32 encode to their canonical bytes (f32 by bit pattern,
    int by the 32-bit value); an unknown type / non-numeric value RAISES.
  * MERKLE (ST-02 acceptance) — same values at a different dict order hash to the
    SAME root; one field mutation reports the EXACT leaf path; a benign-field
    change cannot move a root; a present/absent asymmetry is localized.
  * PRODUCER — identical frames → all identical; one divergent frame → that frame
    localized; the doc is scoped+ordered to `required`; has_state false / one-sided
    state → uncovered (fail closed).
  * ADAPTER — PASS on full identical coverage; FAIL localizes the first divergence
    (M1 negative test); absent / has_state-false / partial-coverage → NOT_CAPTURED;
    bad schema / wrong pillar / foreign / reordered / stale-source → INCONCLUSIVE;
    empty required → INCONCLUSIVE.
  * WIRING — `state` is out of parity_prove.UNBUILT_PILLARS (a real producer now).

Run: nix develop --command python3 tools/test_parity_state.py
"""
from __future__ import annotations

import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import parity_prove  # noqa: E402
from parity import adapt_state, state_metrics_from_view_json  # noqa: E402
from parity.observations import LogicalFrame  # noqa: E402
from parity.state_codec import (  # noqa: E402
    StateCodecError,
    StateSchema,
    build_tree,
    encode_value,
)
from parity.state_merkle import (  # noqa: E402
    first_divergent_leaf,
    merkle_root,
    state_root,
)
from parity.state_producer import compare_states, from_view_json  # noqa: E402

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


SCHEMA = StateSchema.load()


def f32(x: float) -> float:
    """Collapse a double to its f32 value (what orv3_state._norm_f32 emits)."""
    return struct.unpack("f", struct.pack("f", x))[0]


def sfields(**over) -> dict:
    """A baseline captured {field: value} dict spanning rng/phase/player/title_menu
    (submenu_state is benign — included to prove it's dropped)."""
    d = {
        "rng": 305419896, "rngcalls": 100,               # rng
        "db054": 42, "gsim": 3,                           # phase
        "px": f32(-0.3), "py": f32(1.5), "poct": 6, "panim": 2,  # player
        "cursor_pos": 1, "submenu_state": 7,             # title_menu (submenu_state benign)
    }
    d.update(over)
    return d


# ── SCHEMA ────────────────────────────────────────────────────────────────────

def test_schema():
    check(SCHEMA.schema_version == 1, "schema: version 1")
    subs = SCHEMA.subsystems()
    for s in ("rng", "phase", "player", "companion", "interaction",
              "customer_service", "camera", "title_menu", "dialogue_intro"):
        check(s in subs, f"schema: subsystem {s} present")
    check(SCHEMA.fields("rng") == [("rng", "i32")],
          "schema: rng field+type resolved (rngcalls benign-excluded)")
    check(SCHEMA.field_type("player", "px") == "f32", "schema: px resolves to f32")
    check(SCHEMA.field_type("player", "poct") == "i32", "schema: poct resolves to i32")
    tnames = [n for n, _ in SCHEMA.fields("title_menu")]
    check("submenu_state" not in tnames, "schema: benign submenu_state DROPPED from title_menu")
    check(("title_menu", "submenu_state") in SCHEMA.benign, "schema: benign set records submenu_state")
    check(("rng", "rngcalls") in SCHEMA.benign, "schema: benign set records rngcalls (capture-origin)")
    check("rngcalls" not in [n for n, _ in SCHEMA.fields("rng")], "schema: rngcalls dropped from rng")

    # a schema field absent from retail_fields.json is fail-closed
    bad = {"schema_version": 1, "subsystems": {"x": {"va": "0x48670f", "fields": ["nonesuch_field"]}}}
    raises(lambda: StateSchema(bad, {("0x48670f", "px"): "f32"}), StateCodecError,
           "schema: field missing from retail_fields.json RAISES")


# ── CODEC ─────────────────────────────────────────────────────────────────────

def test_encode_value():
    check(encode_value("i32", 1) == b"\x01\x00\x00\x00", "encode: i32 1")
    check(encode_value("i32", -1) == b"\xff\xff\xff\xff", "encode: i32 -1 (two's complement)")
    check(encode_value("u32", 0xDEADBEEF) == struct.pack("<I", 0xDEADBEEF), "encode: u32")
    # a value > 2**31 (a raw LCG state read unsigned) must not overflow pack('<i')
    check(encode_value("i32", 2246047975) == struct.pack("<I", 2246047975 & 0xFFFFFFFF),
          "encode: large i32 via 32-bit mask")
    # signed -X and its unsigned 2**32-X encode identically (same 32 bits = same state)
    check(encode_value("i32", -2048919321) == encode_value("u32", (-2048919321) & 0xFFFFFFFF),
          "encode: signed/unsigned repr of the same 32 bits collide")
    check(encode_value("hex", 0x11223344) == struct.pack("<I", 0x11223344), "encode: hex dword")
    check(encode_value("f32", f32(-0.3)) == struct.pack("<f", f32(-0.3)), "encode: f32 bit pattern")
    raises(lambda: encode_value("weird", 1), StateCodecError, "encode: unknown type RAISES")
    raises(lambda: encode_value("i32", "notanum"), StateCodecError, "encode: non-numeric RAISES")


def test_build_tree():
    t = build_tree(sfields(), SCHEMA)
    check("rng" in t and "player" in t, "tree: present subsystems included")
    check("companion" not in t, "tree: absent subsystem omitted (no captured fields)")
    check(list(t["player"]) == ["px", "py", "poct", "panim"], "tree: fields in schema order")
    typ, val, canon = t["rng"]["rng"]
    check(typ == "i32" and val == 305419896 and canon == struct.pack("<I", 305419896),
          "tree: leaf carries (type, value, canon-bytes)")
    check("rngcalls" not in t.get("rng", {}), "tree: benign rngcalls never enters the tree")
    check("submenu_state" not in t.get("title_menu", {}), "tree: benign field never enters the tree")
    check(build_tree({}, SCHEMA) == {}, "tree: empty capture → empty tree")


# ── MERKLE (ST-02 acceptance) ──────────────────────────────────────────────────

def test_merkle_address_independent():
    # same values, DIFFERENT dict insertion order → identical root (ST-02 #1)
    a = {"rngcalls": 100, "rng": 305419896, "poct": 6, "px": f32(-0.3), "py": f32(1.5),
         "panim": 2, "db054": 42, "gsim": 3, "cursor_pos": 1}
    b = dict(reversed(list(a.items())))
    check(state_root(a, SCHEMA) == state_root(b, SCHEMA),
          "merkle: same values, different order → same root")
    # a value change → a different root
    check(state_root(a, SCHEMA) != state_root({**a, "db054": 43}, SCHEMA),
          "merkle: a value change → different root")
    # a benign (rngcalls) change → SAME root (excluded from the tree)
    check(state_root(a, SCHEMA) == state_root({**a, "rngcalls": 999}, SCHEMA),
          "merkle: benign rngcalls change → same root")


def test_merkle_first_leaf():
    base = sfields()
    # one int field mutated → exact leaf path (M1) (ST-02 #2)
    d = first_divergent_leaf(build_tree(base, SCHEMA),
                             build_tree(sfields(db054=43), SCHEMA), SCHEMA)
    check(d is not None and d.path == "phase/db054", "merkle: int mutation → phase/db054 leaf")
    check(d.a_value == 42 and d.b_value == 43, "merkle: leaf carries both raw values")
    # one f32 field mutated → exact leaf path
    d = first_divergent_leaf(build_tree(base, SCHEMA),
                             build_tree(sfields(px=f32(-0.31)), SCHEMA), SCHEMA)
    check(d is not None and d.path == "player/px", "merkle: f32 mutation → player/px leaf")
    # benign field change → NO divergence, roots equal (ST-02 #3)
    check(state_root(base, SCHEMA) == state_root(sfields(submenu_state=999), SCHEMA),
          "merkle: benign submenu_state change → same root")
    check(first_divergent_leaf(build_tree(base, SCHEMA),
                               build_tree(sfields(submenu_state=999), SCHEMA), SCHEMA) is None,
          "merkle: benign change → no divergent leaf")
    # present/absent asymmetry → localized
    with_poct = build_tree(sfields(), SCHEMA)
    no_poct = build_tree({k: v for k, v in sfields().items() if k != "poct"}, SCHEMA)
    d = first_divergent_leaf(with_poct, no_poct, SCHEMA)
    check(d is not None and d.path == "player/poct" and d.a_present and not d.b_present,
          "merkle: present/absent asymmetry localized (player/poct)")
    # symmetric absence (neither has companion fields) → not flagged
    check(first_divergent_leaf(build_tree(sfields(), SCHEMA),
                               build_tree(sfields(), SCHEMA), SCHEMA) is None,
          "merkle: identical captures → no divergence")
    # identical roots ⇔ no divergent leaf
    check(merkle_root(build_tree(base, SCHEMA), SCHEMA) ==
          merkle_root(build_tree(dict(base), SCHEMA), SCHEMA), "merkle: root reproducible")


# ── PRODUCER ───────────────────────────────────────────────────────────────────

def _paired(label, port, retail):
    return (label, {"port": port, "retail": retail})


def test_compare_states():
    labels = ["SAVE_PICKER_READY#1+0", "SAVE_PICKER_READY#1+1", "SAVE_PICKER_READY#1+2"]
    req = [LogicalFrame.from_label(s) for s in labels]
    # all identical
    paired = dict(_paired(l, sfields(), sfields()) for l in labels)
    doc = compare_states(paired, SCHEMA, req)
    check(len(doc["frames"]) == 3 and all(f["identical"] for f in doc["frames"]),
          "producer: identical port/retail → all frames identical")
    check(doc["pillar"] == "state" and doc["state_schema_version"] == 1, "producer: doc header")
    # one frame diverges in db054
    paired2 = dict(paired)
    paired2[labels[1]] = {"port": sfields(db054=117), "retail": sfields(db054=119)}
    doc2 = compare_states(paired2, SCHEMA, req)
    f1 = doc2["frames"][1]
    check(not f1["identical"] and f1["divergence"]["path"] == "phase/db054",
          "producer: divergent frame localized to phase/db054")
    check(f1["divergence"]["retail_value"] == 119 and f1["divergence"]["port_value"] == 117,
          "producer: divergence carries retail/port values")
    # scoping: paired has an extra label, required only 2 → doc has 2 in required order
    paired3 = dict(paired)
    paired3["OTHER#1+9"] = {"port": sfields(), "retail": sfields()}
    doc3 = compare_states(paired3, SCHEMA, req[:2])
    check([tuple(f["key"]) for f in doc3["frames"]] == [tuple(r) for r in req[:2]],
          "producer: scoped+ordered to required")
    # a required frame missing from paired → omitted (uncovered)
    doc4 = compare_states({labels[0]: paired[labels[0]]}, SCHEMA, req)
    check(len(doc4["frames"]) == 1, "producer: uncovered required frame omitted")


def _view(frames, has_state=True):
    return {"has_state": has_state, "frames": frames}


def test_from_view_json(tmp):
    tmp = Path(tmp)
    labels = ["SAVE_PICKER_READY#1+0", "SAVE_PICKER_READY#1+1"]
    req = [LogicalFrame.from_label(s) for s in labels]
    frames = [{"label": labels[0], "state": {"port": sfields(), "retail": sfields()}},
              {"label": labels[1], "state": {"port": sfields(px=f32(-0.31)),
                                             "retail": sfields()}}]
    vp = tmp / "view.json"
    vp.write_text(json.dumps(_view(frames)))
    doc = from_view_json(vp, required=req)
    check(doc["has_state"] is True and len(doc["frames"]) == 2, "view: both frames bridged")
    check(doc["frames"][0]["identical"] and not doc["frames"][1]["identical"],
          "view: per-frame identical/divergent verdicts")
    check(doc["frames"][1]["divergence"]["path"] == "player/px", "view: divergence localized")
    # has_state false → empty frames
    vp.write_text(json.dumps(_view(frames, has_state=False)))
    doc = from_view_json(vp, required=req)
    check(doc["has_state"] is False and doc["frames"] == [], "view: has_state false → no frames")
    # one-sided state → frame dropped (uncovered)
    one = [{"label": labels[0], "state": {"port": sfields()}}]
    vp.write_text(json.dumps(_view(one)))
    doc = from_view_json(vp, required=req)
    check(doc["frames"] == [], "view: one-sided state frame dropped")


# ── ADAPTER ────────────────────────────────────────────────────────────────────

def adapt_doc(doc, tmp, required, expected_containers=None):
    p = Path(tmp) / "state-metrics.json"
    p.write_text(json.dumps(doc))
    return adapt_state(p, required, expected_containers=expected_containers)


def test_adapter(tmp):
    tmp = Path(tmp)
    labels = ["SAVE_PICKER_READY#1+0", "SAVE_PICKER_READY#1+1"]
    req = [LogicalFrame.from_label(s) for s in labels]
    paired = dict(_paired(l, sfields(), sfields()) for l in labels)

    # PASS — full identical coverage
    doc = compare_states(paired, SCHEMA, req)
    r = adapt_doc(doc, tmp, req)
    check(r.pillar["verdict"] == "PASS", "adapter: full identical coverage → PASS")

    # FAIL — one divergence localized (M1)
    p2 = dict(paired)
    p2[labels[1]] = {"port": sfields(db054=117), "retail": sfields(db054=119)}
    doc = compare_states(p2, SCHEMA, req)
    r = adapt_doc(doc, tmp, req)
    check(r.pillar["verdict"] == "FAIL", "adapter: a divergence → FAIL")
    fd = r.pillar.get("first_divergence") or {}
    check(fd.get("path") == "phase/db054", "adapter: FAIL localizes phase/db054")
    check(fd.get("logical_frame", {}).get("offset") == 1, "adapter: FAIL at the right logical frame")

    # NOT_CAPTURED — absent file
    check(adapt_state(tmp / "absent.json", req).pillar["verdict"] == "NOT_CAPTURED",
          "adapter: absent → NOT_CAPTURED")
    # NOT_CAPTURED — has_state false
    doc = {"schema_version": 1, "pillar": "state", "has_state": False, "frames": []}
    check(adapt_doc(doc, tmp, req).pillar["verdict"] == "NOT_CAPTURED",
          "adapter: has_state false → NOT_CAPTURED")
    # NOT_CAPTURED — partial coverage (one required frame missing)
    doc = compare_states({labels[0]: paired[labels[0]]}, SCHEMA, req)
    check(adapt_doc(doc, tmp, req).pillar["verdict"] == "NOT_CAPTURED",
          "adapter: partial coverage → NOT_CAPTURED")

    # INCONCLUSIVE — unknown schema major
    check(adapt_doc({"schema_version": 999, "pillar": "state", "has_state": True, "frames": []},
                    tmp, req).pillar["verdict"] == "INCONCLUSIVE",
          "adapter: unknown schema → INCONCLUSIVE")
    # INCONCLUSIVE — wrong pillar
    check(adapt_doc({"schema_version": 1, "pillar": "pixels", "has_state": True, "frames": []},
                    tmp, req).pillar["verdict"] == "INCONCLUSIVE",
          "adapter: wrong pillar → INCONCLUSIVE")
    # INCONCLUSIVE — a foreign frame (outside the identity join)
    doc = compare_states(paired, SCHEMA, req)
    doc["frames"].append({"key": ["OTHER", 1, 9], "port_root": "a", "retail_root": "a",
                          "identical": True})
    check(adapt_doc(doc, tmp, req).pillar["verdict"] == "INCONCLUSIVE",
          "adapter: foreign frame → INCONCLUSIVE")
    # INCONCLUSIVE — reordered
    doc = compare_states(paired, SCHEMA, req)
    doc["frames"].reverse()
    check(adapt_doc(doc, tmp, req).pillar["verdict"] == "INCONCLUSIVE",
          "adapter: reordered frames → INCONCLUSIVE")
    # INCONCLUSIVE — empty required
    check(adapt_doc(compare_states(paired, SCHEMA, req), tmp, []).pillar["verdict"] == "INCONCLUSIVE",
          "adapter: empty required → INCONCLUSIVE")

    # source provenance: stale expected_containers → INCONCLUSIVE; matching → PASS
    doc = compare_states(paired, SCHEMA, req, source={"port_container_sha256": "a" * 64})
    check(adapt_doc(doc, tmp, req, expected_containers={"port_container_sha256": "b" * 64}
                    ).pillar["verdict"] == "INCONCLUSIVE", "adapter: stale source → INCONCLUSIVE")
    check(adapt_doc(doc, tmp, req, expected_containers={"port_container_sha256": "a" * 64}
                    ).pillar["verdict"] == "PASS", "adapter: matching source → PASS")


def test_wiring():
    check("state" not in parity_prove.UNBUILT_PILLARS,
          "wiring: state OUT of UNBUILT_PILLARS (a real producer now)")
    check(callable(adapt_state) and callable(state_metrics_from_view_json),
          "wiring: adapt_state + bridge exported from parity")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        test_schema()
        test_encode_value()
        test_build_tree()
        test_merkle_address_independent()
        test_merkle_first_leaf()
        test_compare_states()
        test_from_view_json(tmp)
        test_adapter(tmp)
        test_wiring()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
