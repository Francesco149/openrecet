#!/usr/bin/env python3
"""tools/parity/corpus_store.py — CC-02 call capsule corpus storage and fixture loader.

Manages persistent capsule fixtures stored in docs/schemas/fixtures/capsules/.
Provides discovery, disk export/import, schema validation, and native differential replay.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

from .capsule import (
    CallCapsule,
    CapsuleError,
    CapsuleReplayResult,
    validate_capsule,
)
from .capsule_capture import get_cc01_canonical_fixtures
from .host_diff_adapter import NativeHostDiffAdapter

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_CAPSULE_DIR = REPO_ROOT / "docs" / "schemas" / "fixtures" / "capsules"


class CorpusStore:
    """Manages disk serialization, indexing, and batch replay of call capsules."""

    def __init__(self, fixture_dir: Optional[Path] = None):
        self.fixture_dir = fixture_dir or DEFAULT_CAPSULE_DIR
        self.fixture_dir.mkdir(parents=True, exist_ok=True)

    def save_fixture(self, capsule: CallCapsule, name: Optional[str] = None) -> Path:
        """Saves a CallCapsule to disk as a formatted JSON fixture."""
        validate_capsule(capsule)
        file_name = f"{name}.json" if name else f"capsule-{capsule.capsule_id[:16]}.json"
        out_path = self.fixture_dir / file_name
        data = capsule.to_dict()
        out_path.write_text(json.dumps(data, indent=2) + "\n")
        return out_path

    def load_fixture(self, path_or_name: Union[str, Path]) -> CallCapsule:
        """Loads a CallCapsule from disk and validates its schema."""
        p = Path(path_or_name)
        if not p.is_file():
            # Try resolving relative to fixture_dir
            candidate = self.fixture_dir / (f"{path_or_name}.json" if not str(path_or_name).endswith(".json") else str(path_or_name))
            if candidate.is_file():
                p = candidate
            else:
                raise CapsuleError(f"Fixture file not found: {path_or_name}")

        data = json.loads(p.read_text())
        capsule = CallCapsule.from_dict(data)
        validate_capsule(capsule)
        return capsule

    def load_all_fixtures(self) -> Dict[str, CallCapsule]:
        """Discovers and loads all *.json fixtures in fixture_dir."""
        fixtures: Dict[str, CallCapsule] = {}
        for p in sorted(self.fixture_dir.glob("*.json")):
            try:
                name = p.stem
                capsule = self.load_fixture(p)
                fixtures[name] = capsule
            except Exception as exc:
                raise CapsuleError(f"Failed to load fixture {p.name}: {exc}") from exc
        return fixtures

    def export_canonical_fixtures(self) -> List[Path]:
        """Exports the 5 canonical real engine capsules to disk."""
        canonicals = get_cc01_canonical_fixtures()
        paths = []
        for name, cap in canonicals.items():
            p = self.save_fixture(cap, name=name)
            paths.append(p)
        return paths

    def verify_corpus_against_native(self) -> Dict[str, CapsuleReplayResult]:
        """Replays all capsules in the store against the native C host diff adapter."""
        fixtures = self.load_all_fixtures()
        if not fixtures:
            # Export canonicals if empty
            self.export_canonical_fixtures()
            fixtures = self.load_all_fixtures()

        results: Dict[str, CapsuleReplayResult] = {}
        for name, cap in fixtures.items():
            res = NativeHostDiffAdapter.execute_native(cap)
            results[name] = res
        return results
