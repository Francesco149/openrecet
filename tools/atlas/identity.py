#!/usr/bin/env python3
"""tools/atlas/identity.py — Content-addressed identity computation for Behavior Atlas (BA-00, BA-01).

Implements deterministic preimage generation and SHA-256 hashing for Behavior Nodes,
Behavior Edges, input sequences, and traversal paths per docs/reference/behavior-atlas.md.
"""
from __future__ import annotations

import hashlib
import json
from typing import Any, Dict, List, Optional, Sequence, Union

try:
    from tools.parity.canonical import canonical_bytes
except ImportError:
    def canonical_bytes(obj: dict) -> bytes:
        """Fallback canonical serializer."""
        non_hashed = ("proof_id", "envelope", "human_review")
        core = {k: v for k, v in obj.items() if k not in non_hashed}
        return json.dumps(
            core, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    """Compute 64-hex SHA-256 digest of bytes."""
    return hashlib.sha256(data).hexdigest()


def compute_node_id(
    anchor: str,
    occurrence: int = 1,
    persistent_state_root: Optional[str] = None,
    volatile_state_root: Optional[str] = None,
    rng_state: Optional[int] = None,
    config_id: str = "reference-1024-windowed",
    retail_build_sha256: str = "",
) -> str:
    """Compute the deterministic content-addressed node_id for a Behavior Node.

    node_id = SHA256(canonical({
        "anchor": anchor,
        "occurrence": occurrence,
        "persistent_state_root": persistent_state_root,
        "volatile_state_root": volatile_state_root,
        "rng_state": rng_state,
        "config_id": config_id,
        "retail_build_sha256": retail_build_sha256
    }))
    """
    preimage = {
        "anchor": str(anchor).strip(),
        "occurrence": int(occurrence),
        "persistent_state_root": str(persistent_state_root).lower() if persistent_state_root else None,
        "volatile_state_root": str(volatile_state_root).lower() if volatile_state_root else None,
        "rng_state": int(rng_state) if rng_state is not None else None,
        "config_id": str(config_id).strip(),
        "retail_build_sha256": str(retail_build_sha256).lower().strip(),
    }
    return sha256_bytes(canonical_bytes(preimage))


def compute_input_digest(input_sequence: Union[List[Dict[str, Any]], str, bytes]) -> str:
    """Compute deterministic SHA-256 digest of an input segment or TAS sequence."""
    if isinstance(input_sequence, bytes):
        return sha256_bytes(input_sequence)
    if isinstance(input_sequence, str):
        return sha256_bytes(input_sequence.encode("utf-8"))
    if isinstance(input_sequence, list):
        # Canonicalize list of input events
        norm_events = []
        for ev in input_sequence:
            if isinstance(ev, dict):
                norm_ev = {
                    "frame": int(ev.get("frame", 0)),
                    "buttons": sorted(ev.get("buttons", [])) if isinstance(ev.get("buttons"), list) else ev.get("buttons", ""),
                }
                if "analog" in ev:
                    norm_ev["analog"] = ev["analog"]
                if "mask" in ev:
                    norm_ev["mask"] = int(ev["mask"])
                norm_events.append(norm_ev)
            else:
                norm_events.append(ev)
        raw = json.dumps(norm_events, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return sha256_bytes(raw)
    raise ValueError(f"Unsupported input sequence type: {type(input_sequence)}")


def compute_edge_id(
    src_node_id: str,
    input_digest: str,
    completion_condition: Dict[str, Any],
    normalization_policy: Optional[Dict[str, Any]] = None,
) -> str:
    """Compute the deterministic content-addressed edge_id for a Behavior Edge.

    edge_id = SHA256(canonical({
        "src_node_id": src_node_id,
        "input_digest": input_digest,
        "completion_condition": completion_condition,
        "normalization_policy": normalization_policy
    }))
    """
    preimage = {
        "src_node_id": str(src_node_id).lower().strip(),
        "input_digest": str(input_digest).lower().strip(),
        "completion_condition": dict(completion_condition),
        "normalization_policy": dict(normalization_policy or {}),
    }
    return sha256_bytes(canonical_bytes(preimage))


def compute_path_id(start_node_id: str, edge_ids: Sequence[str]) -> str:
    """Compute deterministic identifier for a multi-edge traversal path."""
    preimage = {
        "start_node_id": str(start_node_id).lower().strip(),
        "edge_ids": [str(e).lower().strip() for e in edge_ids],
    }
    return sha256_bytes(canonical_bytes(preimage))
