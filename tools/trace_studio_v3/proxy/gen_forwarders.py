#!/usr/bin/env python3
"""Generate complete C forwarder thunks + vtable initializers for a D3D8 COM
interface, by parsing the mingw d3d8.h DECLARE_INTERFACE_ declarations.

A proxy vtable must fill EVERY slot (the app calls by vtable index). Most slots
are pure forwarders: swap `This` (wrapper) -> `This->real` and tail-call. We
emit a real C thunk per method so signatures are correct under stdcall. Methods
we intercept ("custom") are NOT emitted here; the vtable points at the hand-
written `my_<Iface>_<Name>` instead.

We don't depend on the header's parameter NAMES: each param is reduced to its
TYPE (everything before the trailing identifier) + a synthetic name a0,a1,...
so forwarding is robust regardless of how args are spelled.

Output: a single .h #included by d3d8_proxy.c.
"""
import re, sys, json

def parse_iface(text, iface):
    m = re.search(r'DECLARE_INTERFACE_IID_\(\s*' + re.escape(iface) + r'\s*,.*?\{(.*?)\n\s*\};',
                  text, re.S)
    if not m:
        raise SystemExit(f"interface {iface} not found")
    region = re.sub(r'\s*\n\s*', ' ', m.group(1))      # join continuation lines
    # Match every STDMETHOD / STDMETHOD_ in order via finditer, so surrounding
    # BEGIN_INTERFACE / END_INTERFACE / PURE tokens are simply ignored. d3d8
    # method args contain no nested parens, so [^()]* captures the arg list.
    pat = re.compile(
        r'STDMETHOD_\(\s*([^,]+?)\s*,\s*(\w+)\s*\)\s*\(([^()]*)\)'   # ret,name
        r'|STDMETHOD\(\s*(\w+)\s*\)\s*\(([^()]*)\)')                 # name (ret=HRESULT)
    methods = []
    for mm in pat.finditer(region):
        if mm.group(2) is not None:
            ret, name, args = mm.group(1).strip(), mm.group(2), mm.group(3)
        else:
            ret, name, args = 'HRESULT', mm.group(4), mm.group(5)
        args = re.sub(r'^\s*THIS_?\s*', '', args.strip())   # drop THIS / THIS_
        types = []
        if args:
            for p in split_top(args):
                p = p.strip()
                if p == 'void' or not p:
                    continue
                # type = everything except the trailing identifier
                t = re.sub(r'(\w+)\s*$', '', p).strip()
                if not t:                                   # bare type, no name (e.g. "REFIID")
                    t = p
                types.append(t)
        methods.append((ret, name, types))
    return methods

def split_top(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '([<': depth += 1
        elif c in ')]>': depth -= 1
        if c == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += c
    if cur.strip(): out.append(cur)
    return out

def emit_census_preamble(fwd_list, out):
    """GX-00 dynamic census: a per-FORWARDED-method call counter (roadmap §9). Each
    fwd_ thunk InterlockedIncrements its slot on EVERY call — process lifetime, NOT
    capture-gated — because a `render_affecting_unsupported` method that fires even
    once ANYWHERE up to the compared window must be caught: device state persists, so
    an uncaptured forwarded call would silently desync the replay. d3d8_proxy.c writes
    these to v3cap.census.json at each kept frame; tools/parity/d3d_census.py
    cross-references them against the risk set (0 observed ⇒ safe to forward for this
    title/scenario, >0 ⇒ GX-01 record-or-fail). `fwd_list` is [(iface, name), …] in
    global vtable order, so the enum index is stable across regenerations."""
    out.append("/* ── GX-00 dynamic census: per-forwarded-method call counters ──")
    out.append(" * Every fwd_ thunk below InterlockedIncrements its FWD_ slot on each")
    out.append(" * call (process lifetime, unconditional). d3d8_proxy.c emits g_fwd_calls")
    out.append(" * to v3cap.census.json; a render_affecting_unsupported method with a")
    out.append(" * non-zero count means the capture is INCOMPLETE for that scene. */")
    out.append("enum {")
    for iface, name in fwd_list:
        out.append(f"    FWD_{iface}_{name},")
    out.append("    FWD__COUNT")
    out.append("};")
    out.append("static volatile LONG g_fwd_calls[FWD__COUNT];")
    out.append("static const char *const g_fwd_names[FWD__COUNT] = {")
    for iface, name in fwd_list:
        out.append(f'    "{iface}.{name}",')
    out.append("};")
    out.append("")


def emit_thunks(iface, methods, wrap, custom, out):
    out.append(f"/* ---- {iface} ({len(methods)} methods) ---- */")
    for ret, name, types in methods:
        if name in custom:
            continue
        params = ", ".join(f"{t} a{i}" for i, t in enumerate(types))
        callargs = ", ".join(f"a{i}" for i in range(len(types)))
        # First param uses the REAL interface type so the thunk's function type
        # matches the vtable member EXACTLY -> no cast needed in the initializer.
        sig = f"{iface} *This" + (", " + params if params else "")
        body_call = f"w->real->lpVtbl->{name}(w->real" + (", " + callargs if callargs else "") + ")"
        ret_kw = "" if ret.strip() == "void" else "return "
        out.append(
            f"static {ret} STDMETHODCALLTYPE fwd_{iface}_{name}({sig}) {{\n"
            f"    {wrap} *w = ({wrap}*)This;\n"
            f"    InterlockedIncrement(&g_fwd_calls[FWD_{iface}_{name}]);\n"
            f"    {ret_kw}{body_call};\n"
            f"}}")
    # vtable initializer (in declaration = vtable order)
    out.append(f"static const {iface}Vtbl g_{iface}_vt = {{")
    for ret, name, types in methods:
        fn = f"my_{iface}_{name}" if name in custom else f"fwd_{iface}_{name}"
        out.append(f"    {fn},")
    out.append("};")
    out.append("")

CONFIG = {
    "IDirect3D8":       ("WrapD3D", {"QueryInterface", "AddRef", "Release", "CreateDevice"}),
    "IDirect3DDevice8": ("WrapDev", json.loads(sys.argv[2]) if len(sys.argv) > 2 else
                          ["QueryInterface", "AddRef", "Release"]),
}

def find_header():
    """Locate the toolchain's d3d8.h by asking the cross-compiler."""
    import subprocess
    r = subprocess.run(["i686-w64-mingw32-gcc", "-xc", "-E", "-M", "-"],
                       input="#include <d3d8.h>\n", capture_output=True, text=True)
    for tok in r.stdout.replace("\\\n", " ").split():
        if tok.endswith("d3d8.h"):
            return tok
    raise SystemExit("could not locate d3d8.h via i686-w64-mingw32-gcc")

if __name__ == "__main__":
    header = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] not in ("", "auto") else find_header()
    text = open(header).read()
    out = ["/* GENERATED by gen_forwarders.py — do not edit. */"]
    # Parse every interface first so the dynamic-census enum can index ALL forwarded
    # methods (both interfaces) in one stable, global vtable order before any thunk
    # references its FWD_ slot.
    parsed, fwd_list = {}, []
    for iface, (wrap, custom) in CONFIG.items():
        custom = set(custom)
        methods = parse_iface(text, iface)
        parsed[iface] = (methods, wrap, custom)
        fwd_list += [(iface, name) for _, name, _ in methods if name not in custom]
    emit_census_preamble(fwd_list, out)
    for iface, (methods, wrap, custom) in parsed.items():
        emit_thunks(iface, methods, wrap, custom, out)
    print("\n".join(out))
