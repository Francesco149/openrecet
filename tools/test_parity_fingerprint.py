#!/usr/bin/env python3
"""tools/test_parity_fingerprint.py — EP-02 gate for tools/parity provenance.

Proves the acceptance criteria the roadmap fixes for EP-02:

  * RELOCATION-INVARIANT — identical bytes hash the same from different absolute
    directories (files and directory manifests).
  * ONE-BYTE SENSITIVE — a single changed byte changes the hash; an added file
    changes the manifest.
  * DIRTY-TREE REPRESENTED — a real temp git repo: clean ⇒ None; unstaged edit,
    a different edit, and staging that edit each produce a distinct patch hash;
    reset ⇒ None again.
  * FAIL CLOSED — missing/unreadable input and ANY symlink under a manifest root
    raise FingerprintError (never a fabricated hash, never a read outside root).
  * SENTINELS / SUBJECTS / ENVIRONMENT build the exact schema shapes and validate.
  * CANONICAL cross-check — the promoted §4.4 rule is stable and excludes
    proof_id/envelope from its own preimage.

The symlink-refusal + missing-input cases are the explicit NEGATIVE tests
(roadmap §15): they prove the fingerprinter catches a deliberately unsafe input.

Run: nix develop --command python3 tools/test_parity_fingerprint.py
Exits non-zero on failure; prints OK on success. Git-dependent checks SKIP
cleanly (still exit 0) if `git` is unavailable.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

from parity import (  # noqa: E402  (tools/ is sys.path[0] for a run script)
    DISPLAY_MODES,
    ENV_REQUIRED,
    EnvValidationError,
    FingerprintError,
    collect_environment,
    dir_manifest_entries,
    dir_manifest_sha256,
    git_dirty_patch_sha256,
    git_head,
    host_probe,
    optional_input_fingerprint,
    port_subject,
    proof_id_of,
    proof_passes,
    retail_subject,
    save_fingerprint,
    sha256_file,
    sha256_hex,
    tool_sha256_or_none,
)

_failures: list[str] = []
_checks = 0
_skips: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


def check_raises(exc, fn, msg: str) -> None:
    global _checks
    _checks += 1
    try:
        fn()
    except exc:
        return
    except Exception as e:  # noqa: BLE001
        _failures.append(f"{msg}: expected {exc.__name__}, got {type(e).__name__}: {e}")
        return
    _failures.append(f"{msg}: expected {exc.__name__}, nothing raised")


def _make_tree(base: Path) -> None:
    base.mkdir(parents=True)
    (base / "a.txt").write_bytes(b"alpha")
    (base / "sub").mkdir()
    (base / "sub" / "b.bin").write_bytes(b"\x00\x01\x02")
    (base / "sub" / "c.txt").write_bytes(b"gamma")


def test_sha256_file(tmp: Path) -> None:
    tmp.mkdir(parents=True, exist_ok=True)
    f = tmp / "hello"
    f.write_bytes(b"hello")
    check(sha256_file(f) == hashlib.sha256(b"hello").hexdigest(), "sha256_file matches hashlib")
    g = tmp / "hello2"
    g.write_bytes(b"hellp")  # one byte differs
    check(sha256_file(f) != sha256_file(g), "one changed byte changes the file hash")
    check(sha256_hex(b"hello") == sha256_file(f), "sha256_hex agrees with sha256_file")

    # fail closed
    check_raises(FingerprintError, lambda: sha256_file(tmp / "nope"), "missing file fails closed")
    check_raises(FingerprintError, lambda: sha256_file(tmp / "sub_missing_dir"), "missing path fails closed")
    d = tmp / "adir"
    d.mkdir()
    check_raises(FingerprintError, lambda: sha256_file(d), "a directory is not a file (fail closed)")


def test_dir_manifest(tmp: Path) -> None:
    r1 = tmp / "loc1" / "assets"
    r2 = tmp / "totally" / "different" / "abs" / "assets"
    _make_tree(r1)
    _make_tree(r2)

    # relocation invariance
    check(dir_manifest_sha256(r1) == dir_manifest_sha256(r2), "same tree hashes equal from different roots")

    entries = dir_manifest_entries(r1)
    rels = [e[0] for e in entries]
    check(rels == ["a.txt", "sub/b.bin", "sub/c.txt"], f"manifest is sorted posix rels, got {rels}")
    check(all(len(e) == 3 and isinstance(e[1], int) for e in entries), "each entry is [rel, size, sha256]")

    # one byte change flips the manifest hash
    before = dir_manifest_sha256(r2)
    (r2 / "sub" / "b.bin").write_bytes(b"\x00\x01\x03")
    check(dir_manifest_sha256(r2) != before, "one changed content byte changes the manifest hash")

    # added file flips the manifest hash
    mid = dir_manifest_sha256(r2)
    (r2 / "d.txt").write_bytes(b"delta")
    check(dir_manifest_sha256(r2) != mid, "an added file changes the manifest hash")

    # fail closed: missing root, non-dir root
    check_raises(FingerprintError, lambda: dir_manifest_sha256(tmp / "no_such_root"), "missing manifest root fails closed")
    check_raises(FingerprintError, lambda: dir_manifest_sha256(r1 / "a.txt"), "a file is not a manifest root (fail closed)")

    # NEGATIVE test: a symlink under the root must fail closed (never read outside)
    link = r1 / "escape"
    try:
        os.symlink(tmp / "loc1", link)  # symlink to a dir outside r1
    except (OSError, NotImplementedError):
        _skips.append("symlink refusal (filesystem lacks symlink support)")
    else:
        check_raises(FingerprintError, lambda: dir_manifest_sha256(r1), "symlink under manifest root fails closed")
        link.unlink()

    filelink = r1 / "sub" / "b_alias.bin"
    try:
        os.symlink(r1 / "a.txt", filelink)
    except (OSError, NotImplementedError):
        pass
    else:
        check_raises(FingerprintError, lambda: dir_manifest_sha256(r1), "symlinked file under manifest root fails closed")
        check_raises(FingerprintError, lambda: sha256_file(filelink), "sha256_file refuses a symlink")
        filelink.unlink()


def test_sentinels(tmp: Path) -> None:
    tmp.mkdir(parents=True, exist_ok=True)
    f = tmp / "save.dat"
    f.write_bytes(b"savebytes")
    fsha = hashlib.sha256(b"savebytes").hexdigest()
    check(save_fingerprint(None) == "@fresh", "save_fingerprint(None) == @fresh")
    check(save_fingerprint(f) == fsha, "save_fingerprint(file) == sha256")
    check(optional_input_fingerprint(None) == "@none", "optional_input_fingerprint(None) == @none")
    check(optional_input_fingerprint(f) == fsha, "optional_input_fingerprint(file) == sha256")
    check(tool_sha256_or_none(None) == "@none", "tool_sha256_or_none(None) == @none")
    check(tool_sha256_or_none(f) == fsha, "tool_sha256_or_none(file) == sha256")


def test_retail_subject(tmp: Path) -> None:
    tmp.mkdir(parents=True, exist_ok=True)
    pe = tmp / "retail.exe"
    pe.write_bytes(b"MZ...")
    subj = retail_subject(pe, "carpe-fulgur-steam-ref")
    check(set(subj) == {"pe_sha256", "reference_id"}, "retail_subject shape")
    check(subj["reference_id"] == "carpe-fulgur-steam-ref", "retail reference_id preserved")
    check_raises(FingerprintError, lambda: retail_subject(pe, ""), "empty retail reference_id fails closed")
    check_raises(FingerprintError, lambda: retail_subject(pe, "   "), "blank retail reference_id fails closed")


def test_environment() -> None:
    full = dict(
        os_build="Windows-10-10.0.19045",
        locale="ja_JP",
        codepage="932",
        d3d_runtime="d3d8-native",
        gpu="NVIDIA RTX",
        driver="551.86",
        resolution="1024x600",
        display_mode="windowed",
    )
    env = collect_environment(full)
    check(list(env) == list(ENV_REQUIRED), "environment returns exactly the required keys in order")
    check(env == full, "environment values preserved")

    for missing in ("gpu", "display_mode"):
        partial = {k: v for k, v in full.items() if k != missing}
        check_raises(EnvValidationError, lambda p=partial: collect_environment(p), f"missing {missing} fails closed")

    bad = dict(full, display_mode="borderless")
    check_raises(EnvValidationError, lambda: collect_environment(bad), "invalid display_mode fails closed")
    check(set(DISPLAY_MODES) == {"windowed", "fullscreen"}, "display modes frozen")

    # detect fills os_build; overrides win over fields
    probe = host_probe()
    check("os_build" in probe and probe["os_build"], "host_probe reports os_build")
    detected = collect_environment(full, detect=True, resolution="800x600")
    check(detected["resolution"] == "800x600", "overrides win over the fields dict")


def test_canonical_promotion() -> None:
    fixture = ROOT / "docs" / "schemas" / "fixtures" / "proof-full.valid.json"
    proof = json.loads(fixture.read_text())
    pid = proof_id_of(proof)
    check(len(pid) == 64 and all(c in "0123456789abcdef" for c in pid), "proof_id is 64-hex")
    check(proof_id_of(proof) == pid, "proof_id deterministic")

    from copy import deepcopy

    excl = deepcopy(proof)
    excl["proof_id"] = "0" * 64
    excl["envelope"] = {"generated_at": "2099-01-01T00:00:00Z", "display_notes": "x"}
    check(proof_id_of(excl) == pid, "proof_id/envelope excluded from the preimage")

    sens = deepcopy(proof)
    sens["subject"]["retail"]["reference_id"] = "other-build"
    check(proof_id_of(sens) != pid, "a hashed field change changes proof_id")

    check(proof_passes(["pixels"], {"pillars": {"pixels": {"verdict": "PASS"}}}), "proof_passes true when required pillar PASS")
    check(not proof_passes(["pixels"], {"pillars": {"pixels": {"verdict": "NOT_CAPTURED"}}}), "NOT_CAPTURED required pillar fails the gate")
    check(not proof_passes(["pixels"], {"pillars": {}}), "absent required pillar fails the gate")


# ── git-backed dirty-tree provenance (hermetic temp repo) ────────────────────

def _git_env() -> dict:
    env = os.environ.copy()
    env.update(
        GIT_CONFIG_GLOBAL=os.devnull,
        GIT_CONFIG_SYSTEM=os.devnull,
        GIT_CONFIG_NOSYSTEM="1",
        GIT_AUTHOR_NAME="t",
        GIT_AUTHOR_EMAIL="t@e",
        GIT_COMMITTER_NAME="t",
        GIT_COMMITTER_EMAIL="t@e",
        GIT_AUTHOR_DATE="2020-01-01T00:00:00 +0000",
        GIT_COMMITTER_DATE="2020-01-01T00:00:00 +0000",
    )
    return env


def _git(repo: Path, *args: str) -> str:
    proc = subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True,
        check=True,
        env=_git_env(),
    )
    return proc.stdout.decode().strip()


def test_git(tmp: Path) -> None:
    repo = tmp / "repo"
    repo.mkdir(parents=True)
    _git(repo, "init", "-q")
    (repo / "a.txt").write_text("base\n")
    _git(repo, "add", "a.txt")
    _git(repo, "-c", "commit.gpgsign=false", "commit", "-q", "-m", "init")

    # clean tree
    check(git_dirty_patch_sha256(repo) is None, "clean tree ⇒ dirty patch None")
    head = git_head(repo)
    check(len(head) == 40 and all(c in "0123456789abcdef" for c in head), "git_head is 40-hex")
    check(head == _git(repo, "rev-parse", "HEAD"), "git_head matches rev-parse")

    # unstaged edit
    (repo / "a.txt").write_text("v1\n")
    h1 = git_dirty_patch_sha256(repo)
    check(h1 is not None, "unstaged edit ⇒ dirty patch hash")

    # a DIFFERENT unstaged edit ⇒ different hash
    (repo / "a.txt").write_text("v2\n")
    h2 = git_dirty_patch_sha256(repo)
    check(h2 is not None and h2 != h1, "a different edit changes the dirty patch hash")

    # staging the same content ⇒ different hash (index state is part of 'dirty')
    _git(repo, "add", "a.txt")
    h3 = git_dirty_patch_sha256(repo)
    check(h3 is not None and h3 != h2, "staged vs unstaged is distinguished")

    # reset ⇒ clean again
    _git(repo, "reset", "--hard", "-q")
    check(git_dirty_patch_sha256(repo) is None, "reset ⇒ clean ⇒ None (round trip)")

    # port_subject shape on a clean repo
    pe = repo / "port.exe"
    pe.write_bytes(b"MZport")
    subj = port_subject(repo, pe)
    check(set(subj) == {"pe_sha256", "git_commit", "dirty_patch_sha256"}, "port_subject shape")
    check(subj["dirty_patch_sha256"] is None, "clean tree ⇒ port_subject dirty=None")
    check(subj["git_commit"] == head, "port_subject git_commit == HEAD")

    # NEGATIVE: git_head on a non-repo fails closed
    nonrepo = tmp / "plain"
    nonrepo.mkdir()
    check_raises(FingerprintError, lambda: git_head(nonrepo), "git_head on a non-repo fails closed")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="orv3-ep02-") as td:
        tmp = Path(td)
        test_sha256_file(tmp / "t_sha")
        test_dir_manifest(tmp / "t_manifest")
        test_sentinels(tmp / "t_sent")
        test_retail_subject(tmp / "t_retail")
        test_environment()
        test_canonical_promotion()
        if shutil.which("git"):
            test_git(tmp / "t_git")
        else:
            _skips.append("git-backed dirty-tree tests (git unavailable)")

    for s in _skips:
        print(f"SKIP: {s}")
    if _failures:
        print("FAIL:")
        for f in _failures:
            print("  -", f)
        return 1
    print(f"OK ({_checks} checks, {len(_skips)} skipped)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
