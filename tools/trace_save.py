#!/usr/bin/env python3
"""tools/trace_save.py — shared helpers for the TAS save-interception layer.

A trace can carry the exact save it ran against via a single-key segtrace op
``{"savefile":"<relpath>"}`` (see src/input_segtrace.h). The blob it points at is
**gzip-compressed and content-addressed**: ``<sha256>.sav.gz``, where the sha256 is
over the RAW (uncompressed) 18 MB save arena. Content-addressing dedupes the same
save across every scenario that embeds it (the "embed current save into all traces"
test → one shared blob), and gzip shrinks the ~99 %-constant arena to a few hundred
KB so it's git-friendly.

This module owns three jobs, used by distill_trace.py / trace_embed_save.py /
scenario-test.py / frida_capture.py:

  * ``store_save``    — gzip a raw save into the content store, return its sha + ref
  * ``embed_in_trace`` — insert/replace the ``{savefile}`` ref line in a trace file
  * ``resolve_save``  — given a trace + its ref, decompress to a raw temp file the
                        port loads via ``--save-override`` (and retail redirects to)

The C port never reads the compressed blob (it can't gunzip); the Python harness
always resolves it and passes the decompressed path. Keeping this single source of
truth means the same trace drives both targets.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path

SAVE_ARENA_BYTES = 0x011f7530   # SAVE_BANK_ARENA_BYTES (src/save_bank.h) = 18,838,832
SAVE_SUFFIX = ".sav.gz"
_CACHE_DIR = Path(tempfile.gettempdir()) / "openrecet-save-cache"

# Sentinel ref for a "fresh game" trace: boot with no save (fresh menu, no LOAD
# GAME), regardless of the user's disk save. resolve_save returns it verbatim;
# the harness maps it to the port's --save-fresh (and a fresh boot on retail).
FRESH_REF = "@fresh"


def sha256_file(path: str | os.PathLike) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def default_store_dir(trace_path: str | os.PathLike) -> Path:
    """Where the content store lives for a given trace.

    For a scenario trace ``.../tests/scenarios/<name>/trace.jsonl`` the store is the
    shared ``.../tests/scenarios/_saves`` (so all scenarios dedupe to one blob). For
    any other trace location it's a ``_saves`` dir alongside the trace file."""
    tp = Path(trace_path).resolve()
    parent = tp.parent
    if parent.parent.name == "scenarios":
        return parent.parent / "_saves"
    return parent / "_saves"


def _rel_ref(trace_path: str | os.PathLike, blob_path: Path) -> str:
    """Relative ref string to embed (POSIX separators), from the trace's dir."""
    trace_dir = Path(trace_path).resolve().parent
    rel = os.path.relpath(blob_path.resolve(), trace_dir)
    return Path(rel).as_posix()


def store_save(raw_save_path: str | os.PathLike, store_dir: str | os.PathLike,
               sha: str | None = None) -> tuple[str, Path]:
    """Gzip a raw save into ``store_dir/<sha>.sav.gz`` (content-addressed).

    Returns (sha256_hex, blob_path). Idempotent: if the blob already exists with the
    right name it's left as-is (content-addressed ⇒ identical bytes). ``sha`` may be
    passed to skip re-hashing (e.g. the recorder already computed it); it's verified."""
    raw = Path(raw_save_path)
    actual = sha256_file(raw)
    if sha is not None and sha.lower() != actual:
        raise ValueError(
            f"save sha mismatch: ref says {sha} but {raw} hashes to {actual}")
    sha = actual
    store = Path(store_dir)
    store.mkdir(parents=True, exist_ok=True)
    blob = store / f"{sha}{SAVE_SUFFIX}"
    if not blob.exists():
        # mtime=0 for reproducible gzip output (so re-storing the same save is a
        # byte-identical blob → no spurious git churn).
        tmp = blob.with_suffix(blob.suffix + ".tmp")
        with open(raw, "rb") as fi, gzip.GzipFile(
                filename="", mode="wb", fileobj=open(tmp, "wb"), mtime=0) as fo:
            shutil.copyfileobj(fi, fo)
        os.replace(tmp, blob)
    return sha, blob


SAVEFILE_KEY = "savefile"


def embed_in_trace(trace_path: str | os.PathLike, ref: str,
                   sha: str | None = None) -> None:
    """Insert (or replace) the ``{"savefile":...}`` op at the top of a trace file.

    The op is trace-global, so it goes first (after any leading ``#`` comment block).
    A pre-existing savefile line is replaced. We emit a single-key object so the C
    segtrace parser accepts it (it rejects unknown keys like sha256/size); the sha is
    instead encoded in the ref filename (content-addressed) and noted in a comment."""
    tp = Path(trace_path)
    lines = tp.read_text().splitlines()
    out, inserted, comment_end = [], False, 0
    # find end of leading comment/blank block
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("#") or not s:
            comment_end = i + 1
        else:
            break
    savefile_line = json.dumps({SAVEFILE_KEY: ref})
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("{"):
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                o = {}
            if isinstance(o, dict) and SAVEFILE_KEY in o:
                continue  # drop any existing savefile op (we re-insert)
        if i == comment_end and not inserted:
            if sha:
                out.append(f"# embedded save: {sha} (override via --save-override "
                           f"on replay; see tools/trace_save.py)")
            out.append(savefile_line)
            inserted = True
        out.append(ln)
    if not inserted:  # trace was all-comments or empty
        if sha:
            out.append(f"# embedded save: {sha}")
        out.append(savefile_line)
    tp.write_text("\n".join(out) + "\n")


def read_ref(trace_path: str | os.PathLike) -> str | None:
    """Return the trace's embedded savefile ref string, or None if it has none."""
    for ln in Path(trace_path).read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("{") and SAVEFILE_KEY in s:
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                continue
            if isinstance(o, dict) and SAVEFILE_KEY in o:
                return str(o[SAVEFILE_KEY])
    return None


def resolve_save(trace_path: str | os.PathLike, ref: str | None = None) -> str | None:
    """Decompress a trace's embedded save to a raw file the port can --save-override.

    Returns the path to the decompressed raw save, or None if the trace has no
    savefile op. The ref is resolved relative to the trace file's directory. Decoded
    output is cached in the system temp dir keyed by the blob's content sha (the sha
    is in the blob filename), so repeated scenario runs don't re-inflate 18 MB."""
    if ref is None:
        ref = read_ref(trace_path)
    if not ref:
        return None
    if ref == FRESH_REF:
        return FRESH_REF   # caller maps to --save-fresh / fresh retail boot
    blob = (Path(trace_path).resolve().parent / ref).resolve()
    if not blob.exists():
        raise FileNotFoundError(
            f"trace {trace_path} references save blob {ref} → {blob} (missing). "
            f"Re-embed with tools/trace_embed_save.py.")
    # sha is the blob's stem (content-addressed: <sha>.sav.gz)
    name = blob.name
    sha = name[:-len(SAVE_SUFFIX)] if name.endswith(SAVE_SUFFIX) else blob.stem
    _CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cached = _CACHE_DIR / f"{sha}.sav"
    if cached.exists() and cached.stat().st_size == SAVE_ARENA_BYTES:
        return str(cached)
    tmp = cached.with_suffix(".sav.tmp")
    with gzip.open(blob, "rb") as fi, open(tmp, "wb") as fo:
        shutil.copyfileobj(fi, fo)
    os.replace(tmp, cached)
    return str(cached)
