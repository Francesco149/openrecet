#!/usr/bin/env python3
"""tools/atlas/importer.py — Scenario Corpus Importer for Behavior Atlas (BA-02).

Imports existing OpenRecet test scenarios (tests/scenarios/*) into Behavior Atlas
Nodes and Edges. Maps start/end anchors, input streams, save configurations,
normalization policies, and EP-05 proof contracts into the content-addressed graph.
"""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

try:
    import yaml
except ImportError:
    yaml = None  # type: ignore

from .identity import compute_edge_id, compute_input_digest, compute_node_id
from .model import CompletionCondition, Edge, Node, NormalizationPolicy
from .store import AtlasStore

SCENARIOS_DIR = Path(__file__).resolve().parent.parent.parent / "tests" / "scenarios"


def _parse_yaml_or_json(path: Path) -> Dict[str, Any]:
    """Parse a YAML or JSON file."""
    if not path.exists():
        return {}
    content = path.read_text(encoding="utf-8")
    if yaml is not None:
        try:
            return yaml.safe_load(content) or {}
        except Exception:
            pass
    # Fallback to JSON or basic key-value extraction
    try:
        return json.loads(content)
    except Exception:
        pass
    # Simple fallback parser for basic YAML key-values
    res: Dict[str, Any] = {}
    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if ":" in line:
            k, _, v = line.partition(":")
            k = k.strip()
            v = v.strip()
            if v.isdigit():
                res[k] = int(v)
            elif v.startswith('"') and v.endswith('"'):
                res[k] = v[1:-1]
            elif v.startswith("'") and v.endswith("'"):
                res[k] = v[1:-1]
            elif v:
                res[k] = v
    return res


def parse_trace_events(trace_path: Path) -> Tuple[List[Dict[str, Any]], Dict[str, Any], List[str]]:
    """Parse trace.jsonl for input events, normalization pins, and semantic anchor hits."""
    if not trace_path.exists():
        return [], {}, []

    inputs: List[Dict[str, Any]] = []
    pins: Dict[str, Any] = {}
    anchors_observed: List[str] = []

    with open(trace_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except Exception:
                continue

            ev_type = ev.get("type") or ev.get("event")
            if ev_type in ("input", "input_state", "buttons", "press", "hold"):
                inputs.append(ev)
            elif ev_type in ("pin", "normalization_pin", "phasepin", "rngseed"):
                if "phasepin" in ev:
                    pins["phasepin"] = int(ev["phasepin"])
                if "rngseed" in ev:
                    pins["rngseed"] = int(ev["rngseed"])
                if "playtimepin" in ev:
                    pins["playtimepin"] = int(ev["playtimepin"])
                if "tutloadpin" in ev:
                    pins["tutloadpin"] = int(ev["tutloadpin"])
                if "csloadpin" in ev:
                    pins["csloadpin"] = int(ev["csloadpin"])
                if "bgnpcpin" in ev:
                    pins["bgnpcpin"] = int(ev["bgnpcpin"])
            elif ev_type in ("anchor", "semantic_anchor", "anchor_hit"):
                anchor_name = ev.get("name") or ev.get("anchor")
                if anchor_name:
                    anchors_observed.append(anchor_name)

    return inputs, pins, anchors_observed


class ScenarioImporter:
    """Imports scenario directories into Behavior Atlas Nodes and Edges."""

    def __init__(self, store: Optional[AtlasStore] = None):
        self.store = store or AtlasStore()

    def import_scenario(self, scenario_dir: Path) -> Optional[Tuple[Node, Edge, Optional[Node]]]:
        """Import a single scenario directory."""
        scenario_dir = Path(scenario_dir)
        if not scenario_dir.is_dir():
            return None

        scenario_name = scenario_dir.name
        yaml_path = scenario_dir / "scenario.yaml"
        trace_path = scenario_dir / "trace.jsonl"

        spec = _parse_yaml_or_json(yaml_path) if yaml_path.exists() else {}
        inputs, trace_pins, trace_anchors = parse_trace_events(trace_path)

        # Determine start anchor and destination anchor from proof spec, trace anchors, transitions, or scenario naming conventions
        proof_spec = spec.get("proof") or {}
        join_spec = proof_spec.get("join") or {}
        cov_expectations = spec.get("coverage_expectations") or {}
        transitions = cov_expectations.get("transitions") or []
        
        start_anchor = spec.get("start_anchor")
        if not start_anchor and proof_spec.get("start_node"):
            start_anchor = proof_spec["start_node"].get("anchor")

        dest_anchor = None
        if join_spec.get("anchor"):
            dest_anchor = join_spec["anchor"]
        elif trace_anchors:
            dest_anchor = trace_anchors[-1]
        elif transitions:
            dest_anchor = transitions[-1]

        if not start_anchor:
            if trace_anchors and len(trace_anchors) >= 2:
                start_anchor = trace_anchors[0]
            elif transitions and len(transitions) >= 2:
                start_anchor = transitions[0]
            elif scenario_name.startswith("house-pause-"):
                start_anchor = "HOUSE_FREEROAM"
                if not dest_anchor:
                    dest_anchor = "PAUSE_OPEN"
            elif scenario_name.startswith("house-"):
                start_anchor = "HOUSE_FREEROAM"
            elif scenario_name.startswith("title-"):
                start_anchor = "TITLE_MENU"
                if not dest_anchor:
                    dest_anchor = f"{scenario_name.split('-')[1].upper()}_READY"
            elif scenario_name.startswith("intro-"):
                start_anchor = "BOOT"
            else:
                start_anchor = "BOOT"

        if not dest_anchor:
            dest_anchor = start_anchor

        # Normalization policy from trace and spec
        norm_dict: Dict[str, Any] = dict(trace_pins)
        if "rng_seed" in spec:
            norm_dict.setdefault("rngseed", spec["rng_seed"])
        norm_policy = NormalizationPolicy.from_dict(norm_dict)

        # Build start node
        config_id = "reference-1024-windowed"
        if proof_spec.get("configurations"):
            config_id = proof_spec["configurations"][0]

        start_node_id = compute_node_id(
            anchor=start_anchor,
            occurrence=1,
            config_id=config_id,
        )

        start_node = Node(
            node_id=start_node_id,
            anchor=start_anchor,
            occurrence=1,
            config_id=config_id,
            tags=[f"scene:{scenario_name.split('-')[0]}"],
            description=f"Initial state for {scenario_name}",
        )
        self.store.insert_node(start_node)

        if start_anchor in ("BOOT", "TITLE_BOOT"):
            self.store.register_entry_node(start_node_id, label=start_anchor)

        # Determine completion condition
        if join_spec.get("anchor"):
            occ = join_spec.get("occurrence", 1)
            comp_cond = CompletionCondition(kind="anchor_reached", anchor=dest_anchor, occurrence=occ)
        elif dest_anchor and dest_anchor != start_anchor:
            comp_cond = CompletionCondition(kind="anchor_reached", anchor=dest_anchor, occurrence=1)
        elif spec.get("max_frames"):
            comp_cond = CompletionCondition(kind="frame_count", count=int(spec["max_frames"]))
        elif inputs:
            comp_cond = CompletionCondition(kind="frame_count", count=len(inputs))
        else:
            comp_cond = CompletionCondition(kind="frame_count", count=1)

        # Build destination node if destination anchor is known
        dest_node = None
        dest_node_id = None
        if dest_anchor:
            dest_node_id = compute_node_id(
                anchor=dest_anchor,
                occurrence=join_spec.get("occurrence", 1),
                config_id=config_id,
            )
            dest_node = Node(
                node_id=dest_node_id,
                anchor=dest_anchor,
                occurrence=join_spec.get("occurrence", 1),
                config_id=config_id,
                tags=[f"scene:{dest_anchor.lower()}"],
                description=f"Destination state reached by {scenario_name}",
            )
            self.store.insert_node(dest_node)
        # Compute input digest
        input_digest = compute_input_digest(inputs if inputs else f"scenario:{scenario_name}")
        duration = spec.get("max_frames", len(inputs))

        # Edge identity and construction
        edge_id = compute_edge_id(
            src_node_id=start_node_id,
            input_digest=input_digest,
            completion_condition=comp_cond.to_dict(),
            normalization_policy=norm_policy.to_dict(),
        )

        status = "port_verified"
        proof_id = None
        if proof_spec:
            status = "parity_proven" if proof_spec.get("required_pillars") else "port_verified"

        coverage_expectations = spec.get("coverage_expectations") or {}
        cov_delta = {
            "functions": coverage_expectations.get("functions", []),
            "blocks": coverage_expectations.get("blocks", []),
            "transitions": coverage_expectations.get("transitions", []),
            "opcodes": coverage_expectations.get("vm_operations", []),
        }

        edge = Edge(
            edge_id=edge_id,
            src_node_id=start_node_id,
            dst_node_id=dest_node_id,
            label=scenario_name,
            input_digest=input_digest,
            duration_frames=duration,
            completion_condition=comp_cond,
            normalization_policy=norm_policy,
            action_tags=[scenario_name],
            scenario_ref=scenario_name,
            proof_id=proof_id,
            coverage_delta=cov_delta,
            status=status,
            metadata={"description": spec.get("description", "")},
        )
        self.store.insert_edge(edge)

        return start_node, edge, dest_node

    def import_all_scenarios(self, scenarios_dir: Path = SCENARIOS_DIR) -> Dict[str, Any]:
        """Scan and import all active scenario directories into the Behavior Atlas."""
        scenarios_dir = Path(scenarios_dir)
        imported_scenarios = 0
        total_scenarios = 0
        errors: List[Dict[str, str]] = []

        if not scenarios_dir.exists():
            return {
                "scenarios_dir": str(scenarios_dir),
                "total_scenarios": 0,
                "imported_scenarios": 0,
                "errors": [{"error": "Scenarios directory does not exist"}],
            }

        for p in sorted(scenarios_dir.iterdir()):
            if p.is_dir() and not p.name.startswith("."):
                total_scenarios += 1
                try:
                    res = self.import_scenario(p)
                    if res is not None:
                        imported_scenarios += 1
                except Exception as exc:
                    errors.append({"scenario": p.name, "error": str(exc)})

        return {
            "scenarios_dir": str(scenarios_dir),
            "total_scenarios": total_scenarios,
            "imported_scenarios": imported_scenarios,
            "errors": errors,
            "summary": self.store.summary(),
        }
