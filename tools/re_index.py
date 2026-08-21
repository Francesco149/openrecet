#!/usr/bin/env python3
"""Offline static reverse-engineering index for OpenRecet (CV-01 / CV-02).

Indexes all 2,620 engine functions from docs/decompiled/functions.csv and
docs/decompiled/by-address/*.c into a fast, queryable SQLite database
(docs/re-index.sqlite) with Python API and CLI.

Provides instant answers for:
  - info <va|name>: function size, calling convention, thunk status, callers/callees/xrefs count, port status
  - text <va|name> [-n]: print decompiled C source text of function
  - disasm <va|name> [--att]: print objdump disassembly of function
  - callers <va|name>: all functions that call target
  - callees <va|name>: all functions called by target
  - xrefs <DAT_va|s_name|hex_va>: all functions reading/writing a global or string
  - tree <va|name> [--depth N]: call tree up to depth N
  - unported-callees <va|name>: callees of target that are not yet implemented in port
  - search <query> [--code]: regex/substring search over functions, globals, strings, and code
  - stats: overall index statistics

Run from repo root:
  python3 tools/re_index.py build
  python3 tools/re_index.py info 0x4905a8
  python3 tools/re_index.py callers 0x4905a8
  python3 tools/re_index.py xrefs DAT_056e6280
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sqlite3
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple
REPO = Path(__file__).resolve().parent.parent
DECOMPILED_DIR = REPO / "docs" / "decompiled"
FUNCTIONS_CSV = DECOMPILED_DIR / "functions.csv"
BY_ADDRESS_DIR = DECOMPILED_DIR / "by-address"
DB_PATH = REPO / "docs" / "re-index.sqlite"
PORT_LEDGER_JSON = REPO / "docs" / "port-ledger.json"

FUN_RE = re.compile(r"\bFUN_([0-9a-fA-F]{8})\b")
DAT_RE = re.compile(r"\b(?:DAT|_DAT)_([0-9a-fA-F]{8})\b")
STR_RE = re.compile(r"\b(s_[A-Za-z0-9_]+)\b")


def parse_va(val: str | int) -> int:
    if isinstance(val, int):
        return val
    s = str(val).strip()
    if s.lower().startswith("fun_"):
        s = s[4:]
    elif s.lower().startswith("0x"):
        s = s[2:]
    return int(s, 16)


def format_va(va: int) -> str:
    return f"0x{va:06x}"


def format_fun(va: int) -> str:
    return f"FUN_{va:08x}"


class ReIndex:
    def __init__(self, db_path: Path = DB_PATH):
        self.db_path = db_path
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

    def build(self, force: bool = False) -> Dict[str, Any]:
        """Build or rebuild docs/re-index.sqlite from docs/decompiled/."""
        if self.db_path.exists() and not force:
            # Check if up-to-date
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
                port_status TEXT,
                runtime_status TEXT
            )
        """)

        cur.execute("""
            CREATE TABLE calls (
                caller_va INTEGER NOT NULL,
                callee_va INTEGER NOT NULL,
                PRIMARY KEY (caller_va, callee_va)
            )
        """)

        cur.execute("""
            CREATE TABLE global_xrefs (
                func_va INTEGER NOT NULL,
                global_va INTEGER NOT NULL,
                PRIMARY KEY (func_va, global_va)
            )
        """)

        cur.execute("""
            CREATE TABLE string_xrefs (
                func_va INTEGER NOT NULL,
                string_name TEXT NOT NULL,
                PRIMARY KEY (func_va, string_name)
            )
        """)

        cur.execute("CREATE INDEX idx_calls_callee ON calls(callee_va)")
        cur.execute("CREATE INDEX idx_global_xrefs_global ON global_xrefs(global_va)")
        cur.execute("CREATE INDEX idx_string_xrefs_str ON string_xrefs(string_name)")

        # Load port ledger statuses if available
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

        # Parse by-address .c files
        func_rows = []
        call_rows: Set[Tuple[int, int]] = set()
        global_rows: Set[Tuple[int, int]] = set()
        string_rows: Set[Tuple[int, str]] = set()

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

            func_rows.append((
                va,
                meta["name"],
                meta["size"],
                meta["is_thunk"],
                meta["calling_conv"],
                str(fpath.relative_to(REPO)),
                line_count,
                p_status,
                runtime_statuses.get(va),
            ))
            # Callees
            for m in FUN_RE.finditer(content):
                callee_va = parse_va(m.group(1))
                if callee_va != va:
                    call_rows.add((va, callee_va))

            # Globals
            for m in DAT_RE.finditer(content):
                g_va = parse_va(m.group(1))
                global_rows.add((va, g_va))

            # Strings
            for m in STR_RE.finditer(content):
                s_name = m.group(1)
                string_rows.add((va, s_name))

        # Also add any functions in functions.csv that had no .c file
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
                    port_statuses.get(va, "discovered"),
                    runtime_statuses.get(va),
                ))

        cur.executemany(
            "INSERT INTO functions (va, name, size, is_thunk, calling_conv, file_path, line_count, port_status, runtime_status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            func_rows
        )

        cur.executemany("INSERT OR IGNORE INTO calls (caller_va, callee_va) VALUES (?, ?)", list(call_rows))
        cur.executemany("INSERT OR IGNORE INTO global_xrefs (func_va, global_va) VALUES (?, ?)", list(global_rows))
        cur.executemany("INSERT OR IGNORE INTO string_xrefs (func_va, string_name) VALUES (?, ?)", list(string_rows))

        cur.execute("INSERT INTO metadata (key, value) VALUES ('func_count', ?)", (str(len(func_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('call_count', ?)", (str(len(call_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('global_xref_count', ?)", (str(len(global_rows)),))
        cur.execute("INSERT INTO metadata (key, value) VALUES ('string_xref_count', ?)", (str(len(string_rows)),))

        conn.commit()
        conn.close()

        return {
            "status": "built",
            "functions": len(func_rows),
            "calls": len(call_rows),
            "global_xrefs": len(global_rows),
            "string_xrefs": len(string_rows),
            "path": str(self.db_path),
        }

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

    def get_callers(self, target: str | int) -> List[Dict[str, Any]]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("""
            SELECT f.va, f.name, f.size, f.is_thunk, f.port_status
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
            SELECT f.va, f.name, f.size, f.is_thunk, f.port_status
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
            SELECT f.va, f.name, f.size, f.port_status
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
            SELECT f.va, f.name, f.size, f.port_status
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
        cur.execute("SELECT global_va FROM global_xrefs WHERE func_va=? ORDER BY global_va", (va,))
        return [r[0] for r in cur.fetchall()]

    def get_function_strings(self, target: str | int) -> List[str]:
        conn = self.connect()
        cur = conn.cursor()
        va = parse_va(target) if not isinstance(target, int) else target
        cur.execute("SELECT string_name FROM string_xrefs WHERE func_va=? ORDER BY string_name", (va,))
        return [r[0] for r in cur.fetchall()]
    def disasm(self, target: str | int, att: bool = False, exe_path: Optional[Path] = None) -> str:
        fn = self.get_function(target)
        if not fn:
            raise ValueError(f"Function not found: {target}")
        va = fn["va"]
        sz = fn["size"]
        if exe_path is None:
            exe_path = REPO / "vendor" / "unpacked" / "recettear.unpacked.exe"
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

        # Search functions by name or hex
        cur.execute("""
            SELECT va, name, size, is_thunk, port_status
            FROM functions
            WHERE name LIKE ? OR printf('0x%06x', va) LIKE ?
            ORDER BY va
            LIMIT ?
        """, (q, q, limit))
        funcs = [dict(r) for r in cur.fetchall()]

        # Search string xrefs
        cur.execute("""
            SELECT string_name, COUNT(func_va) as count
            FROM string_xrefs
            WHERE string_name LIKE ?
            GROUP BY string_name
            ORDER BY count DESC
            LIMIT ?
        """, (q, limit))
        strings = [dict(r) for r in cur.fetchall()]

        # Search global xrefs
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
        cur.execute("SELECT COUNT(*) FROM calls")
        total_calls = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM global_xrefs")
        total_globals = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM string_xrefs")
        total_strings = cur.fetchone()[0]

        cur.execute("SELECT port_status, COUNT(*) FROM functions GROUP BY port_status")
        status_counts = {r[0]: r[1] for r in cur.fetchall()}

        return {
            "total_functions": total_funcs,
            "non_thunk_functions": non_thunk_funcs,
            "total_call_edges": total_calls,
            "global_xrefs": total_globals,
            "string_xrefs": total_strings,
            "inventory_breakdown": status_counts,
        }


# ─── CLI Handlers ────────────────────────────────────────────────────────────

def cmd_build(idx: ReIndex, args: argparse.Namespace) -> int:
    res = idx.build(force=args.force)
    if args.json:
        print(json.dumps(res, indent=1))
    else:
        print(f"ReIndex built: {res.get('functions', 'N/A')} functions, {res.get('calls', 'N/A')} call edges.")
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

    payload = {
        "va": format_va(va),
        "name": fn["name"],
        "size": fn["size"],
        "is_thunk": bool(fn["is_thunk"]),
        "calling_conv": fn["calling_conv"],
        "file_path": fn["file_path"],
        "line_count": fn["line_count"],
        "port_status": fn["port_status"],
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
        print(f"  Source file:  {fn['file_path']} ({fn['line_count']} lines)")
        print(f"  Callers ({len(callers)}): {', '.join(format_fun(c['va']) for c in callers[:10])}{'...' if len(callers) > 10 else ''}")
        print(f"  Callees ({len(callees)}): {', '.join(format_fun(c['va']) for c in callees[:10])}{'...' if len(callees) > 10 else ''}")
        print(f"  Globals ({len(globals_list)}): {', '.join(f'DAT_{g:08x}' for g in globals_list[:8])}{'...' if len(globals_list) > 8 else ''}")
        print(f"  Strings ({len(strings_list)}): {', '.join(strings_list[:6])}{'...' if len(strings_list) > 6 else ''}")
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
        print(json.dumps([{"va": format_va(x["va"]), "name": x["name"], "port_status": x["port_status"]} for xrefs in [xrefs] for x in xrefs], indent=1))
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
        print(f"  Direct call edges:   {st['total_call_edges']}")
        print(f"  Global xrefs:        {st['global_xrefs']}")
        print(f"  String xrefs:        {st['string_xrefs']}")
        print("  Inventory breakdown:")
        for k, v in sorted(st["inventory_breakdown"].items()):
            print(f"    {k:<20}: {v:>5}")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true", help="output JSON")
    ap.add_argument("--db", type=Path, default=DB_PATH, help=f"SQLite database path (default: {DB_PATH})")

    sub = ap.add_subparsers(dest="cmd", required=True)

    sp_build = sub.add_parser("build", help="build or rebuild SQLite index")
    sp_build.add_argument("--force", action="store_true", help="force rebuild")

    sp_info = sub.add_parser("info", help="show function summary")
    sp_info.add_argument("target", help="function VA or name (e.g. 0x4905a8 or FUN_004905a8)")
    sp_text = sub.add_parser("text", help="display decompiled C source text of function")
    sp_text.add_argument("target", help="function VA or name (e.g. 0x4905a8 or FUN_004905a8)")
    sp_text.add_argument("-n", "--numbers", action="store_true", help="show 1-based line numbers")


    sp_callers = sub.add_parser("callers", help="list callers of a function")
    sp_callers.add_argument("target", help="function VA or name")

    sp_callees = sub.add_parser("callees", help="list callees of a function")
    sp_callees.add_argument("target", help="function VA or name")

    sp_xrefs = sub.add_parser("xrefs", help="list functions referencing global or string")
    sp_xrefs.add_argument("target", help="global address (e.g. DAT_056e6280, 0x56e6280) or string name (s_save_dat_005cfa98)")

    sp_tree = sub.add_parser("tree", help="call tree")
    sp_tree.add_argument("target", help="function VA or name")
    sp_tree.add_argument("--depth", type=int, default=3, help="max depth (default: 3)")

    sp_disasm = sub.add_parser("disasm", help="disassemble function from retail binary via objdump")
    sp_disasm.add_argument("target", help="function VA or name")
    sp_disasm.add_argument("--att", action="store_true", help="use AT&T syntax instead of Intel")

    sp_unported = sub.add_parser("unported-callees", help="list unported callees")
    sp_unported.add_argument("target", help="function VA or name")

    sp_search = sub.add_parser("search", help="search functions, strings, globals")
    sp_search.add_argument("query", help="search string")
    sp_search.add_argument("--limit", type=int, default=30, help="max results (default: 30)")
    sp_search.add_argument("-c", "--code", action="store_true", help="search decompiled C function bodies as well")

    sub.add_parser("stats", help="index statistics")

    args = ap.parse_args(argv)
    idx = ReIndex(db_path=args.db)

    handlers = {
        "build": cmd_build,
        "info": cmd_info,
        "callers": cmd_callers,
        "callees": cmd_callees,
        "xrefs": cmd_xrefs,
        "tree": cmd_tree,
        "disasm": cmd_disasm,
        "text": cmd_text,
        "unported-callees": cmd_unported_callees,
        "search": cmd_search,
        "stats": cmd_stats,
    }

    try:
        return handlers[args.cmd](idx, args)
    finally:
        idx.close()


if __name__ == "__main__":
    sys.exit(main())
