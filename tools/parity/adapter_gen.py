#!/usr/bin/env python3
"""tools/parity/adapter_gen.py — CC-02 host adapter generator and boundary mutator.

Provides:
1. Automated C header/shim generation and dynamic runtime ctypes adapter construction
   from CallCaptureSpec descriptors (enabling zero-boilerplate target registration).
2. Systematic boundary mutation generator (probing integer/float/global edge conditions).
3. Divergence injection and validation (deliberate return/poststate/order mismatches).
"""
from __future__ import annotations

import copy
import ctypes
import math
import random
from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Union

from .capsule import (
    CallCapsule,
    CapsuleError,
    CapsuleReplayResult,
    MemoryWrite,
    replay_capsule,
)
from .capsule_capture import CallCaptureSpec, KNOWN_CALL_SPECS


# ─── 1. Automated Host Adapter Code Generator ────────────────────────────────

class AdapterGenerator:
    """Generates C shims and dynamic ctypes bindings from CallCaptureSpec descriptors."""

    @staticmethod
    def c_type_of(typ_str: str) -> str:
        mapping = {
            "int": "int32_t",
            "s32": "int32_t",
            "uint32": "uint32_t",
            "u32": "uint32_t",
            "u16": "uint16_t",
            "s16": "int16_t",
            "u8": "uint8_t",
            "float": "float",
            "double": "double",
            "pointer": "void*",
            "void": "void",
        }
        return mapping.get(typ_str, "int32_t")

    @staticmethod
    def ctypes_type_of(typ_str: str) -> type:
        mapping = {
            "int": ctypes.c_int32,
            "s32": ctypes.c_int32,
            "uint32": ctypes.c_uint32,
            "u32": ctypes.c_uint32,
            "u16": ctypes.c_uint16,
            "s16": ctypes.c_int16,
            "u8": ctypes.c_uint8,
            "float": ctypes.c_float,
            "double": ctypes.c_double,
            "pointer": ctypes.c_void_p,
        }
        return mapping.get(typ_str, ctypes.c_int32)

    @classmethod
    def generate_c_header(cls, spec: CallCaptureSpec) -> str:
        """Generates C struct and function declarations for diff_entry.h."""
        cap_name = "".join(part.capitalize() for part in spec.name.split("_"))
        lines = [
            f"/* ── {spec.name} ({spec.target_va} / {spec.target_symbol}) ── */",
            f"typedef struct Engine{cap_name}In {{",
        ]
        # In struct fields: stack args + declared globals
        for idx, (arg_val, arg_t) in enumerate(zip(spec.args, spec.arg_types)):
            lines.append(f"    {cls.c_type_of(arg_t)} arg_{idx};")
        for g_name, g_desc in spec.declared_globals.items():
            lines.append(f"    {cls.c_type_of(g_desc.get('type', 'u32'))} {g_name};")
        if not spec.args and not spec.declared_globals:
            lines.append("    int32_t _unused;")
        lines.append(f"}} Engine{cap_name}In;\n")

        lines.append(f"typedef struct Engine{cap_name}Out {{")
        if spec.return_type != "void":
            lines.append(f"    {cls.c_type_of(spec.return_type)} ret_value;")
        for g_name, g_desc in spec.declared_globals.items():
            lines.append(f"    {cls.c_type_of(g_desc.get('type', 'u32'))} post_{g_name};")
        lines.append(f"}} Engine{cap_name}Out;\n")

        lines.append(f"void engine_{spec.name}(const Engine{cap_name}In *in, Engine{cap_name}Out *out);")
        return "\n".join(lines)

    @classmethod
    def generate_c_shim(cls, spec: CallCaptureSpec) -> str:
        """Generates C implementation wrapper for diff_entry.c."""
        cap_name = "".join(part.capitalize() for part in spec.name.split("_"))
        lines = [
            f"void engine_{spec.name}(const Engine{cap_name}In *in, Engine{cap_name}Out *out)",
            "{",
        ]
        # Inject globals
        for g_name in spec.declared_globals:
            lines.append(f"    g_{g_name} = in->{g_name};")

        # Invoke function
        call_args = ", ".join(f"in->arg_{idx}" for idx in range(len(spec.args)))
        if spec.return_type == "void":
            lines.append(f"    {spec.target_symbol}({call_args});")
        else:
            lines.append(f"    out->ret_value = {spec.target_symbol}({call_args});")

        # Read back post globals
        for g_name in spec.declared_globals:
            lines.append(f"    out->post_{g_name} = g_{g_name};")

        lines.append("}")
        return "\n".join(lines)

    @classmethod
    def generate_dynamic_ctypes_adapter(
        cls, spec: CallCaptureSpec, c_fn_callable: Callable[..., Any]
    ) -> Callable[[Dict[str, Any]], Dict[str, Any]]:
        """Dynamically creates a runtime ctypes adapter function for the given spec."""
        in_fields = []
        for idx, arg_t in enumerate(spec.arg_types):
            in_fields.append((f"arg_{idx}", cls.ctypes_type_of(arg_t)))
        for g_name, g_desc in spec.declared_globals.items():
            in_fields.append((g_name, cls.ctypes_type_of(g_desc.get("type", "u32"))))
        if not in_fields:
            in_fields.append(("_unused", ctypes.c_int32))

        out_fields = []
        if spec.return_type != "void":
            out_fields.append(("ret_value", cls.ctypes_type_of(spec.return_type)))
        for g_name, g_desc in spec.declared_globals.items():
            out_fields.append((f"post_{g_name}", cls.ctypes_type_of(g_desc.get("type", "u32"))))

        class DynamicIn(ctypes.Structure):
            _fields_ = in_fields

        class DynamicOut(ctypes.Structure):
            _fields_ = out_fields

        def dynamic_adapter(vector: Dict[str, Any]) -> Dict[str, Any]:
            in_inst = DynamicIn()
            for idx in range(len(spec.args)):
                key = f"arg_{idx}"
                if key in vector:
                    setattr(in_inst, key, vector[key])
            for g_name in spec.declared_globals:
                if g_name in vector:
                    setattr(in_inst, g_name, vector[g_name])

            out_inst = DynamicOut()
            # Call wrapped callable
            c_fn_callable(ctypes.pointer(in_inst), ctypes.pointer(out_inst))
            res = {}
            if spec.return_type != "void":
                res["return_val"] = getattr(out_inst, "ret_value")
            for g_name in spec.declared_globals:
                res[g_name] = getattr(out_inst, f"post_{g_name}")
            return res

        return dynamic_adapter


# ─── 2. Boundary Mutation Generator ──────────────────────────────────────────

class BoundaryMutator:
    """Generates boundary-probing test vectors around observed values."""

    INTEGER_EDGES = [0, 1, -1, 2, -2, 5, 10, 0x17, 0x2b, 0x31, 0x3b, 0x49, 100, 1000, 0x7fffffff, -0x80000000]
    RNG_SEED_EDGES = [0, 1, 19937, 12345, 0xaaaaaaaa, 0x55555555, 0x7fffffff, 0x80000000, 0xffffffff]

    @classmethod
    def mutate_integer(cls, val: int) -> List[int]:
        candidates = {
            val,
            val - 1,
            val + 1,
            val - 5,
            val + 5,
            -val,
            val ^ 0xff,
        }
        candidates.update(cls.INTEGER_EDGES)
        # Wrap into signed 32-bit int
        res = []
        for v in candidates:
            v &= 0xffffffff
            if v & 0x80000000:
                v -= 0x100000000
            res.append(v)
        return sorted(set(res))

    @classmethod
    def mutate_seed(cls, seed: int) -> List[int]:
        candidates = {seed, seed - 1, seed + 1, (seed * 2) & 0xffffffff, seed ^ 0xffffffff}
        candidates.update(cls.RNG_SEED_EDGES)
        return sorted(set(c & 0xffffffff for c in candidates))

    @classmethod
    def generate_boundary_vectors(cls, spec: CallCaptureSpec, max_count: int = 25) -> List[Dict[str, Any]]:
        """Generates systematic boundary-probing input vectors for the given spec."""
        vectors: List[Dict[str, Any]] = []

        # Base observed vector
        base_vec: Dict[str, Any] = {}
        for idx, a in enumerate(spec.args):
            base_vec[f"arg_{idx}"] = a
        for g_name, g_desc in spec.declared_globals.items():
            base_vec[g_name] = g_desc.get("val", 0)

        vectors.append(copy.deepcopy(base_vec))

        # Mutate each integer argument independently
        for idx, (a, a_t) in enumerate(zip(spec.args, spec.arg_types)):
            if a_t in {"int", "s32", "u32", "uint32"}:
                for m_val in cls.mutate_integer(int(a)):
                    v = copy.deepcopy(base_vec)
                    v[f"arg_{idx}"] = m_val
                    vectors.append(v)

        # Mutate each declared global
        for g_name, g_desc in spec.declared_globals.items():
            g_val = g_desc.get("val", 0)
            if "seed" in g_name.lower() or "006023a0" in g_name:
                for m_seed in cls.mutate_seed(int(g_val)):
                    v = copy.deepcopy(base_vec)
                    v[g_name] = m_seed
                    vectors.append(v)
            else:
                for m_val in cls.mutate_integer(int(g_val)):
                    v = copy.deepcopy(base_vec)
                    v[g_name] = m_val
                    vectors.append(v)

        # Deduplicate and sample
        seen = set()
        unique_vectors = []
        for vec in vectors:
            key = tuple(sorted((k, str(v)) for k, v in vec.items()))
            if key not in seen:
                seen.add(key)
                unique_vectors.append(vec)

        return unique_vectors[:max_count]


# ─── 3. Deliberate Divergence Injection & Verification ───────────────────────

class DivergenceInjector:
    """Injects precise, deliberate corruptions into CallCapsules to test failure detection."""

    @staticmethod
    def inject_return_mismatch(capsule: CallCapsule) -> CallCapsule:
        """Corrupts the expected return value."""
        mut = copy.deepcopy(capsule)
        if isinstance(mut.return_val, int):
            mut.return_val += 1
        elif isinstance(mut.return_val, float):
            mut.return_val += 1.0
        else:
            mut.return_val = 0xbadf00d
        return mut

    @staticmethod
    def inject_poststate_mismatch(capsule: CallCapsule) -> CallCapsule:
        """Corrupts an expected poststate global."""
        mut = copy.deepcopy(capsule)
        if mut.poststate:
            key = next(iter(mut.poststate))
            mut.poststate[key] = 0xdeadbeef
        else:
            mut.poststate["DAT_CORRUPT"] = 0xbad
        return mut

    @staticmethod
    def inject_write_order_mismatch(capsule: CallCapsule) -> CallCapsule:
        """Reverses the order of recorded writes."""
        mut = copy.deepcopy(capsule)
        if len(mut.ordered_writes) >= 2:
            mut.ordered_writes.reverse()
            for idx, w in enumerate(mut.ordered_writes):
                w.seq = idx
        return mut
