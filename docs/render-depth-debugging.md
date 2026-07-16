# Render depth and phase debugging with Trace Studio v3

> **Status:** current operational playbook  \
> **Last verified:** 2026-07-16  \
> **Historical v2 procedure:** `archive/render-depth-debugging-v2-2026-06.md`

Use this when a billboard, character, particle, shadow, mesh, or render-target effect has
the wrong overlap/layering. The recurring Recettear-specific trap is that identical
world coordinates and identical Z render states can still produce different depth:
passes deliberately swap projection matrices and therefore map the same view-space Z to
different normalized-device depth.

## 1. Establish a stable logical frame

Capture a small state-enabled v3 window:

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_window.py \
  <scenario> --anchor <ANCHOR> --window <OFFSET>:<COUNT> --state --view
```

Requirements before diagnosing rendering:

- input edge and anchor occurrence correspond;
- state/RNG/phase at the chosen pair is either equal or the exact difference is known;
- `pairs.json` gives the port and retail kept-frame indices;
- both sides pass same-side replay verification;
- an RT-using frame is viewed through history replay, not isolated per-frame state.

A complete identity join is correspondence only. Do not infer equal pixels/draws/state.

## 2. Localize the painting draw

In the native viewer:

1. Select the first identity-paired divergent frame.
2. Use pixel-pick on the wrong region to identify the draw.
3. Toggle/solo the draw and inspect the draw-program/material panel.
4. Record port/retail kept-frame indices, draw indices, texture, geometry hash, blend,
   Z state, alpha test, and transform context.

Headless draw-list comparison:

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_draws.py \
  <port-v3cap.bin> <port-frame-index> \
  <retail-v3cap.bin> <retail-frame-index> \
  --material --json
```

Use `--list` for the whole ordered program and `--verts <DRAW>` to inspect captured
geometry for one draw. A material-aligned result does not prove matrices or pixels.

## 3. Compare actual matrices

`orv3_xform.py` decodes the exact matrices passed to D3D8:

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_xform.py \
  <port-v3cap.bin> --frame <port-index> \
  --diff <retail-v3cap.bin> --frame-b <retail-index>
```

It reports:

- VIEW eye/forward/up;
- PROJECTION field-of-view, aspect, near, and far;
- WORLD matrices and counts;
- raw element deltas.

If a draw looks wrong while camera globals appear equal, trust the captured D3D matrix
first. Identify the final `SetTransform` before that draw, then trace its engine owner.

For D3DX right-handed perspective matrices used here:

```text
p10 = -z_far / (z_far - z_near)
p14 = z_near * z_far / (z_near - z_far)
```

Compute clip/NDC depth using the captured WORLD·VIEW·PROJECTION and vertex. Do not compare
world Z directly. Larger `z_far` can move a billboard enough to invert a near-tied
`LESSEQUAL` test.

## 4. Render frame and draw prefixes

```sh
# Full kept frame.
nix develop --command python3 tools/trace_studio_v3/orv3_shot.py \
  <scenario>:retail --frame <index> --out runs/_shot/retail.png

# Clear, then progressive draw prefixes.
nix develop --command python3 tools/trace_studio_v3/orv3_shot.py \
  <scenario>:retail --frame <index> --draws --out runs/_shot/draws.png
```

Draw-prefix isolation answers which draw paints a region. For render-target samples,
isolated per-frame/prefix replay may lack content produced in an earlier frame; use the
history-enabled native viewer/full-frame path and inspect the RT program.

## 5. Inspect render targets

```sh
nix develop --command python3 tools/trace_studio_v3/orv3_rt.py \
  <v3cap.bin> <frame-index> --full

nix develop --command python3 tools/trace_studio_v3/orv3_rt.py \
  <v3cap.bin> --scan
```

The dump shows `SetRenderTarget`, `CopyRects`, clears, draws per target, and draws sampling
an RT. Trace Studio v3 records/replays these operations; history replay is required when
the target was populated in an earlier kept frame. If the RT method census reports an
unsupported call, treat capture as incomplete and stop.

## 6. Attribute the depth divergence

Check in this order:

1. Different logical state/actor position/animation.
2. Different draw order or missing/extra draw.
3. Different vertex/geometry content.
4. Different WORLD matrix.
5. Different VIEW matrix.
6. Different PROJECTION matrix (`z_near`, `z_far`, aspect, FOV).
7. Different ZENABLE/ZWRITE/ZFUNC.
8. Different alpha test or blend causing apparent—not geometric—occlusion.
9. Different render-target history or inherited device state.
10. Capture incompleteness/observer effect.

Map the state-setting call to its engine owner with decompile/disassembly. Port the owner
and restoration behavior, not an ad-hoc state toggle around the visually wrong draw.

## 7. Phase and RNG are separate pillars

Animation phase can change primitive count, sprite cell, hover position, particles, and
draw count. Prove it from state:

```sh
nix develop --command python3 tools/flow_diff.py --verdict \
  --align-field db054 --retail <retail-call_trace.jsonl> \
  --port <port-call_trace.jsonl>
```

- `ALIGNED`: captured values/evolution match under the selected alignment.
- `CONST-OFFSET`: candidate origin difference; verify every consumer and no drift.
- `DRIFT`: real evolution/call-order difference.

Equal RNG call count is insufficient; compare raw values and consumer order. Do not
cross-correlate screenshots and call the result phase unless state proves a constant
offset. On an actively ported trace, close or explicitly normalize every residual.

## 8. Resolved example: foot dust and Tear glow

Do not repeat the old 3D-mesh/ZWRITE hunt:

- records-A dust/effects retail projection uses `z_far=500`; the old port used 2000;
- character body retail projection uses the mapped camera/stage input and produced
  `z_far=1450`; an old uninitialized port input produced 3025;
- correcting the projection owners restored intended near-tie depth behavior;
- the later foot-dust RNG-order repair made the visible dust sequence 1:1.

Ground truth:

- `findings/scene1-walk-dust.md`;
- `findings/scene1-tear-visual-diffs.md`;
- `findings/engine-quirks.md` §93.

## 9. Completion gate

A render-depth fix is done only when:

- chosen state/input/RNG identities match;
- draw order, geometry, material, and transforms meet the scenario contract;
- same-host pixels pass at the affected frames;
- nearby glow, shadows, particles, meshes, UI, and RT consumers have no regression;
- the owner restores/inherits device state like retail;
- evidence is reproducible on a second drive;
- the finding/quirk and proof scope are persisted.
