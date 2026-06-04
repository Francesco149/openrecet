# D3D render-diff orchestrator (D.6)

Phase D.6 of `docs/harness-roadmap.md`.  Consumes the D3D state-trace
JSONLs produced by D.4 (Frida side, retail) and D.5 (port side,
openrecet) and surfaces per-frame call-sequence divergences as
human-readable diff blocks with a context window.

The point of this tool is to replace "read more decompile" with "look
at the first state-trace diff" when the visible output of a render
chip differs between retail and port.  Direct motivation: the
Cf.minimal landing (commit `7dbe0b0`) ships visible HOUSE shop_table
furniture pixels but with three diagnosed-but-unresolved bugs
(translucent rendering, mesh-on-its-side orientation, 2-3× scale).
Asm archaeology alone cannot resolve them.  A state-trace diff at the
walker-draw call sites shows the divergent state directly.

## Quick start

```fish
nix develop
# capture retail boot-idle 0,1,2 (one-time per smoke baseline)
python3 tools/frida_capture.py \
    --run-dir runs/retail-boot-idle-d3d \
    --max-frames 5 --hide-window --turbo --silent-audio \
    --d3d-trace --d3d-trace-frames 0,1,2

# capture port boot-idle 0,1,2 (one-time)
tools/run-openrecet.sh \
    --max-frames 5 --turbo --silent-audio --hidden --rng-seed 1 \
    --input-trace-replay $(wslpath -w "$PWD/tests/scenarios/boot-idle/trace.jsonl") \
    --d3d-trace        $(wslpath -w "$PWD/runs/port-boot-idle-d3d/d3d_trace.jsonl") \
    --d3d-trace-frames 0,1,2

# diff (with the address-noise canceller turned on — see "Pointer
# canonicalisation" below for when to use it)
python3 tools/render_diff.py \
    --retail runs/retail-boot-idle-d3d/d3d_trace.jsonl \
    --port   runs/port-boot-idle-d3d/d3d_trace.jsonl \
    --opaque-pointers
```

## `--explain`: vertex-level divergence (the render-parity engine)

The structural diff above sees only the *command* stream — for an
immediate-mode draw it compares `prim_type`/`prim_count`/`stride`, not the
vertices themselves.  `--explain` closes that gap: capture each side with
vertex bytes on, and the tool FVF-decodes every aligned draw's vertices and
names the **first divergent (vertex, field)** — so a depth bug reads as
`vertex 2 POSITION.z: retail=-7.2 port=-6.5` instead of an opaque hex
mismatch.

```fish
# capture BOTH sides with --d3d-trace-verts (synced via a segtrace window):
python3 tools/export_trace.py tests/scenarios/<scn>/trace.jsonl \
    --caprange <start>,<count> --run-dir runs/<x>/port --d3d-trace --d3d-trace-verts
python3 tools/frida_capture.py --remote cutestation.soy:27042 \
    --run-dir runs/<x>/retail --input-segtrace runs/<x>/port/trace.work.jsonl \
    --d3d-trace --d3d-trace-verts --turbo --silent-audio --force-resolution 1024x768

# explain (implies --opaque-pointers so UP data pointers align):
python3 tools/render_diff.py --explain \
    --retail runs/<x>/retail/d3d_trace.jsonl --port runs/<x>/port/d3d_trace.jsonl
#   FRAME 707: 1 draw divergence(s)
#     [field] DrawPrimitiveUP @ret_va=0x415e61 (r#1,p#1)
#         vertex 2 POSITION.z: retail=-7.2 port=-6.5 (Δ+0.7, fvf=0x142, stride=24)
```

Semantics: vertex-content fields (`vb_bytes` etc.) are excluded from the
SequenceMatcher key, so a *pure-vertex* divergence (identical command,
different vertices) aligns as an "equal" block that `--explain` still inspects.
Float compares use `--vertex-eps` (abs+relative, default `1e-4`) — the engine
is not byte-identical, so an epsilon, not `==`, is correct; DIFFUSE/SPECULAR
colors compare exact.  A draw present on one side only is reported as
`[structural]`.  `--explain-all` shows every divergence per frame (default:
first only).  FVF comes from the in-effect `SetVertexShader` handle, falling
back to a stride table when it landed before the capture window.

Decode + each verdict path (field / count / structural / color / stride
fallback) are exercised by the synthetic-trace checks; the round-trip
(port + retail decode the same screen corner) is validated on the title.

## What the diff prints

Per frame, the tool runs `difflib.SequenceMatcher` over `(op,
canonical_args)` tuples — i.e. two events compare equal iff their op
+ every arg value matches verbatim.  Each non-equal opcode block is
one "divergence".  For each block, the printer shows a context window
(±5 by default, configurable via `--context N`) from each side, with
`>` markers on the offending events:

```
==============================================================================
FRAME 0: retail=42 evts, port=88 evts, 12 diff block(s)

  [block 3/12] tag=replace  retail [10..11)  port [10..11)
  retail [7..14)  (pivot: [10..11))
     #7     SetTextureStageState     args={"stage":0,"type":1,"value":4}
     #8     SetTextureStageState     args={"stage":0,"type":17,"value":2}
     #9     SetTextureStageState     args={"stage":0,"type":16,"value":2}
   > #10    SetTexture               args={"stage":0,"texture":"0x176ab1d0"}
     #11    SetVertexShader          args={"handle":452}
     #12    DrawPrimitiveUP          args={"prim_type":4,"prim_count":2, …}
     #13    SetTexture               args={"stage":0,"texture":"0x176ab4f0"}
  port   [7..14)  (pivot: [10..11))
     …
   > #10    SetTexture               args={"stage":0,"texture":"0x6041db0"}
     …
```

Tags follow the `difflib` opcode set: `replace`, `delete`, `insert`.
A `replace` block of 1↔1 is the simplest case — one retail event
swapped for one port event at the same logical position.  A
`delete` (retail events, no port counterpart) means the port skipped
those calls; an `insert` means the port emitted extra calls retail
didn't.  Mixed-size replace blocks are also legal — the matcher
greedily merges adjacent divergences.

The summary line at the bottom reports overall pass/fail and the
first diverged frame.  Exit code is 0 iff every compared frame is
identical post-coalesce.

## Pipeline (in order)

1. **Load** each JSONL into per-frame event lists.
2. **Scope filter** (optional) — drop events whose `ret_va` falls
   outside `[LO, HI)`.  Per-side via `--retail-scope LO:HI` /
   `--port-scope LO:HI`, or `--scope LO:HI` for both (rare — the two
   binaries have different module-relative VAs).
3. **Opaque-pointers rewrite** (optional, `--opaque-pointers`) — see
   below.
4. **Coalesce redundant writes** (on by default; `--no-coalesce` to
   disable).
5. **Per-frame alignment** via `SequenceMatcher` and pretty-print.

### Coalesce-redundant-writes

Engine D3D drivers internally drop "set state X to value V when it is
already V" writes on the way to the GPU.  Our traces capture every
API-level call regardless, so a target that bulk-uploads state every
frame shows hundreds of writes that are no-ops at the driver level.
We collapse both sides identically: keep the FIRST write of any
`(op, key)`-tuple at each value; drop subsequent writes that don't
change the value.  The cache resets on every draw call (any state set
after a draw is "live" again, because a draw consumes the current
state).

Key fields per op (everything else is the "value"):

| op                       | key                |
|--------------------------|--------------------|
| SetRenderState           | `state`            |
| SetTextureStageState     | `stage`, `type`    |
| SetTransform             | `state`            |
| SetTexture               | `stage`            |
| SetStreamSource          | `stream`           |
| SetIndices               | (singleton)        |
| SetVertexShader          | (singleton)        |
| SetMaterial              | (singleton)        |
| Draw* (any prefix)       | NEVER coalesced    |

### Pointer canonicalisation (`--opaque-pointers`)

D3D texture / VB / IB handles are heap-allocated D3D8 COM objects —
their pointer values are non-deterministic across processes.  A raw
diff between retail and port shows EVERY `SetTexture` / `SetStreamSource`
/ `SetIndices` / `DrawPrimitiveUP` as a "divergence" purely on
address.  This is noise, not signal.

`--opaque-pointers` rewrites each `"0xNN"`-shaped arg value to a
synthetic `"#0"`, `"#1"`, … id, allocated per (op, arg-field) in
first-seen order, per side.  Two events compare equal after the
rewrite iff their pointer args land at the same logical position in
each side's allocation sequence — true whenever both sides load the
same set of textures/VBs/IBs in the same order (the typical walker-
draw case, where both sides walk the same mesh-cache).

When NOT to use it: if you suspect the port is using the WRONG
texture for a draw (e.g. the wrong cache slot binds at the wrong
time), the synthetic-id rewrite hides exactly that bug class.  Run
without `--opaque-pointers` first to spot mis-binding; turn it on
once you've confirmed the binding sequence is correct on both sides.

## Mapping a divergence to source

Each event carries a `ret_va`: the module-relative offset of the call
site.  Add `0x00400000` to get a Ghidra VA inside the corresponding
binary.

| ret_va seen      | binary  | Ghidra VA              | use to find             |
|------------------|---------|------------------------|-------------------------|
| retail-side row  | retail  | `ret_va + 0x00400000`  | engine `FUN_xxxxxxxx`   |
| port-side row    | port    | `ret_va + 0x00400000`  | port symbol via `nm`    |

Port-side lookup:

```fish
nix develop --command i686-w64-mingw32-nm -n openrecet.exe \
  | awk -v t=$((0x400000 + RETVA)) '$1 && strtonum("0x"$1) > t {exit} {prev=$0} END{print prev}'
```

(Or just `objdump -d openrecet.exe | grep -B5 NNNN` near the
ret_va.)  Engine-side lookup is via Ghidra's "Go to address" at
`ret_va + 0x00400000`.

## Scope filters

`--scope LO:HI` / `--retail-scope LO:HI` / `--port-scope LO:HI`.
Hex (`0x…`) and decimal both accepted.  Each filter is applied to
its side ONLY, allowing per-side narrowing when the engine function
and the port function live at different module-relative VAs (the
general case).

Intended use for Cf.minimal diagnosis:

  - `--retail-scope` set to engine `FUN_00457714` module-relative
    range `[0x57714, 0x58567)` — the engine HOUSE-furniture walker.
  - `--port-scope` set to the module-relative range covering
    `scene1_walker_pass_render_house` (look up via `nm` per the
    table above).

That narrows the diff to events emitted from inside the walker,
which is where the alpha / orientation / scale bugs live.  Events
emitted from sibling render paths are filtered out.

## Smoke results (D.6 landing)

| pair                                              | blocks before  | blocks after `--opaque-pointers` |
|---------------------------------------------------|----------------|----------------------------------|
| retail boot-idle 0,1,2 vs port boot-idle 0,1,2    | 12 / frame 0   | 8 / frame 0                      |

Pointer canonicalisation collapses 4 pure address-noise blocks per
frame.  The remaining 8 blocks are REAL divergences in the title-BG
scroll render path:

  - **`DrawPrimitiveUP prim_count`**: retail batches 4 or 6 quads
    per call; port draws every quad as a separate `DrawPrimitiveUP`
    with `prim_count=2`.  Port-side render-path inefficiency, not
    a behavioural bug per se (output is pixel-identical because the
    quads cover disjoint regions); a follow-up port chip can lift
    the batching.
  - **Extra port `SetTextureStageState` cycle** (block 5): port
    re-uploads stage state {24, 11, 13, 14} on each tile that retail
    omits.  Either retail leaves them as previous-frame state or
    port's state-init is uploading TSS values D3D8's default-state
    table already covers.
  - **29 extra port events at frame tail** (block 8): retail
    finishes title BG with 4 batched quads; port continues drawing
    8 more individual textures.  Likely the per-tile draw loop
    over-iterates by a factor matching the batching gap.

None of these are Cf.minimal bugs — they're a free byproduct of the
boot-idle smoke.  Cf.minimal-specific diagnosis lands when the next
session captures a HOUSE-scene frame on both sides + scopes to the
walker call range.

## Self-tests

`tools/test_render_diff.py` covers the diff/coalesce/scope/opaque
logic against synthetic fixtures.  Run before any change to the
orchestrator:

```fish
nix develop --command python3 tools/test_render_diff.py
```

15 tests at landing.  Add a new case alongside any new flag or
canonicalisation rule.

## Cross-references

  - `tools/render_diff.py` — the tool.
  - `tools/test_render_diff.py` — self-tests.
  - `docs/findings/d3d-trace.md` — emitter schema + run-dir layout.
  - `docs/harness-roadmap.md` Phase D.6 — original spec.
  - `docs/findings/scene1-walker-pass-init.md` — the Cf.minimal
    chip the diagnosis workflow is built for.
