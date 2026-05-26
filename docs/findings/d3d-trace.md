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
{"op":"SetTexture",            "args":{"stage":0,"texture":"0xNN"},                                                            "ret_va":N,"frame":N}
{"op":"SetStreamSource",       "args":{"stream":0,"vb":"0xNN","stride":32},                                                    "ret_va":N,"frame":N}
{"op":"SetIndices",            "args":{"ib":"0xNN","base_vertex":0},                                                           "ret_va":N,"frame":N}
{"op":"SetVertexShader",       "args":{"handle":322},                                                                          "ret_va":N,"frame":N}
{"op":"DrawPrimitive",         "args":{"prim_type":4,"start_vertex":0,"prim_count":2},                                         "ret_va":N,"frame":N}
{"op":"DrawIndexedPrimitive",  "args":{"prim_type":4,"min_idx":0,"num_vertices":N,"start_idx":0,"prim_count":N},               "ret_va":N,"frame":N}
{"op":"DrawPrimitiveUP",       "args":{"prim_type":4,"prim_count":2,"vb":"0xNN","vb_stride":32},                               "ret_va":N,"frame":N}
{"op":"DrawIndexedPrimitiveUP","args":{"prim_type":4,"min_vtx_idx":0,"num_vtx_indices":N,"prim_count":N,"ib":"0xNN","ib_fmt":N,"vb":"0xNN","vb_stride":N},"ret_va":N,"frame":N}
```

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

## Cross-references

  - `tools/frida/openrecet-agent.js` — `installD3dTraceHooks` and
    related state.
  - `tools/frida_capture.py` — driver `CaptureConfig.d3d_trace` +
    `--d3d-trace` / `--d3d-trace-frames` CLI flags.
  - `docs/harness-roadmap.md` Phase D.4 — original spec.
  - Future: `src/d3d_trace.{c,h}` (Phase D.5) — port-side emitter
    using the same schema.  `tools/render_diff.py` (Phase D.6) —
    lock-step diff orchestrator.
