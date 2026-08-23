#!/usr/bin/env python3
"""tools/atlas/model.py — Data models for the Behavior Atlas (BA-00, BA-01).

Implements pure Python representations of Behavior Nodes, Behavior Edges,
Completion Conditions, Normalization Policies, Action Grammars, and Behavior Graphs
matching docs/schemas/behavior-atlas-v1.json and docs/reference/behavior-atlas.md.
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Set, Tuple


@dataclass
class Node:
    """A certified game execution state node in the behavior atlas."""
    node_id: str
    anchor: str
    occurrence: int = 1
    persistent_state_root: Optional[str] = None
    volatile_state_root: Optional[str] = None
    rng_state: Optional[int] = None
    config_id: str = "reference-1024-windowed"
    retail_build_sha256: str = ""
    tags: List[str] = field(default_factory=list)
    description: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "node_id": self.node_id,
            "anchor": self.anchor,
            "occurrence": self.occurrence,
            "persistent_state_root": self.persistent_state_root,
            "volatile_state_root": self.volatile_state_root,
            "rng_state": self.rng_state,
            "config_id": self.config_id,
            "retail_build_sha256": self.retail_build_sha256,
            "tags": list(self.tags),
            "description": self.description,
            "metadata": dict(self.metadata),
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> Node:
        return cls(
            node_id=data["node_id"],
            anchor=data["anchor"],
            occurrence=data.get("occurrence", 1),
            persistent_state_root=data.get("persistent_state_root"),
            volatile_state_root=data.get("volatile_state_root"),
            rng_state=data.get("rng_state"),
            config_id=data.get("config_id", "reference-1024-windowed"),
            retail_build_sha256=data.get("retail_build_sha256", ""),
            tags=list(data.get("tags", [])),
            description=data.get("description", ""),
            metadata=dict(data.get("metadata", {})),
        )


@dataclass
class CompletionCondition:
    """The predicate governing when a behavior edge completes."""
    kind: str  # anchor_reached, frame_count, state_predicate, dialogue_advance, scene_change, save_committed
    anchor: Optional[str] = None
    occurrence: Optional[int] = None
    count: Optional[int] = None
    predicate: Optional[str] = None
    timeout_frames: Optional[int] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        res: Dict[str, Any] = {"kind": self.kind}
        if self.anchor is not None:
            res["anchor"] = self.anchor
        if self.occurrence is not None:
            res["occurrence"] = self.occurrence
        if self.count is not None:
            res["count"] = self.count
        if self.predicate is not None:
            res["predicate"] = self.predicate
        if self.timeout_frames is not None:
            res["timeout_frames"] = self.timeout_frames
        res.update(self.extra)
        return res

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> CompletionCondition:
        known = {"kind", "anchor", "occurrence", "count", "predicate", "timeout_frames"}
        extra = {k: v for k, v in data.items() if k not in known}
        return cls(
            kind=data["kind"],
            anchor=data.get("anchor"),
            occurrence=data.get("occurrence"),
            count=data.get("count"),
            predicate=data.get("predicate"),
            timeout_frames=data.get("timeout_frames"),
            extra=extra,
        )


@dataclass
class NormalizationPolicy:
    """Normalization pins required for deterministic edge traversal."""
    phasepin: Optional[int] = None
    rngseed: Optional[int] = None
    playtimepin: Optional[int] = None
    csloadpin: Optional[int] = None
    tutloadpin: Optional[int] = None
    bgnpcpin: Optional[int] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        res: Dict[str, Any] = {}
        if self.phasepin is not None:
            res["phasepin"] = self.phasepin
        if self.rngseed is not None:
            res["rngseed"] = self.rngseed
        if self.playtimepin is not None:
            res["playtimepin"] = self.playtimepin
        if self.csloadpin is not None:
            res["csloadpin"] = self.csloadpin
        if self.tutloadpin is not None:
            res["tutloadpin"] = self.tutloadpin
        if self.bgnpcpin is not None:
            res["bgnpcpin"] = self.bgnpcpin
        res.update(self.extra)
        return res

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> NormalizationPolicy:
        known = {"phasepin", "rngseed", "playtimepin", "csloadpin", "tutloadpin", "bgnpcpin"}
        extra = {k: v for k, v in data.items() if k not in known}
        return cls(
            phasepin=data.get("phasepin"),
            rngseed=data.get("rngseed"),
            playtimepin=data.get("playtimepin"),
            csloadpin=data.get("csloadpin"),
            tutloadpin=data.get("tutloadpin"),
            bgnpcpin=data.get("bgnpcpin"),
            extra=extra,
        )


@dataclass
class Edge:
    """A directed state transition edge in the behavior atlas."""
    edge_id: str
    src_node_id: str
    dst_node_id: Optional[str] = None
    label: str = ""
    input_digest: str = ""
    duration_frames: int = 0
    completion_condition: CompletionCondition = field(default_factory=lambda: CompletionCondition(kind="frame_count", count=1))
    normalization_policy: NormalizationPolicy = field(default_factory=NormalizationPolicy)
    action_tags: List[str] = field(default_factory=list)
    scenario_ref: Optional[str] = None
    proof_id: Optional[str] = None
    coverage_delta: Dict[str, Any] = field(default_factory=dict)
    status: str = "untested"  # untested, port_verified, retail_verified, parity_proven, divergent, needs_r3_boundary
    metadata: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "edge_id": self.edge_id,
            "src_node_id": self.src_node_id,
            "dst_node_id": self.dst_node_id,
            "label": self.label,
            "input_digest": self.input_digest,
            "duration_frames": self.duration_frames,
            "completion_condition": self.completion_condition.to_dict(),
            "normalization_policy": self.normalization_policy.to_dict(),
            "action_tags": list(self.action_tags),
            "scenario_ref": self.scenario_ref,
            "proof_id": self.proof_id,
            "coverage_delta": dict(self.coverage_delta),
            "status": self.status,
            "metadata": dict(self.metadata),
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> Edge:
        return cls(
            edge_id=data["edge_id"],
            src_node_id=data["src_node_id"],
            dst_node_id=data.get("dst_node_id"),
            label=data.get("label", ""),
            input_digest=data.get("input_digest", ""),
            duration_frames=data.get("duration_frames", 0),
            completion_condition=CompletionCondition.from_dict(data["completion_condition"]),
            normalization_policy=NormalizationPolicy.from_dict(data.get("normalization_policy", {})),
            action_tags=list(data.get("action_tags", [])),
            scenario_ref=data.get("scenario_ref"),
            proof_id=data.get("proof_id"),
            coverage_delta=dict(data.get("coverage_delta", {})),
            status=data.get("status", "untested"),
            metadata=dict(data.get("metadata", {})),
        )


@dataclass
class ActionGrammar:
    """Action grammar definition for a scene domain."""
    scene: str
    description: str = ""
    preconditions: List[str] = field(default_factory=list)
    actions: Dict[str, Dict[str, Any]] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "scene": self.scene,
            "description": self.description,
            "preconditions": list(self.preconditions),
            "actions": dict(self.actions),
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> ActionGrammar:
        return cls(
            scene=data["scene"],
            description=data.get("description", ""),
            preconditions=list(data.get("preconditions", [])),
            actions=dict(data.get("actions", {})),
        )


@dataclass
class TraversalStep:
    """One evaluated step along an atlas traversal path."""
    step_index: int
    edge_id: str
    src_node_id: str
    dst_node_id: Optional[str]
    status: str
    proof_id: Optional[str] = None
    duration_frames: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TraversalPath:
    """An ordered path of edges through the behavior atlas."""
    path_id: str
    start_node_id: str
    end_node_id: Optional[str]
    steps: List[TraversalStep] = field(default_factory=list)
    total_frames: int = 0
    certified: bool = False
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class BehaviorGraph:
    """In-memory representation of the full behavior atlas graph."""
    schema_version: int = 1
    metadata: Dict[str, Any] = field(default_factory=dict)
    nodes: Dict[str, Node] = field(default_factory=dict)
    edges: Dict[str, Edge] = field(default_factory=dict)
    entry_nodes: List[str] = field(default_factory=list)
    action_grammars: Dict[str, ActionGrammar] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "metadata": {
                **self.metadata,
                "total_nodes": len(self.nodes),
                "total_edges": len(self.edges),
            },
            "nodes": {nid: n.to_dict() for nid, n in self.nodes.items()},
            "edges": {eid: e.to_dict() for eid, e in self.edges.items()},
            "entry_nodes": list(self.entry_nodes),
            "action_grammars": {s: g.to_dict() for s, g in self.action_grammars.items()},
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> BehaviorGraph:
        nodes = {nid: Node.from_dict(nd) for nid, nd in data.get("nodes", {}).items()}
        edges = {eid: Edge.from_dict(ed) for eid, ed in data.get("edges", {}).items()}
        grammars = {s: ActionGrammar.from_dict(gd) for s, gd in data.get("action_grammars", {}).items()}
        return cls(
            schema_version=data.get("schema_version", 1),
            metadata=dict(data.get("metadata", {})),
            nodes=nodes,
            edges=edges,
            entry_nodes=list(data.get("entry_nodes", [])),
            action_grammars=grammars,
        )
