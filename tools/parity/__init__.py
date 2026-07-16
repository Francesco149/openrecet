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
]
