# D3D state-trace pipeline (Frida side)

Phase D.4 of `docs/harness-roadmap.md`.  Hooks the
`IDirect3DDevice8` vtable in retail via Frida and emits one JSONL
event per state-change or draw call, batched per frame, into
`<run_dir>/d3d_trace.jsonl`.

Pairs with `src/d3d_trace.c` (port side — Phase D.5) and
`tools/render_diff.py` (orchestrator — Phase D.6) to diagnose
render-path divergence between retail and openrecet without further
asm archaeology.  Direct motivation: the Cf.minimal landing (commit
`7dbe0b0`) ships visible HOUSE shop_table furniture pixels but with
three diagnosed-but-unresolved bugs (translucent rendering,
mesh-on-its-side orientation, 2-3x scale).  A state-trace diff at
the walker draw shows the divergent state directly instead of
requiring more decompile reading.

## Quick start

```fish
nix develop
# capture title-z-press frame 90 only (post-fade, INGAME placeholder)
python3 tools/frida_capture.py \
    --run-dir runs/d3d-trace-smoke \
    --max-frames 95 --hide-window --turbo --silent-audio \
    --input-trace tests/scenarios/title-z-press/trace.jsonl \
    --d3d-trace --d3d-trace-frames 0,90
head runs/d3d-trace-smoke/d3d_trace.jsonl
```

The trace lands as one JSONL row per state-change/draw call,
ordered by emission.

## What gets hooked

Twelve `IDirect3DDevice8` vtable slots.  Order per d3d8.h
`IDirect3DDevice8Vtbl` (IUnknown at 0..2, `IDirect3DDevice8`
methods start at slot 3 = `TestCooperativeLevel`):

| slot | method                    | event op                  |
|------|---------------------------|---------------------------|
| 37   | SetTransform              | `SetTransform`            |
| 42   | SetMaterial               | `SetMaterial`             |
| 50   | SetRenderState            | `SetRenderState`          |
| 61   | SetTexture                | `SetTexture`              |
| 63   | SetTextureStageState      | `SetTextureStageState`    |
| 70   | DrawPrimitive             | `DrawPrimitive`           |
| 71   | DrawIndexedPrimitive      | `DrawIndexedPrimitive`    |
| 72   | DrawPrimitiveUP           | `DrawPrimitiveUP`         |
| 73   | DrawIndexedPrimitiveUP    | `DrawIndexedPrimitiveUP`  |
| 76   | SetVertexShader           | `SetVertexShader`         |
| 83   | SetStreamSource           | `SetStreamSource`         |
| 85   | SetIndices                | `SetIndices`              |

Not hooked (yet):  `BeginScene` / `EndScene` / `Clear` (low value
for diffing — these mark frame boundaries that the JSONL already
groups via the per-frame batch); `SetLight` / `LightEnable` / `SetViewport`
(engine sets them once during init and rarely after); `GetTexture`
/ `GetRenderState` / state-block methods (not part of the divergence
class we care about for Cf.minimal).  Add hooks here if a real
diff needs them — every additional slot is six lines in
`installD3dTraceHooks`.

## Event schema

One JSONL row per call.  All numeric args are decimal integers
(D3D8 RENDERSTATETYPE / TRANSFORMSTATETYPE / D3DPRIMITIVETYPE
codes are tractable to look up directly).  Pointer-shaped args
(VB / IB / texture) are emitted as `"0xNN"` hex strings to keep
them distinct from value-shaped args at JSON-parse time.  Matrix
+ material structs are inlined as flat float lists so the diff
doesn't break on pointer identity.

```json
{"op":"SetRenderState",        "args":{"state":27,"value":1},                                                                  "ret_va":N,"frame":N}
{"op":"SetTextureStageState",  "args":{"stage":0,"type":4,"value":4},                                                          "ret_va":N,"frame":N}
{"op":"SetTransform",          "args":{"state":256,"matrix":[16 floats, row-major]},                                           "ret_va":N,"frame":N}
{"op":"SetMaterial",           "args":{"material":[17 floats — 4×D3DCOLORVALUE Diffuse/Ambient/Specular/Emissive + Power]},    "ret_va":N,"frame":N}
{"op":"SetTexture",            "args":{"stage":0,"texture":"0xNN"[,"tex_name":"bmp/foo.tga"]},                                  "ret_va":N,"frame":N}
{"op":"SetStreamSource",       "args":{"stream":0,"vb":"0xNN","stride":32},                                                    "ret_va":N,"frame":N}
{"op":"SetIndices",            "args":{"ib":"0xNN","base_vertex":0},                                                           "ret_va":N,"frame":N}
{"op":"SetVertexShader",       "args":{"handle":322},                                                                          "ret_va":N,"frame":N}
{"op":"DrawPrimitive",         "args":{"prim_type":4,"start_vertex":0,"prim_count":2},                                         "ret_va":N,"frame":N}
{"op":"DrawIndexedPrimitive",  "args":{"prim_type":4,"min_idx":0,"num_vertices":N,"start_idx":0,"prim_count":N},               "ret_va":N,"frame":N}
{"op":"DrawPrimitiveUP",       "args":{"prim_type":4,"prim_count":2,"vb":"0xNN","vb_stride":32},                               "ret_va":N,"frame":N}
{"op":"DrawIndexedPrimitiveUP","args":{"prim_type":4,"min_vtx_idx":0,"num_vtx_indices":N,"prim_count":N,"ib":"0xNN","ib_fmt":N,"vb":"0xNN","vb_stride":N},"ret_va":N,"frame":N}
```

### Vertex-byte capture (`--d3d-trace-verts` / `d3d_trace_verts`)

Off by default.  When enabled, every **immediate-mode** draw (`DrawPrimitiveUP`,
`DrawIndexedPrimitiveUP`) additionally carries its raw vertex bytes so
`tools/render_diff.py --explain` can FVF-decode and name the first divergent
vertex field.  Extra `args` fields:

```jsonc
// DrawPrimitiveUP
"vb_nverts": 6,            // vertex count = vcount(prim_type, prim_count)
"vb_bytes":  "0000804400…" // lowercase hex, vb_nverts × vb_stride bytes
"vb_over":   98304         // present INSTEAD of vb_bytes if over the 64 KiB cap
// DrawIndexedPrimitiveUP also adds:
"ib_nidx":   N,            // index count = vcount(prim_type, prim_count)
"ib_bytes":  "…",          // ib_nidx × (ib_fmt==101 ? 2 : 4) bytes, hex
"vb_nverts": min_vtx_idx + num_vtx_indices   // dumped from vertex 0 so abs indices resolve
```

The FVF in effect is the most-recent `SetVertexShader` handle (fixed-function
FVF codes; the engine uses `0x142` = XYZ|DIFFUSE|TEX1 stride-24 for 3D billboards
and `0x1c4` = XYZRHW|DIFFUSE|SPECULAR|TEX1 stride-32 for 2D/HUD quads).  Both
sides (`src/d3d_trace.c` `d3d_prim_vcount` + the Frida agent's `primVcount`) emit
byte-for-byte identical hex, validated on the title screen (port + retail decode
the same `(1024,768,0,1)` screen corner).  Only the UP paths are captured — VB-
backed `DrawPrimitive`/`DrawIndexedPrimitive` (3D mesh geometry via a locked VB)
are deferred.

### Stable texture identity (`tex_name`)

`SetTexture`'s `texture` arg is the raw COM pointer — allocation-dependent,
so it never matches across port↔retail (or between runs).  When the bound
texture's source asset name is known, an extra **`tex_name`** field carries
it (e.g. `"bmp/title_fuki.tga"`), giving `tools/render_diff.py` a
target-independent, human-readable identity to key on (it drops the raw
pointer from the alignment key whenever `tex_name` is present — see
`render-diff.md`).  Both sides resolve the name from the engine's texture
**loaders**, so the strings are identical (forward-slash paths):

- **Port** (`src/d3d_tex_names.c`): a `texture* → name` registry populated
  at the load chokepoint `sprite.c:sprite_load_impl` (covers 2D UI, chr
  sheets, dialogue, and mesh textures via `sprite_load_mipped`) and cleared
  in `sprite_destroy`.  `d3d_trace_SetTexture` looks the bound pointer up.
- **Retail** (`tools/frida/openrecet-agent.js` `installTexNameHooks`): hooks
  the two loaders — `FUN_0047193c` (UI, ppTexture = arg1 slot, path = arg2)
  and `FUN_00471b24` (mesh, ppTexture = arg0 slot, path = arg1).  Both write
  the created `IDirect3DTexture8*` to the first dword of their output slot
  (the `FUN_004cd30e` / `D3DXCreateTextureFromFileInMemoryEx` ppTexture arg);
  the hook reads `*slot` onLeave and maps `ptr → name` into `g_tex_names`.

A bind with no known name (null unbind, or a texture not from these loaders
— e.g. `font_upload`'s `CreateTexture`d glyph atlas) simply omits `tex_name`;
render_diff then falls back to the opaque-pointer identity for that bind.

Validated cross-side on `boot-idle`: the four shared title textures
(`title01.tga` / `title_bg2.bmp` / `title_fuki.tga` / `title_waku.tga`)
emit identical names on both targets and align under render_diff despite
disjoint pointer values; retail additionally binds `nowloading.tga`, which
now surfaces by name as a real structural difference.

### `ret_va` field

Module-relative offset of the immediate caller's return address
(read via Frida's `this.returnAddress`, then `addr.sub(g_base)`).
Add `0x00400000` (the unpacked exe's preferred ImageBase) to map
to a Ghidra VA.  Example: `ret_va: 21398` → Ghidra VA `0x405396`
→ the engine function containing that VA is the call site.

Free lookup — no `Thread.backtrace()` walk — so the trace stays
fast even on render-heavy frames.  In practice this is one stack
read per traced call.  An entire 1000-call frame adds a few ms
overhead.

### `frame` field

Added by the driver on each row (the agent batches events by frame
and sends one `d3d_trace_batch` message per Present-cycle flush;
the driver unwraps the batch and stamps the frame onto each row
for line-by-line diffability).

## Filter set: `--d3d-trace-frames`

Comma-separated frame numbers.  Only those frames have their
events buffered + flushed.  Default empty = capture every frame
(massive on INGAME — 1000+ events/frame).  Use the filter for
any non-title scenario.

```fish
# only frames 90 and 100
python3 tools/frida_capture.py … --d3d-trace --d3d-trace-frames 90,100
```

Frame numbers are the manual counter (`g_manual_frame_counter`,
bumped at end of every `Present` onEnter), matching the Phase A
scenario `capture_frames:` numbering.

## Batching

Events buffer agent-side into `g_d3d_trace_buffer` (JS array,
module scope).  At the end of every Present onEnter (right before
the manual counter bumps), the agent emits one `send()` carrying
the entire batch:

```json
{"kind":"d3d_trace_batch","frame":N,"count":M,"events":[…]}
```

Per-call `send()` would saturate the Frida wire — one HOUSE-INGAME
frame can push hundreds of state changes.  One batched send per
frame caps the message rate at the Present rate (60 Hz under
default timing; faster under `--turbo`).

The driver unwraps each batch into N JSONL rows so line-by-line
diffing (`diff -u` or `tools/render_diff.py`) works directly.

## Smoke results (D.4 landing)

| scenario                        | frames captured | event counts                                                                                                                                                |
|---------------------------------|-----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|
| boot-idle 0,1,2                 | 39 each         | SetRenderState 36, SetTextureStageState 33, SetTexture 24, SetVertexShader 24 — steady-state title BG scroll, all state setup, no actual geometry           |
| title-z-press 0,90              | 46 / 43         | + DrawPrimitiveUP 11 (post-fade INGAME placeholder uses one immediate-mode quad batch per element)                                                          |

The smoke validates: hooks fire on every traced slot, JSON shape
parses cleanly, per-frame batching delivers exactly the right
frames, `ret_va` annotation produces sensible engine VAs, frame
filter excludes non-listed frames entirely (no buffer growth, no
flush, zero wire traffic).

Frame 90 of title-z-press shows the engine in INGAME placeholder
state (post-fade, before any HOUSE asset load).  To exercise
`SetTransform` / `SetMaterial` / `DrawIndexedPrimitive` we need a
scenario that drives the engine into HOUSE shop_table rendering —
not within D.4 scope (no save-inject in retail yet; tutorial
sequence required); will land naturally with the first D.6
diagnosis pass on Cf.minimal.

## Architecture

```
┌────────────────────────────────┐
│ tools/frida_capture.py         │
│  --d3d-trace                   │
│  --d3d-trace-frames 0,90       │      Per-frame batched send()
└──────────────┬─────────────────┘◀────────────────────────────────┐
               │ frida RPC                                          │
               ▼                                                    │
┌────────────────────────────────┐    ┌─────────────────────────────┐
│ openrecet-agent.js             │    │ vtable slots: 37,42,50,61,  │
│  init({d3d_trace,             }│    │ 63,70,71,72,73,76,83,85     │
│        d3d_trace_frames:[...] })├───▶│ Interceptor.attach          │
│  Present.onEnter → traceFlush() │    │   onEnter: buffer event     │
└──────────────┬─────────────────┘    └─────────────────────────────┘
               │ injected into
               ▼
┌────────────────────────────────┐
│ vendor/unpacked/recettear      │
│ (running, engine main thread   │
│  resumed — opposite of         │
│  diff_test mode)               │
└────────────────────────────────┘

┌────────────────────────────────┐
│ <run_dir>/d3d_trace.jsonl      │
│   {"op":"…","args":{},         │
│    "ret_va":N,"frame":N}       │
│   …                            │
└────────────────────────────────┘
```

## Adding a new vtable slot

Three steps inside `tools/frida/openrecet-agent.js`:

  1. Add the slot constant near the other `V_Dev_*` declarations
     (`const V_Dev_Foo = N;` — index per d3d8.h).
  2. Append an `Interceptor.attach(vtableSlot(devicePtr, V_Dev_Foo),
     { onEnter: function (args) { … } })` block inside
     `installD3dTraceHooks`.  Read args per the d3d8.h signature
     (args[0] = `this`, args[1+] = method params for x86 stdcall).
     Call `traceEmit({ op: 'Foo', args: { … }, ret_va: traceRetVa(
     this.returnAddress) })` from inside the `traceShouldEmit()`
     gate.
  3. Bump the trailing `log('d3d trace hooks installed (N vtable
     slots)')` count.

If the method's args include a pointer to a struct that needs
inlining (matrix, material, viewport, light), add a reader helper
next to `traceReadMatrix` / `traceReadMaterial` to flatten the
struct into a JSON-friendly list.

## Port side (D.5)

Symmetric emitter for our openrecet binary lives in
`src/d3d_trace.{c,h}` + `src/d3d_trace_macros.h`.  Drops the same
JSONL schema as the agent into `<run_dir>/d3d_trace.jsonl`.

### Quick start (port side)

```fish
nix develop
nix develop --command tools/run-openrecet.sh \
    --max-frames 5 --turbo --silent-audio --hidden --rng-seed 1 \
    --input-trace-replay $(wslpath -w "$PWD/tests/scenarios/boot-idle/trace.jsonl") \
    --d3d-trace        $(wslpath -w "$PWD/runs/d3d-trace-smoke/d3d_trace.jsonl") \
    --d3d-trace-frames 0,1,2 \
    --max-duration-ms 3000
head runs/d3d-trace-smoke/d3d_trace.jsonl
```

### Interception mechanism

**The obvious approach (vtable hot-patch) does not work on this
host.**  Reassigning `dev->lpVtbl` to a static-storage byte copy of
the engine's original vtable triggers a reliable
`ACCESS_VIOLATION` (`0xC0000005`) somewhere downstream — the
d3d8.dll implementation evidently caches or type-tags the original
vtable address.  Verified by copying the vtable into a static
struct and pointing `dev->lpVtbl` at the copy WITHOUT wrapping any
slots: still crashes.  Pointing back at the original keeps the run
alive.

The shipped approach is **call-site macro redirection**:

```
   user code                  d3d_trace_macros.h          d3d_trace.c
   ─────────                  ─────────────────────       ────────────
   IDirect3DDevice8_SetRenderState(dev, S, V)
                          ───▶ #define …→ d3d_trace_SetRenderState(dev, S, V)
                                                       ───▶ emit JSON row
                                                            then (dev)->lpVtbl->SetRenderState(dev,S,V)
```

`src/Makefile` adds `-include d3d_trace_macros.h` to `CFLAGS` so the
redirect header is processed at the top of every TU before any
other code, ahead of the `<d3d8.h>` include.  d3d8.h's COBJMACROS
variant emits the standard `(p)->lpVtbl->Foo(p, …)` macros;
`d3d_trace_macros.h` then `#undef`s the 12 we care about and
redefines them to call into the wrappers.

`d3d_trace.c` itself is the one TU that needs the RAW macros to
forward the call — it `#undef`s the redirected macros immediately
after including `d3d_trace_macros.h`, then restores the d3d8.h
COBJMACROS form verbatim.  After that, `IDirect3DDevice8_Foo(p, …)`
inside `d3d_trace.c` expands to the original lpVtbl call, which is
what each `d3d_trace_Foo` wrapper ends up tail-calling.

### Schema differences vs the Frida side

Field shapes match field-for-field (op, args, ret_va, frame).  Only
the float formatter differs: the Frida side hands JS Numbers to
`JSON.stringify` which picks the shortest decimal that round-trips
a double; the port side uses `%.9g` which is sufficient to round-
trip an IEEE-754 single.  Equivalent for any float32 ingested by a
JSON parser on the diff side.

`ret_va` is computed via `__builtin_return_address(0)` minus the
EXE's load base (`GetModuleHandleA(NULL)`).  The openrecet ImageBase
is 0x00400000 — add that to the `ret_va` to get a Ghidra VA inside
the port, the same way `ret_va + 0x00400000` maps to a retail VA on
the Frida side.

### Cost when not enabled

Each wrapper checks one static FILE pointer (`g_f == NULL`) and
forwards.  With `--d3d-trace` unset, `g_f` is NULL and emit is
skipped — the wrapper degenerates to: stash return address, one
branch, one function-pointer call.  Sub-nanosecond on modern x86.

### Smoke results (D.5 landing)

| scenario                        | frames captured | event count |
|---------------------------------|-----------------|-------------|
| boot-idle 0,1,2                 | 90 each (270 total) | SetVertexShader 24, DrawPrimitiveUP 23, SetTexture 23, SetTextureStageState 13, SetRenderState 7 (per frame) |

Counts diverge from the retail-side D.4 smoke (39/frame total)
because the openrecet title scene's render path differs structurally
from retail's.  That divergence IS the bug class Phase D.6 is
designed to surface.  When the diff orchestrator lands and runs both
traces on the same scenario, every per-call gap (extra SetTexture,
missing SetMaterial, swapped DrawPrimitive args) appears as a
diff-able event pair.

### Validation

Canaries bit-exact after the wrappers landed: boot-idle 3/3 +
title-down-press 4/4 + title-options 2/4 + title-z-press 14/14
(same as the C8jb.fin baseline; the 2 fails in title-options are
the pre-existing frames 39/60 regression).  Host suite 2701/2701
pass unchanged.

## Cross-references

  - `tools/frida/openrecet-agent.js` — `installD3dTraceHooks` and
    related state.
  - `tools/frida_capture.py` — driver `CaptureConfig.d3d_trace` +
    `--d3d-trace` / `--d3d-trace-frames` CLI flags.
  - `src/d3d_trace.{c,h}` + `src/d3d_trace_macros.h` — port side.
  - `docs/harness-roadmap.md` Phase D.4 + D.5 — original specs.
  - `tools/render_diff.py` + `docs/findings/render-diff.md` —
    Phase D.6 lock-step diff orchestrator (consumes the JSONLs this
    pipeline emits).
