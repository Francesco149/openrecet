#!/usr/bin/env python3
"""tools/test_parity_prove.py — EP-05 gate for the parity proof compiler.

Proves the acceptance the roadmap fixes for EP-05 (§6 EP-05 + §15):

  * GATE follows §4.1 — all required pillars PASS → exit 0; a required FAIL → 1;
    a required NOT_CAPTURED/INCONCLUSIVE → 2 (a contract never passes by omission).
  * DETERMINISTIC id — identical inputs reproduce the same proof_id; mutating ANY
    hashed field (a pillar verdict, a subject/tool hash) changes it.
  * NO LEAKED PATHS — a machine-local path in the envelope is written to proof.json
    but excluded from the proof_id (the canonical preimage never contains it).
  * IMMUTABLE CAS — the bundle lands at sha256/<first2>/<id>/; a rerun is idempotent.
  * PER-PILLAR NEGATIVE — mutate one pixel diff / one draw verdict / a state field /
    a save byte / a tool hash independently; each flips the expected pillar or id.

The pure core (tools/parity/prove.py) is tested directly; the CLI orchestration
(resolve_observations / build_proof / main) is tested over a SYNTHETIC v3 window +
contract, so no Windows/Frida/live capture is needed.

Run: nix develop --command python3 tools/test_parity_prove.py
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import parity_prove  # noqa: E402
from parity import canonical_bytes, proof_id_of  # noqa: E402
from parity import prove as P  # noqa: E402

_checks = 0
_failures: list[str] = []
_skips: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


# ── synthetic provenance + pillars for the pure-core tests ───────────────────

H = "0" * 64
H2 = "1" * 64


def base_groups():
    subject = {"port": {"pe_sha256": H, "git_commit": "abc1234", "dirty_patch_sha256": None},
               "retail": {"pe_sha256": H2, "reference_id": "recettear-unpacked"}}
    inputs = {"save": "@fresh",
              "scenario_contract": {"id": "syn", "contract_sha256": H, "contract_version": 1},
              "trace_sha256": H, "assets_manifest_sha256": "@none", "recet_ini_sha256": "@none"}
    environment = {"os_build": "Windows-10", "locale": "ja_JP", "codepage": "cp932",
                   "d3d_runtime": "d3d8", "gpu": "ref", "driver": "ref",
                   "resolution": "1024x768", "display_mode": "windowed"}
    tools = {"frida_agent_sha256": "@none", "d3d_proxy_sha256": "@none",
             "replayer_sha256": "@none", "comparator_sha256": H, "schema_sha256": H2}
    return subject, inputs, environment, tools


def pillars(**verdicts) -> dict:
    return {name: {"verdict": v} for name, v in verdicts.items()}


def assemble_with(pil, **kw):
    subject, inputs, environment, tools = base_groups()
    return P.assemble(subject=subject, inputs=inputs, environment=environment, tools=tools,
                      normalization=[], observations={}, pillars=pil, **kw)


# ── prove.py: gate ───────────────────────────────────────────────────────────

def test_gate():
    req = ["identity", "state", "save", "render_program", "pixels"]
    all_pass = pillars(identity="PASS", state="PASS", save="PASS",
                       render_program="PASS", pixels="PASS")
    proof = assemble_with(all_pass)
    check(P.gate(req, proof) == ("PASS", 0, {p: "PASS" for p in req}), "gate: all PASS → 0")

    # NEGATIVE: mutate each required pillar to FAIL independently.
    for p in req:
        mut = dict(all_pass)
        mut[p] = {"verdict": "FAIL"}
        verdict, code, _ = P.gate(req, assemble_with(mut))
        check((verdict, code) == ("FAIL", 1), f"gate: {p} FAIL → exit 1")

    # a required NOT_CAPTURED (no producer) → INCONCLUSIVE, exit 2 (never a pass).
    mut = dict(all_pass)
    mut["state"] = {"verdict": "NOT_CAPTURED"}
    verdict, code, _ = P.gate(req, assemble_with(mut))
    check((verdict, code) == ("INCONCLUSIVE", 2), "gate: required NOT_CAPTURED → exit 2")

    # a required pillar entirely ABSENT from the bundle counts as NOT_CAPTURED.
    partial = pillars(identity="PASS")
    verdict, code, verds = P.gate(req, assemble_with(partial))
    check((verdict, code) == ("INCONCLUSIVE", 2) and verds["pixels"] == "NOT_CAPTURED",
          "gate: omitted required pillar → NOT_CAPTURED/exit 2")

    # a FAIL dominates a NOT_CAPTURED (exit 1, not 2).
    mut = pillars(identity="FAIL", state="NOT_CAPTURED")
    check(P.gate(["identity", "state"], assemble_with(mut))[1] == 1, "gate: FAIL dominates NOT_CAPTURED")


# ── prove.py: determinism + provenance sensitivity ───────────────────────────

def test_determinism():
    pil = pillars(identity="PASS", render_program="PASS")
    a = assemble_with(pil)
    b = assemble_with(pil)
    check(a["proof_id"] == b["proof_id"], "determinism: identical inputs → identical proof_id")
    check(a["proof_id"] == proof_id_of(a), "determinism: stamped id == recomputed id")

    # NEGATIVE: mutate one pillar verdict → id changes.
    c = assemble_with(pillars(identity="PASS", render_program="FAIL"))
    check(c["proof_id"] != a["proof_id"], "sensitivity: a pillar verdict change flips proof_id")

    # NEGATIVE: mutate a tool hash → id changes.
    subject, inputs, environment, tools = base_groups()
    tools2 = dict(tools, comparator_sha256=("9" * 64))
    d = P.assemble(subject=subject, inputs=inputs, environment=environment, tools=tools2,
                   normalization=[], observations={}, pillars=pil)
    check(d["proof_id"] != a["proof_id"], "sensitivity: a tool-hash change flips proof_id")

    # NEGATIVE: mutate a subject (state via save byte proxy) hash → id changes.
    subject2 = {**subject, "retail": {**subject["retail"], "pe_sha256": ("7" * 64)}}
    e = P.assemble(subject=subject2, inputs=inputs, environment=environment, tools=tools,
                   normalization=[], observations={}, pillars=pil)
    check(e["proof_id"] != a["proof_id"], "sensitivity: a subject-hash change flips proof_id")


# ── prove.py: envelope isolation + CAS ───────────────────────────────────────

def test_envelope_and_cas(tmp: Path):
    proof = assemble_with(pillars(identity="PASS"))
    pid = proof["proof_id"]
    secret = "/opt/src/openrecet/vendor/unpacked/recettear.unpacked.exe"

    dest, created = P.write_bundle(proof, tmp, local_paths={H: secret},
                                   generated_at="2026-07-16T00:00:00Z")
    check(created is True, "CAS: first write creates the bundle")
    check(dest == P.store_path(tmp, pid), "CAS: path is sha256/<first2>/<id>/")
    check(dest.name == pid and dest.parent.name == pid[:2], "CAS: content-addressed layout")

    written = json.loads((dest / "proof.json").read_text())
    check(written["envelope"]["local_paths"][H] == secret, "envelope: local path is written")
    check(written["proof_id"] == pid, "envelope: does NOT change proof_id")
    check(secret.encode() not in canonical_bytes(written),
          "envelope: local path is EXCLUDED from the canonical preimage (no leak)")

    # idempotent rerun.
    _, created2 = P.write_bundle(proof, tmp, local_paths={H: secret})
    check(created2 is False, "CAS: rerun with identical id is idempotent")


# ── parity_prove: resolve_observations over a synthetic window ───────────────

def write_window(tmp: Path, *, view_frames=None, px_frames=None, complete=True) -> Path:
    wd = tmp / "win"
    wd.mkdir(parents=True, exist_ok=True)
    keys = [["HOUSE_FREEROAM", 1, i] for i in range(5)]
    pairs = {"anchor": "HOUSE_FREEROAM",
             "join_verdict": "JOIN_COMPLETE" if complete else "JOIN_PARTIAL",
             "port_only": [], "retail_only": ([] if complete else [{"key": ["HOUSE_FREEROAM", 1, 2]}]),
             "pairs": [{"key": k, "port": i, "retail": i} for i, k in enumerate(keys)]}
    (wd / "pairs.json").write_text(json.dumps(pairs))
    vf = view_frames or [{"offset": i, "label": f"HOUSE_FREEROAM#1+{i}", "draw_verdict": "ALIGNED",
                          "port_tris": 5, "retail_tris": 5, "divergent": []} for i in range(5)]
    (wd / "view.json").write_text(json.dumps({"scenario": "syn", "frames": vf}))
    if px_frames is not None:
        (wd / "pixel-metrics.json").write_text(json.dumps(
            {"schema_version": 1, "pillar": "pixels", "mode": "exact", "frames": px_frames}))
    return wd


def mk_contract(required):
    return {"schema_version": 2, "proof": {
        "contract_version": 1,
        "join": {"anchor": "HOUSE_FREEROAM", "occurrence": 1, "window": [0, 4]},
        "required_pillars": required, "seeds": [19937],
        "configurations": ["reference-1024-windowed"]}}


def px(differ_at=None):
    frames = [{"key": ["HOUSE_FREEROAM", 1, i], "differ": 0, "total": 100, "meanabs": 0.0}
              for i in range(5)]
    if differ_at is not None:
        frames[differ_at]["differ"] = 9
    return frames


def test_resolve(tmp: Path):
    contract = mk_contract(["identity", "render_program", "pixels"])["proof"]

    wd = write_window(tmp / "ok", px_frames=px())
    obs, pil, _ = parity_prove.resolve_observations(wd, contract)
    check(pil["identity"]["verdict"] == "PASS", "resolve: complete join → identity PASS")
    check(pil["render_program"]["verdict"] == "PASS", "resolve: all ALIGNED → render PASS")
    check(pil["pixels"]["verdict"] == "PASS", "resolve: all differ==0 → pixels PASS")
    for name in parity_prove.UNBUILT_PILLARS:
        check(pil[name]["verdict"] == "NOT_CAPTURED", f"resolve: {name} → NOT_CAPTURED (no producer)")

    # NEGATIVE: no pixel-metrics doc → pixels NOT_CAPTURED (never a silent pass).
    wd = write_window(tmp / "nopx")
    _, pil, _ = parity_prove.resolve_observations(wd, contract)
    check(pil["pixels"]["verdict"] == "NOT_CAPTURED", "resolve: absent pixel metrics → NOT_CAPTURED")

    # NEGATIVE: one DIVERGENT draw → render FAIL.
    vf = [{"offset": i, "label": f"HOUSE_FREEROAM#1+{i}", "draw_verdict": "ALIGNED",
           "port_tris": 5, "retail_tris": 5, "divergent": []} for i in range(5)]
    vf[3] = {"offset": 3, "label": "HOUSE_FREEROAM#1+3", "draw_verdict": "DIVERGENT",
             "divergent": [{"tex": "beef", "port_tris": 1, "retail_tris": 9}]}
    wd = write_window(tmp / "divergent", view_frames=vf, px_frames=px())
    _, pil, _ = parity_prove.resolve_observations(wd, contract)
    check(pil["render_program"]["verdict"] == "FAIL", "resolve: a DIVERGENT draw → render FAIL")
    check(pil["render_program"]["first_divergence"]["logical_frame"]["offset"] == 3,
          "resolve: render FAIL localizes the divergent frame")

    # NEGATIVE: one pixel diff → pixels FAIL at that frame.
    wd = write_window(tmp / "pxdiff", px_frames=px(differ_at=2))
    _, pil, _ = parity_prove.resolve_observations(wd, contract)
    check(pil["pixels"]["verdict"] == "FAIL", "resolve: a pixel diff → pixels FAIL")
    check(pil["pixels"]["first_divergence"]["logical_frame"]["offset"] == 2,
          "resolve: pixels FAIL localizes the frame")

    # NEGATIVE: honest join gap → identity FAIL.
    wd = write_window(tmp / "gap", complete=False)
    _, pil, _ = parity_prove.resolve_observations(wd, contract)
    check(pil["identity"]["verdict"] == "FAIL", "resolve: honest join gap → identity FAIL")


# ── parity_prove: build_proof + main() end-to-end ────────────────────────────

def _fake_pe(tmp: Path, name: str, data: bytes) -> Path:
    p = tmp / name
    p.write_bytes(data)
    return p


def test_build_and_main(tmp: Path):
    scenario = "house-loaded-display-pinned"  # a real scenario dir with a trace.jsonl
    if not (ROOT / "tests/scenarios" / scenario / "trace.jsonl").exists():
        _skips.append(f"build/main end-to-end ({scenario} trace absent)")
        return
    port_pe = _fake_pe(tmp, "port.exe", b"port-pe")
    retail_pe = _fake_pe(tmp, "retail.exe", b"retail-pe")
    env = {"os_build": "Windows-10", "locale": "ja_JP", "codepage": "cp932",
           "d3d_runtime": "d3d8", "gpu": "ref", "driver": "ref",
           "resolution": "1024x768", "display_mode": "windowed"}

    wd = write_window(tmp / "bm", px_frames=px())
    contract_doc = mk_contract(["identity", "render_program", "pixels"])

    proof, contract, local, _ = parity_prove.build_proof(
        scenario, wd, contract_doc=contract_doc, env=env, port_pe=port_pe,
        retail_pe=retail_pe, retail_ref="retail-test", save_path=None,
        assets_manifest=None, recet_ini=None, normalization=[], from_cache=True)
    v, code, _ = P.gate(contract["required_pillars"], proof)
    check((v, code) == ("PASS", 0), "build_proof: synthetic all-pass window → PASS/0")

    # determinism: rebuild → identical proof_id (same git state within the run).
    proof2, *_ = parity_prove.build_proof(
        scenario, wd, contract_doc=contract_doc, env=env, port_pe=port_pe,
        retail_pe=retail_pe, retail_ref="retail-test", save_path=None,
        assets_manifest=None, recet_ini=None, normalization=[], from_cache=True)
    check(proof["proof_id"] == proof2["proof_id"], "build_proof: rerun reproduces proof_id")

    # main() end-to-end: a real bundle lands in a temp CAS with exit 0.
    import yaml
    cfile = tmp / "contract.yaml"
    cfile.write_text(yaml.safe_dump(contract_doc))
    efile = tmp / "env.json"
    efile.write_text(json.dumps(env))
    proofs_root = tmp / "proofs"
    rc = parity_prove.main([
        scenario, "--from-window", str(wd), "--contract", str(cfile),
        "--env-json", str(efile), "--port-pe", str(port_pe), "--retail-pe", str(retail_pe),
        "--retail-ref", "retail-test", "--proofs-root", str(proofs_root), "--json"])
    check(rc == 0, "main: all-pass synthetic window → exit 0")
    bundles = list(proofs_root.glob("sha256/*/*/proof.json"))
    check(len(bundles) == 1, "main: exactly one content-addressed bundle written")

    # main() FAIL path: a required pixels pillar with no producer → exit 2 (fail closed).
    wd2 = write_window(tmp / "bm2")  # no pixel-metrics
    rc = parity_prove.main([
        scenario, "--from-window", str(wd2), "--contract", str(cfile),
        "--env-json", str(efile), "--port-pe", str(port_pe), "--retail-pe", str(retail_pe),
        "--retail-ref", "retail-test", "--proofs-root", str(proofs_root)])
    check(rc == 2, "main: required pixels NOT_CAPTURED → exit 2 (fail closed)")

    # main() missing env → exit 2.
    rc = parity_prove.main([
        scenario, "--from-window", str(wd), "--contract", str(cfile),
        "--env-json", str(tmp / "nope.json"), "--proofs-root", str(proofs_root)])
    check(rc == 2, "main: absent --env-json → exit 2")


def test_proof_id_portable(tmp: Path):
    """EP-02 acceptance ("same inputs → same ID from different absolute directories")
    + the 2026-07-16 R3-review regression: the SAME window at a DIFFERENT absolute
    dir must yield the SAME proof_id, and no machine-local path may appear in the
    hashed core. Guards the fixed leak — a NOT_CAPTURED-by-absence pillar used to
    bake its absolute probe path into observations.<p>.note / pillars.<p>.detail,
    so an identical logical run hashed differently per checkout dir."""
    scenario = "house-loaded-display-pinned"
    if not (ROOT / "tests/scenarios" / scenario / "trace.jsonl").exists():
        _skips.append("proof_id portability (scenario trace absent)")
        return
    port_pe = _fake_pe(tmp, "port.exe", b"port-pe")
    retail_pe = _fake_pe(tmp, "retail.exe", b"retail-pe")
    env = {"os_build": "Windows-10", "locale": "ja_JP", "codepage": "cp932",
           "d3d_runtime": "d3d8", "gpu": "ref", "driver": "ref",
           "resolution": "1024x768", "display_mode": "windowed"}
    contract_doc = mk_contract(["identity", "render_program", "pixels"])  # no pixel doc ⇒ NOT_CAPTURED
    ids = []
    for sub in ("aaaaaaaaaa/deep/nested", "b"):  # deliberately different-length abs paths
        wd = write_window(tmp / sub)  # px_frames omitted → pixel-metrics absent → the old leak path
        proof, *_ = parity_prove.build_proof(
            scenario, wd, contract_doc=contract_doc, env=env, port_pe=port_pe,
            retail_pe=retail_pe, retail_ref="retail-test", save_path=None,
            assets_manifest=None, recet_ini=None, normalization=[], from_cache=True)
        ids.append(proof["proof_id"])
        core = canonical_bytes(proof).decode()
        check(str(wd) not in core, f"portable: window abs path absent from hashed core ({sub})")
    check(ids[0] == ids[1],
          "portable: identical window at different abs dirs → identical proof_id")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for sub in ("cas", "res", "bm_root", "port"):
            (tmp / sub).mkdir()
        test_gate()
        test_determinism()
        test_envelope_and_cas(tmp / "cas")
        test_resolve(tmp / "res")
        test_build_and_main(tmp / "bm_root")
        test_proof_id_portable(tmp / "port")

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
