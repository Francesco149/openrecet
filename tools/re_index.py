#!/usr/bin/env python3
"""Offline static reverse-engineering index for OpenRecet (CV-01 / CV-02).

Indexes all 2,620 engine functions from docs/decompiled/functions.csv,
docs/decompiled/by-address/*.c, and the reference binary (recettear.unpacked.exe)
or Ghidra JSON export artifacts into a fast, queryable SQLite database
(docs/re-index.sqlite) with Python API and CLI.

Provides instant answers for:
  - info <va|name>: function summary, size, calling convention, thunk status, callers/callees/xrefs count, byte hash, port status
  - blocks <va|name>: basic blocks within function (start, end, size, instruction count, flow type, is_entry, is_exit)
  - flows <va|name>: control flow graph edges (src_block -> dst_block, flow type)
  - switches [<va|name>]: switch jump tables and target cases
  - data-xrefs <va|DAT_va>: memory and global data cross-references with READ/WRITE/DATA access types
  - hash <va|name>: SHA-256 byte hash of function machine code
  - text <va|name> [-n]: print decompiled C source text of function
  - disasm <va|name> [--att]: print objdump disassembly of function
  - callers <va|name>: all functions that call target
  - callees <va|name>: all functions called by target
  - xrefs <DAT_va|s_name|hex_va>: all functions reading/writing a global or string
  - tree <va|name> [--depth N]: call tree up to depth N
  - unported-callees <va|name>: callees of target that are not yet implemented in port
  - export-json [--out <dir>]: export deterministic JSON tables (CV-01)
  - import-json <path|dir>: import JSON tables into SQLite index
  - coverage [--unimplemented] [--unexecuted]: 2-axis inventory and runtime proof coverage report with CV-08 calibration gating
  - calibrate [--scenario <name>] [--mode <mode>] [--cross-check-call-trace <path>]: CV-08 coverage truth calibration
  - prioritize [--front <name>] [--kind <all|functions|edges|semantics|scenarios>]: CV-07 candidate prioritizer
  - search <query> [--code]: regex/substring search over functions, globals, strings, and code
  - stats: overall index statistics, basic block counts, CFG edge counts, and runtime breakdown

Run from repo root:
  nix develop --command python3 tools/re_index.py build
  nix develop --command python3 tools/re_index.py info 0x4905a8
  nix develop --command python3 tools/re_index.py blocks 0x4905a8
  nix develop --command python3 tools/re_index.py flows 0x4905a8
  nix develop --command python3 tools/re_index.py callers 0x4905a8
  nix develop --command python3 tools/re_index.py xrefs DAT_056e6280
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sqlite3
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

REPO = Path(__file__).resolve().parent.parent
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

DECOMPILED_DIR = REPO / "docs" / "decompiled"
FUNCTIONS_CSV = DECOMPILED_DIR / "functions.csv"
BY_ADDRESS_DIR = DECOMPILED_DIR / "by-address"
DB_PATH = REPO / "docs" / "re-index.sqlite"
PORT_LEDGER_JSON = REPO / "docs" / "port-ledger.json"
UNPACKED_EXE = REPO / "vendor" / "unpacked" / "recettear.unpacked.exe"

FUN_RE = re.compile(r"\bFUN_([0-9a-fA-F]{8})\b")
DAT_RE = re.compile(r"\b(?:DAT|_DAT)_([0-9a-fA-F]{8})\b")
STR_RE = re.compile(r"\b(s_[A-Za-z0-9_]+)\b")

# ─── x86-32 Decoder Constants for Static Extraction ──────────────────────────

ONE_BYTE_MODRM = {
    0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x11, 0x12, 0x13,
    0x18, 0x19, 0x1a, 0x1b, 0x20, 0x21, 0x22, 0x23, 0x28, 0x29, 0x2a, 0x2b,
    0x30, 0x31, 0x32, 0x33, 0x38, 0x39, 0x3a, 0x3b, 0x62, 0x63, 0x69, 0x6b,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
    0x8c, 0x8d, 0x8e, 0x8f, 0xc0, 0xc1, 0xc4, 0xc5, 0xc6, 0xc7, 0xd0, 0xd1,
    0xd2, 0xd3, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xf6, 0xf7,
    0xfe, 0xff
}

PREFIXES = {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3}


def parse_va(val: str | int | None) -> int:
    if val is None:
        return 0
    if isinstance(val, int):
        return val
    s = str(val).strip()
    if not s or s.lower() in ("none", "null", "n/a", "0x0", "0"):
        return 0
    for prefix in ("fun_", "dat_", "_dat_", "ptr_", "_ptr_", "s_", "0x"):
        if s.lower().startswith(prefix):
            s = s[len(prefix):]
            break
    try:
        return int(s, 16)
    except ValueError:
        return 0
def format_va(va: int) -> str:
    return f"0x{va:06x}"


def format_fun(va: int) -> str:
    return f"FUN_{va:08x}"


# ─── x86 Instruction Decoder ────────────────────────────────────────────────

def decode_instruction(code_bytes: bytes, pos: int, va: int) -> Optional[Dict[str, Any]]:
    """Decodes length, flow type, branch target, and memory displacement of one x86 instruction."""
    start_pos = pos
    n = len(code_bytes)
    if pos >= n:
        return None

    op_size = 32
    addr_size = 32
    while pos < n and code_bytes[pos] in PREFIXES:
        p = code_bytes[pos]
        if p == 0x66:
            op_size = 16
        elif p == 0x67:
            addr_size = 16
        pos += 1

    if pos >= n:
        return {"len": n - start_pos, "va": va, "type": "UNKNOWN", "target": None, "disp": None, "is_write": False}

    b0 = code_bytes[pos]
    pos += 1

    # Two-byte opcodes (0x0F ...)
    if b0 == 0x0F:
        if pos >= n:
            return {"len": n - start_pos, "va": va, "type": "UNKNOWN", "target": None, "disp": None, "is_write": False}
        b1 = code_bytes[pos]
        pos += 1

        # 0x0F 0x80..0x8F: Jcc rel32
        if 0x80 <= b1 <= 0x8F:
            if pos + 4 <= n:
                rel = struct.unpack_from("<i", code_bytes, pos)[0]
                pos += 4
                target = va + (pos - start_pos) + rel
                return {"len": pos - start_pos, "va": va, "type": "COND_JUMP", "target": target, "disp": None, "is_write": False}

        has_modrm = b1 in {
            0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x2F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
            0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51,
            0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D,
            0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
            0x6A, 0x6B, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
            0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
            0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
            0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA3, 0xAB,
            0xAF, 0xB0, 0xB1, 0xB3, 0xB6, 0xB7, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0,
            0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7
        }
        disp = None
        is_write = (b1 in (0x11, 0x29, 0x7F, 0x89))
        if has_modrm and pos < n:
            modrm = code_bytes[pos]
            pos += 1
            mod = (modrm >> 6) & 3
            rm = modrm & 7
            if mod == 0 and rm == 5:
                if pos + 4 <= n:
                    disp = struct.unpack_from("<I", code_bytes, pos)[0]
                    pos += 4
            elif rm == 4 and mod != 3:
                if pos < n:
                    sib = code_bytes[pos]
                    pos += 1
                    base = sib & 7
                    if mod == 0 and base == 5:
                        if pos + 4 <= n:
                            disp = struct.unpack_from("<I", code_bytes, pos)[0]
                            pos += 4
                    elif mod == 1:
                        pos += 1
                    elif mod == 2:
                        if pos + 4 <= n:
                            disp = struct.unpack_from("<I", code_bytes, pos)[0]
                            pos += 4
            elif mod == 1:
                pos += 1
            elif mod == 2:
                if pos + 4 <= n:
                    disp = struct.unpack_from("<I", code_bytes, pos)[0]
                    pos += 4
        return {"len": max(1, pos - start_pos), "va": va, "type": "OTHER", "target": None, "disp": disp, "is_write": is_write}

    # 1-byte opcode checks
    # Returns (RET, RETF, IRET)
    if b0 in (0xC3, 0xCB, 0xCF):
        return {"len": pos - start_pos, "va": va, "type": "RET", "target": None, "disp": None, "is_write": False}
    if b0 in (0xC2, 0xCA):
        pos += 2
        return {"len": pos - start_pos, "va": va, "type": "RET", "target": None, "disp": None, "is_write": False}

    # Jumps & Calls
    if b0 == 0xEB:  # JMP rel8
        if pos < n:
            rel = struct.unpack_from("<b", code_bytes, pos)[0]
            pos += 1
            target = va + (pos - start_pos) + rel
            return {"len": pos - start_pos, "va": va, "type": "JUMP", "target": target, "disp": None, "is_write": False}
    if b0 == 0xE9:  # JMP rel32
        if pos + 4 <= n:
            rel = struct.unpack_from("<i", code_bytes, pos)[0]
            pos += 4
            target = va + (pos - start_pos) + rel
            return {"len": pos - start_pos, "va": va, "type": "JUMP", "target": target, "disp": None, "is_write": False}
    if b0 == 0xE8:  # CALL rel32
        if pos + 4 <= n:
            rel = struct.unpack_from("<i", code_bytes, pos)[0]
            pos += 4
            target = va + (pos - start_pos) + rel
            return {"len": pos - start_pos, "va": va, "type": "CALL", "target": target, "disp": None, "is_write": False}
    if (0x70 <= b0 <= 0x7F) or b0 == 0xE3:  # Jcc rel8 / JCXZ
        if pos < n:
            rel = struct.unpack_from("<b", code_bytes, pos)[0]
            pos += 1
            target = va + (pos - start_pos) + rel
            return {"len": pos - start_pos, "va": va, "type": "COND_JUMP", "target": target, "disp": None, "is_write": False}

    # Immediates on fixed opcodes
    imm_bytes = 0
    if b0 in (0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0x6A, 0xA8, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xC6, 0xD4, 0xD5):
        imm_bytes = 1
    elif b0 in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D, 0x68, 0xA9, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC7):
        imm_bytes = 2 if op_size == 16 else 4
    elif b0 in (0xA0, 0xA1, 0xA2, 0xA3):  # MOV AL/eAX, moffs32
        imm_bytes = 4

    disp = None
    is_write = (b0 in (0x88, 0x89, 0xA2, 0xA3, 0xC6, 0xC7))
    is_indirect_call = False
    is_indirect_jump = False

    if b0 in ONE_BYTE_MODRM and pos < n:
        modrm = code_bytes[pos]
        pos += 1
        mod = (modrm >> 6) & 3
        reg = (modrm >> 3) & 7
        rm = modrm & 7

        if b0 == 0xFF:
            if reg == 2:  # CALL r/m32
                is_indirect_call = True
            elif reg == 4:  # JMP r/m32 (jump table / switch)
                is_indirect_jump = True

        if b0 in (0x80, 0x82, 0x83, 0xC0, 0xC1, 0xC6):
            imm_bytes = 1
        elif b0 in (0x81, 0xC7):
            imm_bytes = 2 if op_size == 16 else 4
        elif b0 == 0x69:
            imm_bytes = 2 if op_size == 16 else 4
        elif b0 == 0x6B:
            imm_bytes = 1

        if mod == 0 and rm == 5:
            if pos + 4 <= n:
                disp = struct.unpack_from("<I", code_bytes, pos)[0]
                pos += 4
        elif rm == 4 and mod != 3:
            if pos < n:
                sib = code_bytes[pos]
                pos += 1
                base = sib & 7
                if mod == 0 and base == 5:
                    if pos + 4 <= n:
                        disp = struct.unpack_from("<I", code_bytes, pos)[0]
                        pos += 4
                elif mod == 1:
                    pos += 1
                elif mod == 2:
                    if pos + 4 <= n:
                        disp = struct.unpack_from("<I", code_bytes, pos)[0]
                        pos += 4
        elif mod == 1:
            pos += 1
        elif mod == 2:
            if pos + 4 <= n:
                disp = struct.unpack_from("<I", code_bytes, pos)[0]
                pos += 4

    pos += imm_bytes
    inst_type = "CALL_INDIRECT" if is_indirect_call else ("JUMP_INDIRECT" if is_indirect_jump else "OTHER")
    return {
        "len": max(1, pos - start_pos),
        "va": va,
        "type": inst_type,
        "target": None,
        "disp": disp,
        "is_write": is_write,
    }


# ─── ReIndex Database Class ─────────────────────────────────────────────────

class ReIndex:
    """Fast, queryable SQLite static reverse engineering index (CV-01 / CV-02)."""

    def __init__(self, db_path: Path = DB_PATH):
        self.db_path = Path(db_path)
        self._conn: Optional[sqlite3.Connection] = None

    def connect(self) -> sqlite3.Connection:
        if self._conn is None:
            if not self.db_path.exists():
                self.build()
            self._conn = sqlite3.connect(str(self.db_path))
            self._conn.row_factory = sqlite3.Row
        return self._conn

    def close(self):
        if self._conn:
            self._conn.close()
            self._conn = None

    def build(self, force: bool = False, exe_path: Optional[Path] = None) -> Dict[str, Any]:
        """Build or rebuild docs/re-index.sqlite from reference exe, Ghidra JSON export, or decompiled C."""
        if self.db_path.exists() and not force:
            conn = sqlite3.connect(str(self.db_path))
            conn.row_factory = sqlite3.Row
            try:
                cur = conn.cursor()
                cur.execute("SELECT value FROM metadata WHERE key='func_count'")
                row = cur.fetchone()
                if row and int(row[0]) > 2500:
                    conn.close()
                    return {"status": "up-to-date", "path": str(self.db_path)}
            except Exception:
                pass
            conn.close()

        if self.db_path.exists():
            try:
                self.db_path.unlink()
            except OSError:
                pass

        if exe_path is None:
            exe_path = UNPACKED_EXE

        conn = sqlite3.connect(str(self.db_path))
        cur = conn.cursor()

        # Schema Creation
        cur.execute("""
            CREATE TABLE metadata (
                key TEXT PRIMARY KEY,
                value TEXT
            )
        """)

        cur.execute("""
            CREATE TABLE functions (
                va INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                size INTEGER NOT NULL,
                is_thunk INTEGER NOT NULL,
                calling_conv TEXT,
                file_path TEXT,
                line_count INTEGER,
                return_type TEXT DEFAULT 'unknown',
                param_count INTEGER DEFAULT 0,
                byte_hash TEXT,
                port_status TEXT,
                runtime_status TEXT
            )
        """)

        cur.execute("""
            CREATE TABLE blocks (
                block_va INTEGER NOT NULL,
                func_va INTEGER NOT NULL,
                end_va INTEGER NOT NULL,
                size INTEGER NOT NULL,
                instruction_count INTEGER NOT NULL,
                is_entry INTEGER NOT NULL,
                is_exit INTEGER NOT NULL,
                flow_type TEXT NOT NULL,
                PRIMARY KEY (block_va, func_va)
            )
        """)

        cur.execute("""
            CREATE TABLE flows (
                src_va INTEGER NOT NULL,
                dst_va INTEGER NOT NULL,
                func_va INTEGER NOT NULL,
                flow_type TEXT NOT NULL,
                PRIMARY KEY (src_va, dst_va, func_va)
            )
        """)

        cur.execute("""
            CREATE TABLE calls (
                caller_va INTEGER NOT NULL,
                callee_va INTEGER NOT NULL,
                call_site_va INTEGER,
                call_type TEXT DEFAULT 'DIRECT',
                PRIMARY KEY (caller_va, callee_va, call_site_va)
            )
        """)

        cur.execute("""
            CREATE TABLE global_xrefs (
                func_va INTEGER NOT NULL,
                global_va INTEGER NOT NULL,
                access_type TEXT DEFAULT 'READ',
                count INTEGER DEFAULT 1,
                PRIMARY KEY (func_va, global_va, access_type)
            )
        """)

        cur.execute("""
            CREATE TABLE data_xrefs (
                func_va INTEGER NOT NULL,
                site_va INTEGER NOT NULL,
                data_va INTEGER NOT NULL,
                access_type TEXT NOT NULL,
                PRIMARY KEY (func_va, site_va, data_va, access_type)
            )
        """)

        cur.execute("""
            CREATE TABLE string_xrefs (
                func_va INTEGER NOT NULL,
                string_name TEXT NOT NULL,
                string_va INTEGER,
                value TEXT,
                PRIMARY KEY (func_va, string_name)
            )
        """)

        cur.execute("""
            CREATE TABLE switch_cases (
                func_va INTEGER NOT NULL,
                switch_va INTEGER NOT NULL,
                case_val INTEGER NOT NULL,
                target_va INTEGER NOT NULL,
                PRIMARY KEY (func_va, switch_va, case_val, target_va)
            )
        """)

        # Indexes for Sub-Millisecond Query Performance
        cur.execute("CREATE INDEX idx_blocks_func ON blocks(func_va)")
        cur.execute("CREATE INDEX idx_flows_src ON flows(src_va)")
        cur.execute("CREATE INDEX idx_flows_dst ON flows(dst_va)")
        cur.execute("CREATE INDEX idx_flows_func ON flows(func_va)")
        cur.execute("CREATE INDEX idx_calls_callee ON calls(callee_va)")
        cur.execute("CREATE INDEX idx_calls_caller ON calls(caller_va)")
        cur.execute("CREATE INDEX idx_global_xrefs_global ON global_xrefs(global_va)")
        cur.execute("CREATE INDEX idx_data_xrefs_data ON data_xrefs(data_va)")
        cur.execute("CREATE INDEX idx_string_xrefs_str ON string_xrefs(string_name)")
        cur.execute("CREATE INDEX idx_switch_cases_func ON switch_cases(func_va)")
        cur.execute("CREATE INDEX idx_switch_cases_switch ON switch_cases(switch_va)")

        # Load Port Ledger Inventory & Runtime Statuses
        port_statuses: Dict[int, str] = {}
        runtime_statuses: Dict[int, Optional[str]] = {}
        if PORT_LEDGER_JSON.exists():
            try:
                ledger = json.loads(PORT_LEDGER_JSON.read_text(encoding="utf-8"))
                fn_list = ledger.get("functions", [])
                if isinstance(fn_list, list):
                    for entry in fn_list:
                        va = parse_va(entry["va"])
                        port_statuses[va] = entry.get("inventory_state", "discovered")
                        runtime_statuses[va] = entry.get("runtime_state")
                elif isinstance(fn_list, dict):
                    for va_hex, entry in fn_list.items():
                        va = parse_va(va_hex)
                        port_statuses[va] = entry.get("inventory_state", "discovered")
                        runtime_statuses[va] = entry.get("runtime_state")
            except Exception:
                pass

        # Load functions.csv
        funcs: Dict[int, Dict[str, Any]] = {}
        if FUNCTIONS_CSV.exists():
            with FUNCTIONS_CSV.open(encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    va = parse_va(row["entry"])
                    funcs[va] = {
                        "va": va,
                        "name": row.get("name", format_fun(va)),
                        "size": int(row.get("size") or 0),
                        "is_thunk": 1 if row.get("is_thunk") == "true" else 0,
                        "calling_conv": row.get("calling_conv", "unknown"),
                    }

        # Check for Ghidra JSON export artifacts in docs/decompiled/
        json_manifest = DECOMPILED_DIR / "manifest.json"
        if json_manifest.exists() and (DECOMPILED_DIR / "blocks.json").exists():
            # Ingest from Ghidra JSON export
            return self._import_from_json_tables(conn, DECOMPILED_DIR, port_statuses, runtime_statuses)

        # Ingest via Direct x86 Binary Disassembly & Static Analysis
        if exe_path.exists():
            return self._import_from_pe_binary(conn, exe_path, funcs, port_statuses, runtime_statuses)

        # Fallback: Parse decompiled C source files
        return self._import_from_decompiled_c(conn, funcs, port_statuses, runtime_statuses)

    def _import_from_pe_binary(
        self,
        conn: sqlite3.Connection,
        exe_path: Path,
        funcs: Dict[int, Dict[str, Any]],
        port_statuses: Dict[int, str],
        runtime_statuses: Dict[int, Optional[str]],
    ) -> Dict[str, Any]:
        """Extracts complete basic blocks, flow edges, calls, xrefs, and hashes from PE binary."""
        cur = conn.cursor()
        data = exe_path.read_bytes()
        exe_hash = hashlib.sha256(data).hexdigest()

        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        opt_hdr_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
        image_base = struct.unpack_from("<I", data, e_lfanew + 24 + 28)[0]
        num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
        sec_hdr_start = e_lfanew + 24 + opt_hdr_size

        sections: Dict[str, Dict[str, int]] = {}
        for i in range(num_sections):
            sec = data[sec_hdr_start + i * 40 : sec_hdr_start + (i + 1) * 40]
            name = sec[:8].rstrip(b"\x00").decode("latin-1")
            vsize, rva, raw_size, raw_ptr = struct.unpack_from("<IIII", sec, 8)
            sections[name] = {"va": image_base + rva, "size": vsize, "raw_ptr": raw_ptr, "raw_size": raw_size}

        def va_to_offset(va: int) -> Optional[int]:
            for s in sections.values():
                if s["va"] <= va < s["va"] + s["size"]:
                    return s["raw_ptr"] + (va - s["va"])
            return None

        def read_string_at(va: int) -> Optional[str]:
            off = va_to_offset(va)
            if off is None:
                return None
            chunk = data[off : off + 128]
            null_pos = chunk.find(b"\x00")
            if null_pos <= 0:
                return None
            raw_s = chunk[:null_pos]
            try:
                # Require printable characters
                s_val = raw_s.decode("latin-1")
                if all(0x20 <= ord(c) <= 0x7E or c in "\r\n\t" for c in s_val) and len(s_val) >= 2:
                    return s_val
            except Exception:
                pass
            return None

        func_rows = []
        block_rows = []
        flow_rows: Set[Tuple[int, int, int, str]] = set()
        call_rows: Set[Tuple[int, int, Optional[int], str]] = set()
        data_xref_rows: Set[Tuple[int, int, int, str]] = set()
        global_xref_rows: Dict[Tuple[int, int, str], int] = {}
        string_xref_rows: Set[Tuple[int, str, Optional[int], Optional[str]]] = set()
        switch_case_rows: Set[Tuple[int, int, int, int]] = set()

        for va, meta in funcs.items():
            f_size = meta["size"]
            f_name = meta["name"]
            is_thunk = meta["is_thunk"]
            calling_conv = meta["calling_conv"]
            p_status = port_statuses.get(va, "discovered")
            rt_status = runtime_statuses.get(va)

            decomp_cand = BY_ADDRESS_DIR / f"{va:06x}.c"
            if not decomp_cand.exists():
                decomp_cand = BY_ADDRESS_DIR / f"{va:x}.c"
            file_path = str(decomp_cand.relative_to(REPO)) if decomp_cand.exists() else None
            if decomp_cand.exists():
                try:
                    content = decomp_cand.read_text(encoding="utf-8", errors="replace")
                    line_count = len(content.splitlines())

                    for m in STR_RE.finditer(content):
                        s_name = m.group(1)
                        s_va = None
                        m_va = re.search(r"00([0-9a-fA-F]{6})$", s_name)
                        if m_va:
                            s_va = int(m_va.group(1), 16)
                        str_val = read_string_at(s_va) if s_va else None
                        string_xref_rows.add((va, s_name, s_va, str_val))

                    for m in DAT_RE.finditer(content):
                        g_va = parse_va(m.group(1))
                        g_key = (va, g_va, "READ")
                        global_xref_rows[g_key] = global_xref_rows.get(g_key, 0) + 1

                    for m in FUN_RE.finditer(content):
                        c_va = parse_va(m.group(1))
                        if c_va != va:
                            call_rows.add((va, c_va, None, "DIRECT"))
                except Exception:
                    pass
            byte_hash = None
            if f_size > 0:
                off = va_to_offset(va)
                if off is not None:
                    code_bytes = data[off : off + f_size]
                    byte_hash = hashlib.sha256(code_bytes).hexdigest()

                    # Disassemble instructions and partition into basic blocks
                    pos = 0
                    cur_va = va
                    inst_list: List[Dict[str, Any]] = []
                    block_starts: Set[int] = {va}

                    while pos < len(code_bytes):
                        inst = decode_instruction(code_bytes, pos, cur_va)
                        if not inst:
                            break
                        inst_list.append(inst)
                        ilen = inst["len"]
                        nxt_va = cur_va + ilen

                        # Branch target splits
                        if inst["type"] in ("COND_JUMP", "JUMP"):
                            if inst["target"] is not None and va <= inst["target"] < va + f_size:
                                block_starts.add(inst["target"])
                            block_starts.add(nxt_va)
                        elif inst["type"] == "RET":
                            block_starts.add(nxt_va)

                        # Call edges
                        if inst["type"] == "CALL" and inst["target"] is not None:
                            call_rows.add((va, inst["target"], cur_va, "DIRECT"))
                        elif inst["type"] == "CALL_INDIRECT":
                            call_rows.add((va, 0, cur_va, "INDIRECT"))

                        # Memory/Data Xrefs & Strings
                        disp = inst.get("disp")
                        if disp and 0x00400000 <= disp < 0x0A000000:
                            access = "WRITE" if inst.get("is_write") else "READ"
                            data_xref_rows.add((va, cur_va, disp, access))
                            g_key = (va, disp, access)
                            global_xref_rows[g_key] = global_xref_rows.get(g_key, 0) + 1

                            # Check for string at displacement
                            str_val = read_string_at(disp)
                            if str_val:
                                s_name = f"s_{disp:08x}"
                                string_xref_rows.add((va, s_name, disp, str_val))

                        pos += ilen
                        cur_va = nxt_va

                    # Assemble contiguous basic blocks
                    sorted_starts = sorted(b for b in block_starts if va <= b < va + f_size)
                    for idx, b_start in enumerate(sorted_starts):
                        b_end = (sorted_starts[idx + 1] - 1) if idx + 1 < len(sorted_starts) else (va + f_size - 1)
                        b_size = max(1, b_end - b_start + 1)
                        is_entry = 1 if b_start == va else 0

                        # Instructions in this block
                        b_insts = [i for i in inst_list if b_start <= i["va"] <= b_end]
                        inst_cnt = len(b_insts)
                        last_inst = b_insts[-1] if b_insts else None

                        flow_type = "FALL_THROUGH"
                        is_exit = 0

                        if last_inst:
                            if last_inst["type"] == "RET":
                                flow_type = "RETURN"
                                is_exit = 1
                            elif last_inst["type"] == "JUMP":
                                flow_type = "UNCONDITIONAL_JUMP"
                                if last_inst["target"] is not None:
                                    flow_rows.add((b_start, last_inst["target"], va, "UNCONDITIONAL_JUMP"))
                            elif last_inst["type"] == "COND_JUMP":
                                flow_type = "CONDITIONAL_JUMP"
                                if last_inst["target"] is not None:
                                    flow_rows.add((b_start, last_inst["target"], va, "BRANCH_TAKEN"))
                                nxt_ip = last_inst["va"] + last_inst["len"]
                                if va <= nxt_ip < va + f_size:
                                    flow_rows.add((b_start, nxt_ip, va, "FALL_THROUGH"))
                            elif last_inst["type"] == "JUMP_INDIRECT":
                                flow_type = "COMPUTED_JUMP"

                        if flow_type == "FALL_THROUGH" and idx + 1 < len(sorted_starts):
                            flow_rows.add((b_start, sorted_starts[idx + 1], va, "FALL_THROUGH"))

                        block_rows.append((
                            b_start,
                            va,
                            b_end,
                            b_size,
                            inst_cnt,
                            is_entry,
                            is_exit,
                            flow_type,
                        ))
            else:
                # Size 0 stub/thunk entry
                block_rows.append((va, va, va, 1, 0, 1, 1, "TERMINAL"))

            func_rows.append((
                va,
                f_name,
                f_size,
                is_thunk,
                calling_conv,
                file_path,
                line_count,
                "void" if is_thunk else "int",
                0,
                byte_hash,
                p_status,
                rt_status,
            ))

        # Bulk Inserts
        cur.executemany(
            "INSERT INTO functions (va, name, size, is_thunk, calling_conv, file_path, line_count, return_type, param_count, byte_hash, port_status, runtime_status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            func_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO blocks (block_va, func_va, end_va, size, instruction_count, is_entry, is_exit, flow_type) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            block_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO flows (src_va, dst_va, func_va, flow_type) VALUES (?, ?, ?, ?)",
            list(flow_rows),
        )
        cur.executemany(
            "INSERT OR IGNORE INTO calls (caller_va, callee_va, call_site_va, call_type) VALUES (?, ?, ?, ?)",
            list(call_rows),
        )
        cur.executemany(
            "INSERT OR IGNORE INTO data_xrefs (func_va, site_va, data_va, access_type) VALUES (?, ?, ?, ?)",
            list(data_xref_rows),
        )
        cur.executemany(
            "INSERT OR IGNORE INTO global_xrefs (func_va, global_va, access_type, count) VALUES (?, ?, ?, ?)",
            [(f_va, g_va, acc, cnt) for (f_va, g_va, acc), cnt in global_xref_rows.items()],
        )
        cur.executemany(
            "INSERT OR IGNORE INTO string_xrefs (func_va, string_name, string_va, value) VALUES (?, ?, ?, ?)",
            list(string_xref_rows),
        )

        cur.execute("INSERT INTO metadata (key, value) VALUES ('schema_version', 'cv01-v1.0')")
        cur.execute("INSERT INTO metadata (key, value) VALUES ('executable_sha256', ?)", (exe_hash,))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('func_count', ?)", (str(len(func_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('block_count', ?)", (str(len(block_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('flow_count', ?)", (str(len(flow_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('call_count', ?)", (str(len(call_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('data_xref_count', ?)", (str(len(data_xref_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('global_xref_count', ?)", (str(len(global_xref_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('string_xref_count', ?)", (str(len(string_xref_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('built_at', ?)", (str(int(time.time())),))

        conn.commit()
        conn.close()

        return {
            "status": "built",
            "source": "pe_binary",
            "functions": len(func_rows),
            "blocks": len(block_rows),
            "flows": len(flow_rows),
            "calls": len(call_rows),
            "data_xrefs": len(data_xref_rows),
            "global_xrefs": len(global_xref_rows),
            "string_xrefs": len(string_xref_rows),
            "path": str(self.db_path),
        }

    def _import_from_json_tables(
        self,
        conn: sqlite3.Connection,
        json_dir: Path,
        port_statuses: Dict[int, str],
        runtime_statuses: Dict[int, Optional[str]],
    ) -> Dict[str, Any]:
        """Loads index from Ghidra JSON export artifacts."""
        cur = conn.cursor()

        def load_json(name: str) -> List[Dict[str, Any]]:
            p = json_dir / name
            if p.exists():
                try:
                    return json.loads(p.read_text(encoding="utf-8"))
                except Exception:
                    pass
            return []

        funcs_json = load_json("functions.json")
        blocks_json = load_json("blocks.json")
        flows_json = load_json("flows.json")
        calls_json = load_json("calls.json")
        data_xrefs_json = load_json("data_xrefs.json")
        str_xrefs_json = load_json("string_xrefs.json")
        switches_json = load_json("switch_cases.json")

        func_rows = []
        for fn in funcs_json:
            va = parse_va(fn["va"])
            decomp_cand = BY_ADDRESS_DIR / f"{va:06x}.c"
            file_path = str(decomp_cand.relative_to(REPO)) if decomp_cand.exists() else None
            line_count = 0
            if decomp_cand.exists():
                try:
                    line_count = len(decomp_cand.read_text(encoding="utf-8", errors="replace").splitlines())
                except Exception:
                    pass
            func_rows.append((
                va,
                fn.get("name", format_fun(va)),
                int(fn.get("size", 0)),
                1 if fn.get("is_thunk") else 0,
                fn.get("calling_conv", "unknown"),
                file_path,
                line_count,
                fn.get("return_type", "void"),
                int(fn.get("param_count", 0)),
                fn.get("byte_hash"),
                port_statuses.get(va, "discovered"),
                runtime_statuses.get(va),
            ))

        block_rows = []
        for b in blocks_json:
            block_rows.append((
                parse_va(b["block_va"]),
                parse_va(b["func_va"]),
                parse_va(b["end_va"]),
                int(b.get("size", 1)),
                int(b.get("instruction_count", 0)),
                1 if b.get("is_entry") else 0,
                1 if b.get("is_exit") else 0,
                b.get("flow_type", "UNKNOWN"),
            ))

        flow_rows = []
        for fl in flows_json:
            flow_rows.append((
                parse_va(fl["src_va"]),
                parse_va(fl["dst_va"]),
                parse_va(fl["func_va"]),
                fl.get("flow_type", "FLOW"),
            ))

        call_rows = []
        for c in calls_json:
            site_val = c.get("call_site_va")
            call_rows.append((
                parse_va(c["caller_va"]),
                parse_va(c["callee_va"]),
                parse_va(site_val) if site_val is not None else None,
                c.get("call_type", "DIRECT"),
            ))

        data_xref_rows = []
        global_xref_map: Dict[Tuple[int, int, str], int] = {}
        for dx in data_xrefs_json:
            f_va = parse_va(dx["func_va"])
            s_va = parse_va(dx.get("site_va", 0))
            d_va = parse_va(dx["data_va"])
            acc = dx.get("access_type", "READ")
            data_xref_rows.append((f_va, s_va, d_va, acc))
            g_key = (f_va, d_va, acc)
            global_xref_map[g_key] = global_xref_map.get(g_key, 0) + 1

        str_xref_rows = []
        for sx in str_xrefs_json:
            str_va = sx.get("string_va")
            str_xref_rows.append((
                parse_va(sx["func_va"]),
                sx["string_name"],
                parse_va(str_va) if str_va is not None else None,
                sx.get("value"),
            ))

        switch_rows = []
        for sw in switches_json:
            switch_rows.append((
                parse_va(sw["func_va"]),
                parse_va(sw["switch_va"]),
                int(sw.get("case_val", 0)),
                parse_va(sw["target_va"]),
            ))

        cur.executemany(
            "INSERT INTO functions (va, name, size, is_thunk, calling_conv, file_path, line_count, return_type, param_count, byte_hash, port_status, runtime_status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            func_rows,
        )
        cur.executemany(
            "INSERT INTO blocks (block_va, func_va, end_va, size, instruction_count, is_entry, is_exit, flow_type) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            block_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO flows (src_va, dst_va, func_va, flow_type) VALUES (?, ?, ?, ?)",
            flow_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO calls (caller_va, callee_va, call_site_va, call_type) VALUES (?, ?, ?, ?)",
            call_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO data_xrefs (func_va, site_va, data_va, access_type) VALUES (?, ?, ?, ?)",
            data_xref_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO global_xrefs (func_va, global_va, access_type, count) VALUES (?, ?, ?, ?)",
            [(f_va, g_va, acc, cnt) for (f_va, g_va, acc), cnt in global_xref_map.items()],
        )
        cur.executemany(
            "INSERT OR IGNORE INTO string_xrefs (func_va, string_name, string_va, value) VALUES (?, ?, ?, ?)",
            str_xref_rows,
        )
        cur.executemany(
            "INSERT OR IGNORE INTO switch_cases (func_va, switch_va, case_val, target_va) VALUES (?, ?, ?, ?)",
            switch_rows,
        )

        cur.execute("INSERT INTO metadata (key, value) VALUES ('schema_version', 'cv01-v1.0')")
        cur.execute("INSERT INTO metadata (key, value) VALUES ('export_source', 'ghidra_json')")
        cur.execute("INSERT INTO metadata (key, value) VALUES ('func_count', ?)", (str(len(func_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('block_count', ?)", (str(len(block_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('flow_count', ?)", (str(len(flow_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('call_count', ?)", (str(len(call_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('data_xref_count', ?)", (str(len(data_xref_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('global_xref_count', ?)", (str(len(global_xref_map)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('string_xref_count', ?)", (str(len(str_xref_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('switch_count', ?)", (str(len(switch_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('built_at', ?)", (str(int(time.time())),))

        conn.commit()
        conn.close()

        return {
            "status": "built",
            "source": "ghidra_json",
            "functions": len(func_rows),
            "blocks": len(block_rows),
            "flows": len(flow_rows),
            "calls": len(call_rows),
            "data_xrefs": len(data_xref_rows),
            "global_xrefs": len(global_xref_map),
            "string_xrefs": len(str_xref_rows),
            "switch_cases": len(switch_rows),
            "path": str(self.db_path),
        }

    def _import_from_decompiled_c(
        self,
        conn: sqlite3.Connection,
        funcs: Dict[int, Dict[str, Any]],
        port_statuses: Dict[int, str],
        runtime_statuses: Dict[int, Optional[str]],
    ) -> Dict[str, Any]:
        """Fallback importer when raw binary is missing (extracts from decompiled C source)."""
        cur = conn.cursor()
        func_rows = []
        block_rows = []
        call_rows: Set[Tuple[int, int, Optional[int], str]] = set()
        global_rows: Set[Tuple[int, int, str, int]] = set()
        string_rows: Set[Tuple[int, str, Optional[int], Optional[str]]] = set()

        decomp_files = sorted(BY_ADDRESS_DIR.glob("*.c")) if BY_ADDRESS_DIR.exists() else []

        for fpath in decomp_files:
            va = parse_va(fpath.stem)
            content = fpath.read_text(encoding="utf-8", errors="replace")
            lines = content.splitlines()
            line_count = len(lines)

            meta = funcs.get(va, {
                "va": va,
                "name": format_fun(va),
                "size": 0,
                "is_thunk": 0,
                "calling_conv": "unknown",
            })
            p_status = port_statuses.get(va, "discovered")
            rt_status = runtime_statuses.get(va)

            func_rows.append((
                va,
                meta["name"],
                meta["size"],
                meta["is_thunk"],
                meta["calling_conv"],
                str(fpath.relative_to(REPO)),
                line_count,
                "int",
                0,
                None,
                p_status,
                rt_status,
            ))

            # Canonical entry basic block
            b_size = max(1, meta["size"])
            block_rows.append((va, va, va + b_size - 1, b_size, max(1, line_count), 1, 1, "FALL_THROUGH"))

            for m in FUN_RE.finditer(content):
                callee_va = parse_va(m.group(1))
                if callee_va != va:
                    call_rows.add((va, callee_va, None, "DIRECT"))

            for m in DAT_RE.finditer(content):
                g_va = parse_va(m.group(1))
                global_rows.add((va, g_va, "READ", 1))

            for m in STR_RE.finditer(content):
                s_name = m.group(1)
                string_rows.add((va, s_name, None, None))

        seen_vas = {r[0] for r in func_rows}
        for va, meta in funcs.items():
            if va not in seen_vas:
                func_rows.append((
                    va,
                    meta["name"],
                    meta["size"],
                    meta["is_thunk"],
                    meta["calling_conv"],
                    None,
                    0,
                    "int",
                    0,
                    None,
                    port_statuses.get(va, "discovered"),
                    runtime_statuses.get(va),
                ))
                block_rows.append((va, va, va + max(1, meta["size"]) - 1, max(1, meta["size"]), 0, 1, 1, "FALL_THROUGH"))

        cur.executemany(
            "INSERT INTO functions (va, name, size, is_thunk, calling_conv, file_path, line_count, return_type, param_count, byte_hash, port_status, runtime_status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            func_rows,
        )
        cur.executemany(
            "INSERT INTO blocks (block_va, func_va, end_va, size, instruction_count, is_entry, is_exit, flow_type) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            block_rows,
        )
        cur.executemany("INSERT OR IGNORE INTO calls (caller_va, callee_va, call_site_va, call_type) VALUES (?, ?, ?, ?)", list(call_rows))
        cur.executemany("INSERT OR IGNORE INTO global_xrefs (func_va, global_va, access_type, count) VALUES (?, ?, ?, ?)", list(global_rows))
        cur.executemany("INSERT OR IGNORE INTO string_xrefs (func_va, string_name, string_va, value) VALUES (?, ?, ?, ?)", list(string_rows))

        cur.execute("INSERT INTO metadata (key, value) VALUES ('schema_version', 'cv01-v1.0')")
        cur.execute("INSERT INTO metadata (key, value) VALUES ('export_source', 'decompiled_c_fallback')")
        cur.execute("INSERT INTO metadata (key, value) VALUES ('func_count', ?)", (str(len(func_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('block_count', ?)", (str(len(block_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('call_count', ?)", (str(len(call_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('global_xref_count', ?)", (str(len(global_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('string_xref_count', ?)", (str(len(string_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('built_at', ?)", (str(int(time.time())),))

        conn.commit()
        conn.close()

        return {
            "status": "built",
            "source": "decompiled_c_fallback",
            "functions": len(func_rows),
            "blocks": len(block_rows),
            "calls": len(call_rows),
            "global_xrefs": len(global_rows),
            "string_xrefs": len(string_rows),
            "path": str(self.db_path),
        }

    # ─── Query APIs ──────────────────────────────────────────────────────────

    def get_function(self, target: str | int) -> Optional[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        if isinstance(target, int):
            cur.execute("SELECT * FROM functions WHERE va=?", (target,))
        else:
            s = str(target).strip()
            if s.lower().startswith("0x") or s.lower().startswith("fun_") or all(c in "0123456789abcdefABCDEF" for c in s):
                va = parse_va(s)
                cur.execute("SELECT * FROM functions WHERE va=?", (va,))
            else:
                cur.execute("SELECT * FROM functions WHERE name=?", (s,))
        row = cur.fetchone()
        if not row:
            return None
        return dict(row)

    def get_blocks(self, target: str | int) -> List[Dict[str, Any]]:
        """Returns all basic blocks for the specified function."""
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT block_va, func_va, end_va, size, instruction_count, is_entry, is_exit, flow_type
            FROM blocks
            WHERE func_va = ?
            ORDER BY block_va ASC
        """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_flows(self, target: str | int) -> List[Dict[str, Any]]:
        """Returns all CFG flow edges for the specified function."""
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT src_va, dst_va, func_va, flow_type
            FROM flows
            WHERE func_va = ?
            ORDER BY src_va ASC, dst_va ASC
        """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_switch_cases(self, target: Optional[str | int] = None) -> List[Dict[str, Any]]:
        """Returns switch jump table cases for a function or entire binary."""
        conn = self.connect()
        cur = conn.cursor()
        if target is not None:
            va = parse_va(target) if not isinstance(target, int) else target
            cur.execute("""
                SELECT func_va, switch_va, case_val, target_va
                FROM switch_cases
                WHERE func_va = ?
                ORDER BY switch_va ASC, case_val ASC
            """, (va,))
        else:
            cur.execute("""
                SELECT func_va, switch_va, case_val, target_va
                FROM switch_cases
                ORDER BY func_va ASC, switch_va ASC, case_val ASC
            """)
        return [dict(r) for r in cur.fetchall()]

    def get_data_xrefs(self, target: str | int, access_type: Optional[str] = None) -> List[Dict[str, Any]]:
        """Returns memory/data xrefs for a function VA or global target VA."""
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target

        # Check if target is a function entry VA or global data VA
        cur.execute("SELECT 1 FROM functions WHERE va = ?", (va,))
        is_func = cur.fetchone() is not None

        if is_func:
            if access_type:
                cur.execute("""
                    SELECT func_va, site_va, data_va, access_type
                    FROM data_xrefs
                    WHERE func_va = ? AND access_type = ?
                    ORDER BY site_va ASC
                """, (va, access_type.upper()))
            else:
                cur.execute("""
                    SELECT func_va, site_va, data_va, access_type
                    FROM data_xrefs
                    WHERE func_va = ?
                    ORDER BY site_va ASC
                """, (va,))
        else:
            if access_type:
                cur.execute("""
                    SELECT d.func_va, f.name as func_name, d.site_va, d.data_va, d.access_type
                    FROM data_xrefs d
                    JOIN functions f ON d.func_va = f.va
                    WHERE d.data_va = ? AND d.access_type = ?
                    ORDER BY d.func_va ASC, d.site_va ASC
                """, (va, access_type.upper()))
            else:
                cur.execute("""
                    SELECT d.func_va, f.name as func_name, d.site_va, d.data_va, d.access_type
                    FROM data_xrefs d
                    JOIN functions f ON d.func_va = f.va
                    WHERE d.data_va = ?
                    ORDER BY d.func_va ASC, d.site_va ASC
                """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_byte_hash(self, target: str | int) -> Optional[str]:
        """Returns the SHA-256 byte hash of the function's machine code."""
        fn = self.get_function(target)
        if fn:
            return fn.get("byte_hash")
        return None

    def get_callers(self, target: str | int) -> List[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT DISTINCT f.va, f.name, f.size, f.is_thunk, f.port_status
            FROM calls c
            JOIN functions f ON c.caller_va = f.va
            WHERE c.callee_va = ?
            ORDER BY f.va
        """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_callees(self, target: str | int) -> List[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT DISTINCT f.va, f.name, f.size, f.is_thunk, f.port_status
            FROM calls c
            JOIN functions f ON c.callee_va = f.va
            WHERE c.caller_va = ?
            ORDER BY f.va
        """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_global_xrefs(self, target: str | int) -> List[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT DISTINCT f.va, f.name, f.size, f.port_status
            FROM global_xrefs x
            JOIN functions f ON x.func_va = f.va
            WHERE x.global_va = ?
            ORDER BY f.va
        """, (va,))
        return [dict(r) for r in cur.fetchall()]

    def get_string_xrefs(self, str_name: str) -> List[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        cur.execute("""
            SELECT DISTINCT f.va, f.name, f.size, f.port_status
            FROM string_xrefs x
            JOIN functions f ON x.func_va = f.va
            WHERE x.string_name = ?
            ORDER BY f.va
        """, (str_name,))
        return [dict(r) for r in cur.fetchall()]

    def get_function_globals(self, target: str | int) -> List[int]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("SELECT DISTINCT global_va FROM global_xrefs WHERE func_va=? ORDER BY global_va", (va,))
        return [r[0] for r in cur.fetchall()]

    def get_function_strings(self, target: str | int) -> List[str]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("SELECT DISTINCT string_name FROM string_xrefs WHERE func_va=? ORDER BY string_name", (va,))
        return [r[0] for r in cur.fetchall()]

    def disasm(self, target: str | int, att: bool = False, exe_path: Optional[Path] = None) -> str:
        fn = self.get_function(target)
        if not fn:
            raise ValueError(f"Function not found: {target}")
        va = fn["va"]
        sz = fn["size"]
        if exe_path is None:
            exe_path = UNPACKED_EXE
        if not exe_path.exists():
            raise FileNotFoundError(f"Executable not found: {exe_path}")
        syntax = ["-M", "intel"] if not att else []
        cmd = [
            "objdump", "-d", *syntax,
            f"--start-address=0x{va:x}",
            f"--stop-address=0x{va + sz:x}",
            str(exe_path),
        ]
        try:
            return subprocess.check_output(cmd, text=True)
        except subprocess.CalledProcessError as e:
            return f"objdump failed with exit code {e.returncode}: {e.output}"
        except FileNotFoundError:
            return "objdump not found in environment (run via nix develop)"

    def get_text(self, target: str | int, line_numbers: bool = False) -> str:
        fn = self.get_function(target)
        if not fn:
            raise ValueError(f"Function not found: {target}")
        file_path_str = fn.get("file_path")
        p = None
        if file_path_str:
            cand = REPO / file_path_str
            if cand.exists():
                p = cand
        if p is None:
            va = fn["va"]
            for cand in [BY_ADDRESS_DIR / f"{va:x}.c", BY_ADDRESS_DIR / f"{va:06x}.c", BY_ADDRESS_DIR / f"{va:08x}.c"]:
                if cand.exists():
                    p = cand
                    break
        if p is None or not p.exists():
            raise FileNotFoundError(f"Decompiled file not found for {fn['name']} ({format_va(fn['va'])})")

        text = p.read_text(encoding="utf-8", errors="replace")
        if line_numbers:
            lines = text.splitlines()
            width = max(3, len(str(len(lines))))
            return "\n".join(f"{i:>{width}d}: {line}" for i, line in enumerate(lines, 1))
        return text

    def get_call_tree(self, target: str | int, max_depth: int = 3, _visited: Optional[Set[int]] = None) -> Dict[str, Any]:
        if _visited is None:
            _visited = set()
        va = parse_va(target) if not isinstance(target, int) else target
        fn = self.get_function(va) or {
            "va": va,
            "name": format_fun(va),
            "size": 0,
            "port_status": "discovered",
        }
        node = {
            "va": format_va(va),
            "name": fn["name"],
            "size": fn.get("size", 0),
            "port_status": fn.get("port_status", "discovered"),
            "children": [],
        }
        if max_depth <= 0 or va in _visited:
            if va in _visited:
                node["recursive"] = True
            return node

        _visited.add(va)
        callees = self.get_callees(va)
        for c in callees:
            c_va = c["va"]
            child = self.get_call_tree(c_va, max_depth=max_depth - 1, _visited=_visited.copy())
            node["children"].append(child)
        return node

    def search(self, query: str, limit: int = 50, search_code: bool = False) -> Dict[str, List[Dict[str, Any]]]:
        conn = self.connect()
        cur = conn.cursor()
        q = f"%{query}%"

        cur.execute("""
            SELECT va, name, size, is_thunk, port_status
            FROM functions
            WHERE name LIKE ? OR printf('0x%06x', va) LIKE ?
            ORDER BY va
            LIMIT ?
        """, (q, q, limit))
        funcs = [dict(r) for r in cur.fetchall()]

        cur.execute("""
            SELECT string_name, COUNT(func_va) as count
            FROM string_xrefs
            WHERE string_name LIKE ?
            GROUP BY string_name
            ORDER BY count DESC
            LIMIT ?
        """, (q, limit))
        strings = [dict(r) for r in cur.fetchall()]

        cur.execute("""
            SELECT global_va, COUNT(func_va) as count
            FROM global_xrefs
            WHERE printf('0x%06x', global_va) LIKE ? OR printf('DAT_%08x', global_va) LIKE ?
            GROUP BY global_va
            ORDER BY count DESC
            LIMIT ?
        """, (q, q, limit))
        globals_list = [dict(r) for r in cur.fetchall()]

        code_matches = []
        if search_code:
            decomp_files = sorted(BY_ADDRESS_DIR.glob("*.c")) if BY_ADDRESS_DIR.exists() else []
            pattern = re.compile(re.escape(query), re.IGNORECASE)
            for fpath in decomp_files:
                va = parse_va(fpath.stem)
                content = fpath.read_text(encoding="utf-8", errors="replace")
                lines = content.splitlines()
                matches_in_file = []
                for lineno, line in enumerate(lines, 1):
                    if pattern.search(line):
                        matches_in_file.append({"line": lineno, "text": line.strip()})
                        if len(matches_in_file) >= 5:
                            break
                if matches_in_file:
                    fn = self.get_function(va)
                    code_matches.append({
                        "va": format_va(va),
                        "name": fn["name"] if fn else format_fun(va),
                        "port_status": fn.get("port_status", "discovered") if fn else "discovered",
                        "matches": matches_in_file,
                    })
                    if len(code_matches) >= limit:
                        break

        return {
            "functions": funcs,
            "strings": strings,
            "globals": globals_list,
            "code": code_matches,
        }

    def stats(self) -> Dict[str, Any]:
        conn = self.connect()
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM functions")
        total_funcs = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM functions WHERE is_thunk=0")
        non_thunk_funcs = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM blocks")
        total_blocks = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM flows")
        total_flows = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM calls")
        total_calls = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT global_va) FROM global_xrefs")
        total_globals = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT string_name) FROM string_xrefs")
        total_strings = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM switch_cases")
        total_switches = cur.fetchone()[0]

        cur.execute("SELECT port_status, COUNT(*) FROM functions GROUP BY port_status")
        status_counts = {r[0]: r[1] for r in cur.fetchall()}

        cur.execute("SELECT runtime_status, COUNT(*) FROM functions WHERE runtime_status IS NOT NULL GROUP BY runtime_status")
        runtime_counts = {r[0]: r[1] for r in cur.fetchall()}

        return {
            "total_functions": total_funcs,
            "non_thunk_functions": non_thunk_funcs,
            "total_basic_blocks": total_blocks,
            "total_cfg_flows": total_flows,
            "total_call_edges": total_calls,
            "global_xrefs": total_globals,
            "string_xrefs": total_strings,
            "switch_cases": total_switches,
            "inventory_breakdown": status_counts,
            "runtime_breakdown": runtime_counts,
        }

    def export_json(self, out_dir: Path) -> Dict[str, Any]:
        """Exports the SQLite index to deterministic JSON tables (CV-01)."""
        out_dir = Path(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        conn = self.connect()
        cur = conn.cursor()

        # Functions
        cur.execute("SELECT va, name, size, is_thunk, calling_conv, return_type, param_count, byte_hash FROM functions ORDER BY va")
        funcs_data = [{
            "va": format_va(r["va"]),
            "name": r["name"],
            "size": r["size"],
            "is_thunk": bool(r["is_thunk"]),
            "calling_conv": r["calling_conv"],
            "return_type": r["return_type"],
            "param_count": r["param_count"],
            "byte_hash": r["byte_hash"],
        } for r in cur.fetchall()]
        (out_dir / "functions.json").write_text(json.dumps(funcs_data, indent=2), encoding="utf-8")

        # Blocks
        cur.execute("SELECT block_va, func_va, end_va, size, instruction_count, is_entry, is_exit, flow_type FROM blocks ORDER BY block_va")
        blocks_data = [{
            "block_va": format_va(r["block_va"]),
            "func_va": format_va(r["func_va"]),
            "end_va": format_va(r["end_va"]),
            "size": r["size"],
            "instruction_count": r["instruction_count"],
            "is_entry": bool(r["is_entry"]),
            "is_exit": bool(r["is_exit"]),
            "flow_type": r["flow_type"],
        } for r in cur.fetchall()]
        (out_dir / "blocks.json").write_text(json.dumps(blocks_data, indent=2), encoding="utf-8")

        # Flows
        cur.execute("SELECT src_va, dst_va, func_va, flow_type FROM flows ORDER BY src_va, dst_va")
        flows_data = [{
            "src_va": format_va(r["src_va"]),
            "dst_va": format_va(r["dst_va"]),
            "func_va": format_va(r["func_va"]),
            "flow_type": r["flow_type"],
        } for r in cur.fetchall()]
        (out_dir / "flows.json").write_text(json.dumps(flows_data, indent=2), encoding="utf-8")

        # Calls
        cur.execute("SELECT caller_va, callee_va, call_site_va, call_type FROM calls ORDER BY caller_va, callee_va")
        calls_data = [{
            "caller_va": format_va(r["caller_va"]),
            "callee_va": format_va(r["callee_va"]),
            "call_site_va": format_va(r["call_site_va"]) if r["call_site_va"] else None,
            "call_type": r["call_type"],
        } for r in cur.fetchall()]
        (out_dir / "calls.json").write_text(json.dumps(calls_data, indent=2), encoding="utf-8")

        # Data Xrefs
        cur.execute("SELECT func_va, site_va, data_va, access_type FROM data_xrefs ORDER BY func_va, site_va")
        data_xrefs_data = [{
            "func_va": format_va(r["func_va"]),
            "site_va": format_va(r["site_va"]),
            "data_va": format_va(r["data_va"]),
            "access_type": r["access_type"],
        } for r in cur.fetchall()]
        (out_dir / "data_xrefs.json").write_text(json.dumps(data_xrefs_data, indent=2), encoding="utf-8")

        # String Xrefs
        cur.execute("SELECT func_va, string_name, string_va, value FROM string_xrefs ORDER BY func_va, string_name")
        str_xrefs_data = [{
            "func_va": format_va(r["func_va"]),
            "string_name": r["string_name"],
            "string_va": format_va(r["string_va"]) if r["string_va"] else None,
            "value": r["value"],
        } for r in cur.fetchall()]
        (out_dir / "string_xrefs.json").write_text(json.dumps(str_xrefs_data, indent=2), encoding="utf-8")

        # Switch Cases
        cur.execute("SELECT func_va, switch_va, case_val, target_va FROM switch_cases ORDER BY func_va, switch_va, case_val")
        switches_data = [{
            "func_va": format_va(r["func_va"]),
            "switch_va": format_va(r["switch_va"]),
            "case_val": r["case_val"],
            "target_va": format_va(r["target_va"]),
        } for r in cur.fetchall()]
        (out_dir / "switch_cases.json").write_text(json.dumps(switches_data, indent=2), encoding="utf-8")

        manifest = {
            "schema_version": "cv01-v1.0",
            "functions_count": len(funcs_data),
            "blocks_count": len(blocks_data),
            "flows_count": len(flows_data),
            "calls_count": len(calls_data),
            "data_xrefs_count": len(data_xrefs_data),
            "string_xrefs_count": len(str_xrefs_data),
            "switch_cases_count": len(switches_data),
        }
        (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

        return {
            "status": "exported",
            "out_dir": str(out_dir),
            "manifest": manifest,
        }
    def import_json(self, json_dir: Path) -> Dict[str, Any]:
        """Imports structured JSON tables into the SQLite index database."""
        json_dir = Path(json_dir)
        if not json_dir.exists():
            raise FileNotFoundError(f"JSON directory not found: {json_dir}")

        if self.db_path.exists():
            try:
                self.db_path.unlink()
            except OSError:
                pass

        conn = sqlite3.connect(str(self.db_path))
        cur = conn.cursor()

        cur.execute("""
            CREATE TABLE metadata (
                key TEXT PRIMARY KEY,
                value TEXT
            )
        """)
        cur.execute("""
            CREATE TABLE functions (
                va INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                size INTEGER NOT NULL,
                is_thunk INTEGER NOT NULL,
                calling_conv TEXT,
                file_path TEXT,
                line_count INTEGER,
                return_type TEXT DEFAULT 'unknown',
                param_count INTEGER DEFAULT 0,
                byte_hash TEXT,
                port_status TEXT,
                runtime_status TEXT
            )
        """)
        cur.execute("""
            CREATE TABLE blocks (
                block_va INTEGER NOT NULL,
                func_va INTEGER NOT NULL,
                end_va INTEGER NOT NULL,
                size INTEGER NOT NULL,
                instruction_count INTEGER NOT NULL,
                is_entry INTEGER NOT NULL,
                is_exit INTEGER NOT NULL,
                flow_type TEXT NOT NULL,
                PRIMARY KEY (block_va, func_va)
            )
        """)
        cur.execute("""
            CREATE TABLE flows (
                src_va INTEGER NOT NULL,
                dst_va INTEGER NOT NULL,
                func_va INTEGER NOT NULL,
                flow_type TEXT NOT NULL,
                PRIMARY KEY (src_va, dst_va, func_va)
            )
        """)
        cur.execute("""
            CREATE TABLE calls (
                caller_va INTEGER NOT NULL,
                callee_va INTEGER NOT NULL,
                call_site_va INTEGER,
                call_type TEXT DEFAULT 'DIRECT',
                PRIMARY KEY (caller_va, callee_va, call_site_va)
            )
        """)
        cur.execute("""
            CREATE TABLE global_xrefs (
                func_va INTEGER NOT NULL,
                global_va INTEGER NOT NULL,
                access_type TEXT DEFAULT 'READ',
                count INTEGER DEFAULT 1,
                PRIMARY KEY (func_va, global_va, access_type)
            )
        """)
        cur.execute("""
            CREATE TABLE data_xrefs (
                func_va INTEGER NOT NULL,
                site_va INTEGER NOT NULL,
                data_va INTEGER NOT NULL,
                access_type TEXT NOT NULL,
                PRIMARY KEY (func_va, site_va, data_va, access_type)
            )
        """)
        cur.execute("""
            CREATE TABLE string_xrefs (
                func_va INTEGER NOT NULL,
                string_name TEXT NOT NULL,
                string_va INTEGER,
                value TEXT,
                PRIMARY KEY (func_va, string_name)
            )
        """)
        cur.execute("""
            CREATE TABLE switch_cases (
                func_va INTEGER NOT NULL,
                switch_va INTEGER NOT NULL,
                case_val INTEGER NOT NULL,
                target_va INTEGER NOT NULL,
                PRIMARY KEY (func_va, switch_va, case_val, target_va)
            )
        """)

        cur.execute("CREATE INDEX idx_blocks_func ON blocks(func_va)")
        cur.execute("CREATE INDEX idx_flows_src ON flows(src_va)")
        cur.execute("CREATE INDEX idx_flows_dst ON flows(dst_va)")
        cur.execute("CREATE INDEX idx_flows_func ON flows(func_va)")
        cur.execute("CREATE INDEX idx_calls_callee ON calls(callee_va)")
        cur.execute("CREATE INDEX idx_calls_caller ON calls(caller_va)")
        cur.execute("CREATE INDEX idx_global_xrefs_global ON global_xrefs(global_va)")
        cur.execute("CREATE INDEX idx_data_xrefs_data ON data_xrefs(data_va)")
        cur.execute("CREATE INDEX idx_string_xrefs_str ON string_xrefs(string_name)")
        cur.execute("CREATE INDEX idx_switch_cases_func ON switch_cases(func_va)")
        cur.execute("CREATE INDEX idx_switch_cases_switch ON switch_cases(switch_va)")

        port_statuses: Dict[int, str] = {}
        runtime_statuses: Dict[int, Optional[str]] = {}
        if PORT_LEDGER_JSON.exists():
            try:
                ledger = json.loads(PORT_LEDGER_JSON.read_text(encoding="utf-8"))
                fn_list = ledger.get("functions", [])
                if isinstance(fn_list, list):
                    for entry in fn_list:
                        va = parse_va(entry["va"])
                        port_statuses[va] = entry.get("inventory_state", "discovered")
                        runtime_statuses[va] = entry.get("runtime_state")
                elif isinstance(fn_list, dict):
                    for va_hex, entry in fn_list.items():
                        va = parse_va(va_hex)
                        port_statuses[va] = entry.get("inventory_state", "discovered")
                        runtime_statuses[va] = entry.get("runtime_state")
            except Exception:
                pass

        return self._import_from_json_tables(conn, json_dir, port_statuses, runtime_statuses)


    def coverage(self, unimplemented: bool = False, unexecuted: bool = False, limit: int = 50) -> Dict[str, Any]:
        conn = self.connect()
        cur = conn.cursor()

        cur.execute("SELECT port_status, runtime_status, COUNT(*) FROM functions GROUP BY port_status, runtime_status")
        matrix_rows = cur.fetchall()
        matrix: Dict[str, Dict[str, int]] = {}
        for inv, rt, count in matrix_rows:
            rt_key = rt if rt else "unexecuted"
            if inv not in matrix:
                matrix[inv] = {}
            matrix[inv][rt_key] = count

        unimplemented_list = []
        if unimplemented:
            cur.execute("""
                SELECT va, name, size, port_status, runtime_status
                FROM functions
                WHERE runtime_status IS NOT NULL
                  AND port_status IN ('discovered', 'source-referenced')
                ORDER BY va
                LIMIT ?
            """, (limit,))
            unimplemented_list = [dict(r) for r in cur.fetchall()]

        unexecuted_list = []
        if unexecuted:
            cur.execute("""
                SELECT va, name, size, port_status
                FROM functions
                WHERE port_status IN ('implemented', 'instrumented')
                  AND (runtime_status IS NULL OR runtime_status = 'unexecuted')
                ORDER BY va
                LIMIT ?
            """, (limit,))
            unexecuted_list = [dict(r) for r in cur.fetchall()]

        cur.execute("SELECT COUNT(*) FROM functions WHERE port_status IN ('implemented', 'instrumented')")
        implemented_total = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM functions WHERE runtime_status IS NOT NULL")
        proven_total = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM functions")
        all_total = cur.fetchone()[0]

        dynamic_cov = None
        try:
            from tools.coverage_atlas import CoverageAtlas, COVERAGE_DB
            if COVERAGE_DB.exists():
                atlas = CoverageAtlas(db_path=COVERAGE_DB, re_index_path=self.db_path)
                dynamic_cov = atlas.get_summary()
                atlas.close()
        except Exception:
            pass

        return {
            "total_functions": all_total,
            "implemented_functions": implemented_total,
            "proven_functions": proven_total,
            "matrix": matrix,
            "unimplemented": unimplemented_list,
            "unexecuted": unexecuted_list,
            "dynamic_coverage": dynamic_cov,
        }


# ─── CLI Handlers ────────────────────────────────────────────────────────────

def cmd_build(idx: ReIndex, args: argparse.Namespace) -> int:
    res = idx.build(force=args.force)
    if args.json:
        print(json.dumps(res, indent=1))
    else:
        print(f"ReIndex built: {res.get('functions', 'N/A')} functions, {res.get('blocks', 'N/A')} basic blocks, {res.get('calls', 'N/A')} call edges.")
        print(f"Database: {res.get('path', str(DB_PATH))}")
    return 0


def cmd_info(idx: ReIndex, args: argparse.Namespace) -> int:
    fn = idx.get_function(args.target)
    if not fn:
        sys.stderr.write(f"Function not found: {args.target}\n")
        return 1
    va = fn["va"]
    callers = idx.get_callers(va)
    callees = idx.get_callees(va)
    globals_list = idx.get_function_globals(va)
    strings_list = idx.get_function_strings(va)
    blocks_list = idx.get_blocks(va)

    payload = {
        "va": format_va(va),
        "name": fn["name"],
        "size": fn["size"],
        "is_thunk": bool(fn["is_thunk"]),
        "calling_conv": fn["calling_conv"],
        "return_type": fn.get("return_type", "unknown"),
        "param_count": fn.get("param_count", 0),
        "byte_hash": fn.get("byte_hash"),
        "file_path": fn["file_path"],
        "line_count": fn["line_count"],
        "port_status": fn["port_status"],
        "blocks_count": len(blocks_list),
        "callers_count": len(callers),
        "callees_count": len(callees),
        "globals_count": len(globals_list),
        "strings_count": len(strings_list),
        "callers": [format_va(c["va"]) for c in callers],
        "callees": [format_va(c["va"]) for c in callees],
        "globals": [format_va(g) for g in globals_list],
        "strings": strings_list,
    }

    if args.json:
        print(json.dumps(payload, indent=1))
    else:
        print(f"{fn['name']} @ {format_va(va)} ({fn['size']} bytes, {fn['calling_conv']})")
        print(f"  Status:       {fn['port_status']}")
        if fn.get("byte_hash"):
            print(f"  Byte SHA-256: {fn['byte_hash']}")
        print(f"  Basic blocks: {len(blocks_list)}")
        print(f"  Source file:  {fn['file_path']} ({fn['line_count']} lines)")
        print(f"  Callers ({len(callers)}): {', '.join(format_fun(c['va']) for c in callers[:10])}{'...' if len(callers) > 10 else ''}")
        print(f"  Callees ({len(callees)}): {', '.join(format_fun(c['va']) for c in callees[:10])}{'...' if len(callees) > 10 else ''}")
        print(f"  Globals ({len(globals_list)}): {', '.join(f'DAT_{g:08x}' for g in globals_list[:8])}{'...' if len(globals_list) > 8 else ''}")
        print(f"  Strings ({len(strings_list)}): {', '.join(strings_list[:6])}{'...' if len(strings_list) > 6 else ''}")
    return 0


def cmd_blocks(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    blocks = idx.get_blocks(va)
    fn = idx.get_function(va)
    fn_name = fn["name"] if fn else format_fun(va)
    if args.json:
        print(json.dumps([{
            "block_va": format_va(b["block_va"]),
            "end_va": format_va(b["end_va"]),
            "size": b["size"],
            "instruction_count": b["instruction_count"],
            "is_entry": bool(b["is_entry"]),
            "is_exit": bool(b["is_exit"]),
            "flow_type": b["flow_type"],
        } for b in blocks], indent=1))
    else:
        print(f"Basic Blocks for {fn_name} @ {format_va(va)} ({len(blocks)} blocks):")
        for b in blocks:
            entry_badge = " [ENTRY]" if b["is_entry"] else ""
            exit_badge = " [EXIT]" if b["is_exit"] else ""
            print(f"  {format_va(b['block_va'])} - {format_va(b['end_va'])} ({b['size']:>3}B, {b['instruction_count']:>2} insts) -> {b['flow_type']}{entry_badge}{exit_badge}")
    return 0


def cmd_flows(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    flows = idx.get_flows(va)
    fn = idx.get_function(va)
    fn_name = fn["name"] if fn else format_fun(va)
    if args.json:
        print(json.dumps([{
            "src_va": format_va(fl["src_va"]),
            "dst_va": format_va(fl["dst_va"]),
            "flow_type": fl["flow_type"],
        } for fl in flows], indent=1))
    else:
        print(f"Control Flow Graph Edges for {fn_name} @ {format_va(va)} ({len(flows)} edges):")
        for fl in flows:
            print(f"  {format_va(fl['src_va'])}  -->  {format_va(fl['dst_va'])}  [{fl['flow_type']}]")
    return 0


def cmd_switches(idx: ReIndex, args: argparse.Namespace) -> int:
    target = getattr(args, "target", None)
    switches = idx.get_switch_cases(target)
    if args.json:
        print(json.dumps([{
            "func_va": format_va(s["func_va"]),
            "switch_va": format_va(s["switch_va"]),
            "case_val": s["case_val"],
            "target_va": format_va(s["target_va"]),
        } for s in switches], indent=1))
    else:
        print(f"Switch Cases / Jump Tables ({len(switches)} entries):")
        for s in switches:
            print(f"  {format_fun(s['func_va'])} @ {format_va(s['switch_va'])}: case {s['case_val']} -> {format_va(s['target_va'])}")
    return 0


def cmd_data_xrefs(idx: ReIndex, args: argparse.Namespace) -> int:
    target = args.target.strip()
    access = getattr(args, "type", None)
    va = parse_va(target)
    xrefs = idx.get_data_xrefs(va, access_type=access)
    if args.json:
        print(json.dumps([{
            "func_va": format_va(x["func_va"]),
            "func_name": x.get("func_name", format_fun(x["func_va"])),
            "site_va": format_va(x["site_va"]),
            "data_va": format_va(x["data_va"]),
            "access_type": x["access_type"],
        } for x in xrefs], indent=1))
    else:
        print(f"Data Cross-References for {target} ({len(xrefs)} xrefs):")
        for x in xrefs:
            f_label = x.get("func_name", format_fun(x["func_va"]))
            print(f"  {f_label} @ site {format_va(x['site_va'])} -> data {format_va(x['data_va'])} [{x['access_type']}]")
    return 0


def cmd_hash(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    h = idx.get_byte_hash(va)
    fn = idx.get_function(va)
    fn_name = fn["name"] if fn else format_fun(va)
    if args.json:
        print(json.dumps({"va": format_va(va), "name": fn_name, "byte_hash": h}, indent=1))
    else:
        print(f"{fn_name} @ {format_va(va)}: {h if h else 'N/A'}")
    return 0


def cmd_callers(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    callers = idx.get_callers(va)
    if args.json:
        print(json.dumps([{"va": format_va(c["va"]), "name": c["name"], "size": c["size"], "port_status": c["port_status"]} for c in callers], indent=1))
    else:
        print(f"Callers of {format_fun(va)} ({len(callers)} total):")
        for c in callers:
            print(f"  {format_fun(c['va'])} @ {format_va(c['va'])} ({c['size']:>4}B) [{c['port_status']}]")
    return 0


def cmd_callees(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    callees = idx.get_callees(va)
    if args.json:
        print(json.dumps([{"va": format_va(c["va"]), "name": c["name"], "size": c["size"], "port_status": c["port_status"]} for c in callees], indent=1))
    else:
        print(f"Callees of {format_fun(va)} ({len(callees)} total):")
        for c in callees:
            print(f"  {format_fun(c['va'])} @ {format_va(c['va'])} ({c['size']:>4}B) [{c['port_status']}]")
    return 0


def cmd_xrefs(idx: ReIndex, args: argparse.Namespace) -> int:
    target = args.target.strip()
    if target.startswith("s_"):
        xrefs = idx.get_string_xrefs(target)
        label = f"string '{target}'"
    else:
        va = parse_va(target)
        xrefs = idx.get_global_xrefs(va)
        label = f"global DAT_{va:08x}"

    if args.json:
        print(json.dumps([{"va": format_va(x["va"]), "name": x["name"], "port_status": x["port_status"]} for x in xrefs], indent=1))
    else:
        print(f"Cross-references to {label} ({len(xrefs)} functions):")
        for x in xrefs:
            print(f"  {format_fun(x['va'])} @ {format_va(x['va'])} [{x['port_status']}]")
    return 0


def _print_tree_node(node: Dict[str, Any], prefix: str = "", is_last: bool = True):
    conn = "└── " if is_last else "├── "
    rec = " (recursive)" if node.get("recursive") else ""
    print(f"{prefix}{conn}{node['name']} @ {node['va']} [{node['port_status']}]{rec}")
    new_prefix = prefix + ("    " if is_last else "│   ")
    children = node.get("children", [])
    for i, child in enumerate(children):
        _print_tree_node(child, new_prefix, is_last=(i == len(children) - 1))


def cmd_tree(idx: ReIndex, args: argparse.Namespace) -> int:
    tree = idx.get_call_tree(args.target, max_depth=args.depth)
    if args.json:
        print(json.dumps(tree, indent=1))
    else:
        print(f"Call tree for {tree['name']} (depth {args.depth}):")
        print(f"{tree['name']} @ {tree['va']} [{tree['port_status']}]")
        children = tree.get("children", [])
        for i, child in enumerate(children):
            _print_tree_node(child, "", is_last=(i == len(children) - 1))
    return 0


def cmd_disasm(idx: ReIndex, args: argparse.Namespace) -> int:
    try:
        text = idx.disasm(args.target, att=args.att)
        print(text)
        return 0
    except Exception as e:
        sys.stderr.write(f"disasm error: {e}\n")
        return 1


def cmd_text(idx: ReIndex, args: argparse.Namespace) -> int:
    try:
        fn = idx.get_function(args.target)
        if not fn:
            sys.stderr.write(f"Function not found: {args.target}\n")
            return 1
        raw_text = idx.get_text(args.target, line_numbers=False)
        if args.json:
            out = {
                "va": format_va(fn["va"]),
                "name": fn["name"],
                "file_path": fn.get("file_path"),
                "line_count": fn.get("line_count"),
                "port_status": fn.get("port_status"),
                "text": raw_text,
            }
            print(json.dumps(out, indent=1))
        else:
            if args.numbers:
                text = idx.get_text(args.target, line_numbers=True)
            else:
                text = raw_text
            print(text)
        return 0
    except Exception as e:
        sys.stderr.write(f"text error: {e}\n")
        return 1


def cmd_unported_callees(idx: ReIndex, args: argparse.Namespace) -> int:
    va = parse_va(args.target)
    callees = idx.get_callees(va)
    unported = [c for c in callees if c["port_status"] in ("discovered", "source-referenced")]
    if args.json:
        print(json.dumps([{"va": format_va(c["va"]), "name": c["name"], "size": c["size"], "port_status": c["port_status"]} for c in unported], indent=1))
    else:
        print(f"Unported callees of {format_fun(va)} ({len(unported)}/{len(callees)} total):")
        for c in unported:
            print(f"  {format_fun(c['va'])} @ {format_va(c['va'])} ({c['size']:>4}B) [{c['port_status']}]")
    return 0


def cmd_search(idx: ReIndex, args: argparse.Namespace) -> int:
    res = idx.search(args.query, limit=args.limit, search_code=args.code)
    if args.json:
        print(json.dumps(res, indent=1))
    else:
        print(f"Search results for '{args.query}':")
        if res["functions"]:
            print(f"  Functions ({len(res['functions'])}):")
            for f in res["functions"]:
                print(f"    {f['name']} @ {format_va(f['va'])} [{f['port_status']}]")
        if res["strings"]:
            print(f"  Strings ({len(res['strings'])}):")
            for s in res["strings"]:
                print(f"    {s['string_name']} ({s['count']} references)")
        if res["globals"]:
            print(f"  Globals ({len(res['globals'])}):")
            for g in res["globals"]:
                print(f"    DAT_{g['global_va']:08x} ({g['count']} references)")
        if res.get("code"):
            print(f"  Code matches ({len(res['code'])}):")
            for c in res["code"]:
                print(f"    {c['name']} @ {c['va']} [{c['port_status']}]:")
                for m in c["matches"]:
                    print(f"      L{m['line']}: {m['text']}")
    return 0


def cmd_stats(idx: ReIndex, args: argparse.Namespace) -> int:
    st = idx.stats()
    if args.json:
        print(json.dumps(st, indent=1))
    else:
        print("OpenRecet Static Reverse-Engineering Index Stats:")
        print(f"  Total functions:     {st['total_functions']} (non-thunk: {st['non_thunk_functions']})")
        print(f"  Total basic blocks:  {st.get('total_basic_blocks', 0)}")
        print(f"  Total CFG flows:     {st.get('total_cfg_flows', 0)}")
        print(f"  Direct call edges:   {st['total_call_edges']}")
        print(f"  Global xrefs:        {st['global_xrefs']}")
        print(f"  String xrefs:        {st['string_xrefs']}")
        print(f"  Switch cases:        {st.get('switch_cases', 0)}")
        print("  Inventory breakdown:")
        for k, v in sorted(st["inventory_breakdown"].items()):
            print(f"    {k:<20}: {v:>5}")
    return 0


def cmd_export_json(idx: ReIndex, args: argparse.Namespace) -> int:
    out_dir = getattr(args, "out", DECOMPILED_DIR)
    res = idx.export_json(out_dir)
    if args.json:
        print(json.dumps(res, indent=1))
    else:
        print(f"Exported JSON tables to {res['out_dir']}")
        for k, v in res["manifest"].items():
            print(f"  {k:<20}: {v}")
    return 0
def cmd_import_json(idx: ReIndex, args: argparse.Namespace) -> int:
    res = idx.import_json(args.path)
    if args.json:
        print(json.dumps(res, indent=1))
    else:
        print(f"Imported JSON tables from {args.path}:")
        print(f"  Functions:    {res.get('functions', 0)}")
        print(f"  Basic blocks: {res.get('blocks', 0)}")
        print(f"  CFG flows:    {res.get('flows', 0)}")
        print(f"  Calls:        {res.get('calls', 0)}")
        print(f"  Data xrefs:   {res.get('data_xrefs', 0)}")
        print(f"Database: {res.get('path')}")
    return 0



def cmd_coverage(idx: ReIndex, args: argparse.Namespace) -> int:
    cov = idx.coverage(unimplemented=args.unimplemented, unexecuted=args.unexecuted, limit=args.limit)
    if args.json:
        print(json.dumps(cov, indent=1))
    else:
        print("OpenRecet 2-Axis Parity Coverage Report:")
        print(f"  Implemented/Instrumented: {cov['implemented_functions']}/{cov['total_functions']} ({cov['implemented_functions']*100.0/max(1,cov['total_functions']):.1f}%)")
        print(f"  Runtime Proven:           {cov['proven_functions']}/{cov['total_functions']} ({cov['proven_functions']*100.0/max(1,cov['total_functions']):.1f}%)")
        if cov.get("dynamic_coverage") and cov["dynamic_coverage"].get("total_runs", 0) > 0:
            dyn = cov["dynamic_coverage"]
            print(f"  Dynamic Blocks Covered:   {dyn['unique_blocks_covered']}")
            print(f"  Dynamic Edges Covered:    {dyn['unique_edges_covered']}")
            print(f"  Dynamic Functions Reached:{dyn['touched_functions']}/{dyn['total_functions_in_index']} ({dyn['function_coverage_ratio']*100:.1f}%)")
            print(f"  Scenarios Ingested:       {dyn['total_scenarios']} ({dyn['total_runs']} run(s))")
            if dyn.get("cv08_calibration"):
                calib = dyn["cv08_calibration"]
                if calib.get("calibrated"):
                    print(f"  CV-08 Calibration:        {calib['verdict']} [Mode: {calib['collection_mode']}, Confidence: {calib['confidence_score']:.2f} ({calib['confidence_band']})]")
                    print(f"  Global Coverage Claim:    {calib['coverage_claim']}")
                else:
                    print(f"  CV-08 Calibration:        ⚠️  {calib.get('verdict', 'UNCALIBRATED')} (Run 're_index.py calibrate')")
                    print(f"  Global Coverage Claim:    {calib.get('coverage_claim', 'UNCALIBRATED')}")
            if dyn.get("semantic_dimensions"):
                print(f"  Semantic Dimensions:      {len(dyn['semantic_dimensions'])} dimension(s) tracked (CV-05)")
                for d_name, d_info in sorted(dyn["semantic_dimensions"].items()):
                    print(f"    - {d_name:<18}: {d_info['unique_items']:>4} items ({d_info['total_hits']} hits)")
        print("\n  2-Axis Cross-Tab (Inventory × Runtime):")
        for inv_state, row in sorted(cov["matrix"].items()):
            row_str = ", ".join(f"{rt}: {cnt}" for rt, cnt in sorted(row.items()))
            print(f"    {inv_state:<20} -> {row_str}")
        if args.unimplemented:
            print(f"\n  Executed but Unimplemented Functions ({len(cov['unimplemented'])}):")
            for f in cov["unimplemented"]:
                print(f"    {f['name']} @ {format_va(f['va'])} (size {f['size']}B) [{f['port_status']} | {f['runtime_status']}]")

        if args.unexecuted:
            print(f"\n  Implemented but Unexecuted Functions ({len(cov['unexecuted'])}):")
            for f in cov["unexecuted"]:
                print(f"    {f['name']} @ {format_va(f['va'])} (size {f['size']}B) [{f['port_status']}]")
    return 0


def cmd_prioritize(idx: ReIndex, args: argparse.Namespace) -> int:
    try:
        from tools.coverage_atlas import CoverageAtlas, cmd_prioritize as atlas_cmd_prioritize
        atlas = CoverageAtlas(re_index_path=idx.db_path)
        try:
            return atlas_cmd_prioritize(atlas, args)
        finally:
            atlas.close()
    except Exception as e:
        sys.stderr.write(f"Failed to run prioritizer: {e}\n")
        return 1


def cmd_calibrate(idx: ReIndex, args: argparse.Namespace) -> int:
    try:
        from tools.coverage_atlas import CoverageAtlas, cmd_calibrate as atlas_cmd_calibrate, COVERAGE_DB
        atlas = CoverageAtlas(db_path=COVERAGE_DB, re_index_path=idx.db_path)
        try:
            return atlas_cmd_calibrate(atlas, args)
        finally:
            atlas.close()
    except Exception as e:
        sys.stderr.write(f"Failed to run coverage calibration: {e}\n")
        return 1


def main(argv: Optional[List[str]] = None) -> int:
    parent = argparse.ArgumentParser(add_help=False)
    parent.add_argument("--json", action="store_true", help="output JSON")
    parent.add_argument("--db", type=Path, default=argparse.SUPPRESS, help=f"SQLite database path (default: {DB_PATH})")

    ap = argparse.ArgumentParser(description=__doc__, parents=[parent], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp_build = sub.add_parser("build", parents=[parent], help="build or rebuild SQLite index")
    sp_build.add_argument("--force", action="store_true", help="force rebuild")

    sp_info = sub.add_parser("info", parents=[parent], help="show function summary")
    sp_info.add_argument("target", help="function VA or name (e.g. 0x4905a8 or FUN_004905a8)")

    sp_blocks = sub.add_parser("blocks", parents=[parent], help="list basic blocks in function")
    sp_blocks.add_argument("target", help="function VA or name")

    sp_flows = sub.add_parser("flows", parents=[parent], help="list control flow graph edges in function")
    sp_flows.add_argument("target", help="function VA or name")

    sp_switches = sub.add_parser("switches", parents=[parent], help="list switch cases / jump tables")
    sp_switches.add_argument("target", nargs="?", help="optional function VA or name")

    sp_data_xrefs = sub.add_parser("data-xrefs", parents=[parent], help="list memory and global data xrefs")
    sp_data_xrefs.add_argument("target", help="function VA or global DAT_ address")
    sp_data_xrefs.add_argument("--type", choices=["READ", "WRITE", "DATA"], help="filter by access type")

    sp_hash = sub.add_parser("hash", parents=[parent], help="show SHA-256 byte hash of function machine code")
    sp_hash.add_argument("target", help="function VA or name")

    sp_export_json = sub.add_parser("export-json", parents=[parent], help="export SQLite index to JSON tables (CV-01)")
    sp_export_json.add_argument("--out", type=Path, default=DECOMPILED_DIR, help=f"output directory (default: {DECOMPILED_DIR})")
    sp_import_json = sub.add_parser("import-json", parents=[parent], help="import JSON tables into SQLite index (CV-01)")
    sp_import_json.add_argument("path", type=Path, help="directory containing JSON tables")


    sp_text = sub.add_parser("text", parents=[parent], help="display decompiled C source text of function")
    sp_text.add_argument("target", help="function VA or name (e.g. 0x4905a8 or FUN_004905a8)")
    sp_text.add_argument("-n", "--numbers", action="store_true", help="show 1-based line numbers")

    sp_callers = sub.add_parser("callers", parents=[parent], help="list callers of a function")
    sp_callers.add_argument("target", help="function VA or name")

    sp_callees = sub.add_parser("callees", parents=[parent], help="list callees of a function")
    sp_callees.add_argument("target", help="function VA or name")

    sp_xrefs = sub.add_parser("xrefs", parents=[parent], help="list functions referencing global or string")
    sp_xrefs.add_argument("target", help="global address (e.g. DAT_056e6280, 0x56e6280) or string name (s_save_dat_005cfa98)")

    sp_tree = sub.add_parser("tree", parents=[parent], help="call tree")
    sp_tree.add_argument("target", help="function VA or name")
    sp_tree.add_argument("--depth", type=int, default=3, help="max depth (default: 3)")

    sp_disasm = sub.add_parser("disasm", parents=[parent], help="disassemble function from retail binary via objdump")
    sp_disasm.add_argument("target", help="function VA or name")
    sp_disasm.add_argument("--att", action="store_true", help="use AT&T syntax instead of Intel")

    sp_unported = sub.add_parser("unported-callees", parents=[parent], help="list unported callees")
    sp_unported.add_argument("target", help="function VA or name")

    sp_search = sub.add_parser("search", parents=[parent], help="search functions, strings, globals")
    sp_search.add_argument("query", help="search string")
    sp_search.add_argument("--limit", type=int, default=30, help="max results (default: 30)")
    sp_search.add_argument("-c", "--code", action="store_true", help="search decompiled C function bodies as well")

    sp_stats = sub.add_parser("stats", parents=[parent], help="show overall index statistics")

    sp_coverage = sub.add_parser("coverage", parents=[parent], help="show 2-axis inventory and runtime coverage report")
    sp_coverage.add_argument("--unimplemented", action="store_true", help="list executed-but-unimplemented functions (CV-06 gap)")
    sp_coverage.add_argument("--unexecuted", action="store_true", help="list implemented functions lacking runtime proof")
    sp_coverage.add_argument("--limit", type=int, default=50, help="max functions to list (default: 50)")

    sp_prio = sub.add_parser("prioritize", parents=[parent], help="CV-07 next-experiment candidate prioritizer")
    sp_prio.add_argument("--kind", choices=["all", "functions", "edges", "semantics", "scenarios"], default="all", help="candidate kinds to evaluate (default: all)")
    sp_prio.add_argument("--front", help="active development front focus (e.g. customer_service, day2_transition, shop_loop, save_system, dungeon)")
    sp_prio.add_argument("--min-readiness", choices=["discovered", "referenced", "stubbed", "ported", "verified"], help="minimum port readiness filter")
    sp_prio.add_argument("--weights", help="JSON string overriding scoring weights")
    sp_prio.add_argument("--markdown", action="store_true", help="output formatted markdown table")
    sp_prio.add_argument("--limit", type=int, default=20, help="max candidates to display (default: 20)")

    sp_calib = sub.add_parser("calibrate", parents=[parent], help="CV-08 coverage truth calibration")
    sp_calib.add_argument("--scenario", help="scenario name to calibrate (default: all)")
    sp_calib.add_argument("--mode", choices=["STALKER_ONLY", "HYBRID_CALL_TRACE", "FULL_OBSERVATION"], help="collection mode assertion")
    sp_calib.add_argument("--cross-check-call-trace", type=Path, help="path to call_trace.jsonl for cross-collector validation")
    sp_calib.add_argument("--repeat-runs", nargs="+", help="run IDs to compare for determinism scoring")
    sp_calib.add_argument("--min-confidence", type=float, default=0.70, help="minimum acceptable confidence score (default: 0.70)")

    args = ap.parse_args(argv)

    db_path = getattr(args, "db", DB_PATH)
    idx = ReIndex(db_path=db_path)

    handlers = {
        "build": cmd_build,
        "info": cmd_info,
        "blocks": cmd_blocks,
        "flows": cmd_flows,
        "switches": cmd_switches,
        "data-xrefs": cmd_data_xrefs,
        "hash": cmd_hash,
        "export-json": cmd_export_json,
        "import-json": cmd_import_json,
        "callers": cmd_callers,
        "callees": cmd_callees,
        "xrefs": cmd_xrefs,
        "tree": cmd_tree,
        "disasm": cmd_disasm,
        "text": cmd_text,
        "unported-callees": cmd_unported_callees,
        "search": cmd_search,
        "stats": cmd_stats,
        "coverage": cmd_coverage,
        "prioritize": cmd_prioritize,
        "calibrate": cmd_calibrate,
    }

    try:
        return handlers[args.cmd](idx, args)
    finally:
        idx.close()


if __name__ == "__main__":
    sys.exit(main())
