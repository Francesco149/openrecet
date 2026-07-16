#!/usr/bin/env python3
"""tools/parity/fingerprint.py — EP-02 provenance fingerprinting.

Turns the concrete inputs of a parity run into the stable, portable hashes the
proof bundle's `subject`/`inputs`/`tools` groups require (schema
docs/schemas/parity-proof-v1.schema.json; format docs/reference/parity-proof-format.md).

Design rules honored (roadmap §3):
  * FAIL CLOSED — a missing/unreadable input raises FingerprintError; nothing here
    ever invents a hash or silently substitutes a placeholder.
  * INPUTS DETERMINE IDENTITY — the same bytes hash the same from any absolute
    directory; a single changed byte changes the hash.
  * NEVER READ OUTSIDE SUPPLIED ROOTS — directory manifests refuse to follow ANY
    symlink (file or dir): a symlink could escape the root or silently omit a
    subtree, so it is an error, not a skip.
  * NO PROPRIETARY BYTES — only hashes/sizes leave this module; file contents do not.

`git_dirty_patch_sha256` implements EXACTLY the EP-01-frozen field semantics
("SHA-256 of the combined staged+unstaged diff, or null for a clean tree"):
untracked files are NOT "dirty" by this field's definition. The built port PE's own
sha256 (subject.port.pe_sha256) is the authoritative build-identity backstop, so a
build-affecting untracked source file is still distinguished by the PE hash.
"""
from __future__ import annotations

import hashlib
import os
import re
import subprocess
from pathlib import Path
from typing import Optional

_CHUNK = 1 << 20
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


class FingerprintError(Exception):
    """A provenance input was missing, unreadable, unsafe, or unresolvable.

    Raised instead of returning a fabricated/placeholder value so every caller
    fails closed (roadmap §3 rule 1)."""


# ── raw hashing ──────────────────────────────────────────────────────────────

def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path) -> str:
    """Stream the SHA-256 of a regular file. Fail closed on missing / permission /
    directory / any OS error."""
    p = Path(path)
    if p.is_symlink():
        raise FingerprintError(f"refusing to hash a symlink: {p}")
    h = hashlib.sha256()
    try:
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(_CHUNK), b""):
                h.update(chunk)
    except OSError as exc:
        raise FingerprintError(f"cannot read {p}: {exc}") from exc
    return h.hexdigest()


# ── directory manifest (relocation-invariant, symlink-refusing) ──────────────

def dir_manifest_entries(root) -> list[list]:
    """Deterministic manifest of every regular file under `root`, as a sorted list
    of `[posix_relative_path, size_bytes, sha256]`. Relocation-invariant (paths are
    relative to `root`, posix-normalized). Refuses every symlink so it can never
    read outside `root` or silently drop a symlinked subtree."""
    r = Path(root)
    if not r.exists():
        raise FingerprintError(f"manifest root does not exist: {r}")
    if r.is_symlink():
        raise FingerprintError(f"manifest root is a symlink: {r}")
    if not r.is_dir():
        raise FingerprintError(f"manifest root is not a directory: {r}")

    entries: list[list] = []
    for dirpath, dirnames, filenames in os.walk(r, followlinks=False):
        d = Path(dirpath)
        for name in dirnames:
            if (d / name).is_symlink():
                raise FingerprintError(
                    f"symlinked directory in manifest root (would hide a subtree): {d / name}"
                )
        for name in filenames:
            full = d / name
            if full.is_symlink():
                raise FingerprintError(
                    f"symlink in manifest root (would read outside root): {full}"
                )
            rel = full.relative_to(r).as_posix()
            entries.append([rel, full.stat().st_size, sha256_file(full)])
    entries.sort(key=lambda e: e[0])
    return entries


def dir_manifest_sha256(root) -> str:
    """SHA-256 of the canonical JSON encoding of dir_manifest_entries(root)."""
    import json

    entries = dir_manifest_entries(root)
    blob = json.dumps(entries, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return sha256_hex(blob)


# ── git provenance ───────────────────────────────────────────────────────────

def _run_git(repo, args: list[str], *, text: bool = False):
    try:
        proc = subprocess.run(
            ["git", "-C", str(repo), *args],
            capture_output=True,
            check=True,
        )
    except FileNotFoundError as exc:
        raise FingerprintError("git executable not found") from exc
    except subprocess.CalledProcessError as exc:
        msg = exc.stderr.decode("utf-8", "replace").strip()
        raise FingerprintError(f"git {' '.join(args)} failed: {msg}") from exc
    return proc.stdout.decode("utf-8", "strict").strip() if text else proc.stdout


def git_head(repo) -> str:
    """Full 40-hex commit SHA of HEAD. Fail closed if `repo` is not a git repo."""
    head = _run_git(repo, ["rev-parse", "HEAD"], text=True)
    if not _COMMIT_RE.match(head):
        raise FingerprintError(f"unexpected HEAD sha: {head!r}")
    return head


# Deterministic diff flags: pin autocrlf, defeat config-driven external/textconv
# diff drivers and colouring so the same tree yields the same bytes everywhere.
_DIFF_FLAGS = ["--no-color", "--no-ext-diff", "--no-textconv"]
_SEP = b"\x00\x1e--staged|unstaged--\x1e\x00"


def git_dirty_patch_sha256(repo) -> Optional[str]:
    """SHA-256 of the combined staged+unstaged diff, or None for a clean tree.

    Concatenates `git diff --cached` (HEAD→index) and `git diff` (index→worktree)
    with a fixed separator, so the SAME change reads differently when staged vs
    unstaged (the index state is part of "dirty"). Untracked files are out of scope
    of this field by the EP-01 definition (see module docstring)."""
    autocrlf = ["-c", "core.autocrlf=false"]
    staged = _run_git(repo, [*autocrlf, "diff", "--cached", *_DIFF_FLAGS])
    unstaged = _run_git(repo, [*autocrlf, "diff", *_DIFF_FLAGS])
    if not staged and not unstaged:
        return None
    return sha256_hex(staged + _SEP + unstaged)


# ── schema-shaped builders (subjects + input sentinels) ──────────────────────

def port_subject(repo, pe_path) -> dict:
    """Build proof `subject.port` = {pe_sha256, git_commit, dirty_patch_sha256}."""
    return {
        "pe_sha256": sha256_file(pe_path),
        "git_commit": git_head(repo),
        "dirty_patch_sha256": git_dirty_patch_sha256(repo),
    }


def retail_subject(pe_path, reference_id: str) -> dict:
    """Build proof `subject.retail` = {pe_sha256, reference_id}."""
    if not reference_id or not reference_id.strip():
        raise FingerprintError("retail reference_id is required and must be non-empty")
    return {"pe_sha256": sha256_file(pe_path), "reference_id": reference_id}


def save_fingerprint(path) -> str:
    """`inputs.save`: SHA-256 of the resolved save bytes, or "@fresh" for no save."""
    return "@fresh" if path is None else sha256_file(path)


def optional_input_fingerprint(path) -> str:
    """`inputs.assets_manifest_sha256` / `recet_ini_sha256`: SHA-256 or "@none"."""
    return "@none" if path is None else sha256_file(path)


def tool_sha256_or_none(path) -> str:
    """`tools.*_sha256` for a capture tool that may not participate: SHA-256 or
    "@none" (comparator/schema hashes are always required — hash those directly)."""
    return "@none" if path is None else sha256_file(path)
