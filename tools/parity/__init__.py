"""tools/parity — EP-01/EP-02 parity evidence library.

The single home for proof identity + provenance fingerprinting, consumed by the
EP-01 schema gate (tools/test_parity_schema.py), tools/test_parity_fingerprint.py,
and parity_prove.py (EP-05). Importable as `parity` when tools/ is on sys.path
(the default for `python3 tools/<script>.py`).
"""
from __future__ import annotations

from .canonical import NON_HASHED, canonical_bytes, proof_id_of, proof_passes
from .environment import (
    DISPLAY_MODES,
    EnvValidationError,
    collect_environment,
    host_probe,
)
from .environment import REQUIRED as ENV_REQUIRED
from .fingerprint import (
    FingerprintError,
    dir_manifest_entries,
    dir_manifest_sha256,
    git_dirty_patch_sha256,
    git_head,
    optional_input_fingerprint,
    port_subject,
    retail_subject,
    save_fingerprint,
    sha256_file,
    sha256_hex,
    tool_sha256_or_none,
)
from .observations import (
    FAIL,
    INCONCLUSIVE,
    NOT_CAPTURED,
    NOT_REQUIRED,
    OBS_SCHEMA_VERSION,
    PASS,
    AdapterResult,
    LogicalFrame,
    ObservationError,
    adapt_identity,
    artifact_ref,
    first_divergence,
    load_json,
    load_required,
    match_frames,
    observation,
    paired_frames,
    pillar_result,
)
from .pixels import adapt_pixels
from .render_program import adapt_render_program
from .render_program import from_view_json as render_metrics_from_view_json
from .save import adapt_save
from .state import adapt_state
from .state_diff import build_report as build_state_diff_report
from .state_diff import report_from_view_json as state_diff_from_view_json
from .state_producer import from_view_json as state_metrics_from_view_json
from .state_map import Locus, StateMap, StateMapError

__all__ = [
    # canonicalization (§4.4, frozen)
    "NON_HASHED",
    "canonical_bytes",
    "proof_id_of",
    "proof_passes",
    # fingerprinting
    "FingerprintError",
    "sha256_hex",
    "sha256_file",
    "dir_manifest_entries",
    "dir_manifest_sha256",
    "git_head",
    "git_dirty_patch_sha256",
    "port_subject",
    "retail_subject",
    "save_fingerprint",
    "optional_input_fingerprint",
    "tool_sha256_or_none",
    # environment
    "ENV_REQUIRED",
    "DISPLAY_MODES",
    "EnvValidationError",
    "host_probe",
    "collect_environment",
    # EP-04 observation normalization + pillar adjudication
    "OBS_SCHEMA_VERSION",
    "PASS",
    "FAIL",
    "NOT_CAPTURED",
    "NOT_REQUIRED",
    "INCONCLUSIVE",
    "ObservationError",
    "LogicalFrame",
    "AdapterResult",
    "load_json",
    "load_required",
    "paired_frames",
    "match_frames",
    "artifact_ref",
    "observation",
    "pillar_result",
    "first_divergence",
    "adapt_identity",
    "adapt_pixels",
    "adapt_render_program",
    "render_metrics_from_view_json",
    "adapt_save",
    "adapt_state",
    "state_metrics_from_view_json",
    "build_state_diff_report",
    "state_diff_from_view_json",
    "StateMap",
    "StateMapError",
    "Locus",
]
