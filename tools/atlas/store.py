#!/usr/bin/env python3
"""tools/atlas/store.py — Storage engine for the Behavior Atlas (BA-01).

Manages the SQLite database and JSON graph representations of the Behavior Atlas.
Guarantees content-addressed deduplication, relocation invariance, atomic updates,
and metadata preservation without storing proprietary game assets.
"""
from __future__ import annotations

import json
import sqlite3
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

from .identity import compute_edge_id, compute_node_id
from .model import (
    ActionGrammar,
    BehaviorGraph,
    CompletionCondition,
    Edge,
    Node,
    NormalizationPolicy,
    TraversalPath,
    TraversalStep,
)

DEFAULT_ATLAS_DB = Path(__file__).resolve().parent.parent.parent / "docs" / "behavior-atlas.sqlite"
DEFAULT_ATLAS_JSON = Path(__file__).resolve().parent.parent.parent / "docs" / "behavior-atlas.json"


SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS atlas_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS atlas_nodes (
    node_id TEXT PRIMARY KEY,
    anchor TEXT NOT NULL,
    occurrence INTEGER NOT NULL DEFAULT 1,
    persistent_state_root TEXT,
    volatile_state_root TEXT,
    rng_state INTEGER,
    config_id TEXT NOT NULL,
    retail_build_sha256 TEXT NOT NULL,
    tags_json TEXT NOT NULL DEFAULT '[]',
    description TEXT NOT NULL DEFAULT '',
    metadata_json TEXT NOT NULL DEFAULT '{}',
    created_at REAL NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_nodes_anchor ON atlas_nodes(anchor);
CREATE INDEX IF NOT EXISTS idx_nodes_p_root ON atlas_nodes(persistent_state_root);
CREATE INDEX IF NOT EXISTS idx_nodes_v_root ON atlas_nodes(volatile_state_root);

CREATE TABLE IF NOT EXISTS atlas_edges (
    edge_id TEXT PRIMARY KEY,
    src_node_id TEXT NOT NULL,
    dst_node_id TEXT,
    label TEXT NOT NULL DEFAULT '',
    input_digest TEXT NOT NULL DEFAULT '',
    duration_frames INTEGER NOT NULL DEFAULT 0,
    completion_condition_json TEXT NOT NULL,
    normalization_policy_json TEXT NOT NULL DEFAULT '{}',
    action_tags_json TEXT NOT NULL DEFAULT '[]',
    scenario_ref TEXT,
    proof_id TEXT,
    coverage_delta_json TEXT NOT NULL DEFAULT '{}',
    status TEXT NOT NULL DEFAULT 'untested',
    metadata_json TEXT NOT NULL DEFAULT '{}',
    created_at REAL NOT NULL,
    FOREIGN KEY(src_node_id) REFERENCES atlas_nodes(node_id),
    FOREIGN KEY(dst_node_id) REFERENCES atlas_nodes(node_id)
);

CREATE INDEX IF NOT EXISTS idx_edges_src ON atlas_edges(src_node_id);
CREATE INDEX IF NOT EXISTS idx_edges_dst ON atlas_edges(dst_node_id);
CREATE INDEX IF NOT EXISTS idx_edges_scenario ON atlas_edges(scenario_ref);
CREATE INDEX IF NOT EXISTS idx_edges_proof ON atlas_edges(proof_id);
CREATE INDEX IF NOT EXISTS idx_edges_status ON atlas_edges(status);

CREATE TABLE IF NOT EXISTS atlas_entry_nodes (
    node_id TEXT PRIMARY KEY,
    label TEXT NOT NULL DEFAULT '',
    created_at REAL NOT NULL,
    FOREIGN KEY(node_id) REFERENCES atlas_nodes(node_id)
);

CREATE TABLE IF NOT EXISTS atlas_grammars (
    scene TEXT PRIMARY KEY,
    description TEXT NOT NULL DEFAULT '',
    preconditions_json TEXT NOT NULL DEFAULT '[]',
    actions_json TEXT NOT NULL DEFAULT '{}',
    created_at REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS atlas_paths (
    path_id TEXT PRIMARY KEY,
    start_node_id TEXT NOT NULL,
    end_node_id TEXT,
    edge_ids_json TEXT NOT NULL,
    total_frames INTEGER NOT NULL DEFAULT 0,
    certified INTEGER NOT NULL DEFAULT 0,
    metadata_json TEXT NOT NULL DEFAULT '{}',
    created_at REAL NOT NULL,
    FOREIGN KEY(start_node_id) REFERENCES atlas_nodes(node_id),
    FOREIGN KEY(end_node_id) REFERENCES atlas_nodes(node_id)
);
"""


class AtlasStore:
    """Persistent storage manager for the Behavior Atlas."""

    def __init__(self, db_path: Path = DEFAULT_ATLAS_DB):
        self.db_path = Path(db_path)
        self.init_db()

    def _get_conn(self):
        """Context manager yielding a SQLite connection and ensuring it closes on exit."""
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.db_path), timeout=30.0)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON")
        class _ConnCtx:
            def __enter__(self):
                return conn
            def __exit__(self, exc_type, exc_val, exc_tb):
                if exc_type is None:
                    conn.commit()
                else:
                    conn.rollback()
                conn.close()
        return _ConnCtx()
    def init_db(self) -> None:
        """Initialize SQLite tables and schema version."""
        with self._get_conn() as conn:
            conn.executescript(SCHEMA_SQL)
            conn.execute(
                "INSERT OR REPLACE INTO atlas_meta (key, value) VALUES ('schema_version', '1')"
            )
            conn.execute(
                "INSERT OR IGNORE INTO atlas_meta (key, value) VALUES ('created_at', ?)",
                (str(time.time()),),
            )

    # ── Node Operations ──────────────────────────────────────────────────────

    def insert_node(self, node: Node) -> str:
        """Insert or ignore a Behavior Node by node_id."""
        with self._get_conn() as conn:
            conn.execute(
                """
                INSERT INTO atlas_nodes (
                    node_id, anchor, occurrence, persistent_state_root,
                    volatile_state_root, rng_state, config_id, retail_build_sha256,
                    tags_json, description, metadata_json, created_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(node_id) DO UPDATE SET
                    description = CASE WHEN description = '' THEN excluded.description ELSE description END,
                    metadata_json = excluded.metadata_json
                """,
                (
                    node.node_id,
                    node.anchor,
                    node.occurrence,
                    node.persistent_state_root,
                    node.volatile_state_root,
                    node.rng_state,
                    node.config_id,
                    node.retail_build_sha256,
                    json.dumps(node.tags),
                    node.description,
                    json.dumps(node.metadata),
                    time.time(),
                ),
            )
        return node.node_id

    def get_node(self, node_id: str) -> Optional[Node]:
        """Fetch a Node by node_id."""
        with self._get_conn() as conn:
            row = conn.execute(
                "SELECT * FROM atlas_nodes WHERE node_id = ?", (node_id,)
            ).fetchone()
            if not row:
                return None
            return Node(
                node_id=row["node_id"],
                anchor=row["anchor"],
                occurrence=row["occurrence"],
                persistent_state_root=row["persistent_state_root"],
                volatile_state_root=row["volatile_state_root"],
                rng_state=row["rng_state"],
                config_id=row["config_id"],
                retail_build_sha256=row["retail_build_sha256"],
                tags=json.loads(row["tags_json"]),
                description=row["description"],
                metadata=json.loads(row["metadata_json"]),
            )

    def list_nodes(self, anchor: Optional[str] = None) -> List[Node]:
        """List all nodes, optionally filtered by anchor."""
        with self._get_conn() as conn:
            if anchor:
                rows = conn.execute(
                    "SELECT * FROM atlas_nodes WHERE anchor = ? ORDER BY occurrence ASC",
                    (anchor,),
                ).fetchall()
            else:
                rows = conn.execute("SELECT * FROM atlas_nodes ORDER BY anchor, occurrence").fetchall()

            return [
                Node(
                    node_id=r["node_id"],
                    anchor=r["anchor"],
                    occurrence=r["occurrence"],
                    persistent_state_root=r["persistent_state_root"],
                    volatile_state_root=r["volatile_state_root"],
                    rng_state=r["rng_state"],
                    config_id=r["config_id"],
                    retail_build_sha256=r["retail_build_sha256"],
                    tags=json.loads(r["tags_json"]),
                    description=r["description"],
                    metadata=json.loads(r["metadata_json"]),
                )
                for r in rows
            ]

    # ── Edge Operations ──────────────────────────────────────────────────────

    def insert_edge(self, edge: Edge) -> str:
        """Insert or update a Behavior Edge by edge_id."""
        with self._get_conn() as conn:
            conn.execute(
                """
                INSERT INTO atlas_edges (
                    edge_id, src_node_id, dst_node_id, label, input_digest,
                    duration_frames, completion_condition_json, normalization_policy_json,
                    action_tags_json, scenario_ref, proof_id, coverage_delta_json,
                    status, metadata_json, created_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(edge_id) DO UPDATE SET
                    dst_node_id = COALESCE(excluded.dst_node_id, atlas_edges.dst_node_id),
                    proof_id = COALESCE(excluded.proof_id, atlas_edges.proof_id),
                    status = excluded.status,
                    coverage_delta_json = excluded.coverage_delta_json,
                    metadata_json = excluded.metadata_json
                """,
                (
                    edge.edge_id,
                    edge.src_node_id,
                    edge.dst_node_id,
                    edge.label,
                    edge.input_digest,
                    edge.duration_frames,
                    json.dumps(edge.completion_condition.to_dict()),
                    json.dumps(edge.normalization_policy.to_dict()),
                    json.dumps(edge.action_tags),
                    edge.scenario_ref,
                    edge.proof_id,
                    json.dumps(edge.coverage_delta),
                    edge.status,
                    json.dumps(edge.metadata),
                    time.time(),
                ),
            )
        return edge.edge_id

    def get_edge(self, edge_id: str) -> Optional[Edge]:
        """Fetch an Edge by edge_id."""
        with self._get_conn() as conn:
            row = conn.execute(
                "SELECT * FROM atlas_edges WHERE edge_id = ?", (edge_id,)
            ).fetchone()
            if not row:
                return None
            return self._row_to_edge(row)

    def get_outgoing_edges(self, src_node_id: str) -> List[Edge]:
        """Find all outgoing edges starting from src_node_id."""
        with self._get_conn() as conn:
            rows = conn.execute(
                "SELECT * FROM atlas_edges WHERE src_node_id = ?", (src_node_id,)
            ).fetchall()
            return [self._row_to_edge(r) for r in rows]

    def get_incoming_edges(self, dst_node_id: str) -> List[Edge]:
        """Find all incoming edges terminating at dst_node_id."""
        with self._get_conn() as conn:
            rows = conn.execute(
                "SELECT * FROM atlas_edges WHERE dst_node_id = ?", (dst_node_id,)
            ).fetchall()
            return [self._row_to_edge(r) for r in rows]

    def list_edges(self, status: Optional[str] = None) -> List[Edge]:
        """List all edges, optionally filtered by status."""
        with self._get_conn() as conn:
            if status:
                rows = conn.execute(
                    "SELECT * FROM atlas_edges WHERE status = ?", (status,)
                ).fetchall()
            else:
                rows = conn.execute("SELECT * FROM atlas_edges").fetchall()
            return [self._row_to_edge(r) for r in rows]

    def _row_to_edge(self, row: sqlite3.Row) -> Edge:
        return Edge(
            edge_id=row["edge_id"],
            src_node_id=row["src_node_id"],
            dst_node_id=row["dst_node_id"],
            label=row["label"],
            input_digest=row["input_digest"],
            duration_frames=row["duration_frames"],
            completion_condition=CompletionCondition.from_dict(
                json.loads(row["completion_condition_json"])
            ),
            normalization_policy=NormalizationPolicy.from_dict(
                json.loads(row["normalization_policy_json"])
            ),
            action_tags=json.loads(row["action_tags_json"]),
            scenario_ref=row["scenario_ref"],
            proof_id=row["proof_id"],
            coverage_delta=json.loads(row["coverage_delta_json"]),
            status=row["status"],
            metadata=json.loads(row["metadata_json"]),
        )

    # ── Entry Nodes & Grammars ───────────────────────────────────────────────

    def register_entry_node(self, node_id: str, label: str = "") -> None:
        """Register a node as a root entry node."""
        with self._get_conn() as conn:
            conn.execute(
                """
                INSERT OR REPLACE INTO atlas_entry_nodes (node_id, label, created_at)
                VALUES (?, ?, ?)
                """,
                (node_id, label, time.time()),
            )

    def list_entry_nodes(self) -> List[Tuple[str, str]]:
        """List all registered entry (node_id, label) pairs."""
        with self._get_conn() as conn:
            rows = conn.execute(
                "SELECT node_id, label FROM atlas_entry_nodes ORDER BY created_at"
            ).fetchall()
            return [(r["node_id"], r["label"]) for r in rows]

    def register_grammar(self, grammar: ActionGrammar) -> None:
        """Register or update an action grammar for a scene."""
        with self._get_conn() as conn:
            conn.execute(
                """
                INSERT OR REPLACE INTO atlas_grammars (
                    scene, description, preconditions_json, actions_json, created_at
                ) VALUES (?, ?, ?, ?, ?)
                """,
                (
                    grammar.scene,
                    grammar.description,
                    json.dumps(grammar.preconditions),
                    json.dumps(grammar.actions),
                    time.time(),
                ),
            )

    def get_grammar(self, scene: str) -> Optional[ActionGrammar]:
        """Fetch action grammar for a scene."""
        with self._get_conn() as conn:
            row = conn.execute(
                "SELECT * FROM atlas_grammars WHERE scene = ?", (scene,)
            ).fetchone()
            if not row:
                return None
            return ActionGrammar(
                scene=row["scene"],
                description=row["description"],
                preconditions=json.loads(row["preconditions_json"]),
                actions=json.loads(row["actions_json"]),
            )

    def list_grammars(self) -> List[ActionGrammar]:
        """List all registered action grammars."""
        with self._get_conn() as conn:
            rows = conn.execute("SELECT * FROM atlas_grammars ORDER BY scene").fetchall()
            return [
                ActionGrammar(
                    scene=r["scene"],
                    description=r["description"],
                    preconditions=json.loads(r["preconditions_json"]),
                    actions=json.loads(r["actions_json"]),
                )
                for r in rows
            ]

    # ── Graph Serialization & Export ─────────────────────────────────────────

    def export_graph(self) -> BehaviorGraph:
        """Export full graph state to a BehaviorGraph container."""
        nodes = {n.node_id: n for n in self.list_nodes()}
        edges = {e.edge_id: e for e in self.list_edges()}
        entry_nodes = [nid for nid, _ in self.list_entry_nodes()]
        grammars = {g.scene: g for g in self.list_grammars()}
        return BehaviorGraph(
            schema_version=1,
            metadata={"exported_at": time.time()},
            nodes=nodes,
            edges=edges,
            entry_nodes=entry_nodes,
            action_grammars=grammars,
        )

    def import_graph(self, graph: BehaviorGraph) -> Dict[str, int]:
        """Import nodes, edges, entry nodes, and grammars from a BehaviorGraph."""
        nodes_imported = 0
        edges_imported = 0

        for node in graph.nodes.values():
            self.insert_node(node)
            nodes_imported += 1

        for edge in graph.edges.values():
            self.insert_edge(edge)
            edges_imported += 1

        for entry_id in graph.entry_nodes:
            if entry_id in graph.nodes:
                self.register_entry_node(entry_id, label=graph.nodes[entry_id].anchor)

        for grammar in graph.action_grammars.values():
            self.register_grammar(grammar)

        return {
            "nodes_imported": nodes_imported,
            "edges_imported": edges_imported,
            "entry_nodes_imported": len(graph.entry_nodes),
            "grammars_imported": len(graph.action_grammars),
        }

    def export_json(self, path: Path = DEFAULT_ATLAS_JSON) -> None:
        """Export Behavior Atlas graph to a JSON file."""
        graph = self.export_graph()
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(graph.to_dict(), f, indent=2)

    def import_json(self, path: Path) -> Dict[str, int]:
        """Import Behavior Atlas graph from a JSON file."""
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        graph = BehaviorGraph.from_dict(data)
        return self.import_graph(graph)

    # ── Summary & Statistics ─────────────────────────────────────────────────

    def summary(self) -> Dict[str, Any]:
        """Compute summary metrics over the atlas graph."""
        with self._get_conn() as conn:
            total_nodes = conn.execute("SELECT COUNT(*) FROM atlas_nodes").fetchone()[0]
            total_edges = conn.execute("SELECT COUNT(*) FROM atlas_edges").fetchone()[0]
            status_counts = dict(
                conn.execute(
                    "SELECT status, COUNT(*) FROM atlas_edges GROUP BY status"
                ).fetchall()
            )
            anchor_counts = dict(
                conn.execute(
                    "SELECT anchor, COUNT(*) FROM atlas_nodes GROUP BY anchor ORDER BY COUNT(*) DESC"
                ).fetchall()
            )
            proven_edges = conn.execute(
                "SELECT COUNT(*) FROM atlas_edges WHERE proof_id IS NOT NULL"
            ).fetchone()[0]
            scenarios_indexed = conn.execute(
                "SELECT COUNT(DISTINCT scenario_ref) FROM atlas_edges WHERE scenario_ref IS NOT NULL"
            ).fetchone()[0]

        return {
            "total_nodes": total_nodes,
            "total_edges": total_edges,
            "proven_edges": proven_edges,
            "scenarios_indexed": scenarios_indexed or 0,
            "status_breakdown": status_counts,
            "top_anchors": anchor_counts,
        }
