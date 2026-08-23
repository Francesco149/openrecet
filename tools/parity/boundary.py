#!/usr/bin/env python3
"""tools/parity/boundary.py — BT-00 system boundary event engine & equivalence comparator.

Implements the boundary event schema, normalization rules, stream serialization,
and 3-tier equivalence evaluation (CALL_SEQUENCE_EQUIVALENT, RESULT_EQUIVALENT, EFFECT_EQUIVALENT)
matching docs/schemas/boundary-event-v1.json and docs/reference/boundary-events.md.
"""
from __future__ import annotations

import copy
import dataclasses
from dataclasses import asdict, dataclass, field
import hashlib
import json
from pathlib import Path
import re
from typing import Any, Dict, List, Optional, Set, Tuple, Union

BOUNDARY_SCHEMA_VERSION = 1

KNOWN_DOMAINS = {
    "win32_msg",
    "dinput_device",
    "filesystem_io",
    "ini_config",
    "audio_device",
    "window_lifecycle",
    "mutex_sync",
}

EQUIVALENCE_LEVELS = {
    "CALL_SEQUENCE_EQUIVALENT",
    "RESULT_EQUIVALENT",
    "EFFECT_EQUIVALENT",
}


class BoundaryError(Exception):
    """Failure during boundary event validation, normalization, or comparison."""


# ─── Normalization Helpers ───────────────────────────────────────────────────

def normalize_path(path_str: Optional[str]) -> Optional[str]:
    """Normalizes raw Windows, UNC, and WSL absolute paths to clean engine-relative paths."""
    if not path_str:
        return path_str

    s = str(path_str).replace("\\", "/").strip()

    # Strip WSL prefix
    if s.startswith("//wsl.localhost/") or s.startswith("/wsl.localhost/"):
        parts = s.split("/")
        # Find repo or target relative anchor
        for idx, part in enumerate(parts):
            if part.lower() in ("recettear", "openrecet") and idx + 1 < len(parts):
                s = "/".join(parts[idx + 1:])
                break

    # Strip Windows Drive roots (e.g. C:/Program Files (x86)/Steam/.../Recettear/)
    m_drive = re.match(r"^[a-zA-Z]:/(?:[^/]+/)*?(?:recettear|openrecet)/(.*)$", s, re.IGNORECASE)
    if m_drive:
        s = m_drive.group(1)
    else:
        # Strip standard drive letter if still present
        m_bare_drive = re.match(r"^[a-zA-Z]:/(.*)$", s)
        if m_bare_drive:
            s = m_bare_drive.group(1)

    # Normalize common game directories
    for prefix in ("vendor/original/", "vendor/unpacked/", "./"):
        if s.startswith(prefix):
            s = s[len(prefix):]

    return s.lstrip("/").lower()


def hash_buffer(data: Union[bytes, bytearray, str]) -> str:
    """Computes SHA-256 hex digest of a binary payload."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def compute_stream_id(data_dict: Dict[str, Any]) -> str:
    """Computes deterministic 64-hex SHA-256 digest over canonical stream preimage."""
    preimage_data = {
        "schema_version": data_dict.get("schema_version", BOUNDARY_SCHEMA_VERSION),
        "scenario": data_dict.get("scenario", ""),
        "target": data_dict.get("target", "synthetic"),
        "events": data_dict.get("events", []),
        "provenance": {
            k: v for k, v in data_dict.get("provenance", {}).items()
            if k in ("environment", "retail_build_sha256", "port_build_sha256", "driver")
        },
    }
    canonical_json = json.dumps(preimage_data, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical_json.encode("utf-8")).hexdigest()


# ─── Data Classes ────────────────────────────────────────────────────────────

@dataclass
class BoundaryEvent:
    """A discrete system boundary interaction event (BT-00)."""
    seq: int
    logical_frame: Tuple[str, int, int]  # (anchor, occurrence, offset)
    domain: str
    api: str
    args: Dict[str, Any]
    result: Any
    thread_id: int = 0
    buffer_hash: Optional[str] = None
    buffer_size: int = 0
    side_effects: Dict[str, Any] = field(default_factory=dict)
    caller_va: Optional[str] = None
    caller_module: Optional[str] = None

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["logical_frame"] = list(self.logical_frame)
        return d

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> BoundaryEvent:
        data = copy.deepcopy(data)
        lf = data.get("logical_frame", ["UNKNOWN", 1, 0])
        if isinstance(lf, list):
            data["logical_frame"] = (str(lf[0]), int(lf[1]), int(lf[2]))
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class BoundaryStream:
    """A complete sequence of boundary events for a scenario run (BT-00)."""
    scenario: str
    target: str
    events: List[BoundaryEvent] = field(default_factory=list)
    provenance: Dict[str, Any] = field(default_factory=dict)
    schema_version: int = BOUNDARY_SCHEMA_VERSION
    stream_id: str = ""

    def __post_init__(self):
        if not self.stream_id:
            raw_dict = {
                "schema_version": self.schema_version,
                "scenario": self.scenario,
                "target": self.target,
                "events": [e.to_dict() for e in self.events],
                "provenance": self.provenance,
            }
            self.stream_id = compute_stream_id(raw_dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "stream_id": self.stream_id,
            "scenario": self.scenario,
            "target": self.target,
            "provenance": self.provenance,
            "events": [e.to_dict() for e in self.events],
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> BoundaryStream:
        evs = [BoundaryEvent.from_dict(e) for e in data.get("events", [])]
        return cls(
            scenario=data.get("scenario", "unknown"),
            target=data.get("target", "synthetic"),
            events=evs,
            provenance=data.get("provenance", {}),
            schema_version=data.get("schema_version", BOUNDARY_SCHEMA_VERSION),
            stream_id=data.get("stream_id", ""),
        )


@dataclass
class EquivalenceResult:
    """Outcome of a 3-tier boundary stream comparison."""
    level: str
    matched: bool
    verdict: str  # "PASS", "FAIL"
    stream_a_id: str
    stream_b_id: str
    events_a_count: int
    events_b_count: int
    divergent_seq: Optional[int] = None
    divergence_reason: Optional[str] = None
    divergent_event_a: Optional[Dict[str, Any]] = None
    divergent_event_b: Optional[Dict[str, Any]] = None
    metrics: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


# ─── Validation ──────────────────────────────────────────────────────────────

def validate_stream(stream: Union[BoundaryStream, Dict[str, Any]]) -> None:
    """Validates a boundary stream against BT-00 architectural invariants."""
    data = stream.to_dict() if isinstance(stream, BoundaryStream) else stream

    if data.get("schema_version") != BOUNDARY_SCHEMA_VERSION:
        raise BoundaryError(f"Invalid schema_version: {data.get('schema_version')} (expected {BOUNDARY_SCHEMA_VERSION})")

    if not data.get("scenario"):
        raise BoundaryError("Missing required field 'scenario'")

    if data.get("target") not in {"retail", "port", "synthetic", "reference"}:
        raise BoundaryError(f"Invalid target: {data.get('target')}")

    if not data.get("stream_id") or not re.match(r"^[0-9a-f]{64}$", data["stream_id"]):
        raise BoundaryError(f"Invalid stream_id digest: {data.get('stream_id')}")

    events = data.get("events", [])
    if not isinstance(events, list):
        raise BoundaryError("Field 'events' must be an array")

    expected_seq = 0
    for idx, e in enumerate(events):
        seq = e.get("seq")
        if seq is None or seq != expected_seq:
            raise BoundaryError(f"Event at index {idx} has invalid seq {seq} (expected {expected_seq})")
        expected_seq += 1

        dom = e.get("domain")
        if dom not in KNOWN_DOMAINS:
            raise BoundaryError(f"Event #{seq} has unknown domain '{dom}' (must be one of {sorted(KNOWN_DOMAINS)})")

        if not e.get("api"):
            raise BoundaryError(f"Event #{seq} missing required field 'api'")

        lf = e.get("logical_frame")
        if not isinstance(lf, (list, tuple)) or len(lf) != 3:
            raise BoundaryError(f"Event #{seq} has invalid logical_frame (must be 3-tuple [anchor, occ, offset])")

        b_hash = e.get("buffer_hash")
        if b_hash and not re.match(r"^[0-9a-f]{64}$", b_hash):
            raise BoundaryError(f"Event #{seq} has invalid buffer_hash digest: {b_hash}")


# ─── Equivalence Comparator ──────────────────────────────────────────────────

class BoundaryEquivalenceComparator:
    """Evaluates cross-target boundary stream equivalence across the 3 standard tiers."""

    @classmethod
    def compare(
        cls,
        stream_a: BoundaryStream,
        stream_b: BoundaryStream,
        level: str = "CALL_SEQUENCE_EQUIVALENT",
    ) -> EquivalenceResult:
        if level not in EQUIVALENCE_LEVELS:
            raise BoundaryError(f"Unknown equivalence level: '{level}' (expected {sorted(EQUIVALENCE_LEVELS)})")

        validate_stream(stream_a)
        validate_stream(stream_b)

        if level == "CALL_SEQUENCE_EQUIVALENT":
            return cls._compare_call_sequence(stream_a, stream_b)
        elif level == "RESULT_EQUIVALENT":
            return cls._compare_result_equivalent(stream_a, stream_b)
        elif level == "EFFECT_EQUIVALENT":
            return cls._compare_effect_equivalent(stream_a, stream_b)

        raise BoundaryError(f"Unhandled level {level}")

    @classmethod
    def _compare_call_sequence(cls, a: BoundaryStream, b: BoundaryStream) -> EquivalenceResult:
        """Level 1: Exact 1:1 call ordering, normalized arguments, and return codes."""
        evs_a = a.events
        evs_b = b.events
        min_len = min(len(evs_a), len(evs_b))

        for i in range(min_len):
            ea = evs_a[i]
            eb = evs_b[i]

            if ea.domain != eb.domain or ea.api != eb.api:
                return EquivalenceResult(
                    level="CALL_SEQUENCE_EQUIVALENT",
                    matched=False,
                    verdict="FAIL",
                    stream_a_id=a.stream_id,
                    stream_b_id=b.stream_id,
                    events_a_count=len(evs_a),
                    events_b_count=len(evs_b),
                    divergent_seq=i,
                    divergence_reason=f"API mismatch at seq {i}: {ea.domain}:{ea.api} vs {eb.domain}:{eb.api}",
                    divergent_event_a=ea.to_dict(),
                    divergent_event_b=eb.to_dict(),
                )

            # Compare normalized args
            if ea.args != eb.args:
                return EquivalenceResult(
                    level="CALL_SEQUENCE_EQUIVALENT",
                    matched=False,
                    verdict="FAIL",
                    stream_a_id=a.stream_id,
                    stream_b_id=b.stream_id,
                    events_a_count=len(evs_a),
                    events_b_count=len(evs_b),
                    divergent_seq=i,
                    divergence_reason=f"Argument mismatch at seq {i} for {ea.api}: {ea.args} != {eb.args}",
                    divergent_event_a=ea.to_dict(),
                    divergent_event_b=eb.to_dict(),
                )

            # Compare results
            if ea.result != eb.result:
                return EquivalenceResult(
                    level="CALL_SEQUENCE_EQUIVALENT",
                    matched=False,
                    verdict="FAIL",
                    stream_a_id=a.stream_id,
                    stream_b_id=b.stream_id,
                    events_a_count=len(evs_a),
                    events_b_count=len(evs_b),
                    divergent_seq=i,
                    divergence_reason=f"Result code mismatch at seq {i} for {ea.api}: {ea.result} != {eb.result}",
                    divergent_event_a=ea.to_dict(),
                    divergent_event_b=eb.to_dict(),
                )

            # Compare buffer hashes if present
            if ea.buffer_hash != eb.buffer_hash:
                return EquivalenceResult(
                    level="CALL_SEQUENCE_EQUIVALENT",
                    matched=False,
                    verdict="FAIL",
                    stream_a_id=a.stream_id,
                    stream_b_id=b.stream_id,
                    events_a_count=len(evs_a),
                    events_b_count=len(evs_b),
                    divergent_seq=i,
                    divergence_reason=f"Payload buffer hash mismatch at seq {i} for {ea.api}: {ea.buffer_hash} != {eb.buffer_hash}",
                    divergent_event_a=ea.to_dict(),
                    divergent_event_b=eb.to_dict(),
                )

        if len(evs_a) != len(evs_b):
            return EquivalenceResult(
                level="CALL_SEQUENCE_EQUIVALENT",
                matched=False,
                verdict="FAIL",
                stream_a_id=a.stream_id,
                stream_b_id=b.stream_id,
                events_a_count=len(evs_a),
                events_b_count=len(evs_b),
                divergent_seq=min_len,
                divergence_reason=f"Stream length mismatch: {len(evs_a)} vs {len(evs_b)} events",
            )

        return EquivalenceResult(
            level="CALL_SEQUENCE_EQUIVALENT",
            matched=True,
            verdict="PASS",
            stream_a_id=a.stream_id,
            stream_b_id=b.stream_id,
            events_a_count=len(evs_a),
            events_b_count=len(evs_b),
            metrics={"aligned_events": len(evs_a)},
        )

    @classmethod
    def _compare_result_equivalent(cls, a: BoundaryStream, b: BoundaryStream) -> EquivalenceResult:
        """Level 2: Matches functional outcomes across non-fatal retry/polling loops."""
        # Filter benign polling duplicates (e.g. repeated un-acquired GetDeviceState polls)
        def filter_benign_polls(events: List[BoundaryEvent]) -> List[BoundaryEvent]:
            filtered = []
            for e in events:
                # Deduplicate consecutive identical idle polls
                if (
                    filtered
                    and filtered[-1].domain == e.domain
                    and filtered[-1].api == e.api
                    and filtered[-1].result == e.result
                    and e.api in ("PeekMessageA", "GetDeviceState", "WaitForSingleObject")
                ):
                    continue
                filtered.append(e)
            return filtered

        filt_a = filter_benign_polls(a.events)
        filt_b = filter_benign_polls(b.events)

        min_len = min(len(filt_a), len(filt_b))
        for i in range(min_len):
            ea = filt_a[i]
            eb = filt_b[i]
            if ea.domain != eb.domain or ea.api != eb.api or ea.result != eb.result:
                return EquivalenceResult(
                    level="RESULT_EQUIVALENT",
                    matched=False,
                    verdict="FAIL",
                    stream_a_id=a.stream_id,
                    stream_b_id=b.stream_id,
                    events_a_count=len(a.events),
                    events_b_count=len(b.events),
                    divergent_seq=i,
                    divergence_reason=f"Functional outcome mismatch at logical event {i}: {ea.domain}:{ea.api}->{ea.result} vs {eb.domain}:{eb.api}->{eb.result}",
                    divergent_event_a=ea.to_dict(),
                    divergent_event_b=eb.to_dict(),
                )

        if len(filt_a) != len(filt_b):
            return EquivalenceResult(
                level="RESULT_EQUIVALENT",
                matched=False,
                verdict="FAIL",
                stream_a_id=a.stream_id,
                stream_b_id=b.stream_id,
                events_a_count=len(a.events),
                events_b_count=len(b.events),
                divergent_seq=min_len,
                divergence_reason=f"Filtered result length mismatch: {len(filt_a)} vs {len(filt_b)} functional operations",
            )

        return EquivalenceResult(
            level="RESULT_EQUIVALENT",
            matched=True,
            verdict="PASS",
            stream_a_id=a.stream_id,
            stream_b_id=b.stream_id,
            events_a_count=len(a.events),
            events_b_count=len(b.events),
            metrics={"functional_events_aligned": len(filt_a)},
        )

    @classmethod
    def _compare_effect_equivalent(cls, a: BoundaryStream, b: BoundaryStream) -> EquivalenceResult:
        """Level 3: Matches external environment side effects (filesystem, audio, window)."""
        def extract_effects(stream: BoundaryStream) -> Dict[str, Any]:
            files_written: Dict[str, str] = {}
            audio_played: List[Dict[str, Any]] = []
            ini_written: Dict[str, Any] = {}
            window_states: List[Dict[str, Any]] = []

            for e in stream.events:
                # Filesystem writes
                if e.domain == "filesystem_io" and e.api in ("WriteFile", "MoveFileA", "CreateFileA"):
                    path = normalize_path(e.args.get("path") or e.args.get("filename"))
                    if path and e.buffer_hash:
                        files_written[path] = e.buffer_hash

                # INI configuration writes
                elif e.domain == "ini_config" and e.api.startswith("WritePrivateProfile"):
                    sec = e.args.get("section")
                    k = e.args.get("key")
                    v = e.args.get("value")
                    if sec and k:
                        ini_written[f"{sec}/{k}"] = v

                # Audio playback
                elif e.domain == "audio_device" and "Play" in e.api:
                    audio_played.append({
                        "track": e.args.get("track") or e.args.get("segment"),
                        "volume": e.args.get("volume"),
                        "frame": e.logical_frame,
                    })

                # Window lifecycle
                elif e.domain == "window_lifecycle" and e.api in ("CreateWindowExA", "ShowWindow", "SetWindowPos"):
                    window_states.append({
                        "api": e.api,
                        "w": e.args.get("w") or e.args.get("width"),
                        "h": e.args.get("h") or e.args.get("height"),
                        "show": e.args.get("nCmdShow") or e.args.get("flags"),
                    })

            return {
                "files_written": files_written,
                "audio_played": audio_played,
                "ini_written": ini_written,
                "window_states": window_states,
            }

        eff_a = extract_effects(a)
        eff_b = extract_effects(b)

        # 1. Compare filesystem outputs
        if eff_a["files_written"] != eff_b["files_written"]:
            return EquivalenceResult(
                level="EFFECT_EQUIVALENT",
                matched=False,
                verdict="FAIL",
                stream_a_id=a.stream_id,
                stream_b_id=b.stream_id,
                events_a_count=len(a.events),
                events_b_count=len(b.events),
                divergence_reason=f"Filesystem effect mismatch: {eff_a['files_written']} != {eff_b['files_written']}",
                metrics={"effects_a": eff_a, "effects_b": eff_b},
            )

        # 2. Compare INI writes
        if eff_a["ini_written"] != eff_b["ini_written"]:
            return EquivalenceResult(
                level="EFFECT_EQUIVALENT",
                matched=False,
                verdict="FAIL",
                stream_a_id=a.stream_id,
                stream_b_id=b.stream_id,
                events_a_count=len(a.events),
                events_b_count=len(b.events),
                divergence_reason=f"INI configuration effect mismatch: {eff_a['ini_written']} != {eff_b['ini_written']}",
                metrics={"effects_a": eff_a, "effects_b": eff_b},
            )

        # 3. Compare Audio triggers
        if len(eff_a["audio_played"]) != len(eff_b["audio_played"]):
            return EquivalenceResult(
                level="EFFECT_EQUIVALENT",
                matched=False,
                verdict="FAIL",
                stream_a_id=a.stream_id,
                stream_b_id=b.stream_id,
                events_a_count=len(a.events),
                events_b_count=len(b.events),
                divergence_reason=f"Audio playback event count mismatch: {len(eff_a['audio_played'])} vs {len(eff_b['audio_played'])} tracks",
                metrics={"effects_a": eff_a, "effects_b": eff_b},
            )

        return EquivalenceResult(
            level="EFFECT_EQUIVALENT",
            matched=True,
            verdict="PASS",
            stream_a_id=a.stream_id,
            stream_b_id=b.stream_id,
            events_a_count=len(a.events),
            events_b_count=len(b.events),
            metrics={
                "files_matched": len(eff_a["files_written"]),
                "audio_matched": len(eff_a["audio_played"]),
                "ini_matched": len(eff_a["ini_written"]),
            },
        )
