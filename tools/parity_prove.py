#!/usr/bin/env python3
"""tools/parity_prove.py — EP-05 the parity proof compiler (CLI).

One command that turns a scenario's declared parity contract + a captured
Trace Studio v3 window into a content-addressed proof bundle:

    nix develop --command python3 tools/parity_prove.py <scenario> \
        --from-window runs/studio-v3-windows/<scenario>/win-<off>-<count> \
        --env-json <host-environment.json> --json

It gathers real provenance (EP-02 fingerprinting), resolves each contract pillar
through the EP-04 observation adapters (identity from pairs.json, render_program
from view.json, pixels from an optional metrics doc; pillars with no producer yet
are NOT_CAPTURED), assembles + gates + content-addresses the bundle (EP-05
tools/parity/prove.py), and exits per §4.1:

    0  every required pillar PASS
    1  a required pillar FAIL
    2  invalid input / a required pillar NOT_CAPTURED|INCONCLUSIVE

FAIL CLOSED throughout: a missing capture window, an unresolvable fingerprint, or
an absent environment is an error (exit 2), never a silent pass. The bundle lands
under runs/proofs/sha256/<first2>/<proof_id>/proof.json; the machine-local paths
live only in the non-hashed envelope, so the proof_id is portable and a rerun with
identical inputs reproduces it byte-for-byte.

The heavy retail drive is NOT done here — capture the window first with
`orv3_window … --state --view`, then prove it. `--drive` (thin shell-out) is a
convenience that runs orv3_window then resolves the window it wrote.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import (  # noqa: E402
    EnvValidationError,
    FingerprintError,
    ObservationError,
    adapt_identity,
    adapt_pixels,
    adapt_render_program,
    collect_environment,
    dir_manifest_sha256,
    load_required,
    observation,
    optional_input_fingerprint,
    pillar_result,
    port_subject,
    render_metrics_from_view_json,
    retail_subject,
    save_fingerprint,
    sha256_file,
    sha256_hex,
    tool_sha256_or_none,
)
from parity import prove as _prove  # noqa: E402

SCHEMA = ROOT / "docs/schemas/parity-proof-v1.schema.json"
CONTRACT_SCHEMA = ROOT / "docs/schemas/parity-contract-v1.schema.json"
DEFAULT_PROOFS_ROOT = ROOT / "runs/proofs"

# Pillars with no producer wired yet (later ST/AU/RT/BT packages). Declared so the
# bundle carries them explicitly as NOT_CAPTURED rather than silently omitting them.
UNBUILT_PILLARS = ("state", "save", "audio_events", "timing", "boundary")


class ProveError(Exception):
    """A fatal, fail-closed condition → exit 2 (invalid input / unresolvable)."""


# ── contract ─────────────────────────────────────────────────────────────────

def load_scenario_contract(scenario: str, *, contract_path: Path | None = None) -> dict:
    """Return the scenario dict carrying the `proof` contract block. Uses an
    explicit --contract file if given, else tests/scenarios/<scenario>/scenario.yaml."""
    import yaml

    path = contract_path or (ROOT / "tests/scenarios" / scenario / "scenario.yaml")
    if not path.exists():
        raise ProveError(f"no scenario contract at {path}")
    doc = yaml.safe_load(path.read_text()) or {}
    if "proof" not in doc:
        raise ProveError(
            f"{path} declares no `proof:` block — add a parity contract "
            f"(docs/reference/parity-proof-format.md) before proving")
    return doc


def validate_contract(scenario_doc: dict) -> None:
    try:
        import jsonschema
    except Exception:
        print("WARN: jsonschema unavailable — skipping contract schema validation", file=sys.stderr)
        return
    schema = json.loads(CONTRACT_SCHEMA.read_text())
    jsonschema.validate(scenario_doc, schema)


def contract_window(contract: dict):
    """(anchor, occurrence, off_lo, off_hi) from proof.join — the required-frame
    filter over the identity join. None if the contract declares no join window."""
    join = contract.get("join")
    if not join:
        return None
    win = join.get("window") or [None, None]
    return (join["anchor"], int(join.get("occurrence", 1)), int(win[0]), int(win[1]))


def contract_sha256(contract: dict) -> str:
    """Stable hash of the `proof` contract block (canonical JSON)."""
    return sha256_hex(json.dumps(contract, sort_keys=True, separators=(",", ":")).encode("utf-8"))


# ── observation resolution (EP-04 adapters over a v3 window) ─────────────────

def resolve_observations(window_dir: Path, contract: dict):
    """Run the EP-04 adapters over an existing v3 window dir. Returns
    (observations, pillars, local_paths). A pillar with no evidence/producer is
    NOT_CAPTURED (fail closed)."""
    pairs = window_dir / "pairs.json"
    view = window_dir / "view.json"
    if not pairs.exists():
        raise ProveError(
            f"no pairs.json in {window_dir} — capture the window first "
            f"(orv3_window … --state --view)")
    window = contract_window(contract)

    observations: dict = {}
    pillars: dict = {}
    local_paths: dict = {}

    def put(name: str, res, path: Path | None = None):
        observations[name] = res.observation
        pillars[name] = res.pillar
        if path is not None and path.exists():
            local_paths[sha256_file(path)] = str(path)

    # required frames = identity join ∩ contract window (empty if the join is absent)
    try:
        required = load_required(pairs, window)
    except ObservationError:
        required = []

    # identity (from pairs.json)
    put("identity", adapt_identity(pairs, window=window), pairs)

    # render_program — bridge the real view.json into a normalized metrics doc,
    # cache it beside the window (a reproducible derived artifact), then adjudicate.
    if view.exists():
        # scope the bridged doc to exactly the contract's in-window frames — a v3
        # window is often multi-anchor, so an unscoped bridge would carry frames the
        # adapter (rightly) rejects as foreign.
        doc = render_metrics_from_view_json(view, required=required)
        rm = window_dir / "render-metrics.json"
        rm.write_text(json.dumps(doc))
        put("render_program", adapt_render_program(rm, required), rm)
    else:
        put("render_program", _prove_not_captured("render_program", "no view.json in window"))

    # pixels — optional producer output (no headless pixel producer yet ⇒ NOT_CAPTURED)
    pm = window_dir / "pixel-metrics.json"
    put("pixels", adapt_pixels(pm, required), pm if pm.exists() else None)

    # pillars whose producer is a later package
    for name in UNBUILT_PILLARS:
        put(name, _prove_not_captured(name, "pillar producer not yet built (later package)"))

    return observations, pillars, local_paths


def _prove_not_captured(name: str, reason: str):
    from parity import AdapterResult

    return AdapterResult(
        observation(captured=False, note=reason),
        pillar_result("NOT_CAPTURED", detail=reason),
    )


# ── provenance (EP-02) ───────────────────────────────────────────────────────

def gather_provenance(scenario: str, contract: dict, *, port_pe: Path, retail_pe: Path,
                      retail_ref: str, env: dict, save_path: Path | None,
                      assets_manifest: Path | None, recet_ini: Path | None,
                      normalization: list, from_cache: bool):
    """Build the proof's subject/inputs/environment/tools/normalization groups from
    real files (EP-02). Fail closed: any unresolvable required input raises."""
    subject = {
        "port": port_subject(ROOT, port_pe),
        "retail": retail_subject(retail_pe, retail_ref),
    }
    trace = ROOT / "tests/scenarios" / scenario / "trace.jsonl"
    if not trace.exists():
        raise ProveError(f"no trace at {trace}")
    inputs = {
        "save": save_fingerprint(save_path),
        "scenario_contract": {
            "id": scenario,
            "contract_sha256": contract_sha256(contract),
            "contract_version": int(contract.get("contract_version", 1)),
        },
        "trace_sha256": sha256_file(trace),
        "assets_manifest_sha256": optional_input_fingerprint(assets_manifest),
        "recet_ini_sha256": optional_input_fingerprint(recet_ini),
    }
    # comparator = the EP-04/05 parity package; schema = the proof schema. Both
    # always hashed. The capture tools are best-effort current-on-disk versions.
    agent = ROOT / "tools/frida/openrecet-agent.js"
    proxy = ROOT / "tools/trace_studio_v3/proxy/d3d8_proxy.c"
    viewer = ROOT / "tools/trace_studio_v3/viewer/viewer.exe"
    tools = {
        "frida_agent_sha256": tool_sha256_or_none(agent if agent.exists() else None),
        "d3d_proxy_sha256": tool_sha256_or_none(proxy if proxy.exists() else None),
        "replayer_sha256": tool_sha256_or_none(viewer if viewer.exists() else None),
        "comparator_sha256": dir_manifest_sha256(ROOT / "tools/parity"),
        "schema_sha256": sha256_file(SCHEMA),
    }
    return subject, inputs, env, tools, list(normalization)


# ── orchestration ────────────────────────────────────────────────────────────

def build_proof(scenario: str, window_dir: Path, *, contract_doc: dict, env: dict,
                port_pe: Path, retail_pe: Path, retail_ref: str,
                save_path: Path | None, assets_manifest: Path | None,
                recet_ini: Path | None, normalization: list, from_cache: bool,
                caveats: list[str] | None = None):
    """Assemble (but do not yet store) the proof bundle + its local_paths envelope."""
    contract = contract_doc["proof"]
    observations, pillars, obs_local = resolve_observations(window_dir, contract)
    subject, inputs, environment, tools, norm = gather_provenance(
        scenario, contract, port_pe=port_pe, retail_pe=retail_pe, retail_ref=retail_ref,
        env=env, save_path=save_path, assets_manifest=assets_manifest,
        recet_ini=recet_ini, normalization=normalization, from_cache=from_cache)

    proof = _prove.assemble(
        subject=subject, inputs=inputs, environment=environment, tools=tools,
        normalization=norm, observations=observations, pillars=pillars,
        coverage={"captured": False}, exceptions=list(contract.get("exceptions") or []),
        human_review=None)
    return proof, contract, obs_local, caveats or []


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="EP-05 parity proof compiler")
    ap.add_argument("scenario")
    ap.add_argument("--from-window", type=Path, default=None,
                    help="an existing v3 window dir (pairs.json + view.json)")
    ap.add_argument("--window", default=None, metavar="OFF:COUNT",
                    help="locate runs/studio-v3-windows/<scenario>/win-OFF-COUNT")
    ap.add_argument("--contract", type=Path, default=None,
                    help="external contract yaml (default: the scenario.yaml proof block)")
    ap.add_argument("--env-json", type=Path, required=True,
                    help="host environment JSON (the 8 required fields) — fail closed if absent")
    ap.add_argument("--retail-pe", type=Path, default=ROOT / "vendor/unpacked/recettear.unpacked.exe")
    ap.add_argument("--retail-ref", default="recettear-unpacked")
    ap.add_argument("--port-pe", type=Path, default=ROOT / "build/openrecet.exe")
    ap.add_argument("--save", type=Path, default=None, help="resolved save file (default @fresh)")
    ap.add_argument("--assets-manifest", type=Path, default=None)
    ap.add_argument("--recet-ini", type=Path, default=None)
    ap.add_argument("--normalization-json", type=Path, default=None,
                    help="JSON array of {name, params} pins/hooks applied (default [])")
    ap.add_argument("--proofs-root", type=Path, default=DEFAULT_PROOFS_ROOT)
    ap.add_argument("--json", action="store_true", help="print the JSON summary")
    args = ap.parse_args(argv)

    try:
        # locate the window
        window_dir = args.from_window
        if window_dir is None and args.window:
            off, _, count = args.window.partition(":")
            window_dir = ROOT / "runs/studio-v3-windows" / args.scenario / f"win-{off}-{count}"
        if window_dir is None:
            raise ProveError("pass --from-window <dir> or --window OFF:COUNT")
        if not window_dir.is_dir():
            raise ProveError(f"window dir not found: {window_dir}")

        # environment (required, validated)
        if not args.env_json.exists():
            raise ProveError(f"--env-json not found: {args.env_json}")
        env = collect_environment(json.loads(args.env_json.read_text()))

        # contract
        contract_doc = load_scenario_contract(args.scenario, contract_path=args.contract)
        validate_contract(contract_doc)

        normalization = []
        if args.normalization_json:
            normalization = json.loads(args.normalization_json.read_text())

        from_cache = args.from_window is not None or args.window is not None
        caveats = []
        if from_cache:
            caveats.append(
                "capture-tool hashes (agent/proxy/replayer) are current-on-disk, "
                "not verified against the cached window (EP-08 re-keys caches by provenance)")

        proof, contract, obs_local, caveats = build_proof(
            args.scenario, window_dir, contract_doc=contract_doc, env=env,
            port_pe=args.port_pe, retail_pe=args.retail_pe, retail_ref=args.retail_ref,
            save_path=args.save, assets_manifest=args.assets_manifest,
            recet_ini=args.recet_ini, normalization=normalization,
            from_cache=from_cache, caveats=caveats)

        required = contract.get("required_pillars") or []
        local_paths = dict(obs_local)
        generated_at = datetime.now(timezone.utc).isoformat()
        bundle_dir, created = _prove.write_bundle(
            proof, args.proofs_root, local_paths=local_paths, generated_at=generated_at)

        summary = _prove.summarize(proof, required, bundle_dir=bundle_dir)
        summary["created"] = created
        if caveats:
            summary["caveats"] = caveats

    except (ProveError, FingerprintError, EnvValidationError, ObservationError) as exc:
        err = {"error": str(exc), "exit_code": 2}
        print(json.dumps(err, indent=1) if args.json else f"parity_prove: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # jsonschema.ValidationError etc.
        print(f"parity_prove: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(summary, indent=1))
    else:
        v = summary["verdict"]
        print(f"proof {summary['proof_id'][:16]}…  {v}  (exit {summary['exit_code']})")
        print(f"  bundle: {summary['bundle_dir']}  {'(new)' if created else '(idempotent)'}")
        for p, ver in summary["pillar_verdicts"].items():
            print(f"    {p:<16} {ver}")
        for fd in summary["first_divergences"]:
            lf = fd["logical_frame"]
            print(f"  first divergence [{fd['pillar']}] @ {lf['anchor']}#{lf['occurrence']}+{lf['offset']}"
                  f"  {fd.get('path', '')}")
        for c in summary.get("caveats", []):
            print(f"  caveat: {c}")
    return summary["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
