#!/usr/bin/env python3
"""Coverage Atlas & Query API for OpenRecet (CV-02, CV-03, CV-04, CV-05, CV-06, CV-07, CV-08).

Manages static-to-dynamic basic block, edge, and semantic coverage maps,
ingests dynamic Frida Stalker coverage artifacts, validates scenario coverage
declarations (CV-04), tracks semantic dimensions (CV-05: VM opcodes, scene
transitions, table/content IDs, asset loads, audio IDs, save operations),
computes multi-scenario deltas, generates CV-06 gap analysis reports,
implements the CV-07 next-experiment prioritizer (multi-factor candidate ranking),
and enforces CV-08 coverage truth calibration (collection modes, confidence
scoring, cross-collector validation, CFG consistency, repeat-run determinism,
blind-spot documentation, and uncalibrated global percentage gating).

Tables:
  - coverage_runs: metadata and provenance per scenario/drive
  - coverage_blocks: block-level execution counts mapped to owning functions
  - coverage_edges: edge-level execution counts (src_block_end -> dst_block_start)
  - coverage_semantics: semantic dimension events (dimension, item_id, hits, frame)
  - coverage_calibrations: verified coverage calibration records and verdicts (CV-08)

CLI Subcommands:
  - import: ingest coverage.json artifact from scenario run
  - summary: display global dynamic coverage statistics with CV-08 calibration gating
  - calibrate: verify collector integrity, cross-check call traces, and calibrate confidence (CV-08)
  - function: inspect detailed block/edge coverage for a single function
  - semantics: inspect semantic coverage dimensions (CV-05)
  - validate-scenario: validate scenario coverage contract and declarations (CV-04)
  - audit-scenarios: audit all scenario declarations across repository (CV-04)
  - delta: compute coverage delta between two scenarios
  - gaps: report executed-but-unimplemented, unexecuted, or branch gaps (CV-06)
  - prioritize: rank candidate next-experiments with factor breakdowns and explanations (CV-07)
  - export: dump aggregated coverage atlas as JSON
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import os
import re
import sqlite3
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

REPO = Path(__file__).resolve().parent.parent
DOCS_DIR = REPO / "docs"
RE_INDEX_DB = DOCS_DIR / "re-index.sqlite"
COVERAGE_DB = DOCS_DIR / "coverage-atlas.sqlite"
PORT_LEDGER_JSON = DOCS_DIR / "port-ledger.json"
FUNCTIONS_CSV = DOCS_DIR / "decompiled" / "functions.csv"
CONTRACT_SCHEMA_PATH = DOCS_DIR / "schemas" / "parity-contract-v1.schema.json"
SCENARIOS_DIR = REPO / "tests" / "scenarios"
PORT_DEBT_JSON = DOCS_DIR / "port-debt.json"

# Optional YAML and JSONSchema support
try:
    import yaml
except ImportError:
    yaml = None  # type: ignore

try:
    from jsonschema import Draft202012Validator
except ImportError:
    Draft202012Validator = None  # type: ignore

# Add tools to sys.path for re_index import if available
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

try:
    from tools.re_index import ReIndex, format_fun, format_va, parse_va
except ImportError:
    # Standalone fallbacks
    def parse_va(val: str | int) -> int:
        if isinstance(val, int):
            return val
        s = str(val).strip()
        for prefix in ("fun_", "dat_", "ptr_", "s_", "0x"):
            if s.lower().startswith(prefix):
                s = s[len(prefix):]
                break
        return int(s, 16)

    def format_va(va: int) -> str:
        return f"0x{va:06x}"

    def format_fun(va: int) -> str:
        return f"FUN_{va:08x}"

    ReIndex = None  # type: ignore


# ─── Semantic Dimension Patterns and Canonical Constants (CV-05) ─────────────

KNOWN_SEMANTIC_DIMENSIONS = (
    "functions",
    "blocks",
    "vm_operations",
    "transitions",
    "content_ids",
    "assets",
    "audio_ids",
    "save_ops",
    "boundary_outcomes",
)

RE_FUNC_ID = re.compile(r"^(FUN_[0-9a-fA-F]{8}|0x[0-9a-fA-F]+|[0-9]+)$")
RE_BLOCK_ID = re.compile(r"^0x[0-9a-fA-F]+$")
RE_VM_OP = re.compile(r"^(TUTO_[A-Z0-9_]+|DLG_[A-Z0-9_]+|OP_0x[0-9a-fA-F]+|[A-Z0-9_]+:[A-Z0-9_]+)$")
RE_TRANSITION = re.compile(r"^([A-Z0-9_]+(->[A-Z0-9_]+)?|SCENE_[A-Z0-9_]+)$")
RE_CONTENT_ID = re.compile(r"^(ITEM|KYAKU|ENEMY|EVENT|STAGE|ITEM_CAT):[0-9a-zA-Z_\-]+$")
RE_ASSET_ID = re.compile(r"^(mesh:|tex:|sound:|data:|bg:|chara:|effect:|font:|model:)?.*?\.(x|tga|bmp|lnk|ini|wav|mp3|mid|bin)$", re.IGNORECASE)
RE_AUDIO_ID = re.compile(r"^(BGM:[0-9]+|SE:se_[0-9]{3}_id[0-9a-fA-F]{4}|SE_FILE:.*?|se_[0-9]{3}_id[0-9a-fA-F]{4}|[0-9]+)$")
RE_SAVE_OP = re.compile(r"^SAVE:(slot_[0-9]+|commit|read|verify|deserialize|serialize|header).*?$")
RE_OUTCOME = re.compile(r"^(OUTCOME:)?[A-Z0-9_\-]+$")

# ─── CV-07 Prioritization Policy Constants ──────────────────────────────────

CV07_POLICY_VERSION = "CV-07-v1.0"

CV07_DEFAULT_WEIGHTS: Dict[str, float] = {
    "new_coverage_potential": 0.25,
    "new_semantic_potential": 0.15,
    "distance_from_certified": 0.20,
    "port_readiness": 0.15,
    "proof_deficit": 0.15,
    "runtime_cost_efficiency": 0.10,
    "active_front_affinity": 0.20,
}

READINESS_LEVELS: Dict[str, int] = {
    "discovered": 0,
    "source-referenced": 1,
    "referenced": 1,
    "stubbed": 2,
    "ported": 3,
    "verified": 4,
    "runtime_proven": 5,
    "proven": 5,
}

FRONT_KEYWORDS: Dict[str, List[str]] = {
    "customer_service": [
        "cs_", "FUN_00465372", "FUN_00464af0", "FUN_004639f5", "FUN_00463cfb",
        "FUN_00464a26", "FUN_00460eba", "FUN_00461303", "FUN_00460b93",
        "FUN_00436623", "FUN_004361b2", "customer_service", "haggle", "roster",
        "order", "bargain",
    ],
    "day2_transition": [
        "scene1_tutorial_dispatch", "FUN_0044bd0d", "FUN_0048670f", "FUN_0040a765",
        "scene1_postload", "FUN_0048526d", "b924", "b928", "scene1_fx_overlays",
        "day_card", "day2", "iv2_",
    ],
    "shop_loop": [
        "scene1_", "house_", "shelf", "display", "pricing", "catalog", "table", "shopaccum",
    ],
    "save_system": [
        "save_", "save_io", "save_bank", "save_work", "slot_",
    ],
    "dungeon": [
        "dungeon_", "stage_", "monster", "combat", "battle", "floor", "item_use",
    ],
    "title_prologue": [
        "scene0_", "title_", "prologue_", "load_game", "opening",
    ],
    "menus": [
        "menu_", "pause_", "config_", "setting_",
    ],
}


# ─── CV-08 Coverage Truth Calibration Constants ────────────────────────────

CV08_POLICY_VERSION = "CV-08-v1.0"


class CollectionMode:
    DYNAMIC_STALKER = "DYNAMIC_STALKER"
    DYNAMIC_CALL_TRACE = "DYNAMIC_CALL_TRACE"
    DYNAMIC_TTD = "DYNAMIC_TTD"
    STATIC_CFG_INFERRED = "STATIC_CFG_INFERRED"
    STATIC_PROVEN = "STATIC_PROVEN"
    SYNTHETIC_HYBRID = "SYNTHETIC_HYBRID"
    UNSPECIFIED = "UNSPECIFIED"

    ALL = (
        DYNAMIC_STALKER,
        DYNAMIC_CALL_TRACE,
        DYNAMIC_TTD,
        STATIC_CFG_INFERRED,
        STATIC_PROVEN,
        SYNTHETIC_HYBRID,
        UNSPECIFIED,
    )


class ConfidenceBand:
    CERTIFIED = "CERTIFIED"        # 0.95 - 1.00
    HIGH = "HIGH"                  # 0.85 - 0.94
    MODERATE = "MODERATE"          # 0.60 - 0.84
    LOW = "LOW"                    # 0.30 - 0.59
    UNVERIFIED = "UNVERIFIED"      # 0.01 - 0.29
    UNCALIBRATED = "UNCALIBRATED"  # 0.00


class BlindSpotKind:
    INDIRECT_CALL = "INDIRECT_CALL"
    SWITCH_TABLE = "SWITCH_TABLE"
    EXCEPTION_HANDLER = "EXCEPTION_HANDLER"
    SHORT_BLOCK = "SHORT_BLOCK"
    EXTERNAL_THUNK = "EXTERNAL_THUNK"
    ASYNCHRONOUS_THREAD = "ASYNCHRONOUS_THREAD"
    JIT_COALESCED = "JIT_COALESCED"


class CalibrationVerdict:
    PASS = "PASS"
    CONDITIONAL_PASS = "CONDITIONAL_PASS"
    FAIL = "FAIL"
    UNCALIBRATED = "UNCALIBRATED"


CV08_CALIBRATION_WEIGHTS: Dict[str, float] = {
    "collector_integrity": 0.25,
    "cross_collector_agreement": 0.30,
    "cfg_structural_validity": 0.25,
    "determinism": 0.20,
}

def validate_semantic_item_id(dimension: str, item_id: str) -> Tuple[bool, Optional[str]]:
    """Validate format and bounds for a declared semantic coverage item (CV-04/CV-05)."""
    if not isinstance(item_id, str) or not item_id.strip():
        return False, "Item ID must be a non-empty string"

    item_id = item_id.strip()

    if dimension == "functions":
        if not RE_FUNC_ID.match(item_id):
            return False, f"Invalid function identifier format: '{item_id}' (expected FUN_00XXXXXX or 0xXXXXXX)"
        return True, None

    elif dimension == "blocks":
        if not RE_BLOCK_ID.match(item_id):
            return False, f"Invalid block identifier format: '{item_id}' (expected 0xXXXXXX)"
        try:
            va = parse_va(item_id)
            if va < 0x401000 or va > 0x540000:
                return False, f"Block VA {item_id} out of module code range (0x401000..0x540000)"
        except Exception as e:
            return False, f"Failed to parse block VA {item_id}: {e}"
        return True, None

    elif dimension == "vm_operations":
        if not RE_VM_OP.match(item_id):
            return False, f"Invalid VM opcode format: '{item_id}' (expected TUTO_*, DLG_*, or OP_0x*)"
        return True, None

    elif dimension == "transitions":
        if not RE_TRANSITION.match(item_id):
            return False, f"Invalid transition/anchor identifier: '{item_id}' (expected ANCHOR_NAME or FROM->TO)"
        return True, None

    elif dimension == "content_ids":
        if not RE_CONTENT_ID.match(item_id):
            return False, f"Invalid content ID format: '{item_id}' (expected ITEM:N, KYAKU:N, ENEMY:N, EVENT:N, STAGE:N)"
        return True, None

    elif dimension == "assets":
        if not RE_ASSET_ID.match(item_id):
            return False, f"Invalid asset path/identifier: '{item_id}' (expected valid extension .x, .tga, .bmp, .lnk, .ini)"
        return True, None

    elif dimension == "audio_ids":
        if not RE_AUDIO_ID.match(item_id):
            return False, f"Invalid audio ID format: '{item_id}' (expected BGM:N, SE:se_NNN_idXXXX, or se_NNN_idXXXX)"
        return True, None

    elif dimension == "save_ops":
        if not RE_SAVE_OP.match(item_id):
            return False, f"Invalid save operation format: '{item_id}' (expected SAVE:slot_N_commit, SAVE:read, etc.)"
        return True, None

    elif dimension == "boundary_outcomes":
        if not RE_OUTCOME.match(item_id):
            return False, f"Invalid outcome identifier: '{item_id}' (expected PASS, FAIL, GATEFAIL, SKIP)"
        return True, None

    else:
        # User-defined dimensions allowed if alphanumeric
        if re.match(r"^[A-Za-z0-9_:\-]+$", item_id):
            return True, None
        return False, f"Unknown dimension '{dimension}' and invalid item identifier: '{item_id}'"


@dataclass
class FunctionRange:
    va: int
    name: str
    size: int
    end_va: int
    is_thunk: bool
    port_status: str
    runtime_status: Optional[str]


class CoverageAtlas:
    """Dynamic basic block, edge, & semantic coverage atlas engine (CV-02..CV-06)."""

    def __init__(
        self,
        db_path: Path = COVERAGE_DB,
        re_index_path: Path = RE_INDEX_DB,
        re_index: Optional[Any] = None,
    ):
        self.db_path = Path(db_path)
        self.re_index_path = Path(re_index_path)
        self._re_index = re_index
        self._conn: Optional[sqlite3.Connection] = None
        self._func_ranges: List[FunctionRange] = []
        self._func_starts: List[int] = []
        self._func_map: Dict[int, FunctionRange] = {}

    def connect(self) -> sqlite3.Connection:
        if self._conn is None:
            self._ensure_tables()
            self._conn = sqlite3.connect(str(self.db_path))
            self._conn.row_factory = sqlite3.Row
        return self._conn

    def close(self) -> None:
        if self._conn:
            self._conn.close()
            self._conn = None

    def _ensure_tables(self) -> None:
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.db_path))
        cur = conn.cursor()

        cur.execute("""
            CREATE TABLE IF NOT EXISTS coverage_runs (
                run_id TEXT PRIMARY KEY,
                scenario TEXT NOT NULL,
                start_frame INTEGER NOT NULL,
                end_frame INTEGER NOT NULL,
                total_events INTEGER NOT NULL,
                module_events INTEGER NOT NULL,
                out_of_module_events INTEGER NOT NULL,
                lost_events INTEGER NOT NULL,
                unique_blocks INTEGER NOT NULL,
                unique_edges INTEGER NOT NULL,
                artifact_hash TEXT NOT NULL,
                imported_at TEXT NOT NULL
            )
        """)

        cur.execute("""
            CREATE TABLE IF NOT EXISTS coverage_blocks (
                run_id TEXT NOT NULL,
                block_va INTEGER NOT NULL,
                func_va INTEGER NOT NULL,
                hits INTEGER NOT NULL,
                PRIMARY KEY (run_id, block_va)
            )
        """)

        cur.execute("""
            CREATE TABLE IF NOT EXISTS coverage_edges (
                run_id TEXT NOT NULL,
                src_va INTEGER NOT NULL,
                dst_va INTEGER NOT NULL,
                hits INTEGER NOT NULL,
                PRIMARY KEY (run_id, src_va, dst_va)
            )
        """)

        cur.execute("""
            CREATE TABLE IF NOT EXISTS coverage_semantics (
                run_id TEXT NOT NULL,
                dimension TEXT NOT NULL,
                item_id TEXT NOT NULL,
                hits INTEGER NOT NULL DEFAULT 1,
                first_frame INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (run_id, dimension, item_id)
            )
        """)

        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_blocks_bva ON coverage_blocks(block_va)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_blocks_fva ON coverage_blocks(func_va)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_edges_src ON coverage_edges(src_va)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_edges_dst ON coverage_edges(dst_va)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_runs_scen ON coverage_runs(scenario)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_sem_dim_item ON coverage_semantics(dimension, item_id)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_sem_run ON coverage_semantics(run_id)")


        cur.execute("""
            CREATE TABLE IF NOT EXISTS coverage_calibrations (
                calibration_id TEXT PRIMARY KEY,
                scenario TEXT,
                run_id TEXT,
                collection_mode TEXT NOT NULL,
                confidence_score REAL NOT NULL,
                confidence_band TEXT NOT NULL,
                verdict TEXT NOT NULL,
                cross_collector_agreement REAL,
                cfg_structural_validity REAL,
                determinism_score REAL,
                blind_spots_count INTEGER NOT NULL,
                blind_spots_json TEXT NOT NULL,
                details_json TEXT NOT NULL,
                calibrated_at TEXT NOT NULL
            )
        """)
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_calib_scen ON coverage_calibrations(scenario)")
        cur.execute("CREATE INDEX IF NOT EXISTS idx_cov_calib_verdict ON coverage_calibrations(verdict)")
        conn.commit()
        conn.close()

    def _load_functions(self) -> None:
        """Load static function ranges for block-to-function attribution."""
        if self._func_ranges:
            return

        ranges: List[FunctionRange] = []

        # 1. Try re-index SQLite if available
        if self.re_index_path.exists():
            try:
                rconn = sqlite3.connect(str(self.re_index_path))
                rconn.row_factory = sqlite3.Row
                cur = rconn.cursor()
                cur.execute("""
                    SELECT va, name, size, is_thunk, port_status, runtime_status
                    FROM functions
                    ORDER BY va ASC
                """)
                for row in cur.fetchall():
                    va = int(row["va"])
                    size = int(row["size"])
                    name = str(row["name"])
                    is_thunk = bool(row["is_thunk"])
                    p_status = str(row["port_status"] or "discovered")
                    rt_status = row["runtime_status"]
                    ranges.append(
                        FunctionRange(
                            va=va,
                            name=name,
                            size=size,
                            end_va=va + size,
                            is_thunk=is_thunk,
                            port_status=p_status,
                            runtime_status=rt_status,
                        )
                    )
                rconn.close()
            except Exception:
                ranges = []

        # 2. Fallback to functions.csv if re-index SQLite not yet built
        if not ranges and FUNCTIONS_CSV.exists():
            try:
                with FUNCTIONS_CSV.open(encoding="utf-8") as f:
                    reader = csv.DictReader(f)
                    for row in reader:
                        va = parse_va(row["entry"])
                        size = int(row.get("size") or 0)
                        name = row.get("name", format_fun(va))
                        is_thunk = row.get("is_thunk") == "true"
                        ranges.append(
                            FunctionRange(
                                va=va,
                                name=name,
                                size=size,
                                end_va=va + size,
                                is_thunk=is_thunk,
                                port_status="discovered",
                                runtime_status=None,
                            )
                        )
                ranges.sort(key=lambda r: r.va)
            except Exception:
                ranges = []

        self._func_ranges = ranges
        self._func_starts = [r.va for r in ranges]
        self._func_map = {r.va: r for r in ranges}

    def resolve_function(self, va: int) -> Optional[FunctionRange]:
        """Find the owning function for any instruction/block VA."""
        self._load_functions()
        if not self._func_ranges:
            return None

        # Binary search for closest function start <= va
        idx = bisect.bisect_right(self._func_starts, va) - 1
        if idx >= 0 and idx < len(self._func_ranges):
            fr = self._func_ranges[idx]
            # If function has defined size, check if va < end_va
            if fr.size > 0:
                if fr.va <= va < fr.end_va:
                    return fr
            else:
                # If size unknown (0), attribute if within reasonable span before next fn
                next_start = self._func_starts[idx + 1] if idx + 1 < len(self._func_starts) else fr.va + 0x1000
                if fr.va <= va < next_start:
                    return fr

        return None

    def import_run(
        self,
        data: Dict[str, Any] | Path | str,
        scenario: str = "",
        run_id: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Ingest a dynamic coverage artifact JSON into the atlas (CV-03/05)."""
        if isinstance(data, (Path, str)):
            p = Path(data)
            content = p.read_text(encoding="utf-8")
            raw_dict = json.loads(content)
            if not scenario:
                scenario = p.parent.name if p.parent.name != "frames" else p.parent.parent.name
        else:
            raw_dict = data
            content = json.dumps(raw_dict, sort_keys=True)

        if not scenario:
            scenario = raw_dict.get("scenario", "unnamed_scenario")

        # Compute deterministic content hash
        art_hash = hashlib.sha256(content.encode("utf-8")).hexdigest()

        if not run_id:
            run_id = f"{scenario}_{art_hash[:12]}"

        total_events = int(raw_dict.get("total_events", 0))
        module_events = int(raw_dict.get("module_events", 0))
        out_of_mod = int(raw_dict.get("out_of_module_events", 0))
        lost_events = int(raw_dict.get("lost_events", 0))
        start_frame = int(raw_dict.get("start_frame", 0))
        end_frame = int(raw_dict.get("end_frame", -1))
        imported_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

        blocks = raw_dict.get("blocks", [])
        edges = raw_dict.get("edges", [])

        conn = self.connect()
        cur = conn.cursor()

        # Begin transaction
        cur.execute("BEGIN TRANSACTION")
        try:
            # Upsert run record
            cur.execute("""
                INSERT OR REPLACE INTO coverage_runs (
                    run_id, scenario, start_frame, end_frame, total_events,
                    module_events, out_of_module_events, lost_events,
                    unique_blocks, unique_edges, artifact_hash, imported_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                run_id, scenario, start_frame, end_frame, total_events,
                module_events, out_of_mod, lost_events, len(blocks),
                len(edges), art_hash, imported_at
            ))

            # Clear previous records for this run_id
            cur.execute("DELETE FROM coverage_blocks WHERE run_id = ?", (run_id,))
            cur.execute("DELETE FROM coverage_edges WHERE run_id = ?", (run_id,))
            cur.execute("DELETE FROM coverage_semantics WHERE run_id = ?", (run_id,))

            # Ingest blocks
            block_rows = []
            func_hits: Dict[int, int] = {}
            for b in blocks:
                b_va = parse_va(b["va"])
                hits = int(b.get("hits", 1))
                fn = self.resolve_function(b_va)
                f_va = fn.va if fn else 0
                block_rows.append((run_id, b_va, f_va, hits))
                if f_va != 0:
                    func_hits[f_va] = func_hits.get(f_va, 0) + hits

            cur.executemany("""
                INSERT INTO coverage_blocks (run_id, block_va, func_va, hits)
                VALUES (?, ?, ?, ?)
            """, block_rows)

            # Ingest edges
            edge_rows = []
            for e in edges:
                src_va = parse_va(e["src"])
                dst_va = parse_va(e["dst"])
                hits = int(e.get("hits", 1))
                edge_rows.append((run_id, src_va, dst_va, hits))

            cur.executemany("""
                INSERT INTO coverage_edges (run_id, src_va, dst_va, hits)
                VALUES (?, ?, ?, ?)
            """, edge_rows)

            # ── Ingest Semantic Dimensions (CV-05) ──────────────────────────
            semantic_rows = []

            # 1. Derive function-level semantic coverage automatically
            for f_va, hits in func_hits.items():
                fn_name = format_fun(f_va)
                semantic_rows.append((run_id, "functions", fn_name, hits, start_frame))

            # 2. Derive block-level semantic coverage automatically
            for b in blocks:
                b_va = parse_va(b["va"])
                hits = int(b.get("hits", 1))
                semantic_rows.append((run_id, "blocks", format_va(b_va), hits, start_frame))

            # 3. Ingest explicit semantics payload if present
            raw_semantics = raw_dict.get("semantics") or {}
            if isinstance(raw_semantics, dict):
                for dim, items in raw_semantics.items():
                    if isinstance(items, list):
                        for it in items:
                            if isinstance(it, dict):
                                it_id = str(it.get("item_id") or it.get("id") or "")
                                it_hits = int(it.get("hits", 1))
                                it_frame = int(it.get("first_frame", it.get("frame", start_frame)))
                            else:
                                it_id = str(it)
                                it_hits = 1
                                it_frame = start_frame

                            if it_id:
                                is_valid, _ = validate_semantic_item_id(dim, it_id)
                                if is_valid:
                                    semantic_rows.append((run_id, dim, it_id, it_hits, it_frame))

            # 4. Ingest transitions / anchors if listed in artifact
            raw_anchors = raw_dict.get("anchors") or []
            if isinstance(raw_anchors, list):
                for anc in raw_anchors:
                    if isinstance(anc, dict):
                        a_name = str(anc.get("anchor") or anc.get("name") or "")
                        a_frame = int(anc.get("frame", start_frame))
                    else:
                        a_name = str(anc)
                        a_frame = start_frame
                    if a_name:
                        semantic_rows.append((run_id, "transitions", a_name, 1, a_frame))

            # 5. Ingest audio events if listed in artifact
            raw_audio = raw_dict.get("audio") or []
            if isinstance(raw_audio, list):
                for aud in raw_audio:
                    if isinstance(aud, dict):
                        aud_name = str(aud.get("name") or aud.get("id") or "")
                        aud_frame = int(aud.get("frame", start_frame))
                    else:
                        aud_name = str(aud)
                        aud_frame = start_frame
                    if aud_name:
                        semantic_rows.append((run_id, "audio_ids", aud_name, 1, aud_frame))

            if semantic_rows:
                cur.executemany("""
                    INSERT OR REPLACE INTO coverage_semantics (run_id, dimension, item_id, hits, first_frame)
                    VALUES (?, ?, ?, ?, ?)
                """, semantic_rows)

            conn.commit()
        except Exception:
            conn.rollback()
            raise

        return {
            "status": "imported",
            "run_id": run_id,
            "scenario": scenario,
            "blocks_imported": len(block_rows),
            "edges_imported": len(edge_rows),
            "semantics_imported": len(semantic_rows),
            "artifact_hash": art_hash,
        }

    def import_semantics(
        self,
        run_id: str,
        dimension: str,
        items: List[Dict[str, Any] | str],
    ) -> int:
        """Manually import a batch of semantic events for a run (CV-05)."""
        rows = []
        for it in items:
            if isinstance(it, dict):
                it_id = str(it.get("item_id") or it.get("id") or "")
                hits = int(it.get("hits", 1))
                frame = int(it.get("first_frame", it.get("frame", 0)))
            else:
                it_id = str(it)
                hits = 1
                frame = 0

            is_valid, err = validate_semantic_item_id(dimension, it_id)
            if not is_valid:
                raise ValueError(f"Invalid semantic item for {dimension}: {err}")

            rows.append((run_id, dimension, it_id, hits, frame))

        conn = self.connect()
        cur = conn.cursor()
        cur.executemany("""
            INSERT OR REPLACE INTO coverage_semantics (run_id, dimension, item_id, hits, first_frame)
            VALUES (?, ?, ?, ?, ?)
        """, rows)
        conn.commit()
        return len(rows)

    def get_latest_calibration(self, scenario: Optional[str] = None) -> Optional[Dict[str, Any]]:
        """Retrieve the most recent coverage calibration record (CV-08)."""
        conn = self.connect()
        cur = conn.cursor()
        if scenario:
            cur.execute("""
                SELECT * FROM coverage_calibrations
                WHERE scenario = ? OR scenario = 'all_scenarios'
                ORDER BY calibrated_at DESC LIMIT 1
            """, (scenario,))
        else:
            cur.execute("""
                SELECT * FROM coverage_calibrations
                ORDER BY calibrated_at DESC LIMIT 1
            """)
        row = cur.fetchone()
        if not row:
            return None
        return dict(row)

    def calibrate_coverage(
        self,
        scenario: Optional[str] = None,
        run_id: Optional[str] = None,
        mode: str = CollectionMode.DYNAMIC_STALKER,
        call_trace_vas: Optional[Set[int] | List[int] | Path | str] = None,
        repeat_run_ids: Optional[List[str]] = None,
        min_confidence: float = 0.85,
        save_to_db: bool = True,
    ) -> Dict[str, Any]:
        """Calibrate dynamic coverage truth, assess collector integrity, validate CFG, detect blind spots, and compute confidence (CV-08)."""
        conn = self.connect()
        cur = conn.cursor()
        self._load_functions()

        # 1. Resolve Target Runs
        if run_id:
            cur.execute("SELECT * FROM coverage_runs WHERE run_id = ?", (run_id,))
            runs = [dict(r) for r in cur.fetchall()]
        elif scenario:
            cur.execute("SELECT * FROM coverage_runs WHERE scenario = ? ORDER BY imported_at DESC", (scenario,))
            runs = [dict(r) for r in cur.fetchall()]
        else:
            cur.execute("SELECT * FROM coverage_runs ORDER BY imported_at DESC")
            runs = [dict(r) for r in cur.fetchall()]

        if not runs:
            return {
                "version": CV08_POLICY_VERSION,
                "scenario": scenario,
                "run_id": run_id,
                "collection_mode": mode,
                "confidence_score": 0.0,
                "confidence_band": ConfidenceBand.UNCALIBRATED,
                "verdict": CalibrationVerdict.UNCALIBRATED,
                "error": "No coverage runs found in atlas to calibrate",
            }

        # Aggregate run events
        total_events = sum(r["total_events"] for r in runs)
        module_events = sum(r["module_events"] for r in runs)
        out_of_module_events = sum(r["out_of_module_events"] for r in runs)
        lost_events = sum(r["lost_events"] for r in runs)
        selected_run_ids = [r["run_id"] for r in runs]
        placeholders = ",".join("?" for _ in selected_run_ids)

        # 2. Factor 1: Collector Integrity & Event Loss
        if total_events == 0 and lost_events == 0:
            integrity_score = 0.0 if not runs[0].get("unique_blocks") else 1.0
        elif lost_events == 0:
            integrity_score = 1.0
        else:
            loss_ratio = lost_events / max(1, total_events + lost_events)
            integrity_score = max(0.0, 1.0 - (loss_ratio * 10.0))

        # 3. Factor 2: Cross-Collector Agreement (Stalker vs Call Trace / Ground Truth)
        cur.execute(f"""
            SELECT DISTINCT func_va FROM coverage_blocks
            WHERE run_id IN ({placeholders}) AND func_va != 0
        """, selected_run_ids)
        stalker_func_vas: Set[int] = {row[0] for row in cur.fetchall()}

        expected_call_vas: Set[int] = set()
        if call_trace_vas is not None:
            if isinstance(call_trace_vas, (Path, str)):
                p = Path(call_trace_vas)
                if p.exists():
                    try:
                        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
                            line = line.strip()
                            if not line or line.startswith("#"):
                                continue
                            if line.startswith("{") and line.endswith("}"):
                                try:
                                    entry = json.loads(line)
                                    if "va" in entry:
                                        expected_call_vas.add(parse_va(entry["va"]))
                                    elif "func_va" in entry:
                                        expected_call_vas.add(parse_va(entry["func_va"]))
                                except Exception:
                                    pass
                            else:
                                for token in line.split():
                                    try:
                                        if token.startswith("FUN_") or token.startswith("0x") or len(token) >= 6:
                                            expected_call_vas.add(parse_va(token))
                                    except Exception:
                                        pass
                    except Exception:
                        pass
            elif isinstance(call_trace_vas, (set, list, tuple)):
                for item in call_trace_vas:
                    try:
                        expected_call_vas.add(parse_va(item))
                    except Exception:
                        pass

        missing_in_stalker: List[str] = []
        extra_in_stalker: List[str] = []
        if expected_call_vas:
            matched_vas = stalker_func_vas & expected_call_vas
            missing_vas = expected_call_vas - stalker_func_vas
            extra_vas = stalker_func_vas - expected_call_vas

            missing_in_stalker = [format_fun(va) for va in sorted(missing_vas)]
            extra_in_stalker = [format_fun(va) for va in sorted(extra_vas)]

            agreement_ratio = len(matched_vas) / max(1, len(expected_call_vas))
            cross_collector_score = max(0.0, agreement_ratio)
        else:
            cur.execute(f"""
                SELECT DISTINCT item_id FROM coverage_semantics
                WHERE run_id IN ({placeholders}) AND dimension = 'functions'
            """, selected_run_ids)
            sem_funcs = {parse_va(row[0]) for row in cur.fetchall()}
            if sem_funcs:
                agreement_ratio = len(stalker_func_vas & sem_funcs) / max(1, len(sem_funcs))
                cross_collector_score = 1.0 if agreement_ratio >= 0.99 else agreement_ratio
            else:
                agreement_ratio = 1.0
                cross_collector_score = 1.0

        # 4. Factor 3: Static CFG Structural Alignment
        cur.execute(f"""
            SELECT DISTINCT block_va, func_va FROM coverage_blocks
            WHERE run_id IN ({placeholders})
        """, selected_run_ids)
        covered_blocks = cur.fetchall()

        valid_block_count = 0
        total_block_count = len(covered_blocks)
        funcs_with_entry_covered = 0
        funcs_with_blocks = set()

        for row in covered_blocks:
            b_va = int(row["block_va"])
            f_va = int(row["func_va"])
            if 0x401000 <= b_va <= 0x540000:
                valid_block_count += 1
            if f_va != 0:
                funcs_with_blocks.add(f_va)

        for f_va in funcs_with_blocks:
            cur.execute(f"""
                SELECT 1 FROM coverage_blocks
                WHERE run_id IN ({placeholders}) AND block_va = ?
                LIMIT 1
            """, (*selected_run_ids, f_va))
            if cur.fetchone():
                funcs_with_entry_covered += 1

        cfg_block_validity = (valid_block_count / max(1, total_block_count)) if total_block_count > 0 else 1.0
        entry_coverage_ratio = (funcs_with_entry_covered / max(1, len(funcs_with_blocks))) if funcs_with_blocks else 1.0
        cfg_structural_score = 0.7 * cfg_block_validity + 0.3 * entry_coverage_ratio

        # 5. Factor 4: Repeat-Run Determinism
        if len(runs) >= 2 or (repeat_run_ids and len(repeat_run_ids) >= 2):
            cmp_ids = repeat_run_ids if repeat_run_ids else [r["run_id"] for r in runs[:2]]
            cur.execute("SELECT block_va FROM coverage_blocks WHERE run_id = ?", (cmp_ids[0],))
            b1 = {row[0] for row in cur.fetchall()}
            cur.execute("SELECT block_va FROM coverage_blocks WHERE run_id = ?", (cmp_ids[1],))
            b2 = {row[0] for row in cur.fetchall()}

            cur.execute("SELECT src_va || '->' || dst_va FROM coverage_edges WHERE run_id = ?", (cmp_ids[0],))
            e1 = {row[0] for row in cur.fetchall()}
            cur.execute("SELECT src_va || '->' || dst_va FROM coverage_edges WHERE run_id = ?", (cmp_ids[1],))
            e2 = {row[0] for row in cur.fetchall()}

            jaccard_blocks = (len(b1 & b2) / max(1, len(b1 | b2))) if (b1 or b2) else 1.0
            jaccard_edges = (len(e1 & e2) / max(1, len(e1 | e2))) if (e1 or e2) else 1.0
            determinism_score = 0.6 * jaccard_blocks + 0.4 * jaccard_edges
        else:
            determinism_score = 0.90

        # 6. Blind Spot Cataloging & Quantification
        blind_spots: List[Dict[str, Any]] = []
        for f_va in sorted(funcs_with_blocks):
            fn = self.resolve_function(f_va)
            if not fn:
                continue

            if fn.is_thunk:
                blind_spots.append({
                    "va": format_va(f_va),
                    "function": fn.name,
                    "kind": BlindSpotKind.EXTERNAL_THUNK,
                    "severity": "LOW",
                    "description": f"Thunk/wrapper boundary {fn.name} dispatching to external runtime/API",
                    "remedy": "Instrument underlying Win32/D3D8 API wrapper",
                })

            if 0 < fn.size <= 16:
                blind_spots.append({
                    "va": format_va(f_va),
                    "function": fn.name,
                    "kind": BlindSpotKind.SHORT_BLOCK,
                    "severity": "LOW",
                    "description": f"Short function ({fn.size} bytes) subject to inline caching / fast-return",
                    "remedy": "Verify Stalker inline-cache transform settings",
                })

            if self.re_index_path.exists():
                try:
                    rconn = sqlite3.connect(str(self.re_index_path))
                    rcur = rconn.cursor()
                    rcur.execute("SELECT callee_va FROM calls WHERE caller_va = ?", (f_va,))
                    callees = [row[0] for row in rcur.fetchall()]
                    if any(c == 0 for c in callees):
                        blind_spots.append({
                            "va": format_va(f_va),
                            "function": fn.name,
                            "kind": BlindSpotKind.INDIRECT_CALL,
                            "severity": "MODERATE",
                            "description": f"Function {fn.name} contains indirect branch/call with dynamic target",
                            "remedy": "Log dynamic call target register at runtime",
                        })
                    rconn.close()
                except Exception:
                    pass

        blind_spot_penalty = min(0.15, len(blind_spots) * 0.005)

        # 7. Composite Confidence Score & Band
        raw_composite = (
            CV08_CALIBRATION_WEIGHTS["collector_integrity"] * integrity_score
            + CV08_CALIBRATION_WEIGHTS["cross_collector_agreement"] * cross_collector_score
            + CV08_CALIBRATION_WEIGHTS["cfg_structural_validity"] * cfg_structural_score
            + CV08_CALIBRATION_WEIGHTS["determinism"] * determinism_score
        ) - blind_spot_penalty

        confidence_score = max(0.0, min(1.0, round(raw_composite, 4)))

        if confidence_score >= 0.95 and lost_events == 0 and len(missing_in_stalker) == 0:
            band = ConfidenceBand.CERTIFIED
        elif confidence_score >= 0.85:
            band = ConfidenceBand.HIGH
        elif confidence_score >= 0.60:
            band = ConfidenceBand.MODERATE
        elif confidence_score >= 0.30:
            band = ConfidenceBand.LOW
        elif confidence_score > 0.0:
            band = ConfidenceBand.UNVERIFIED
        else:
            band = ConfidenceBand.UNCALIBRATED

        # 8. Verdict
        if confidence_score >= min_confidence and lost_events == 0 and len(missing_in_stalker) == 0:
            verdict = CalibrationVerdict.PASS
        elif confidence_score >= 0.60:
            verdict = CalibrationVerdict.CONDITIONAL_PASS
        else:
            verdict = CalibrationVerdict.FAIL

        # 9. Natural Language Explanations
        explanations = []
        if verdict == CalibrationVerdict.PASS:
            explanations.append(f"Calibration PASSED with {confidence_score:.2f} ({band}) confidence under {mode}.")
        elif verdict == CalibrationVerdict.CONDITIONAL_PASS:
            explanations.append(f"Calibration CONDITIONAL ({confidence_score:.2f}, {band}) under {mode}; caveats noted.")
        else:
            explanations.append(f"Calibration FAILED ({confidence_score:.2f}, {band}) under {mode} below required {min_confidence:.2f}.")

        if lost_events > 0:
            explanations.append(f"Warning: {lost_events} lost events observed in Frida Stalker buffer.")
        if missing_in_stalker:
            explanations.append(f"Cross-collector discrepancy: {len(missing_in_stalker)} function(s) recorded in call trace were missed by Stalker (e.g. {missing_in_stalker[:3]}).")
        if blind_spots:
            explanations.append(f"Identified {len(blind_spots)} potential instrumentation blind spot(s) across touched code.")

        calib_id = f"calib_{scenario or 'all'}_{time.strftime('%Y%m%d_%H%M%S', time.gmtime())}"
        calibrated_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

        result = {
            "version": CV08_POLICY_VERSION,
            "calibration_id": calib_id,
            "scenario": scenario or "all_scenarios",
            "runs_evaluated": len(runs),
            "collection_mode": mode,
            "confidence_score": confidence_score,
            "confidence_band": band,
            "verdict": verdict,
            "min_confidence_threshold": min_confidence,
            "factors": {
                "collector_integrity": round(integrity_score, 4),
                "cross_collector_agreement": round(cross_collector_score, 4),
                "cfg_structural_validity": round(cfg_structural_score, 4),
                "determinism": round(determinism_score, 4),
                "blind_spot_penalty": round(blind_spot_penalty, 4),
            },
            "metrics": {
                "total_events": total_events,
                "module_events": module_events,
                "lost_events": lost_events,
                "unique_blocks": len(covered_blocks),
                "functions_touched": len(funcs_with_blocks),
                "cross_collector_missing_count": len(missing_in_stalker),
                "cross_collector_extra_count": len(extra_in_stalker),
                "blind_spots_count": len(blind_spots),
            },
            "missing_in_stalker": missing_in_stalker,
            "extra_in_stalker": extra_in_stalker,
            "blind_spots": blind_spots,
            "explanations": explanations,
            "calibrated_at": calibrated_at,
        }

        if save_to_db:
            cur.execute("""
                INSERT OR REPLACE INTO coverage_calibrations (
                    calibration_id, scenario, run_id, collection_mode,
                    confidence_score, confidence_band, verdict,
                    cross_collector_agreement, cfg_structural_validity,
                    determinism_score, blind_spots_count, blind_spots_json,
                    details_json, calibrated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                calib_id, scenario, run_id, mode, confidence_score, band, verdict,
                cross_collector_score, cfg_structural_score, determinism_score,
                len(blind_spots), json.dumps(blind_spots), json.dumps(result),
                calibrated_at
            ))
            conn.commit()

        return result

    def get_summary(self, scenario: Optional[str] = None) -> Dict[str, Any]:
        """Aggregate coverage summary across all imported runs with CV-08 calibration gating."""
        conn = self.connect()
        cur = conn.cursor()

        cur.execute("SELECT COUNT(*), COUNT(DISTINCT scenario) FROM coverage_runs")
        run_count, scenario_count = cur.fetchone()

        cur.execute("SELECT COUNT(DISTINCT block_va) FROM coverage_blocks")
        unique_blocks = cur.fetchone()[0]

        cur.execute("SELECT COUNT(DISTINCT src_va || '->' || dst_va) FROM coverage_edges")
        unique_edges = cur.fetchone()[0]

        cur.execute("SELECT COUNT(DISTINCT func_va) FROM coverage_blocks WHERE func_va != 0")
        touched_funcs = cur.fetchone()[0]

        self._load_functions()
        total_funcs = len(self._func_ranges)

        cur.execute("""
            SELECT scenario, COUNT(DISTINCT block_va) as blocks, COUNT(DISTINCT src_va || '->' || dst_va) as edges
            FROM coverage_runs r
            JOIN coverage_blocks b ON r.run_id = b.run_id
            JOIN coverage_edges e ON r.run_id = e.run_id
            GROUP BY scenario
        """)
        scenarios_breakdown = {}
        for row in cur.fetchall():
            scenarios_breakdown[row["scenario"]] = {
                "unique_blocks": row["blocks"],
                "unique_edges": row["edges"],
            }

        # Semantic dimensions summary (CV-05)
        cur.execute("""
            SELECT dimension, COUNT(DISTINCT item_id) as item_count, SUM(hits) as total_hits
            FROM coverage_semantics
            GROUP BY dimension
            ORDER BY dimension ASC
        """)
        semantic_dims = {}
        for row in cur.fetchall():
            semantic_dims[row["dimension"]] = {
                "unique_items": row["item_count"],
                "total_hits": row["total_hits"],
            }

        # CV-08 Calibration Gating Check
        calib = self.get_latest_calibration(scenario=scenario)
        calibrated = calib is not None and calib["verdict"] in (CalibrationVerdict.PASS, CalibrationVerdict.CONDITIONAL_PASS)
        calib_verdict = calib["verdict"] if calib else CalibrationVerdict.UNCALIBRATED
        col_mode = calib["collection_mode"] if calib else CollectionMode.UNSPECIFIED
        conf_score = float(calib["confidence_score"]) if calib else 0.0
        conf_band = calib["confidence_band"] if calib else ConfidenceBand.UNCALIBRATED

        cov_ratio = (touched_funcs / max(1, total_funcs))
        if calibrated:
            coverage_claim = f"{cov_ratio*100:.1f}% ({touched_funcs}/{total_funcs} functions) [CALIBRATED: {calib_verdict}, mode={col_mode}, confidence={conf_score:.2f} ({conf_band})]"
        else:
            coverage_claim = "UNCALIBRATED (Calibration Required: CV-08 — run 'coverage_atlas.py calibrate')"

        return {
            "total_runs": run_count,
            "total_scenarios": scenario_count,
            "total_functions_in_index": total_funcs,
            "touched_functions": touched_funcs,
            "unique_blocks_covered": unique_blocks,
            "unique_edges_covered": unique_edges,
            "function_coverage_ratio": cov_ratio,
            "scenarios": scenarios_breakdown,
            "semantic_dimensions": semantic_dims,
            "cv08_calibration": {
                "calibrated": calibrated,
                "verdict": calib_verdict,
                "collection_mode": col_mode,
                "confidence_score": conf_score,
                "confidence_band": conf_band,
                "coverage_claim": coverage_claim,
                "calibrated_at": calib["calibrated_at"] if calib else None,
            },
        }

    def get_semantic_summary(self) -> Dict[str, Any]:
        """Get semantic coverage statistics grouped by dimension (CV-05)."""
        conn = self.connect()
        cur = conn.cursor()

        cur.execute("""
            SELECT dimension, COUNT(DISTINCT item_id) as item_count, SUM(hits) as total_hits,
                   COUNT(DISTINCT run_id) as runs_count
            FROM coverage_semantics
            GROUP BY dimension
            ORDER BY dimension ASC
        """)
        dims = {}
        for row in cur.fetchall():
            dims[row["dimension"]] = {
                "unique_items": row["item_count"],
                "total_hits": row["total_hits"],
                "runs_count": row["runs_count"],
            }
        return dims

    def get_dimension_items(
        self,
        dimension: str,
        scenario: Optional[str] = None,
    ) -> List[Dict[str, Any]]:
        """Get all covered items in a specific semantic dimension (CV-05)."""
        conn = self.connect()
        cur = conn.cursor()

        if scenario:
            cur.execute("""
                SELECT s.item_id, SUM(s.hits) as total_hits, MIN(s.first_frame) as first_frame,
                       COUNT(DISTINCT s.run_id) as run_count
                FROM coverage_semantics s
                JOIN coverage_runs r ON s.run_id = r.run_id
                WHERE s.dimension = ? AND r.scenario = ?
                GROUP BY s.item_id
                ORDER BY total_hits DESC, s.item_id ASC
            """, (dimension, scenario))
        else:
            cur.execute("""
                SELECT item_id, SUM(hits) as total_hits, MIN(first_frame) as first_frame,
                       COUNT(DISTINCT run_id) as run_count
                FROM coverage_semantics
                WHERE dimension = ?
                GROUP BY item_id
                ORDER BY total_hits DESC, item_id ASC
            """, (dimension,))

        results = []
        for row in cur.fetchall():
            results.append({
                "item_id": row["item_id"],
                "total_hits": row["total_hits"],
                "first_frame": row["first_frame"],
                "runs": row["run_count"],
            })
        return results

    def get_function_coverage(self, target: str | int) -> Optional[Dict[str, Any]]:
        """Get detailed block and edge coverage breakdown for a function."""
        va = parse_va(target)
        self._load_functions()
        fn = self._func_map.get(va) or self.resolve_function(va)
        if not fn:
            return None

        conn = self.connect()
        cur = conn.cursor()

        # Covered blocks for this function
        cur.execute("""
            SELECT block_va, SUM(hits) as total_hits, COUNT(DISTINCT run_id) as run_count
            FROM coverage_blocks
            WHERE func_va = ?
            GROUP BY block_va
            ORDER BY block_va ASC
        """, (fn.va,))
        blocks = [{"va": format_va(row["block_va"]), "raw_va": row["block_va"], "hits": row["total_hits"], "runs": row["run_count"]} for row in cur.fetchall()]

        # Internal edges (both src and dst within function bounds)
        cur.execute("""
            SELECT src_va, dst_va, SUM(hits) as total_hits
            FROM coverage_edges
            WHERE src_va >= ? AND src_va < ? AND dst_va >= ? AND dst_va < ?
            GROUP BY src_va, dst_va
            ORDER BY src_va ASC
        """, (fn.va, fn.end_va, fn.va, fn.end_va))
        internal_edges = [{"src": format_va(row["src_va"]), "dst": format_va(row["dst_va"]), "hits": row["total_hits"]} for row in cur.fetchall()]

        # Incoming edges (from outside function into function)
        cur.execute("""
            SELECT src_va, dst_va, SUM(hits) as total_hits
            FROM coverage_edges
            WHERE (src_va < ? OR src_va >= ?) AND dst_va >= ? AND dst_va < ?
            GROUP BY src_va, dst_va
            ORDER BY dst_va ASC
        """, (fn.va, fn.end_va, fn.va, fn.end_va))
        incoming_edges = [{"src": format_va(row["src_va"]), "dst": format_va(row["dst_va"]), "hits": row["total_hits"]} for row in cur.fetchall()]

        # Outgoing edges (from function to outside)
        cur.execute("""
            SELECT src_va, dst_va, SUM(hits) as total_hits
            FROM coverage_edges
            WHERE src_va >= ? AND src_va < ? AND (dst_va < ? OR dst_va >= ?)
            GROUP BY src_va, dst_va
            ORDER BY src_va ASC
        """, (fn.va, fn.end_va, fn.va, fn.end_va))
        outgoing_edges = [{"src": format_va(row["src_va"]), "dst": format_va(row["dst_va"]), "hits": row["total_hits"]} for row in cur.fetchall()]

        total_hits = sum(b["hits"] for b in blocks)
        is_covered = len(blocks) > 0

        return {
            "va": format_va(fn.va),
            "name": fn.name,
            "size": fn.size,
            "port_status": fn.port_status,
            "runtime_status": fn.runtime_status,
            "is_covered": is_covered,
            "total_hits": total_hits,
            "unique_blocks_count": len(blocks),
            "internal_edges_count": len(internal_edges),
            "incoming_edges_count": len(incoming_edges),
            "outgoing_edges_count": len(outgoing_edges),
            "blocks": blocks,
            "internal_edges": internal_edges,
            "incoming_edges": incoming_edges,
            "outgoing_edges": outgoing_edges,
        }

    def get_scenario_delta(self, scenario_a: str, scenario_b: str) -> Dict[str, Any]:
        """Compute coverage delta between scenario A and scenario B."""
        conn = self.connect()
        cur = conn.cursor()

        # Blocks for scenario A
        cur.execute("""
            SELECT block_va, func_va
            FROM coverage_blocks b
            JOIN coverage_runs r ON b.run_id = r.run_id
            WHERE r.scenario = ?
        """, (scenario_a,))
        blocks_a = {row["block_va"]: row["func_va"] for row in cur.fetchall()}

        # Blocks for scenario B
        cur.execute("""
            SELECT block_va, func_va
            FROM coverage_blocks b
            JOIN coverage_runs r ON b.run_id = r.run_id
            WHERE r.scenario = ?
        """, (scenario_b,))
        blocks_b = {row["block_va"]: row["func_va"] for row in cur.fetchall()}

        # Edges for scenario A
        cur.execute("""
            SELECT DISTINCT src_va, dst_va
            FROM coverage_edges e
            JOIN coverage_runs r ON e.run_id = r.run_id
            WHERE r.scenario = ?
        """, (scenario_a,))
        edges_a = {(row["src_va"], row["dst_va"]) for row in cur.fetchall()}

        # Edges for scenario B
        cur.execute("""
            SELECT DISTINCT src_va, dst_va
            FROM coverage_edges e
            JOIN coverage_runs r ON e.run_id = r.run_id
            WHERE r.scenario = ?
        """, (scenario_b,))
        edges_b = {(row["src_va"], row["dst_va"]) for row in cur.fetchall()}

        only_a_blocks = sorted(set(blocks_a.keys()) - set(blocks_b.keys()))
        only_b_blocks = sorted(set(blocks_b.keys()) - set(blocks_a.keys()))
        shared_blocks = sorted(set(blocks_a.keys()) & set(blocks_b.keys()))

        only_a_edges = sorted(edges_a - edges_b)
        only_b_edges = sorted(edges_b - edges_a)
        shared_edges = sorted(edges_a & edges_b)

        funcs_a = {f for f in blocks_a.values() if f != 0}
        funcs_b = {f for f in blocks_b.values() if f != 0}

        only_a_funcs = sorted(funcs_a - funcs_b)
        only_b_funcs = sorted(funcs_b - funcs_a)
        shared_funcs = sorted(funcs_a & funcs_b)

        return {
            "scenario_a": scenario_a,
            "scenario_b": scenario_b,
            "blocks_summary": {
                "scenario_a_total": len(blocks_a),
                "scenario_b_total": len(blocks_b),
                "only_in_a": len(only_a_blocks),
                "only_in_b": len(only_b_blocks),
                "shared": len(shared_blocks),
            },
            "edges_summary": {
                "scenario_a_total": len(edges_a),
                "scenario_b_total": len(edges_b),
                "only_in_a": len(only_a_edges),
                "only_in_b": len(only_b_edges),
                "shared": len(shared_edges),
            },
            "functions_summary": {
                "scenario_a_total": len(funcs_a),
                "scenario_b_total": len(funcs_b),
                "only_in_a": len(only_a_funcs),
                "only_in_b": len(only_b_funcs),
                "shared": len(shared_funcs),
            },
            "new_blocks_in_b": [format_va(b) for b in only_b_blocks[:100]],
            "new_edges_in_b": [f"{format_va(s)}->{format_va(d)}" for s, d in only_b_edges[:100]],
            "new_functions_in_b": [format_fun(f) for f in only_b_funcs[:50]],
        }

    def get_gaps(
        self,
        unimplemented: bool = False,
        unexecuted: bool = False,
        branches: bool = False,
        limit: int = 100,
    ) -> Dict[str, Any]:
        """Generate CV-06 executed-but-unimplemented and branch-gap report."""
        self._load_functions()
        conn = self.connect()
        cur = conn.cursor()

        # Find all functions with at least one covered block
        cur.execute("""
            SELECT func_va, COUNT(DISTINCT block_va) as blocks_count, SUM(hits) as total_hits
            FROM coverage_blocks
            WHERE func_va != 0
            GROUP BY func_va
        """)
        covered_func_stats = {row["func_va"]: (row["blocks_count"], row["total_hits"]) for row in cur.fetchall()}

        unimplemented_list = []
        unexecuted_list = []
        branch_gaps_list = []

        for fn in self._func_ranges:
            if fn.is_thunk:
                continue

            has_cov = fn.va in covered_func_stats
            is_ported = fn.port_status in ("verified", "ported", "stubbed")

            # 1. Executed in retail, but unported in openrecet (CV-06 critical gap)
            if has_cov and not is_ported:
                b_count, hits = covered_func_stats[fn.va]
                unimplemented_list.append({
                    "va": format_va(fn.va),
                    "name": fn.name,
                    "size": fn.size,
                    "port_status": fn.port_status,
                    "runtime_status": fn.runtime_status,
                    "covered_blocks": b_count,
                    "total_hits": hits,
                })

            # 2. Implemented in port, but never executed in captured retail runs
            if is_ported and not has_cov:
                unexecuted_list.append({
                    "va": format_va(fn.va),
                    "name": fn.name,
                    "size": fn.size,
                    "port_status": fn.port_status,
                    "runtime_status": fn.runtime_status,
                })

            # 3. Partial coverage / branch gaps
            if has_cov and branches:
                b_count, hits = covered_func_stats[fn.va]
                # If function is large and has very few covered blocks, flag as branch gap
                if fn.size > 64 and b_count < 3:
                    branch_gaps_list.append({
                        "va": format_va(fn.va),
                        "name": fn.name,
                        "size": fn.size,
                        "port_status": fn.port_status,
                        "covered_blocks": b_count,
                        "total_hits": hits,
                    })

        # Sort results
        unimplemented_list.sort(key=lambda f: f["total_hits"], reverse=True)
        unexecuted_list.sort(key=lambda f: f["size"], reverse=True)
        branch_gaps_list.sort(key=lambda f: f["size"], reverse=True)

        return {
            "summary": {
                "executed_unimplemented_count": len(unimplemented_list),
                "implemented_unexecuted_count": len(unexecuted_list),
                "branch_gaps_count": len(branch_gaps_list),
            },
            "unimplemented": unimplemented_list[:limit] if (unimplemented or not (unexecuted or branches)) else [],
            "unexecuted": unexecuted_list[:limit] if unexecuted else [],
            "branch_gaps": branch_gaps_list[:limit] if branches else [],
        }
    def prioritize_experiments(
        self,
        kind: str = "all",
        front: Optional[str] = None,
        limit: int = 20,
        min_readiness: Optional[str] = None,
        weights: Optional[Dict[str, float]] = None,
        include_explanations: bool = True,
    ) -> Dict[str, Any]:
        """Rank next-experiment candidate edges, functions, semantics, and scenarios (CV-07).

        Scoring incorporates:
          1. new_coverage_potential: estimated unvisited code volume (bytes/blocks) unlocked in subgraph
          2. new_semantic_potential: unobserved VM opcodes, transitions, content IDs, or audio IDs
          3. distance_from_certified: proximity/hop distance from already covered/proven execution frontiers
          4. port_readiness: implementation state (verified > ported > stubbed > referenced > discovered)
          5. proof_deficit: executed-unimplemented, executed-unproven, or port-debt presence
          6. runtime_cost_efficiency: estimated execution complexity and distance-to-verify
          7. active_front_affinity: dynamic boost for user-selected or active development focus front

        Returns ranked candidate list with transparent factor breakdown and human-readable explanation.
        """
        self._load_functions()
        conn = self.connect()
        cur = conn.cursor()

        # 1. Load call graph from re-index DB if available
        callees: Dict[int, Set[int]] = {}
        callers: Dict[int, Set[int]] = {}
        string_xrefs_map: Dict[int, Set[str]] = {}
        if self.re_index_path.exists():
            try:
                rconn = sqlite3.connect(str(self.re_index_path))
                rconn.row_factory = sqlite3.Row
                rcur = rconn.cursor()
                rcur.execute("SELECT caller_va, callee_va FROM calls")
                for row in rcur.fetchall():
                    c_va = int(row["caller_va"])
                    e_va = int(row["callee_va"])
                    callees.setdefault(c_va, set()).add(e_va)
                    callers.setdefault(e_va, set()).add(c_va)

                rcur.execute("SELECT func_va, string_name FROM string_xrefs")
                for row in rcur.fetchall():
                    f_va = int(row["func_va"])
                    s_name = str(row["string_name"])
                    string_xrefs_map.setdefault(f_va, set()).add(s_name)
                rconn.close()
            except Exception:
                pass

        # 2. Load port-debt.json if available
        debt_counts_by_fn: Dict[str, int] = {}
        if PORT_DEBT_JSON.exists():
            try:
                debt_data = json.loads(PORT_DEBT_JSON.read_text(encoding="utf-8"))
                debt_counts_by_fn = debt_data.get("counts", {}).get("by_engine_fn", {})
            except Exception:
                debt_counts_by_fn = {}

        # 3. Query covered functions, blocks, edges, and semantics
        cur.execute("""
            SELECT func_va, COUNT(DISTINCT block_va) as blocks_count, SUM(hits) as total_hits
            FROM coverage_blocks
            WHERE func_va != 0
            GROUP BY func_va
        """)
        covered_func_stats = {row["func_va"]: (row["blocks_count"], row["total_hits"]) for row in cur.fetchall()}
        covered_func_vas = set(covered_func_stats.keys())

        # Also add proven/verified functions to certified frontier
        for fn in self._func_ranges:
            if fn.runtime_status in ("proven", "identity-joined") or fn.port_status == "verified":
                covered_func_vas.add(fn.va)

        cur.execute("SELECT DISTINCT dimension, item_id FROM coverage_semantics")
        covered_semantics = {(row["dimension"], row["item_id"]) for row in cur.fetchall()}

        # 4. BFS shortest-path distance from certified / covered frontier
        dist: Dict[int, int] = {}
        if covered_func_vas:
            queue = list(covered_func_vas)
            for va in covered_func_vas:
                dist[va] = 0
            head = 0
            while head < len(queue):
                curr = queue[head]
                head += 1
                d = dist[curr]
                for next_callee in callees.get(curr, ()):
                    if next_callee not in dist:
                        dist[next_callee] = d + 1
                        queue.append(next_callee)
        else:
            # Fallback if no coverage runs: root is 0x4905a8 (WinMain) or first function
            root_vas = [fn.va for fn in self._func_ranges if fn.name in ("FUN_004905a8", "WinMain") or fn.va == 0x4905a8]
            if not root_vas and self._func_ranges:
                root_vas = [self._func_ranges[0].va]
            queue = list(root_vas)
            for r in root_vas:
                dist[r] = 0
            head = 0
            while head < len(queue):
                curr = queue[head]
                head += 1
                d = dist[curr]
                for next_callee in callees.get(curr, ()):
                    if next_callee not in dist:
                        dist[next_callee] = d + 1
                        queue.append(next_callee)

        # 5. Compute weights configuration
        active_weights = dict(CV07_DEFAULT_WEIGHTS)
        if weights:
            active_weights.update(weights)

        front_key = front.strip().lower() if front else None
        if not front_key:
            active_weights["active_front_affinity"] = 0.0

        total_w = sum(active_weights.values())
        norm_weights = {k: v / total_w for k, v in active_weights.items()} if total_w > 0 else {k: 1.0 / len(active_weights) for k in active_weights}

        # 6. Helper for front affinity
        front_kw_list = []
        if front_key:
            front_kw_list = FRONT_KEYWORDS.get(front_key, [front_key])

        def compute_front_affinity(fn_name: str, fn_va: int) -> Tuple[float, Optional[str]]:
            if not front_key:
                return 0.0, None
            name_lower = fn_name.lower()
            va_hex = f"0x{fn_va:06x}"
            fun_hex = f"fun_{fn_va:08x}"
            for kw in front_kw_list:
                kw_lower = kw.lower()
                if kw_lower in name_lower or kw_lower == va_hex or kw_lower == fun_hex:
                    return 1.0, front_key
                # check string xrefs
                for s_ref in string_xrefs_map.get(fn_va, ()):
                    if kw_lower in s_ref.lower():
                        return 0.9, front_key
                # check direct callers
                for caller_va in callers.get(fn_va, ()):
                    caller_fn = self._func_map.get(caller_va)
                    if caller_fn and (kw_lower in caller_fn.name.lower()):
                        return 0.75, front_key
            return 0.0, None

        # 7. Helper for transitive unvisited sub-tree calculation
        def compute_unvisited_subtree(start_va: int, max_depth: int = 4) -> Tuple[int, int]:
            visited = set()
            sub_q = [(start_va, 0)]
            total_bytes = 0
            total_funcs = 0
            while sub_q:
                curr_va, depth = sub_q.pop(0)
                if curr_va in visited or depth > max_depth:
                    continue
                visited.add(curr_va)
                if curr_va not in covered_func_vas:
                    fn_obj = self._func_map.get(curr_va)
                    if fn_obj:
                        total_bytes += fn_obj.size
                        total_funcs += 1
                for nxt in callees.get(curr_va, ()):
                    if nxt not in visited and nxt not in covered_func_vas:
                        sub_q.append((nxt, depth + 1))
            return total_bytes, total_funcs

        # Filter readiness
        min_lvl = READINESS_LEVELS.get(min_readiness.lower(), 0) if min_readiness else 0

        candidates: List[Dict[str, Any]] = []

        # ── Evaluate Function Candidates ─────────────────────────────────────
        if kind in ("all", "functions"):
            for fn in self._func_ranges:
                if fn.is_thunk:
                    continue

                fn_lvl = READINESS_LEVELS.get(fn.port_status.lower(), 0)
                if fn_lvl < min_lvl:
                    continue

                is_covered = fn.va in covered_func_stats
                b_count, hits = covered_func_stats.get(fn.va, (0, 0))
                hop_dist = dist.get(fn.va, 999)

                # 1. Coverage potential score
                unvis_bytes, unvis_funcs = compute_unvisited_subtree(fn.va)
                if is_covered:
                    # branch gap potential
                    if fn.size > 64 and b_count < 3:
                        cov_score = 0.6
                        cov_bytes = fn.size
                        cov_funcs = 1
                    else:
                        cov_score = 0.2
                        cov_bytes = 0
                        cov_funcs = 0
                else:
                    cov_score = min(1.0, (unvis_bytes / 25000.0) ** 0.5) if unvis_bytes > 0 else 0.1
                    cov_bytes = unvis_bytes
                    cov_funcs = unvis_funcs

                # 2. Semantic potential score
                s_refs = string_xrefs_map.get(fn.va, set())
                unseen_sem = sum(1 for s in s_refs if ("TUTO_" in s or "DLG_" in s or "s_" in s or "SCENE_" in s))
                sem_score = min(1.0, 0.2 + (unseen_sem * 0.2))

                # 3. Distance from certified score
                if hop_dist == 0:
                    dist_score = 1.0 if not is_covered or (fn.size > 64 and b_count < 3) else 0.5
                elif hop_dist == 1:
                    dist_score = 0.90
                elif hop_dist == 2:
                    dist_score = 0.70
                elif hop_dist == 3:
                    dist_score = 0.45
                elif hop_dist <= 6:
                    dist_score = max(0.15, 0.6 ** hop_dist)
                else:
                    dist_score = 0.05

                # 4. Port readiness score
                if fn.port_status in ("verified", "runtime_proven") or fn.runtime_status in ("proven", "identity-joined"):
                    ready_score = 1.0
                elif fn.port_status == "ported":
                    ready_score = 0.85
                elif fn.port_status == "stubbed":
                    ready_score = 0.70
                elif fn.port_status in ("source-referenced", "referenced"):
                    ready_score = 0.40
                else:
                    ready_score = 0.15

                # 5. Proof deficit score
                debt_cnt = debt_counts_by_fn.get(fn.name, 0)
                if is_covered and fn.port_status == "discovered":
                    deficit_score = 1.0  # critical gap (CV-06)
                    deficit_reason = "critical gap: executed in retail but unported"
                elif is_covered and fn.runtime_status != "proven":
                    deficit_score = 0.90
                    deficit_reason = "executed in retail but unproven"
                elif debt_cnt > 0:
                    deficit_score = 0.80
                    deficit_reason = f"has {debt_cnt} port-debt tag(s)"
                elif fn.port_status in ("ported", "stubbed") and not is_covered:
                    deficit_score = 0.55
                    deficit_reason = "implemented in port but unexecuted in retail"
                else:
                    deficit_score = 0.20
                    deficit_reason = "unexecuted and unported"

                # 6. Runtime cost efficiency
                cost_score = max(0.2, 1.0 - (hop_dist * 0.15)) if hop_dist < 999 else 0.1

                # 7. Active front affinity
                front_score, matching_front = compute_front_affinity(fn.name, fn.va)

                factor_scores = {
                    "new_coverage_potential": round(cov_score, 4),
                    "new_semantic_potential": round(sem_score, 4),
                    "distance_from_certified": round(dist_score, 4),
                    "port_readiness": round(ready_score, 4),
                    "proof_deficit": round(deficit_score, 4),
                    "runtime_cost_efficiency": round(cost_score, 4),
                    "active_front_affinity": round(front_score, 4),
                }

                composite = 100.0 * sum(factor_scores[k] * norm_weights[k] for k in norm_weights)

                # Explanation composition
                if include_explanations:
                    exp_parts = []
                    if hop_dist == 0:
                        if is_covered and fn.port_status == "discovered":
                            exp_parts.append("critical gap (executed in retail dynamic trace but unported)")
                        elif is_covered:
                            exp_parts.append("already executed in retail traces (has branch/proof gap)")
                        else:
                            exp_parts.append("at certified frontier")
                    elif hop_dist == 1:
                        caller_names = [self._func_map[c].name for c in callers.get(fn.va, ()) if c in covered_func_vas and c in self._func_map]
                        c_str = caller_names[0] if caller_names else "covered code"
                        exp_parts.append(f"immediate callee (1 call hop) of covered {c_str}")
                    elif hop_dist < 999:
                        exp_parts.append(f"{hop_dist} call hops from covered frontier")
                    else:
                        exp_parts.append("unreached in static call graph from covered frontier")

                    if matching_front:
                        exp_parts.append(f"matches active '{matching_front}' front")

                    exp_parts.append(f"port status is {fn.port_status}")

                    if cov_bytes > 0:
                        exp_parts.append(f"unlocks ~{cov_bytes}B unvisited logic across {cov_funcs} callee(s)")

                    if debt_cnt > 0:
                        exp_parts.append(f"retires {debt_cnt} port-debt tag(s)")
                    elif deficit_reason and "gap" not in exp_parts[0]:
                        exp_parts.append(deficit_reason)

                    explanation = f"{fn.name} ({format_va(fn.va)}): " + "; ".join(exp_parts) + "."
                else:
                    explanation = ""

                candidates.append({
                    "target": fn.name,
                    "candidate_type": "function",
                    "name": fn.name,
                    "va": format_va(fn.va),
                    "size": fn.size,
                    "port_status": fn.port_status,
                    "runtime_status": fn.runtime_status,
                    "distance": hop_dist,
                    "composite_score": round(composite, 2),
                    "factors": factor_scores,
                    "metrics": {
                        "unvisited_bytes": cov_bytes,
                        "unvisited_funcs": cov_funcs,
                        "debt_count": debt_cnt,
                        "hits": hits,
                    },
                    "explanation": explanation,
                })

        # ── Evaluate Call Edge Candidates ─────────────────────────────────────
        if kind in ("all", "edges"):
            for caller_va, callee_set in callees.items():
                if caller_va not in covered_func_vas:
                    continue
                caller_fn = self._func_map.get(caller_va)
                caller_name = caller_fn.name if caller_fn else format_fun(caller_va)

                for callee_va in callee_set:
                    callee_fn = self._func_map.get(callee_va)
                    if not callee_fn or callee_fn.is_thunk:
                        continue

                    is_callee_covered = callee_va in covered_func_stats
                    if is_callee_covered:
                        continue  # Edge already covered

                    unvis_bytes, unvis_funcs = compute_unvisited_subtree(callee_va)
                    cov_score = min(1.0, (unvis_bytes / 25000.0) ** 0.5) if unvis_bytes > 0 else 0.1
                    sem_score = 0.5
                    dist_score = 0.95  # frontier edge!

                    callee_lvl = READINESS_LEVELS.get(callee_fn.port_status.lower(), 0)
                    if callee_lvl < min_lvl:
                        continue

                    if callee_fn.port_status in ("verified", "runtime_proven"):
                        ready_score = 1.0
                    elif callee_fn.port_status == "ported":
                        ready_score = 0.85
                    elif callee_fn.port_status == "stubbed":
                        ready_score = 0.70
                    elif callee_fn.port_status in ("source-referenced", "referenced"):
                        ready_score = 0.40
                    else:
                        ready_score = 0.15

                    debt_cnt = debt_counts_by_fn.get(callee_fn.name, 0)
                    deficit_score = 0.85 if debt_cnt > 0 else (0.75 if callee_fn.port_status != "discovered" else 0.40)
                    cost_score = 0.90
                    front_score, matching_front = compute_front_affinity(callee_fn.name, callee_fn.va)

                    factor_scores = {
                        "new_coverage_potential": round(cov_score, 4),
                        "new_semantic_potential": round(sem_score, 4),
                        "distance_from_certified": round(dist_score, 4),
                        "port_readiness": round(ready_score, 4),
                        "proof_deficit": round(deficit_score, 4),
                        "runtime_cost_efficiency": round(cost_score, 4),
                        "active_front_affinity": round(front_score, 4),
                    }
                    composite = 100.0 * sum(factor_scores[k] * norm_weights[k] for k in norm_weights)

                    target_str = f"{format_va(caller_va)}->{format_va(callee_va)}"
                    edge_exp = f"Unexercised call edge from covered {caller_name} to {callee_fn.name} ({callee_fn.port_status}); unlocks ~{unvis_bytes}B across {unvis_funcs} unreached callee(s)."
                    if matching_front:
                        edge_exp += f" Matches active '{matching_front}' front."

                    candidates.append({
                        "target": target_str,
                        "candidate_type": "edge",
                        "name": f"{caller_name} -> {callee_fn.name}",
                        "va": format_va(callee_va),
                        "size": callee_fn.size,
                        "port_status": callee_fn.port_status,
                        "runtime_status": callee_fn.runtime_status,
                        "distance": 1,
                        "composite_score": round(composite, 2),
                        "factors": factor_scores,
                        "metrics": {
                            "unvisited_bytes": unvis_bytes,
                            "unvisited_funcs": unvis_funcs,
                            "debt_count": debt_cnt,
                            "caller": caller_name,
                        },
                        "explanation": edge_exp,
                    })

        # ── Evaluate Semantic Dimension Candidates ───────────────────────────
        if kind in ("all", "semantics"):
            for dim in KNOWN_SEMANTIC_DIMENSIONS:
                sample_unexercised: List[str] = []
                if dim == "transitions":
                    for t in ("SCENE_SHOP->SCENE_NIGHT", "SCENE_TOWN->SCENE_SHOP", "CONV_POSE_START", "CUSTOMER_SERVICE_ENTER", "TEXT_ANIM_START"):
                        if (dim, t) not in covered_semantics:
                            sample_unexercised.append(t)
                elif dim == "vm_operations":
                    for op in ("TUTO_OPEN_SHOP", "TUTO_SELL_ITEM", "DLG_BRANCH_CHOICE", "OP_0x40"):
                        if (dim, op) not in covered_semantics:
                            sample_unexercised.append(op)
                elif dim == "save_ops":
                    for s in ("SAVE:slot_1_commit", "SAVE:slot_2_verify", "SAVE:deserialize"):
                        if (dim, s) not in covered_semantics:
                            sample_unexercised.append(s)

                for item_id in sample_unexercised:
                    cov_score = 0.5
                    sem_score = 1.0  # new semantic dimension item
                    dist_score = 0.70
                    ready_score = 0.70
                    deficit_score = 0.75
                    cost_score = 0.85
                    front_score = 0.80 if front_key and (front_key in dim or front_key in item_id.lower()) else 0.0

                    factor_scores = {
                        "new_coverage_potential": round(cov_score, 4),
                        "new_semantic_potential": round(sem_score, 4),
                        "distance_from_certified": round(dist_score, 4),
                        "port_readiness": round(ready_score, 4),
                        "proof_deficit": round(deficit_score, 4),
                        "runtime_cost_efficiency": round(cost_score, 4),
                        "active_front_affinity": round(front_score, 4),
                    }
                    composite = 100.0 * sum(factor_scores[k] * norm_weights[k] for k in norm_weights)

                    candidates.append({
                        "target": f"{dim}:{item_id}",
                        "candidate_type": "semantics",
                        "name": f"{dim}:{item_id}",
                        "va": None,
                        "size": 0,
                        "port_status": "unexercised",
                        "runtime_status": None,
                        "distance": 2,
                        "composite_score": round(composite, 2),
                        "factors": factor_scores,
                        "metrics": {
                            "dimension": dim,
                            "item_id": item_id,
                        },
                        "explanation": f"Unobserved semantic dimension event {dim}:{item_id} (never recorded in dynamic coverage runs).",
                    })

        # ── Evaluate Scenario Candidates ─────────────────────────────────────
        if kind in ("all", "scenarios") and SCENARIOS_DIR.exists():
            for s_yaml in sorted(SCENARIOS_DIR.glob("*/scenario.yaml")):
                val_rep = self.validate_scenario_declarations(s_yaml)
                s_name = val_rep["scenario"]
                unmet = val_rep.get("total_unmet", 0)
                declared = val_rep.get("total_declared", 0)
                if unmet > 0:
                    cov_score = min(1.0, unmet / 10.0)
                    sem_score = 0.90
                    dist_score = 0.90
                    ready_score = 0.90
                    deficit_score = 0.95
                    cost_score = 0.75
                    front_score = 1.0 if front_key and (front_key in s_name.lower()) else 0.0

                    factor_scores = {
                        "new_coverage_potential": round(cov_score, 4),
                        "new_semantic_potential": round(sem_score, 4),
                        "distance_from_certified": round(dist_score, 4),
                        "port_readiness": round(ready_score, 4),
                        "proof_deficit": round(deficit_score, 4),
                        "runtime_cost_efficiency": round(cost_score, 4),
                        "active_front_affinity": round(front_score, 4),
                    }
                    composite = 100.0 * sum(factor_scores[k] * norm_weights[k] for k in norm_weights)

                    candidates.append({
                        "target": s_name,
                        "candidate_type": "scenario",
                        "name": s_name,
                        "va": None,
                        "size": 0,
                        "port_status": "declared",
                        "runtime_status": None,
                        "distance": 1,
                        "composite_score": round(composite, 2),
                        "factors": factor_scores,
                        "metrics": {
                            "unmet_expectations": unmet,
                            "declared_expectations": declared,
                        },
                        "explanation": f"Scenario {s_name} has {unmet}/{declared} declared expectations unsatisfied in the dynamic coverage atlas.",
                    })

        # Sort by composite_score descending, then by distance ascending, then by unvisited bytes descending
        candidates.sort(
            key=lambda c: (
                c["composite_score"],
                -c["distance"],
                c.get("metrics", {}).get("unvisited_bytes", 0),
            ),
            reverse=True,
        )

        # Assign ranks
        for idx, cand in enumerate(candidates[:limit], start=1):
            cand["rank"] = idx

        return {
            "version": CV07_POLICY_VERSION,
            "policy": CV07_POLICY_VERSION,
            "weights": norm_weights,
            "raw_weights": active_weights,
            "active_front": front,
            "kind_filter": kind,
            "min_readiness_filter": min_readiness,
            "total_candidates_evaluated": len(candidates),
            "candidates_returned": min(len(candidates), limit),
            "candidates": candidates[:limit],
        }


    def validate_scenario_declarations(
        self,
        scenario_target: Path | str | Dict[str, Any],
    ) -> Dict[str, Any]:
        """Validate a scenario's parity contract and declared coverage expectations (CV-04)."""
        scenario_name = "unknown"
        data: Dict[str, Any] = {}

        if isinstance(scenario_target, (Path, str)):
            p = Path(scenario_target)
            if p.is_dir():
                p = p / "scenario.yaml"
            if not p.exists():
                return {
                    "scenario": p.name,
                    "valid": False,
                    "error": f"Scenario file not found: {p}",
                }
            scenario_name = p.parent.name
            if yaml is not None:
                data = yaml.safe_load(p.read_text(encoding="utf-8")) or {}
            else:
                # Basic JSON fallback if YAML parser unavailable
                try:
                    data = json.loads(p.read_text(encoding="utf-8"))
                except Exception:
                    data = {}
        elif isinstance(scenario_target, dict):
            data = scenario_target
            scenario_name = data.get("scenario", "dict_scenario")

        # 1. Validate against contract schema if opted in (schema_version == 2)
        schema_valid = True
        schema_errors = []
        if data.get("schema_version") == 2 and "proof" in data:
            if CONTRACT_SCHEMA_PATH.exists() and Draft202012Validator is not None:
                try:
                    schema = json.loads(CONTRACT_SCHEMA_PATH.read_text(encoding="utf-8"))
                    validator = Draft202012Validator(schema)
                    errs = list(validator.iter_errors(data))
                    if errs:
                        schema_valid = False
                        schema_errors = [e.message for e in errs]
                except Exception as ex:
                    schema_errors.append(f"Schema validation error: {ex}")

        # 2. Extract coverage expectations
        exp = data.get("coverage_expectations") or {}
        declared_dims: Dict[str, List[str]] = {}
        for k, v in exp.items():
            if isinstance(v, list):
                declared_dims[k] = [str(x) for x in v]

        # 3. Validate ID formats fail-closed & check against coverage atlas
        conn = self.connect()
        cur = conn.cursor()

        invalid_ids = []
        dim_reports = {}
        total_declared = 0
        total_satisfied = 0
        total_unmet = 0

        for dim, items in declared_dims.items():
            dim_declared = len(items)
            total_declared += dim_declared
            satisfied = []
            unmet = []

            for item_id in items:
                is_valid, err_msg = validate_semantic_item_id(dim, item_id)
                if not is_valid:
                    invalid_ids.append({
                        "dimension": dim,
                        "item_id": item_id,
                        "error": err_msg,
                    })
                    unmet.append(item_id)
                    continue

                # Query atlas for hits
                if dim == "functions":
                    f_va = parse_va(item_id)
                    cur.execute("SELECT SUM(hits) FROM coverage_blocks WHERE func_va = ?", (f_va,))
                    h = cur.fetchone()[0]
                    if h and h > 0:
                        satisfied.append(item_id)
                    else:
                        unmet.append(item_id)
                elif dim == "blocks":
                    b_va = parse_va(item_id)
                    cur.execute("SELECT SUM(hits) FROM coverage_blocks WHERE block_va = ?", (b_va,))
                    h = cur.fetchone()[0]
                    if h and h > 0:
                        satisfied.append(item_id)
                    else:
                        unmet.append(item_id)
                else:
                    cur.execute("""
                        SELECT SUM(hits) FROM coverage_semantics
                        WHERE dimension = ? AND item_id = ?
                    """, (dim, item_id))
                    h = cur.fetchone()[0]
                    if h and h > 0:
                        satisfied.append(item_id)
                    else:
                        unmet.append(item_id)

            total_satisfied += len(satisfied)
            total_unmet += len(unmet)

            dim_reports[dim] = {
                "declared_count": dim_declared,
                "satisfied_count": len(satisfied),
                "unmet_count": len(unmet),
                "satisfied": satisfied,
                "unmet": unmet,
            }

        is_valid = schema_valid and (len(invalid_ids) == 0)

        return {
            "scenario": scenario_name,
            "valid": is_valid,
            "schema_valid": schema_valid,
            "schema_errors": schema_errors,
            "has_declarations": total_declared > 0,
            "total_declared": total_declared,
            "total_satisfied": total_satisfied,
            "total_unmet": total_unmet,
            "invalid_ids": invalid_ids,
            "dimensions": dim_reports,
        }

    def audit_all_scenarios(
        self,
        scenarios_dir: Path = SCENARIOS_DIR,
    ) -> Dict[str, Any]:
        """Audit all scenario YAMLs in the repository for CV-04 declarations and compliance."""
        if not scenarios_dir.exists():
            return {"error": f"Scenarios directory not found: {scenarios_dir}"}

        scenario_files = sorted(scenarios_dir.glob("*/scenario.yaml"))
        scenarios = []
        total_declared_all = 0
        total_satisfied_all = 0
        opted_in_count = 0
        invalid_scenarios = []

        for sf in scenario_files:
            rep = self.validate_scenario_declarations(sf)
            if not rep.get("valid", False):
                invalid_scenarios.append(rep["scenario"])
            if rep.get("schema_valid") and rep.get("has_declarations"):
                opted_in_count += 1

            total_declared_all += rep.get("total_declared", 0)
            total_satisfied_all += rep.get("total_satisfied", 0)
            scenarios.append(rep)

        return {
            "total_scenarios": len(scenario_files),
            "opted_in_scenarios": opted_in_count,
            "invalid_scenarios_count": len(invalid_scenarios),
            "invalid_scenarios": invalid_scenarios,
            "total_declared_expectations": total_declared_all,
            "total_satisfied_expectations": total_satisfied_all,
            "scenarios": scenarios,
        }

    def export_atlas(self) -> Dict[str, Any]:
        """Export full aggregated coverage atlas dictionary (CV-02/03/05)."""
        conn = self.connect()
        cur = conn.cursor()

        cur.execute("SELECT * FROM coverage_runs ORDER BY scenario ASC")
        runs = [dict(row) for row in cur.fetchall()]

        cur.execute("""
            SELECT block_va, func_va, SUM(hits) as total_hits, COUNT(DISTINCT run_id) as run_count
            FROM coverage_blocks
            GROUP BY block_va
            ORDER BY block_va ASC
        """)
        blocks = [{"va": format_va(row["block_va"]), "func_va": format_va(row["func_va"]) if row["func_va"] else None, "hits": row["total_hits"], "runs": row["run_count"]} for row in cur.fetchall()]

        cur.execute("""
            SELECT src_va, dst_va, SUM(hits) as total_hits, COUNT(DISTINCT run_id) as run_count
            FROM coverage_edges
            GROUP BY src_va, dst_va
            ORDER BY src_va ASC
        """)
        edges = [{"src": format_va(row["src_va"]), "dst": format_va(row["dst_va"]), "hits": row["total_hits"], "runs": row["run_count"]} for row in cur.fetchall()]

        cur.execute("""
            SELECT dimension, item_id, SUM(hits) as total_hits, MIN(first_frame) as first_frame,
                   COUNT(DISTINCT run_id) as runs_count
            FROM coverage_semantics
            GROUP BY dimension, item_id
            ORDER BY dimension ASC, total_hits DESC
        """)
        semantics: Dict[str, List[Dict[str, Any]]] = {}
        for row in cur.fetchall():
            dim = row["dimension"]
            if dim not in semantics:
                semantics[dim] = []
            semantics[dim].append({
                "item_id": row["item_id"],
                "total_hits": row["total_hits"],
                "first_frame": row["first_frame"],
                "runs": row["runs_count"],
            })

        return {
            "version": "1.1.0",
            "schema": "parity-coverage-atlas-v1",
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "runs": runs,
            "unique_blocks": len(blocks),
            "unique_edges": len(edges),
            "blocks": blocks,
            "edges": edges,
            "semantics": semantics,
        }


# ─── CLI Subcommands ────────────────────────────────────────────────────────


def cmd_import(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    path = Path(args.path)
    if not path.exists():
        sys.stderr.write(f"File not found: {path}\n")
        return 1

    res = atlas.import_run(path, scenario=args.scenario or "")
    if args.json:
        print(json.dumps(res, indent=2))
    else:
        print(f"Imported coverage artifact: {path}")
        print(f"  Scenario:  {res['scenario']}")
        print(f"  Run ID:    {res['run_id']}")
        print(f"  Blocks:    {res['blocks_imported']}")
        print(f"  Edges:     {res['edges_imported']}")
        print(f"  Semantics: {res.get('semantics_imported', 0)}")
        print(f"  SHA256:    {res['artifact_hash']}")
    return 0


def cmd_summary(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    st = atlas.get_summary(scenario=getattr(args, "scenario", None))
    if args.json:
        print(json.dumps(st, indent=2))
    else:
        print("OpenRecet Dynamic Coverage Atlas Summary (CV-02/03/05/08):")
        print(f"  Total Runs:          {st['total_runs']} ({st['total_scenarios']} distinct scenarios)")
        print(f"  Unique Blocks:       {st['unique_blocks_covered']}")
        print(f"  Unique Edges:        {st['unique_edges_covered']}")
        print(f"  Functions Reached:   {st['touched_functions']}/{st['total_functions_in_index']} ({st['function_coverage_ratio']*100:.1f}%)")

        calib = st.get("cv08_calibration", {})
        if calib.get("calibrated"):
            print(f"  CV-08 Calibration:   {calib['verdict']} [Mode: {calib['collection_mode']}, Confidence: {calib['confidence_score']:.2f} ({calib['confidence_band']})]")
            print(f"  Global Claim:        {calib['coverage_claim']}")
        else:
            print(f"  CV-08 Calibration:   ⚠️  {calib.get('verdict', 'UNCALIBRATED')} (Run 'coverage_atlas.py calibrate')")
            print(f"  Global Claim:        {calib.get('coverage_claim', 'UNCALIBRATED')}")

        if st.get("semantic_dimensions"):
            print("\n  Semantic Dimensions (CV-05):")
            for dim_name, dim_data in sorted(st["semantic_dimensions"].items()):
                print(f"    {dim_name:<20} -> {dim_data['unique_items']:>5} items ({dim_data['total_hits']} hits)")

        if st["scenarios"]:
            print("\n  Per-Scenario Coverage:")
            for sc_name, sc_data in sorted(st["scenarios"].items()):
                print(f"    {sc_name:<30} -> {sc_data['unique_blocks']:>5} blocks, {sc_data['unique_edges']:>5} edges")
    return 0


def cmd_calibrate(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    call_trace_vas = None
    if getattr(args, "cross_check_call_trace", None):
        call_trace_vas = args.cross_check_call_trace

    res = atlas.calibrate_coverage(
        scenario=getattr(args, "scenario", None),
        run_id=getattr(args, "run_id", None),
        mode=getattr(args, "mode", CollectionMode.DYNAMIC_STALKER),
        call_trace_vas=call_trace_vas,
        min_confidence=getattr(args, "min_confidence", 0.85),
        save_to_db=not getattr(args, "no_save", False),
    )

    if args.json:
        print(json.dumps(res, indent=2))
        return 0 if res["verdict"] in (CalibrationVerdict.PASS, CalibrationVerdict.CONDITIONAL_PASS) else 1

    if getattr(args, "markdown", False):
        print(f"## CV-08 Coverage Truth Calibration Report ({res['version']})\n")
        print(f"- **Verdict:** `{res['verdict']}`")
        print(f"- **Collection Mode:** `{res['collection_mode']}`")
        print(f"- **Confidence Score:** `{res['confidence_score']:.2f}` (`{res['confidence_band']}`)")
        print(f"- **Scenario Target:** `{res['scenario']}` ({res['runs_evaluated']} run(s) evaluated)\n")
        print("### Calibration Factor Breakdown\n")
        print("| Factor | Score | Weight |")
        print("|:-------|------:|:------:|")
        print(f"| Collector Integrity | {res['factors']['collector_integrity']:.2f} | 0.25 |")
        print(f"| Cross-Collector Agreement | {res['factors']['cross_collector_agreement']:.2f} | 0.30 |")
        print(f"| CFG Structural Validity | {res['factors']['cfg_structural_validity']:.2f} | 0.25 |")
        print(f"| Repeat-Run Determinism | {res['factors']['determinism']:.2f} | 0.20 |")
        print(f"| Blind Spot Penalty | -{res['factors']['blind_spot_penalty']:.2f} | — |\n")

        if res.get("blind_spots"):
            print(f"### Documented Blind Spots ({len(res['blind_spots'])})\n")
            print("| VA | Function | Kind | Severity | Description |")
            print("|:---|:---------|:-----|:---------|:------------|")
            for bs in res["blind_spots"][:15]:
                print(f"| `{bs['va']}` | `{bs['function']}` | `{bs['kind']}` | {bs['severity']} | {bs['description']} |")
            if len(res["blind_spots"]) > 15:
                print(f"\n*... and {len(res['blind_spots']) - 15} more blind spots.*")

        print("\n### Explanations & Notes\n")
        for exp in res["explanations"]:
            print(f"- {exp}")
        return 0 if res["verdict"] in (CalibrationVerdict.PASS, CalibrationVerdict.CONDITIONAL_PASS) else 1

    # Table output
    status_symbol = "✅ PASS" if res["verdict"] == CalibrationVerdict.PASS else ("⚠️ CONDITIONAL" if res["verdict"] == CalibrationVerdict.CONDITIONAL_PASS else "❌ FAIL")
    print(f"OpenRecet CV-08 Coverage Truth Calibration ({res['version']}): {status_symbol}")
    print(f"  Scenario Target:       {res['scenario']} ({res['runs_evaluated']} run(s))")
    print(f"  Collection Mode:       {res['collection_mode']}")
    print(f"  Confidence Score:      {res['confidence_score']:.2f} / 1.00 [{res['confidence_band']}] (threshold: {res['min_confidence_threshold']:.2f})")
    print(f"  Verdict:               {res['verdict']}")
    print("\n  Factors Breakdown:")
    print(f"    Collector Integrity:       {res['factors']['collector_integrity']:.2f} (lost events: {res['metrics']['lost_events']})")
    print(f"    Cross-Collector Agreement: {res['factors']['cross_collector_agreement']:.2f} (missing in stalker: {res['metrics']['cross_collector_missing_count']})")
    print(f"    CFG Structural Validity:   {res['factors']['cfg_structural_validity']:.2f} (unique blocks: {res['metrics']['unique_blocks']})")
    print(f"    Repeat-Run Determinism:    {res['factors']['determinism']:.2f}")
    print(f"    Blind Spot Penalty:       -{res['factors']['blind_spot_penalty']:.2f} ({res['metrics']['blind_spots_count']} blind spot(s))")

    if res.get("missing_in_stalker"):
        print("\n  ⚠️  Missing in Stalker (called in trace but unobserved in blocks):")
        for fn in res["missing_in_stalker"][:10]:
            print(f"    - {fn}")

    if res.get("blind_spots"):
        print(f"\n  Documented Instrumentation Blind Spots ({len(res['blind_spots'])}):")
        for bs in res["blind_spots"][:10]:
            print(f"    [{bs['kind']}] {bs['function']} @ {bs['va']}: {bs['description']}")
        if len(res["blind_spots"]) > 10:
            print(f"    ... and {len(res['blind_spots']) - 10} more")

    print("\n  Explanations:")
    for exp in res["explanations"]:
        print(f"    • {exp}")

    return 0 if res["verdict"] in (CalibrationVerdict.PASS, CalibrationVerdict.CONDITIONAL_PASS) else 1


def cmd_function(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    fn_cov = atlas.get_function_coverage(args.target)
    if not fn_cov:
        sys.stderr.write(f"Function not found: {args.target}\n")
        return 1

    if args.json:
        print(json.dumps(fn_cov, indent=2))
    else:
        status_str = f"[{fn_cov['port_status']}]"
        cov_str = "COVERED" if fn_cov["is_covered"] else "UNEXECUTED"
        print(f"{fn_cov['name']} @ {fn_cov['va']} ({fn_cov['size']}B) {status_str} — {cov_str}")
        print(f"  Total Hits:      {fn_cov['total_hits']}")
        print(f"  Unique Blocks:   {fn_cov['unique_blocks_count']}")
        print(f"  Internal Edges:  {fn_cov['internal_edges_count']}")
        print(f"  Incoming Edges:  {fn_cov['incoming_edges_count']}")
        print(f"  Outgoing Edges:  {fn_cov['outgoing_edges_count']}")

        if fn_cov["blocks"]:
            print("  Blocks:")
            for b in fn_cov["blocks"][:15]:
                print(f"    {b['va']}: {b['hits']} hits ({b['runs']} runs)")
            if len(fn_cov["blocks"]) > 15:
                print(f"    ... and {len(fn_cov['blocks']) - 15} more blocks")

        if fn_cov["internal_edges"]:
            print("  Internal Edges:")
            for e in fn_cov["internal_edges"][:10]:
                print(f"    {e['src']} -> {e['dst']}: {e['hits']} hits")
    return 0


def cmd_semantics(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    if args.dimension:
        items = atlas.get_dimension_items(args.dimension, scenario=args.scenario)
        if args.json:
            print(json.dumps(items, indent=2))
        else:
            print(f"Semantic Coverage Dimension: {args.dimension} ({len(items)} items)")
            for it in items[:args.limit]:
                print(f"  {it['item_id']:<35} -> {it['total_hits']:>5} hits (first frame: {it['first_frame']})")
            if len(items) > args.limit:
                print(f"  ... and {len(items) - args.limit} more items")
    else:
        summary = atlas.get_semantic_summary()
        if args.json:
            print(json.dumps(summary, indent=2))
        else:
            print("Semantic Coverage Dimensions Summary (CV-05):")
            for dim, d in sorted(summary.items()):
                print(f"  {dim:<20} -> {d['unique_items']:>5} unique items, {d['total_hits']:>7} hits across {d['runs_count']} run(s)")
    return 0


def cmd_validate_scenario(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    rep = atlas.validate_scenario_declarations(args.path)
    if args.json:
        print(json.dumps(rep, indent=2))
    else:
        status_str = "VALID" if rep.get("valid") else "INVALID"
        print(f"Scenario Declarations Validation (CV-04): {rep['scenario']} — {status_str}")
        print(f"  Schema Valid:     {rep.get('schema_valid')}")
        print(f"  Total Declared:   {rep.get('total_declared', 0)}")
        print(f"  Satisfied:        {rep.get('total_satisfied', 0)}")
        print(f"  Unmet:            {rep.get('total_unmet', 0)}")

        if rep.get("invalid_ids"):
            print("\n  ⚠️ Invalid Identifiers:")
            for inv in rep["invalid_ids"]:
                print(f"    [{inv['dimension']}] {inv['item_id']}: {inv['error']}")

        if rep.get("dimensions"):
            print("\n  Declared Dimension Breakdown:")
            for dim, dinfo in rep["dimensions"].items():
                print(f"    {dim:<18}: {dinfo['satisfied_count']}/{dinfo['declared_count']} satisfied")
                if dinfo["unmet"]:
                    for u in dinfo["unmet"][:5]:
                        print(f"      - unmet: {u}")
    return 0 if rep.get("valid") else 1


def cmd_audit_scenarios(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    scen_dir = Path(args.scenarios_dir) if args.scenarios_dir else SCENARIOS_DIR
    res = atlas.audit_all_scenarios(scenarios_dir=scen_dir)
    if args.json:
        print(json.dumps(res, indent=2))
    else:
        print("Scenario Declarations Repository Audit (CV-04):")
        print(f"  Total Scenarios:              {res['total_scenarios']}")
        print(f"  Opted-in with Declarations:   {res['opted_in_scenarios']}")
        print(f"  Total Declared Expectations:  {res['total_declared_expectations']}")
        print(f"  Total Satisfied in Atlas:     {res['total_satisfied_expectations']}")
        print(f"  Invalid Scenarios:            {res['invalid_scenarios_count']}")

        if res["invalid_scenarios"]:
            print(f"\n  ⚠️ Scenarios failing validation:")
            for sc in res["invalid_scenarios"]:
                print(f"    - {sc}")
    return 0 if res.get("invalid_scenarios_count", 0) == 0 else 1


def cmd_delta(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    delta = atlas.get_scenario_delta(args.scenario_a, args.scenario_b)
    if args.json:
        print(json.dumps(delta, indent=2))
    else:
        print(f"Coverage Delta: {args.scenario_a} vs {args.scenario_b}")
        print(f"  Blocks:    {args.scenario_a}={delta['blocks_summary']['scenario_a_total']} | {args.scenario_b}={delta['blocks_summary']['scenario_b_total']} | only in A={delta['blocks_summary']['only_in_a']} | only in B={delta['blocks_summary']['only_in_b']} | shared={delta['blocks_summary']['shared']}")
        print(f"  Edges:     {args.scenario_a}={delta['edges_summary']['scenario_a_total']} | {args.scenario_b}={delta['edges_summary']['scenario_b_total']} | only in A={delta['edges_summary']['only_in_a']} | only in B={delta['edges_summary']['only_in_b']} | shared={delta['edges_summary']['shared']}")
        print(f"  Functions: {args.scenario_a}={delta['functions_summary']['scenario_a_total']} | {args.scenario_b}={delta['functions_summary']['scenario_b_total']} | only in A={delta['functions_summary']['only_in_a']} | only in B={delta['functions_summary']['only_in_b']} | shared={delta['functions_summary']['shared']}")

        if delta["new_functions_in_b"]:
            print(f"\n  New Functions Reached in {args.scenario_b} ({len(delta['new_functions_in_b'])}):")
            for fn_name in delta["new_functions_in_b"][:20]:
                print(f"    + {fn_name}")
    return 0


def cmd_gaps(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    gaps = atlas.get_gaps(
        unimplemented=args.unimplemented,
        unexecuted=args.unexecuted,
        branches=args.branches,
        limit=args.limit,
    )
    if args.json:
        print(json.dumps(gaps, indent=2))
    else:
        print("CV-06 Parity Coverage Gap Analysis Report:")
        print(f"  Executed in retail but Unimplemented in port: {gaps['summary']['executed_unimplemented_count']}")
        print(f"  Implemented in port but Unexecuted in retail:  {gaps['summary']['implemented_unexecuted_count']}")
        print(f"  Functions with Branch/Block Gaps:             {gaps['summary']['branch_gaps_count']}")

        if gaps["unimplemented"]:
            print(f"\n  ★ Executed but Unimplemented Functions (Priority Ports):")
            for f in gaps["unimplemented"]:
                print(f"    {f['name']} @ {f['va']} ({f['size']:>4}B) [{f['port_status']}] — {f['covered_blocks']} blocks, {f['total_hits']} hits")

        if gaps["unexecuted"]:
            print(f"\n  Implemented but Unexecuted Functions (Lacking Runtime Proof):")
            for f in gaps["unexecuted"]:
                print(f"    {f['name']} @ {f['va']} ({f['size']:>4}B) [{f['port_status']}]")

        if gaps["branch_gaps"]:
            print(f"\n  Functions with Uncovered Branches:")
            for f in gaps["branch_gaps"]:
                print(f"    {f['name']} @ {f['va']} ({f['size']:>4}B) [{f['port_status']}] — only {f['covered_blocks']} blocks covered")
    return 0

def cmd_prioritize(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    custom_weights = None
    if args.weights:
        try:
            custom_weights = json.loads(args.weights)
        except Exception as e:
            sys.stderr.write(f"Invalid JSON for --weights: {e}\n")
            return 1

    res = atlas.prioritize_experiments(
        kind=args.kind,
        front=args.front,
        limit=args.limit,
        min_readiness=args.min_readiness,
        weights=custom_weights,
        include_explanations=True,
    )

    if args.json:
        print(json.dumps(res, indent=2))
        return 0

    if getattr(args, "markdown", False):
        print(f"## CV-07 Next-Experiment Prioritization Report ({res['policy']})\n")
        print(f"- **Active Front:** `{res['active_front'] or 'None'}`")
        print(f"- **Candidates Evaluated:** {res['total_candidates_evaluated']} (returning top {res['candidates_returned']})")
        print(f"- **Weights:** " + ", ".join(f"`{k}`={v:.2f}" for k, v in res["weights"].items()) + "\n")
        print("| Rank | Score | Type | Target | Port Status | Dist | Explanation |")
        print("|:----:|------:|:----:|:-------|:-----------:|:----:|:------------|")
        for c in res["candidates"]:
            print(f"| {c['rank']} | {c['composite_score']:.2f} | {c['candidate_type']} | `{c['target']}` | {c['port_status']} | {c['distance']} | {c['explanation']} |")
        return 0

    # Default table output
    print(f"OpenRecet CV-07 Next-Experiment Prioritizer ({res['policy']})")
    print(f"  Active Front:        {res['active_front'] or 'None'}")
    print(f"  Candidates Analyzed: {res['total_candidates_evaluated']} (showing top {res['candidates_returned']})")
    w_str = " ".join(f"{k.split('_')[0]}={v:.2f}" for k, v in res["weights"].items())
    print(f"  Weights:             {w_str}\n")

    print(f"{'Rank':<4}  {'Score':<6}  {'Type':<9}  {'Target / Name':<28}  {'Status':<12}  {'Dist':<4}  {'Explanation'}")
    print("─" * 105)
    for c in res["candidates"]:
        target_display = c["target"]
        if len(target_display) > 28:
            target_display = target_display[:25] + "..."
        exp_display = c["explanation"]
        if len(exp_display) > 60:
            exp_display = exp_display[:57] + "..."
        print(f"{c['rank']:<4}  {c['composite_score']:>6.2f}  {c['candidate_type']:<9}  {target_display:<28}  {c['port_status']:<12}  {c['distance']:<4}  {exp_display}")

    return 0


def cmd_export(atlas: CoverageAtlas, args: argparse.Namespace) -> int:
    exp = atlas.export_atlas()
    print(json.dumps(exp, indent=2 if args.pretty else None))
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parent = argparse.ArgumentParser(add_help=False)
    parent.add_argument("--json", action="store_true", help="output JSON")
    parent.add_argument("--db", type=Path, default=argparse.SUPPRESS, help=f"coverage database path (default: {COVERAGE_DB})")
    parent.add_argument("--re-index", type=Path, default=argparse.SUPPRESS, help=f"re_index database path (default: {RE_INDEX_DB})")
    ap = argparse.ArgumentParser(description=__doc__, parents=[parent], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp_imp = sub.add_parser("import", parents=[parent], help="ingest coverage.json artifact")
    sp_imp.add_argument("path", help="path to coverage.json")
    sp_imp.add_argument("--scenario", help="scenario name override")

    sp_sum = sub.add_parser("summary", parents=[parent], help="display global dynamic coverage statistics")

    sp_sum.add_argument("--scenario", help="filter summary for a specific scenario")

    sp_cal = sub.add_parser("calibrate", parents=[parent], help="calibrate dynamic coverage truth and compute confidence (CV-08)")
    sp_cal.add_argument("--scenario", help="target scenario to calibrate")
    sp_cal.add_argument("--run-id", help="specific run_id to calibrate")
    sp_cal.add_argument("--mode", choices=CollectionMode.ALL, default=CollectionMode.DYNAMIC_STALKER, help=f"collection mode (default: {CollectionMode.DYNAMIC_STALKER})")
    sp_cal.add_argument("--cross-check-call-trace", help="path to call trace JSONL or comma-separated VAs to cross-check")
    sp_cal.add_argument("--min-confidence", type=float, default=0.85, help="minimum confidence threshold for PASS verdict (default: 0.85)")
    sp_cal.add_argument("--markdown", action="store_true", help="output formatted markdown report")
    sp_cal.add_argument("--no-save", action="store_true", help="do not persist calibration record into database")
    sp_fn = sub.add_parser("function", parents=[parent], help="inspect detailed coverage for a function")
    sp_fn.add_argument("target", help="function VA or name (e.g. 0x4905a8 or FUN_004905a8)")

    sp_sem = sub.add_parser("semantics", parents=[parent], help="inspect semantic coverage dimensions (CV-05)")
    sp_sem.add_argument("--dimension", help="filter by specific semantic dimension")
    sp_sem.add_argument("--scenario", help="filter by specific scenario")
    sp_sem.add_argument("--limit", type=int, default=50, help="max items to display (default: 50)")

    sp_val = sub.add_parser("validate-scenario", parents=[parent], help="validate scenario coverage contract and declarations (CV-04)")
    sp_val.add_argument("path", help="path to scenario.yaml or scenario directory")

    sp_aud = sub.add_parser("audit-scenarios", parents=[parent], help="audit all scenario declarations in repo (CV-04)")
    sp_aud.add_argument("--scenarios-dir", help=f"scenarios directory (default: {SCENARIOS_DIR})")

    sp_delta = sub.add_parser("delta", parents=[parent], help="compute coverage delta between two scenarios")
    sp_delta.add_argument("scenario_a", help="scenario A name")
    sp_delta.add_argument("scenario_b", help="scenario B name")

    sp_gaps = sub.add_parser("gaps", parents=[parent], help="CV-06 gap analysis report")
    sp_gaps.add_argument("--unimplemented", action="store_true", help="list executed-but-unimplemented functions")
    sp_gaps.add_argument("--unexecuted", action="store_true", help="list implemented-but-unexecuted functions")
    sp_gaps.add_argument("--branches", action="store_true", help="list functions with partial branch coverage")
    sp_gaps.add_argument("--limit", type=int, default=50, help="max items to display (default: 50)")

    sp_prio = sub.add_parser("prioritize", parents=[parent], help="CV-07 next-experiment prioritizer")
    sp_prio.add_argument("--kind", choices=["all", "functions", "edges", "semantics", "scenarios"], default="all", help="candidate kinds to evaluate (default: all)")
    sp_prio.add_argument("--front", help="active development front focus (e.g. customer_service, day2_transition, shop_loop, save_system, dungeon)")
    sp_prio.add_argument("--min-readiness", choices=["discovered", "referenced", "stubbed", "ported", "verified"], help="minimum port readiness filter")
    sp_prio.add_argument("--weights", help="JSON string overriding scoring weights")
    sp_prio.add_argument("--markdown", action="store_true", help="output formatted markdown table")
    sp_prio.add_argument("--limit", type=int, default=20, help="max candidates to display (default: 20)")

    sp_exp = sub.add_parser("export", parents=[parent], help="dump aggregated coverage atlas as JSON")
    sp_exp.add_argument("--pretty", action="store_true", help="pretty-print JSON output")

    args = ap.parse_args(argv)
    db_path = getattr(args, "db", COVERAGE_DB)
    re_index_path = getattr(args, "re_index", RE_INDEX_DB)
    atlas = CoverageAtlas(db_path=db_path, re_index_path=re_index_path)
    handlers = {
        "import": cmd_import,
        "summary": cmd_summary,
        "calibrate": cmd_calibrate,
        "function": cmd_function,
        "semantics": cmd_semantics,
        "validate-scenario": cmd_validate_scenario,
        "audit-scenarios": cmd_audit_scenarios,
        "delta": cmd_delta,
        "gaps": cmd_gaps,
        "export": cmd_export,
        "prioritize": cmd_prioritize,
    }

    try:
        return handlers[args.cmd](atlas, args)
    finally:
        atlas.close()


if __name__ == "__main__":
    sys.exit(main())
