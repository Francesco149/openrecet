#!/usr/bin/env python3
"""
tools/extract/xfile.py — DirectX .x text-format parser (golden oracle for C port).

Parses the full DirectX retained-mode .x text format (xof 0303txt 0032) as used
by Recettear's 242 mesh files.  Emits structured JSON matching the contract schema
defined in docs/findings/mesh-loader.md (C2 chip).

Usage:
    xfile.py path/to/model.x                          # full JSON to stdout
    xfile.py --brief path/to/model.x                  # arrays replaced with null
    xfile.py --per-file-out DIR path1.x path2.x ...   # one JSON per file under DIR
    xfile.py --scan vendor/original/xfile/            # legacy scan (histogram)
    xfile.py --scan vendor/original/xfile/ --aggregate
    xfile.py --self-test                               # built-in smoke test
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Iterator, NamedTuple

# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

class Token(NamedTuple):
    kind: str   # IDENT INT FLOAT STRING LBRACE RBRACE SEMI COMMA UUID EOF
    value: object
    line: int


_COMMENT_RE = re.compile(r'//[^\n]*|/\*.*?\*/', re.DOTALL)


def _strip_comments(src: str) -> str:
    return _COMMENT_RE.sub(lambda m: '\n' * m.group().count('\n'), src)


def _tokenize(src: str) -> list[Token]:
    """Tokenize .x text source into a list of Tokens."""
    src = _strip_comments(src)
    pos = 0
    n = len(src)
    line = 1
    result: list[Token] = []

    ws_re    = re.compile(r'[ \t\r]+')
    uuid_re  = re.compile(r'<[0-9A-Fa-f\-]+>')
    str_re   = re.compile(r'"([^"]*)"')
    float_re = re.compile(r'[+-]?(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|[+-]?\d+[eE][+-]?\d+')
    int_re   = re.compile(r'[+-]?\d+')
    ident_re = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

    while pos < n:
        c = src[pos]

        if c == '\n':
            line += 1
            pos += 1
            continue

        m = ws_re.match(src, pos)
        if m:
            pos = m.end()
            continue

        # UUID  <XXXX-...>
        m = uuid_re.match(src, pos)
        if m:
            result.append(Token('UUID', m.group(), line))
            pos = m.end()
            continue

        # string literal
        if c == '"':
            m = str_re.match(src, pos)
            if m:
                result.append(Token('STRING', m.group(1), line))
                pos = m.end()
                continue

        # single-char punctuation
        if c == '{':
            result.append(Token('LBRACE', '{', line))
            pos += 1
            continue
        if c == '}':
            result.append(Token('RBRACE', '}', line))
            pos += 1
            continue
        if c == ';':
            result.append(Token('SEMI', ';', line))
            pos += 1
            continue
        if c == ',':
            result.append(Token('COMMA', ',', line))
            pos += 1
            continue

        # Try float first (longer match); must not be just an int
        m = float_re.match(src, pos)
        if m:
            result.append(Token('FLOAT', float(m.group()), line))
            pos = m.end()
            continue

        m = int_re.match(src, pos)
        if m:
            result.append(Token('INT', int(m.group()), line))
            pos = m.end()
            continue

        m = ident_re.match(src, pos)
        if m:
            result.append(Token('IDENT', m.group(), line))
            pos = m.end()
            continue

        # Unknown character — skip
        pos += 1

    result.append(Token('EOF', None, line))
    return result


# ---------------------------------------------------------------------------
# Parser state
# ---------------------------------------------------------------------------

class ParseError(Exception):
    pass


class Parser:
    def __init__(self, tokens: list[Token], path: str):
        self._toks = tokens
        self._pos = 0
        self._path = path
        self.skipped: Counter[str] = Counter()

    # ---- low-level helpers ----

    def peek(self) -> Token:
        return self._toks[self._pos]

    def peek2(self) -> Token:
        p = self._pos + 1
        return self._toks[p] if p < len(self._toks) else Token('EOF', None, -1)

    def advance(self) -> Token:
        t = self._toks[self._pos]
        if t.kind != 'EOF':
            self._pos += 1
        return t

    def expect(self, kind: str) -> Token:
        t = self.advance()
        if t.kind != kind:
            raise ParseError(
                f"{self._path}:{t.line}: expected {kind}, got {t.kind!r} ({t.value!r})"
            )
        return t

    def expect_value(self, kind: str) -> object:
        return self.expect(kind).value

    def eat_if(self, kind: str, value=None) -> bool:
        t = self.peek()
        if t.kind == kind and (value is None or t.value == value):
            self.advance()
            return True
        return False

    def eat_number(self) -> float:
        """Consume an INT or FLOAT token; return as float."""
        t = self.advance()
        if t.kind in ('INT', 'FLOAT'):
            return float(t.value)
        raise ParseError(
            f"{self._path}:{t.line}: expected number, got {t.kind!r} ({t.value!r})"
        )

    def eat_number_raw(self):
        """Consume INT or FLOAT; return int for INT, float for FLOAT."""
        t = self.advance()
        if t.kind == 'INT':
            return int(t.value)
        if t.kind == 'FLOAT':
            return float(t.value)
        raise ParseError(
            f"{self._path}:{t.line}: expected number, got {t.kind!r} ({t.value!r})"
        )

    def skip_block_body(self):
        """Skip tokens until we reach the RBRACE matching the already-consumed LBRACE.
        Does NOT consume the final RBRACE — leaves position AT the RBRACE.
        Caller must call p.expect('RBRACE') afterwards."""
        depth = 1
        while depth > 0:
            t = self.peek()
            if t.kind == 'EOF':
                raise ParseError(f"{self._path}: unexpected EOF in skip_block_body")
            if t.kind == 'LBRACE':
                depth += 1
                self.advance()
            elif t.kind == 'RBRACE':
                depth -= 1
                if depth > 0:
                    self.advance()
                # at depth==0: stop, leave RBRACE in stream
            else:
                self.advance()

    def count_tokens_block_body(self) -> int:
        """Count non-structural tokens inside a block (caller consumed LBRACE).
        Saves and restores position.  Does NOT include the final RBRACE."""
        saved = self._pos
        depth = 1
        count = 0
        while depth > 0:
            t = self.advance()
            if t.kind == 'EOF':
                break
            if t.kind == 'LBRACE':
                depth += 1
                count += 1
            elif t.kind == 'RBRACE':
                depth -= 1
                if depth > 0:
                    count += 1
            else:
                count += 1
        self._pos = saved
        return count

    def skip_optional_uuid(self):
        self.eat_if('UUID')

    def skip_optional_instance_name(self) -> str | None:
        """Consume and return instance name if one is present.

        An instance name is a sequence of IDENT tokens that immediately
        precedes a LBRACE or UUID.  Some material names contain hyphens
        (e.g. PDX02_-_Default) — the '-' is silently dropped by the
        tokenizer, leaving consecutive IDENT tokens which we stitch together.

        We only consume tokens if we can confirm a LBRACE or UUID follows
        within the candidate name tokens (no intervening structural tokens).
        """
        # First check: current token must be IDENT
        if self.peek().kind != 'IDENT':
            return None

        # Scan ahead to find where LBRACE/UUID appears, collecting IDENT parts
        parts = []
        i = self._pos
        while i < len(self._toks):
            t = self._toks[i]
            if t.kind in ('LBRACE', 'UUID'):
                # Confirmed: body follows, parts is the instance name
                break
            if t.kind == 'IDENT':
                parts.append(t.value)
                i += 1
            else:
                # Any non-IDENT, non-LBRACE, non-UUID token means this IDENT
                # sequence is NOT an instance name (it's a data value or stray)
                parts = []
                break

        if parts:
            self._pos = i  # advance past the name tokens
            return ''.join(parts)
        return None

    # ---- number / struct readers ----

    def read_number_raw(self):
        return self.eat_number_raw()

    def read_vector3(self) -> list:
        x = self.eat_number(); self.expect('SEMI')
        y = self.eat_number(); self.expect('SEMI')
        z = self.eat_number(); self.expect('SEMI')
        return [x, y, z]

    def read_coords2d(self) -> list:
        u = self.eat_number(); self.expect('SEMI')
        v = self.eat_number(); self.expect('SEMI')
        return [u, v]

    def read_color_rgba(self) -> list:
        r = self.eat_number(); self.expect('SEMI')
        g = self.eat_number(); self.expect('SEMI')
        b = self.eat_number(); self.expect('SEMI')
        a = self.eat_number(); self.expect('SEMI')
        return [r, g, b, a]

    def read_color_rgb(self) -> list:
        r = self.eat_number(); self.expect('SEMI')
        g = self.eat_number(); self.expect('SEMI')
        b = self.eat_number(); self.expect('SEMI')
        return [r, g, b]

    def read_matrix16(self) -> list:
        """Read 16 comma-separated floats ending with ;;."""
        result = []
        for i in range(16):
            result.append(self.eat_number())
            if i < 15:
                self.expect('COMMA')
            else:
                self.expect('SEMI')
                self.expect('SEMI')
        return result

    def read_mesh_face(self) -> list:
        """Read one MeshFace: count; v0,v1,...,vN;  Returns list of vertex indices."""
        count = int(self.eat_number_raw())
        self.expect('SEMI')
        verts = []
        for i in range(count):
            verts.append(int(self.eat_number_raw()))
            if i < count - 1:
                self.expect('COMMA')
        self.expect('SEMI')
        return verts

    def skip_unknown_nested(self, tname: str):
        """Skip an unknown nested instance whose LBRACE has NOT yet been consumed.
        Adds tname to skipped counter."""
        self.skip_optional_uuid()
        if self.peek().kind == 'LBRACE':
            self.advance()
            self.skip_block_body()
            self.expect('RBRACE')
        self.skipped[tname] += 1


# ---------------------------------------------------------------------------
# Template parsers
# (convention: caller has consumed LBRACE; we stop BEFORE RBRACE)
# ---------------------------------------------------------------------------

def parse_texture_filename(p: Parser) -> str | None:
    """Parse TextureFilename body. Returns filename string."""
    p.skip_optional_uuid()
    t = p.peek()
    if t.kind == 'STRING':
        filename = p.advance().value
        p.expect('SEMI')
        return filename
    # drain until RBRACE
    while p.peek().kind not in ('RBRACE', 'EOF'):
        p.advance()
    return None


def parse_material_body(p: Parser) -> dict:
    """Parse Material body. Caller consumed LBRACE, we stop before RBRACE."""
    p.skip_optional_uuid()

    # ColorRGBA faceColor (4 floats; each ending in SEMI, then extra SEMI = ;;)
    diffuse = p.read_color_rgba()
    p.expect('SEMI')   # second SEMI of ;;

    power = p.eat_number()
    p.expect('SEMI')

    specular = p.read_color_rgb()
    p.expect('SEMI')   # second SEMI of ;;

    emissive = p.read_color_rgb()
    p.expect('SEMI')   # second SEMI of ;;

    texture = None

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind == 'IDENT' and t.value == 'TextureFilename':
            p.advance()
            _iname = p.skip_optional_instance_name()
            p.skip_optional_uuid()
            p.expect('LBRACE')
            texture = parse_texture_filename(p)
            p.expect('RBRACE')
        elif t.kind == 'IDENT':
            tname = p.advance().value
            p.skip_unknown_nested(tname)
        else:
            p.advance()  # stray token

    return {
        'diffuse': diffuse,
        'power': power,
        'specular': specular,
        'emissive': emissive,
        'texture': texture,
    }


def parse_mesh_normals(p: Parser) -> dict:
    """Parse MeshNormals body. Stops before RBRACE."""
    p.skip_optional_uuid()

    n_normals = int(p.eat_number_raw())
    p.expect('SEMI')
    normals = []
    for i in range(n_normals):
        normals.append(p.read_vector3())
        if i < n_normals - 1:
            p.expect('COMMA')
        else:
            p.expect('SEMI')   # trailing ;; second semi

    n_faces = int(p.eat_number_raw())
    p.expect('SEMI')
    face_normals = []
    for i in range(n_faces):
        face_normals.append(p.read_mesh_face())
        if i < n_faces - 1:
            p.expect('COMMA')
        else:
            p.expect('SEMI')   # trailing ;; second semi

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind == 'IDENT':
            tname = p.advance().value
            p.skip_unknown_nested(tname)
        else:
            p.advance()

    return {'normals': normals, 'face_normals': face_normals}


def parse_mesh_texture_coords(p: Parser) -> dict:
    """Parse MeshTextureCoords body. Stops before RBRACE."""
    p.skip_optional_uuid()

    n = int(p.eat_number_raw())
    p.expect('SEMI')
    uvs = []
    for i in range(n):
        uvs.append(p.read_coords2d())
        if i < n - 1:
            p.expect('COMMA')
        else:
            p.expect('SEMI')   # trailing ;; second semi

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind == 'IDENT':
            tname = p.advance().value
            p.skip_unknown_nested(tname)
        else:
            p.advance()

    return {'uvs': uvs}


def parse_mesh_vertex_colors(p: Parser) -> dict:
    """Parse MeshVertexColors body. Stops before RBRACE.

    Array format for IndexedColor (index; ColorRGBA):
        index; r; g; b; a; SEP
    where SEP is ',' between items and ';' after the last (making ';;').
    Some exporters use ';;' as an item separator too (both are accepted
    by d3dxof), so we accept either SEMI or COMMA as the separator token.
    """
    p.skip_optional_uuid()

    n = int(p.eat_number_raw())
    p.expect('SEMI')
    colors = []
    for i in range(n):
        # IndexedColor: DWORD index; ColorRGBA(r;g;b;a;)
        # Exporter variants for the per-item terminator:
        #  - cave_dun style: index;r;g;b;a;;  (one extra SEMI) for most items,
        #    index;r;g;b;a;, for second-to-last (SEMI then COMMA)
        #  - boss_omu style: index;r;g;b;a;;, (SEMI SEMI COMMA) for non-last
        # We consume any trailing SEMIs and one optional COMMA between items.
        idx = int(p.eat_number_raw())
        p.expect('SEMI')
        color = p.read_color_rgba()   # reads r;g;b;a; (4 values, 4 SEMIs)
        # Consume any extra SEMIs (from ;; or ;;)
        while p.peek().kind == 'SEMI':
            p.advance()
        # Consume the comma separator between items (if present)
        if p.peek().kind == 'COMMA':
            p.advance()
        colors.append({'index': idx, 'color': color})

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind == 'IDENT':
            tname = p.advance().value
            p.skip_unknown_nested(tname)
        else:
            p.advance()

    return {'vertex_colors': colors}


def parse_mesh_material_list(p: Parser, global_materials: dict) -> dict:
    """Parse MeshMaterialList body. Stops before RBRACE."""
    p.skip_optional_uuid()

    n_mats = int(p.eat_number_raw())
    p.expect('SEMI')
    n_face_idxs = int(p.eat_number_raw())
    p.expect('SEMI')

    # array DWORD faceIndexes[nFaceIndexes]
    # Format varies by exporter:
    #   - Scalar style: val,val,...,val;  (one trailing ; after last val)
    #   - With extra terminator: val,val,...,val;;  (;; after last)
    # We accept both by reading N values with , or ; separators between
    # them, then consuming a ; after the last (and optionally one more ;).
    face_indexes: list[int] = []
    if n_face_idxs > 0:
        for i in range(n_face_idxs):
            face_indexes.append(int(p.eat_number_raw()))
            sep = p.advance()   # SEMI or COMMA
            if sep.kind not in ('SEMI', 'COMMA'):
                raise ParseError(
                    f"{p._path}:{sep.line}: expected ; or , in face_indexes array,"
                    f" got {sep.kind!r} ({sep.value!r})"
                )
        # After last value we already consumed one SEMI; optionally consume a second
        if p.peek().kind == 'SEMI':
            p.advance()
    else:
        # n_face_idxs == 0: consume the ; or ;; that follows
        p.advance()   # at minimum one SEMI
        if p.peek().kind == 'SEMI':
            p.advance()

    material_refs: list[str] = []
    inline_materials: list[dict] = []

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind == 'LBRACE':
            # reference block: { MaterialName }
            # Material names can contain hyphens (e.g. PDX02_-_Default),
            # so consume all non-RBRACE tokens and stitch into the name.
            p.advance()   # consume {
            parts = []
            while p.peek().kind not in ('RBRACE', 'EOF'):
                parts.append(str(p.advance().value))
            p.expect('RBRACE')
            ref_name = ''.join(parts)
            material_refs.append(ref_name)
        elif t.kind == 'IDENT' and t.value == 'Material':
            p.advance()
            inst_name = p.skip_optional_instance_name()
            p.skip_optional_uuid()
            p.expect('LBRACE')
            mat = parse_material_body(p)
            p.expect('RBRACE')
            if inst_name:
                mat['name'] = inst_name
                global_materials[inst_name] = mat
            inline_materials.append(mat)
        elif t.kind == 'IDENT':
            tname = p.advance().value
            p.skip_unknown_nested(tname)
        else:
            p.advance()

    return {
        'n_materials': n_mats,
        'face_indexes': face_indexes,
        'material_refs': material_refs,
        'inline_materials': inline_materials,
    }


def parse_frame_transform_matrix(p: Parser) -> list:
    """Parse FrameTransformMatrix body (16-float matrix). Stops before RBRACE."""
    p.skip_optional_uuid()
    matrix = p.read_matrix16()
    # drain any stray tokens
    while p.peek().kind not in ('RBRACE', 'EOF'):
        p.advance()
    return matrix


def parse_mesh_body(p: Parser, name: str, frame_path: list[str],
                    global_materials: dict) -> dict:
    """Parse Mesh body. Caller consumed LBRACE; stops before RBRACE."""
    p.skip_optional_uuid()

    n_verts = int(p.eat_number_raw())
    p.expect('SEMI')

    vertices = []
    for i in range(n_verts):
        vertices.append(p.read_vector3())
        if i < n_verts - 1:
            p.expect('COMMA')
        else:
            p.expect('SEMI')   # trailing ;; second semi

    n_faces = int(p.eat_number_raw())
    p.expect('SEMI')

    faces = []
    for i in range(n_faces):
        faces.append(p.read_mesh_face())
        if i < n_faces - 1:
            p.expect('COMMA')
        else:
            p.expect('SEMI')   # trailing ;; second semi

    normals: list = []
    face_normals: list = []
    uvs: list = []
    vertex_colors: list = []
    has_material_list = False
    material_count = 0
    material_refs: list[str] = []
    inline_materials: list[dict] = []
    face_material_indexes: list[int] = []

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind != 'IDENT':
            p.advance()
            continue

        tname = t.value
        p.advance()
        iname = p.skip_optional_instance_name()
        p.skip_optional_uuid()

        if p.peek().kind != 'LBRACE':
            p.skipped[tname] += 1
            continue
        p.advance()   # consume LBRACE

        if tname == 'MeshNormals':
            data = parse_mesh_normals(p)
            normals = data['normals']
            face_normals = data['face_normals']
        elif tname == 'MeshTextureCoords':
            data = parse_mesh_texture_coords(p)
            uvs = data['uvs']
        elif tname == 'MeshMaterialList':
            data = parse_mesh_material_list(p, global_materials)
            has_material_list = True
            material_count = data['n_materials']
            material_refs = data['material_refs']
            inline_materials = data['inline_materials']
            face_material_indexes = data['face_indexes']
        elif tname == 'MeshVertexColors':
            data = parse_mesh_vertex_colors(p)
            vertex_colors = data['vertex_colors']
        else:
            p.skip_block_body()
            p.skipped[tname] += 1

        p.expect('RBRACE')

    return {
        'name': name or '',
        'frame_path': list(frame_path),
        'vertex_count': n_verts,
        'face_count': n_faces,
        'normal_count': len(normals),
        'uv_count': len(uvs),
        'vertex_color_count': len(vertex_colors),
        'has_material_list': has_material_list,
        'material_count': material_count,
        'material_references': material_refs,
        'inline_materials': inline_materials,
        'vertices': vertices,
        'faces': faces,
        'normals': normals,
        'face_normals': face_normals,
        'uvs': uvs,
        'face_material_indexes': face_material_indexes,
    }


def parse_frame_body(p: Parser, frame_name: str, frame_path: list[str],
                     global_materials: dict,
                     all_meshes: list, all_frames: list) -> dict:
    """Parse Frame body. Caller consumed LBRACE; stops before RBRACE."""
    p.skip_optional_uuid()

    transform = None
    child_frame_names: list[str] = []
    mesh_count_in_frame = 0

    while p.peek().kind not in ('RBRACE', 'EOF'):
        t = p.peek()
        if t.kind != 'IDENT':
            p.advance()
            continue

        tname = t.value
        p.advance()
        iname = p.skip_optional_instance_name()
        p.skip_optional_uuid()

        if p.peek().kind != 'LBRACE':
            p.skipped[tname] += 1
            continue
        p.advance()   # consume LBRACE

        if tname == 'FrameTransformMatrix':
            transform = parse_frame_transform_matrix(p)
        elif tname == 'Frame':
            child_name = iname or ''
            child_path = frame_path + [child_name]
            child_frame = parse_frame_body(
                p, child_name, child_path, global_materials, all_meshes, all_frames
            )
            all_frames.append(child_frame)
            child_frame_names.append(child_name)
        elif tname == 'Mesh':
            mesh_name = iname or ''
            mesh_data = parse_mesh_body(p, mesh_name, frame_path, global_materials)
            all_meshes.append(mesh_data)
            mesh_count_in_frame += 1
        else:
            p.skip_block_body()
            p.skipped[tname] += 1

        p.expect('RBRACE')

    return {
        'name': frame_name,
        'transform': transform,
        'child_count': len(child_frame_names),
        'mesh_count': mesh_count_in_frame,
        'children_names': child_frame_names,
    }


# ---------------------------------------------------------------------------
# Top-level file parser
# ---------------------------------------------------------------------------

# Templates we parse or skip silently (no skipped_templates entry)
_SILENT_SKIP = {'Header'}


def parse_xfile(path: Path) -> dict:
    data = path.read_bytes()
    size = len(data)

    m = re.match(rb'xof (\d{4})(txt|bin|tzip|bzip) (\d{4})', data[:16])
    if not m:
        raise ParseError(f"{path}: invalid xof header: {data[:16]!r}")

    header = {
        'version': m.group(1).decode(),
        'encoding': m.group(2).decode(),
        'float_size': int(m.group(3)),
    }

    src = data.decode('latin-1')
    tokens = _tokenize(src)
    p = Parser(tokens, str(path))

    global_materials: dict[str, dict] = {}
    all_meshes: list[dict] = []
    all_frames: list[dict] = []

    # Skip the xof header tokens (xof 0303 txt 0032) — they tokenize as misc
    # IDENT/INT but will be handled gracefully: they don't have a {, so they
    # get ignored via the skip_unknown_nested path.

    while p.peek().kind != 'EOF':
        t = p.peek()

        if t.kind in ('SEMI', 'COMMA', 'UUID', 'INT', 'FLOAT', 'STRING'):
            p.advance()
            continue

        if t.kind != 'IDENT':
            p.advance()
            continue

        tname = t.value
        p.advance()

        # template keyword: skip entire block
        if tname == 'template':
            _tmpl_name_tok = p.advance()   # template type name
            p.skip_optional_uuid()
            if p.peek().kind == 'LBRACE':
                p.advance()
                p.skip_block_body()
                p.expect('RBRACE')
            continue

        # instance: optional name then optional UUID
        iname = p.skip_optional_instance_name()
        p.skip_optional_uuid()

        if p.peek().kind != 'LBRACE':
            # No body — stray IDENT (e.g. tokens from xof header line)
            continue

        p.advance()   # consume LBRACE

        if tname == 'Material':
            mat = parse_material_body(p)
            mat_name = iname or ''
            mat['name'] = mat_name
            global_materials[mat_name] = mat

        elif tname == 'Frame':
            frame_name = iname or ''
            frame_path = [frame_name]
            frame = parse_frame_body(
                p, frame_name, frame_path, global_materials, all_meshes, all_frames
            )
            all_frames.insert(0, frame)   # top-level frames first in output

        elif tname == 'Mesh':
            mesh_name = iname or ''
            mesh_data = parse_mesh_body(p, mesh_name, [], global_materials)
            all_meshes.append(mesh_data)

        elif tname in _SILENT_SKIP:
            p.skip_block_body()

        else:
            p.skip_block_body()
            p.skipped[tname] += 1

        p.expect('RBRACE')

    # Build stats
    total_vertices = sum(m['vertex_count'] for m in all_meshes)
    total_faces = sum(m['face_count'] for m in all_meshes)
    total_normals = sum(m['normal_count'] for m in all_meshes)

    all_textures: list[str] = []
    seen_tex: set[str] = set()

    def _collect_tex(mat: dict):
        tx = mat.get('texture')
        if tx and tx not in seen_tex:
            seen_tex.add(tx)
            all_textures.append(tx)

    for mat in global_materials.values():
        _collect_tex(mat)
    for mesh in all_meshes:
        for mat in mesh.get('inline_materials', []):
            _collect_tex(mat)

    stats = {
        'mesh_count': len(all_meshes),
        'frame_count': len(all_frames),
        'global_material_count': len(global_materials),
        'total_vertices': total_vertices,
        'total_faces': total_faces,
        'total_normals': total_normals,
        'unique_textures': len(seen_tex),
    }

    gmat_list = []
    for mat_name, mat in global_materials.items():
        gmat_list.append({
            'name': mat_name,
            'diffuse': mat['diffuse'],
            'power': mat['power'],
            'specular': mat['specular'],
            'emissive': mat['emissive'],
            'texture': mat.get('texture'),
        })

    try:
        rel_path = str(path.relative_to(Path('/opt/src/openrecet')))
    except ValueError:
        rel_path = str(path)

    return {
        'path': rel_path,
        'size': size,
        'header': header,
        'stats': stats,
        'textures': all_textures,
        'global_materials': gmat_list,
        'meshes': all_meshes,
        'frames': all_frames,
        'skipped_templates': dict(p.skipped),
    }


def make_brief(full: dict) -> dict:
    """Replace per-vertex/per-face arrays with null; keep counts/metadata."""
    brief = copy.deepcopy(full)
    _ARRAY_KEYS = {'vertices', 'faces', 'normals', 'face_normals', 'uvs',
                   'face_material_indexes'}
    for mesh in brief.get('meshes', []):
        for k in _ARRAY_KEYS:
            if k in mesh:
                mesh[k] = None
    return brief


# ---------------------------------------------------------------------------
# Legacy scan / aggregate (back-compat from original stub)
# ---------------------------------------------------------------------------

def _legacy_template_counts(text: str) -> dict[str, int]:
    counter: Counter[str] = Counter()
    for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\b[^{};]*\{', text):
        name = m.group(1)
        if name in {'FLOAT', 'DWORD', 'WORD', 'CHAR', 'STRING', 'ARRAY',
                    'template', 'void', 'binary'}:
            continue
        counter[name] += 1
    return dict(counter.most_common())


def legacy_summarize(path: Path) -> dict:
    data = path.read_bytes()
    info: dict = {'path': str(path), 'size': len(data)}
    m = re.match(rb'xof (\d{4})(txt|bin|tzip|bzip) (\d{4})', data[:16])
    if not m:
        info['valid'] = False
        return info
    info['valid'] = True
    info['version'] = m.group(1).decode()
    info['encoding'] = m.group(2).decode()
    info['float_size'] = int(m.group(3))
    try:
        text = data.decode('latin-1')
    except Exception as e:
        info['decode_error'] = str(e)
        return info
    info['templates'] = _legacy_template_counts(text)
    return info


def legacy_scan(root: Path) -> list[dict]:
    return [legacy_summarize(p) for p in sorted(root.rglob('*.x'))]


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

def self_test():
    ice01 = Path('/opt/src/openrecet/vendor/original/xfile/etc/ice01.x')
    if not ice01.exists():
        print('SKIP: ice01.x not found at expected location', flush=True)
        return

    print('Running self-test on ice01.x ...', flush=True)
    result = parse_xfile(ice01)

    stats = result['stats']
    assert stats['mesh_count'] == 1,          f"mesh_count {stats['mesh_count']} != 1"
    assert stats['total_vertices'] == 41,     f"total_vertices {stats['total_vertices']} != 41"
    assert stats['total_faces'] == 30,        f"total_faces {stats['total_faces']} != 30"
    assert stats['total_normals'] == 17,      f"total_normals {stats['total_normals']} != 17"

    gmats = result['global_materials']
    assert len(gmats) == 2, f"global_material_count {len(gmats)} != 2"

    def find_mat(name):
        for m in gmats:
            if m['name'] == name:
                return m
        return None

    xof_default = find_mat('xof_default')
    assert xof_default is not None, 'missing xof_default material'
    diff = xof_default['diffuse']
    assert abs(diff[0] - 0.4) < 1e-5, f'xof_default diffuse R={diff[0]} != 0.4'
    assert abs(diff[1] - 0.4) < 1e-5, f'xof_default diffuse G={diff[1]} != 0.4'
    assert abs(diff[2] - 0.4) < 1e-5, f'xof_default diffuse B={diff[2]} != 0.4'
    assert abs(diff[3] - 1.0) < 1e-5, f'xof_default diffuse A={diff[3]} != 1.0'
    assert abs(xof_default['power'] - 32.0) < 1e-4, \
        f"xof_default power={xof_default['power']} != 32.0"
    assert xof_default['texture'] is None, \
        f"xof_default should have no texture, got {xof_default['texture']!r}"

    mat25 = find_mat('Material__25')
    assert mat25 is not None, 'missing Material__25'
    assert mat25['texture'] == 'w_ice.bmp', \
        f"Material__25 texture {mat25['texture']!r} != 'w_ice.bmp'"

    meshes = result['meshes']
    assert meshes[0]['name'] == 'Box01', f"meshes[0].name {meshes[0]['name']!r} != 'Box01'"

    v0 = meshes[0]['vertices'][0]
    EPS = 1e-4
    assert abs(v0[0] - (-8.577065)) < EPS, f'vertex[0][0]={v0[0]} != -8.577065'
    assert abs(v0[1] - (-3.734980)) < EPS, f'vertex[0][1]={v0[1]} != -3.734980'
    assert abs(v0[2] - (-7.484766)) < EPS, f'vertex[0][2]={v0[2]} != -7.484766'

    frames = result['frames']
    assert frames[0]['name'] == 'Frame_World', \
        f"frames[0].name={frames[0]['name']!r} != 'Frame_World'"
    assert 'Frame_Box01' in frames[0]['children_names'], \
        f"Frame_World children {frames[0]['children_names']} missing Frame_Box01"

    transform = frames[0]['transform']
    assert transform is not None, 'Frame_World transform is None'
    for idx in (0, 5, 10, 15):
        assert abs(transform[idx] - 1.0) < 1e-5, \
            f'Frame_World transform[{idx}]={transform[idx]} != 1.0'

    print('All ice01.x assertions PASSED.', flush=True)
    print(f'  skipped_templates: {result["skipped_templates"]}', flush=True)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('paths', nargs='*', type=Path, help='.x files to parse')
    ap.add_argument('--scan', type=Path, default=None,
                    help='Recursively scan a directory tree')
    ap.add_argument('--out', type=Path, default=None,
                    help='Write JSON output to this file (default: stdout)')
    ap.add_argument('--aggregate', action='store_true',
                    help='With --scan, emit aggregate template histogram (legacy mode)')
    ap.add_argument('--brief', action='store_true',
                    help='Omit per-vertex/per-face arrays from output')
    ap.add_argument('--full', action='store_true',
                    help='Include all arrays (default, opposite of --brief)')
    ap.add_argument('--per-file-out', type=Path, default=None, metavar='DIR',
                    help='Write one <path>.json per file under DIR')
    ap.add_argument('--self-test', action='store_true',
                    help='Run built-in assertions on ice01.x and exit')
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    # Legacy --scan without explicit paths → histogram mode
    if args.scan and not args.paths:
        results = legacy_scan(args.scan)
        output: dict = {'files': results}
        if args.aggregate:
            agg: Counter[str] = Counter()
            for r in results:
                for k, v in (r.get('templates') or {}).items():
                    agg[k] += v
            output['aggregate_templates'] = dict(agg.most_common())
            output['file_count'] = len(results)
            output['total_bytes'] = sum(r.get('size', 0) for r in results)
        text = json.dumps(output, indent=2, ensure_ascii=False)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(text)
            print(f'wrote {args.out} ({len(results)} files)')
        else:
            print(text)
        return 0

    # Full-parse mode
    paths_to_parse: list[Path] = list(args.paths)
    if args.scan:
        paths_to_parse.extend(sorted(args.scan.rglob('*.x')))

    if not paths_to_parse:
        ap.error('provide one or more .x paths, or --scan DIR')

    use_brief = args.brief and not args.full
    results = []
    fail_count = 0
    empty_mesh_files = []

    for path in paths_to_parse:
        try:
            parsed = parse_xfile(path)
            if use_brief:
                parsed = make_brief(parsed)
            if not parsed['meshes']:
                empty_mesh_files.append(str(path))
            results.append(parsed)
            if args.per_file_out:
                out_path = args.per_file_out / (str(path).lstrip('/') + '.json')
                out_path.parent.mkdir(parents=True, exist_ok=True)
                out_path.write_text(json.dumps(parsed, indent=2, ensure_ascii=False))
        except Exception as exc:
            fail_count += 1
            import traceback
            print(f'ERROR parsing {path}: {exc}', file=sys.stderr)
            traceback.print_exc(file=sys.stderr)

    if empty_mesh_files:
        print(f'\nFiles with empty meshes ({len(empty_mesh_files)}):', file=sys.stderr)
        for f in empty_mesh_files:
            print(f'  {f}', file=sys.stderr)

    if fail_count:
        print(f'\n{fail_count} file(s) failed to parse.', file=sys.stderr)

    if not args.per_file_out:
        if len(results) == 1:
            text = json.dumps(results[0], indent=2, ensure_ascii=False)
        else:
            text = json.dumps({'files': results, 'count': len(results)},
                              indent=2, ensure_ascii=False)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(text)
            print(f'wrote {args.out} ({len(results)} files)')
        else:
            print(text)

    return 1 if fail_count else 0


if __name__ == '__main__':
    sys.exit(main())
