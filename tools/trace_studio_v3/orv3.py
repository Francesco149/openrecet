#!/usr/bin/env python3
"""Trace Studio v3 — orv3 capture-container reader (Python).

A faithful Python parser for the flat orv3 container the proxy writes and the
replayer reads (format: format/orv3_format.h). The C replayer renders frames;
this library lets the Python side REASON about a container — frame count, each
kept frame's present-count + byte range + resources, and (for the slice cache)
re-emit a sub-window as a standalone container.

A container is `[MAGIC][VERSION][DEV_PARAMS]` then, per kept frame, a section:
`[new RES…][scalar-state preamble][this frame's calls][Present]`. Resources are
content-hash dedup'd across the WHOLE window (a mesh bound every frame is stored
ONCE), so a frame's bound resource may have been DEFINED in an earlier section —
slicing must pull those forward (see slice_window).

Used by orv3_sync.py (identity join) and the slice cache. Streaming, native
endian (both ends i686); no external deps.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = 0x33565241  # "ARV3"

# record types (mirror orv3_format.h)
DEV_PARAMS = 1
RES_TEX, RES_VB, RES_IB = 2, 3, 4
SetRenderState, SetTextureStageState, SetTransform, SetMaterial = 10, 11, 12, 13
SetTexture, SetStreamSource, SetIndices, SetVertexShader = 14, 15, 16, 17
DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP = 18, 19, 20, 21
Clear, SetLight, LightEnable, BeginScene, EndScene, Present, EOF = 22, 23, 24, 25, 26, 27, 99

OPNAME = {
    DEV_PARAMS: "DEV_PARAMS", RES_TEX: "RES_TEX", RES_VB: "RES_VB", RES_IB: "RES_IB",
    SetRenderState: "SetRenderState", SetTextureStageState: "SetTextureStageState",
    SetTransform: "SetTransform", SetMaterial: "SetMaterial", SetTexture: "SetTexture",
    SetStreamSource: "SetStreamSource", SetIndices: "SetIndices", SetVertexShader: "SetVertexShader",
    DrawPrimitive: "DrawPrimitive", DrawIndexedPrimitive: "DrawIndexedPrimitive",
    DrawPrimitiveUP: "DrawPrimitiveUP", DrawIndexedPrimitiveUP: "DrawIndexedPrimitiveUP",
    Clear: "Clear", SetLight: "SetLight", LightEnable: "LightEnable",
    BeginScene: "BeginScene", EndScene: "EndScene", Present: "Present", EOF: "EOF",
}

DEV_FIELDS = ("w", "h", "bbfmt", "depthfmt", "windowed", "bbcount",
              "presentflags", "behavior", "interval", "adapter", "devtype", "autods")


@dataclass
class Frame:
    """One kept frame's section, delimited by its Present record."""
    index: int                       # 0-based kept-frame index
    present: int                     # Present payload = absolute present-count (g_frame)
    byte_start: int                  # container offset of this section's first record
    byte_end: int                    # container offset just past this section's Present
    res_defined: list[int] = field(default_factory=list)    # resource ids first stored here
    res_referenced: set[int] = field(default_factory=set)   # ids cited by this section's calls
    n_draws: int = 0
    n_calls: int = 0


class Container:
    """Parsed orv3 container. `frames` are the kept-frame sections in order;
    `resources` maps id -> (type, byte_start, byte_end) for slice re-emit."""

    def __init__(self, data: bytes):
        self.data = data
        self.magic = 0
        self.version = 0
        self.dev: dict[str, int] = {}
        self.frames: list[Frame] = []
        self.resources: dict[int, tuple[int, int, int]] = {}  # id -> (type, start, end)
        self._parse()

    @classmethod
    def load(cls, path: str | Path) -> "Container":
        return cls(Path(path).read_bytes())

    # ── record-size walk ──
    def _parse(self) -> None:
        d = self.data
        n = len(d)
        p = 0

        def u(off: int) -> int:
            return struct.unpack_from("<I", d, off)[0]

        self.magic = u(p); p += 4
        self.version = u(p); p += 4
        if self.magic != MAGIC:
            raise ValueError(f"bad magic 0x{self.magic:08x} (want 0x{MAGIC:08x})")

        # section accumulators (reset at each Present)
        sect_start = p
        res_defined: list[int] = []
        res_ref: set[int] = set()
        ndraws = ncalls = 0

        while p < n:
            t = u(p)
            rec_start = p
            p += 4
            if t == DEV_PARAMS:
                vals = struct.unpack_from("<12I", d, p); p += 48
                self.dev = dict(zip(DEV_FIELDS, vals))
                sect_start = p
            elif t == RES_TEX:
                rid = u(p); p += 4
                levels = u(p); p += 4
                for _ in range(levels):
                    p += 12               # w,h,fmt
                    rb = u(p); p += 4      # rowbytes (unused here)
                    dl = u(p); p += 4
                    p += dl
                self.resources[rid] = (RES_TEX, rec_start, p)
                res_defined.append(rid)
            elif t in (RES_VB, RES_IB):
                rid = u(p); p += 4
                p += 8                     # size, fvf/fmt
                dl = u(p); p += 4
                p += dl
                self.resources[rid] = (t, rec_start, p)
                res_defined.append(rid)
            elif t == SetRenderState:
                p += 8
            elif t == SetTextureStageState:
                p += 12
            elif t == SetTransform:
                p += 4 + 64
            elif t == SetMaterial:
                p += 68
            elif t == SetTexture:
                p += 4                     # stage
                rid = struct.unpack_from("<i", d, p)[0]; p += 4
                if rid >= 0:
                    res_ref.add(rid)
            elif t == SetStreamSource:
                p += 4                     # stream
                rid = struct.unpack_from("<i", d, p)[0]; p += 4
                p += 4                     # stride
                if rid >= 0:
                    res_ref.add(rid)
            elif t == SetIndices:
                rid = struct.unpack_from("<i", d, p)[0]; p += 4
                p += 4                     # basevertex
                if rid >= 0:
                    res_ref.add(rid)
            elif t == SetVertexShader:
                p += 4
            elif t == DrawPrimitive:
                p += 12; ndraws += 1; ncalls += 1
            elif t == DrawIndexedPrimitive:
                p += 20; ndraws += 1; ncalls += 1
            elif t == DrawPrimitiveUP:
                p += 12                    # pt, primcount, stride
                dl = u(p); p += 4
                p += dl
                ndraws += 1; ncalls += 1
            elif t == DrawIndexedPrimitiveUP:
                p += 20                    # pt, minvi, numvi, primcount, idxfmt
                il = u(p); p += 4
                p += il
                p += 4                     # stride
                vl = u(p); p += 4
                p += vl
                ndraws += 1; ncalls += 1
            elif t == Clear:
                cnt = u(p); p += 4
                p += cnt * 16              # rects
                p += 16                    # flags, color, z, stencil
                ncalls += 1
            elif t == SetLight:
                p += 4                     # index
                dl = u(p); p += 4
                p += dl
            elif t == LightEnable:
                p += 8
            elif t in (BeginScene, EndScene):
                pass
            elif t == Present:
                payload = u(p); p += 4
                self.frames.append(Frame(
                    index=len(self.frames), present=payload,
                    byte_start=sect_start, byte_end=p,
                    res_defined=res_defined, res_referenced=res_ref,
                    n_draws=ndraws, n_calls=ncalls))
                sect_start = p
                res_defined, res_ref = [], set()
                ndraws = ncalls = 0
            elif t == EOF:
                break
            else:
                raise ValueError(f"unknown op {t} at {rec_start}")

    # ── convenience ──
    @property
    def n_frames(self) -> int:
        return len(self.frames)

    @property
    def presents(self) -> list[int]:
        return [f.present for f in self.frames]

    def header_bytes(self) -> bytes:
        """[MAGIC][VERSION][DEV_PARAMS] — the prefix every slice re-uses."""
        return self.data[: self.frames[0].byte_start] if self.frames else self.data

    def slice_window(self, a: int, b: int) -> bytes:
        """Re-emit kept frames [a, b) as a STANDALONE container's bytes.

        Pulls forward any resource a kept frame references but that was first
        DEFINED in an earlier (now-excluded) section — content-hash dedup stores
        each resource once, possibly before the slice start. The replayer creates
        every RES it sees and issues only the target section's calls, so prepending
        the missing defs (in id order, before the frames) keeps every id defined
        before use. Frame indices in the slice become 0-based again."""
        if not (0 <= a < b <= self.n_frames):
            raise ValueError(f"slice [{a},{b}) out of range (0..{self.n_frames})")
        out = bytearray(self.header_bytes())
        # which resource ids does the slice need, and which are already defined
        # INSIDE it (those carry their own RES record in the copied section bytes)?
        need: set[int] = set()
        defined_inside: set[int] = set()
        for f in self.frames[a:b]:
            need |= f.res_referenced
            defined_inside.update(f.res_defined)
        # pull forward defs that the slice needs but doesn't carry, in id order so
        # a later record never cites an id the replayer hasn't created yet
        for rid in sorted(need - defined_inside):
            entry = self.resources.get(rid)
            if entry:
                _t, s, e = entry
                out += self.data[s:e]
        # the section bytes verbatim (preamble + calls + Present per frame)
        for f in self.frames[a:b]:
            out += self.data[f.byte_start:f.byte_end]
        out += struct.pack("<I", EOF)
        return bytes(out)

    def tex_info(self, rid: int) -> dict | None:
        """(w, h, fmt, datalen, levels, is_rt) of texture `rid` from its stored
        RES_TEX record, or None if `rid` isn't a texture. datalen==0 ⇒ a
        dynamically-created RENDER TARGET (no captured pixels) vs a file asset
        (datalen>0) — the distinction that tells a captured-screen draw from a
        loaded sprite (this is what nailed the pause-menu [0] backdrop as the
        captured-screen RT `DAT_073de648`, not a static board asset)."""
        entry = self.resources.get(rid)
        if not entry or entry[0] != RES_TEX:
            return None
        d = self.data
        p = entry[1] + 8                 # skip [type][id]
        levels = struct.unpack_from("<I", d, p)[0]; p += 4
        w, h, fmt = struct.unpack_from("<III", d, p); p += 12
        p += 4                           # rowbytes
        dl = struct.unpack_from("<I", d, p)[0]
        return {"w": w, "h": h, "fmt": fmt, "datalen": dl, "levels": levels,
                "is_rt": dl == 0}


def summary(path: str | Path) -> dict:
    """One-line-friendly dict: frame count, present range, dev dims, draws."""
    c = Container.load(path)
    return {
        "file": str(path),
        "version": c.version,
        "n_frames": c.n_frames,
        "present_first": c.frames[0].present if c.frames else None,
        "present_last": c.frames[-1].present if c.frames else None,
        "dims": f"{c.dev.get('w')}x{c.dev.get('h')}",
        "n_resources": len(c.resources),
        "draws_total": sum(f.n_draws for f in c.frames),
    }


if __name__ == "__main__":
    import json
    import sys
    if len(sys.argv) < 2:
        raise SystemExit("usage: orv3.py <cap.bin>  — prints a structured summary")
    print(json.dumps(summary(sys.argv[1]), indent=1))
