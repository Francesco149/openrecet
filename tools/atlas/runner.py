#!/usr/bin/env python3
"""tools/atlas/runner.py — Behavior Atlas Traversal Runner (BA-03).

Executes, verifies, and validates paths of edges across the Behavior Atlas graph.
Provides graph traversal algorithms (shortest path, reachable subgraphs, cycle detection),
step-by-step edge execution, completion contract validation, and proof integration.
"""
from __future__ import annotations

import collections
import json
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

from .identity import compute_path_id
from .model import (
    BehaviorGraph,
    CompletionCondition,
    Edge,
    Node,
    NormalizationPolicy,
    TraversalPath,
    TraversalStep,
)
from .store import AtlasStore


class TraversalError(Exception):
    """Failure during atlas traversal execution."""


class AtlasRunner:
    """Graph traversal and edge execution engine for the Behavior Atlas."""

    def __init__(self, store: Optional[AtlasStore] = None):
        self.store = store or AtlasStore()

    # ── Graph Algorithms & Path Finding ──────────────────────────────────────

    def find_path(self, start_node_id: str, end_node_id: str) -> Optional[List[Edge]]:
        """Find the shortest sequence of edges connecting start_node_id to end_node_id via BFS."""
        if start_node_id == end_node_id:
            return []

        start_node = self.store.get_node(start_node_id)
        end_node = self.store.get_node(end_node_id)
        if not start_node or not end_node:
            return None

        queue: collections.deque[Tuple[str, List[Edge]]] = collections.deque([(start_node_id, [])])
        visited: Set[str] = {start_node_id}

        while queue:
            curr_nid, path = queue.popleft()
            if curr_nid == end_node_id:
                return path

            for edge in self.store.get_outgoing_edges(curr_nid):
                dst = edge.dst_node_id
                if dst and dst not in visited:
                    visited.add(dst)
                    queue.append((dst, path + [edge]))

        return None

    def find_all_reachable_nodes(self, start_node_id: str) -> Set[str]:
        """Find all node IDs reachable from a given start node."""
        reachable: Set[str] = set()
        queue: collections.deque[str] = collections.deque([start_node_id])

        while queue:
            curr = queue.popleft()
            if curr in reachable:
                continue
            reachable.add(curr)
            for edge in self.store.get_outgoing_edges(curr):
                if edge.dst_node_id and edge.dst_node_id not in reachable:
                    queue.append(edge.dst_node_id)

        return reachable

    def detect_cycles(self, start_node_id: Optional[str] = None) -> List[List[str]]:
        """Detect all cycles reachable from start_node_id (or whole graph if None)."""
        nodes_to_check = [start_node_id] if start_node_id else [n.node_id for n in self.store.list_nodes()]
        cycles: List[List[str]] = []
        visited_global: Set[str] = set()

        for root in nodes_to_check:
            if not root or root in visited_global:
                continue

            stack: List[str] = []
            visited_local: Set[str] = set()

            def dfs(u: str) -> None:
                visited_local.add(u)
                visited_global.add(u)
                stack.append(u)

                for edge in self.store.get_outgoing_edges(u):
                    v = edge.dst_node_id
                    if not v:
                        continue
                    if v in stack:
                        # Cycle found!
                        cycle_idx = stack.index(v)
                        cycles.append(list(stack[cycle_idx:] + [v]))
                    elif v not in visited_local:
                        dfs(v)

                stack.pop()

            dfs(root)

        return cycles

    # ── Traversal Execution ──────────────────────────────────────────────────

    def execute_step(
        self,
        edge: Edge,
        step_index: int,
        target: str = "openrecet",
        mock_driver: Optional[Callable[[Edge, str], Dict[str, Any]]] = None,
    ) -> TraversalStep:
        """Execute or evaluate a single edge transition step."""
        src_node = self.store.get_node(edge.src_node_id)
        if not src_node:
            return TraversalStep(
                step_index=step_index,
                edge_id=edge.edge_id,
                src_node_id=edge.src_node_id,
                dst_node_id=edge.dst_node_id,
                status="missing_src_node",
                duration_frames=0,
            )

        if mock_driver:
            driver_result = mock_driver(edge, target)
            status = driver_result.get("status", "port_verified")
            proof_id = driver_result.get("proof_id", edge.proof_id)
            duration = driver_result.get("duration_frames", edge.duration_frames)
            metadata = driver_result.get("metadata", {})
        else:
            status = edge.status
            proof_id = edge.proof_id
            duration = edge.duration_frames
            metadata = {}

        return TraversalStep(
            step_index=step_index,
            edge_id=edge.edge_id,
            src_node_id=edge.src_node_id,
            dst_node_id=edge.dst_node_id,
            status=status,
            proof_id=proof_id,
            duration_frames=duration,
            metadata=metadata,
        )

    def run_traversal(
        self,
        edge_ids: List[str],
        start_node_id: Optional[str] = None,
        target: str = "openrecet",
        mock_driver: Optional[Callable[[Edge, str], Dict[str, Any]]] = None,
    ) -> TraversalPath:
        """Execute an ordered sequence of edges as a connected traversal path."""
        if not edge_ids:
            raise TraversalError("Cannot run empty traversal edge list")

        edges: List[Edge] = []
        for eid in edge_ids:
            e = self.store.get_edge(eid)
            if not e:
                raise TraversalError(f"Edge {eid} not found in atlas store")
            edges.append(e)

        actual_start_node_id = start_node_id or edges[0].src_node_id
        path_id = compute_path_id(actual_start_node_id, edge_ids)

        steps: List[TraversalStep] = []
        curr_node_id = actual_start_node_id
        total_frames = 0
        all_passed = True

        for idx, edge in enumerate(edges):
            # Check topology continuity
            if edge.src_node_id != curr_node_id:
                # Discontinuous traversal
                steps.append(
                    TraversalStep(
                        step_index=idx,
                        edge_id=edge.edge_id,
                        src_node_id=edge.src_node_id,
                        dst_node_id=edge.dst_node_id,
                        status="topology_mismatch",
                        duration_frames=0,
                        metadata={"expected_src": curr_node_id, "actual_src": edge.src_node_id},
                    )
                )
                all_passed = False
                break

            step = self.execute_step(edge, idx, target=target, mock_driver=mock_driver)
            steps.append(step)
            total_frames += step.duration_frames

            if step.status not in ("port_verified", "retail_verified", "parity_proven"):
                all_passed = False
                break

            curr_node_id = edge.dst_node_id or curr_node_id

        return TraversalPath(
            path_id=path_id,
            start_node_id=actual_start_node_id,
            end_node_id=curr_node_id,
            steps=steps,
            total_frames=total_frames,
            certified=all_passed and len(steps) == len(edges),
            metadata={"target": target, "completed_steps": len(steps), "total_steps": len(edges)},
        )
