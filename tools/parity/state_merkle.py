#!/usr/bin/env python3
"""tools/parity/state_merkle.py — ST-02 Merkle roots over the volatile-state tree.

A frame's canonical volatile-state tree (state_codec.build_tree) hashes into a
domain-separated Merkle root:  root -> subsystem -> field-leaf. Two roots let the
`state` pillar answer "is this frame's whole volatile state identical?" with one
compare; first_divergent_leaf drills a mismatch to the exact (subsystem, field).

Domain separation (a leaf can never collide with an inner node, and the schema
version is bound into every hash domain, so a schema change never silently
matches an old root):
  leaf       H(0x00 ‖ sv ‖ sub ‖ 0 ‖ field ‖ 0 ‖ type ‖ 0 ‖ canon_bytes)
  subsystem  H(0x01 ‖ sv ‖ sub ‖ 0 ‖ ‖leaf_hash…‖)          (fields in schema order)
  root       H(0x02 ‖ sv ‖ ‖sub ‖ 0 ‖ sub_hash…‖)           (subsystems in schema order)

Order is the schema's declared order (a fixed canonical total order over every
possible subsystem/field) — so the root depends ONLY on the values, never on
capture/insertion order (ST-02: same semantic state hashes equally). A benign
field is already absent from the tree (state_codec drops it) ⇒ it can't move a
root. first_divergent_leaf walks the SAME order, so its "first" is the semantic
priority rng→phase→player→… (an rng divergence outranks a camera one)."""
from __future__ import annotations

import struct
from hashlib import sha256
from typing import NamedTuple, Optional

from .state_codec import StateSchema, build_tree


class LeafDiff(NamedTuple):
    """The first divergent leaf between two trees. `path` = 'subsystem/field'.
    A present/absent asymmetry sets one of the *_present flags False (its value
    is then None)."""

    path: str
    a_value: object
    b_value: object
    a_present: bool
    b_present: bool


def _sv_bytes(sv: int) -> bytes:
    return struct.pack("<I", sv & 0xFFFFFFFF)


def _leaf_hash(sv: int, sub: str, field: str, typ: str, canon: bytes) -> bytes:
    h = sha256()
    h.update(b"\x00")
    h.update(_sv_bytes(sv))
    h.update(sub.encode("utf-8")); h.update(b"\x00")
    h.update(field.encode("utf-8")); h.update(b"\x00")
    h.update(typ.encode("utf-8")); h.update(b"\x00")
    h.update(canon)
    return h.digest()


def _subsystem_hash(sv: int, sub: str, node) -> bytes:
    h = sha256()
    h.update(b"\x01")
    h.update(_sv_bytes(sv))
    h.update(sub.encode("utf-8")); h.update(b"\x00")
    for field, (typ, _val, canon) in node.items():   # tree order = schema order
        h.update(_leaf_hash(sv, sub, field, typ, canon))
    return h.digest()


def merkle_root(tree, schema: StateSchema) -> str:
    """The hex Merkle root of a canonical volatile-state tree. An empty tree (no
    captured fields) hashes to the well-defined root-of-nothing (still domain +
    version bound), never to a fixed sentinel a real state could collide with."""
    sv = schema.schema_version
    h = sha256()
    h.update(b"\x02")
    h.update(_sv_bytes(sv))
    for sub in schema.subsystems():           # canonical order; absent subsystems skip
        node = tree.get(sub)
        if node:
            h.update(sub.encode("utf-8")); h.update(b"\x00")
            h.update(_subsystem_hash(sv, sub, node))
    return h.hexdigest()


def first_divergent_leaf(tree_a, tree_b, schema: StateSchema) -> Optional[LeafDiff]:
    """Walk both trees in canonical (schema) order; return the first leaf whose
    canonical bytes differ, or whose presence is asymmetric. None ⇒ the two trees
    are byte-identical (⇔ equal roots). A symmetrically-absent subsystem/field is
    skipped (both sides agree it isn't there this frame)."""
    for sub in schema.subsystems():
        a_node = tree_a.get(sub) or {}
        b_node = tree_b.get(sub) or {}
        if not a_node and not b_node:
            continue
        for field, _typ in schema.fields(sub):
            a = a_node.get(field)
            b = b_node.get(field)
            pa, pb = a is not None, b is not None
            if pa != pb:
                return LeafDiff(f"{sub}/{field}",
                                a[1] if pa else None, b[1] if pb else None, pa, pb)
            if pa and a[2] != b[2]:            # canonical bytes differ
                return LeafDiff(f"{sub}/{field}", a[1], b[1], True, True)
    return None


def state_root(state_fields: dict, schema: StateSchema) -> str:
    """Convenience: a flat captured {field: value} dict -> its Merkle root."""
    return merkle_root(build_tree(state_fields, schema), schema)
