# Debugging render DEPTH + PHASE divergences (the playbook)

How the Tear face-glow + foot-dust occlusion bugs were found (2026-06-04), turned
into a repeatable workflow. Both were the **same root class** — a per-pass
projection `z_far` mismatch — and neither was visible in a plain render-state diff.
Read this before iterating on a billboard-occlusion or "wrong sprite layer" bug.

## The one-paragraph lesson

When a translucent/additive billboard (wing-glow, dust, sparkle, overlay) is
**occluded when it should draw over** (or vice-versa), the cause is usually **not**
ZWRITE/ZFUNC/ALPHATEST or draw order — those typically already match retail and a
plain `d3d_state_diff.py diff` says "ok". The cause is the **projected depth**:
two billboards at the *same world XYZ + scale* land at different NDC-z purely from
which `z_far` was live at each draw (this engine layers passes by swapping the
PROJECTION — near meshes 350, char body 1450, chr-walker glow 2000, records-A
effects 500). **Compute and compare the per-draw NDC-z, not the Z-state.** A
frequent port trigger is a stubbed camera/stage global left at 0 that only feeds a
computed `z_far` (it shifts depth ordering while leaving screen X/Y untouched).
See engine-quirks §93, [[feedback_zfar_depth_footgun]].

## Step 1 — capture a SYNCED port↔retail d3d-trace

Same scenario, same anchor-relative `{caprange}` window → captures align by index
(Recette's walk is 1:1, so the same cap index = the same sim instant). Use a small
window for depth, a LONG one for phase.

```sh
# PORT (writes frames/ + d3d_trace.jsonl + the work-trace it drove):
python3 tools/export_trace.py tests/scenarios/<scn>/trace.jsonl \
    --caprange <start>,<count> --run-dir runs/<x>/port --d3d-trace

# RETAIL (reuse the SAME work-trace so the caprange window matches):
python3 tools/frida_capture.py --remote cutestation.soy:27042 \
    --run-dir runs/<x>/retail --input-segtrace runs/<x>/port/trace.work.jsonl \
    --d3d-trace --turbo --silent-audio --force-resolution 1024x768 --max-frames 4000
```

cap_NN ⇒ absolute frame = (first captured frame) + NN, on each side independently.

## Step 2 — find the bug: `d3d_state_diff.py depthdiff`

This matches retail↔port draws by **(world-pos, blend, ZWRITE, prim_count)** —
binary-independent, no VA alignment needed — and flags `z_far` mismatches and NDC-z
order inversions. It would have found BOTH bugs in one command:

```sh
python3 tools/d3d_state_diff.py depthdiff \
    runs/<x>/retail/d3d_trace.jsonl runs/<x>/port/d3d_trace.jsonl \
    --frame-a <retail_abs> --frame-b <port_abs>
#   pos=(-0.30,0.00,9.50) SRCALPHA/INVSRCALPHA pc16: ZFAR retail=1450 port=3025 ...
#   pos=(-0.33,0.32,9.61) SRCALPHA/INVSRCCOLOR pc2:  ZFAR retail=500  port=2000 ...
```

Drill into one frame with `depth` (sorted near→far, `--nm` resolves the port
ret_va → function, `--near-pos X,Y,Z[,R]` filters to an actor):

```sh
python3 tools/d3d_state_diff.py depth runs/<x>/port/d3d_trace.jsonl \
    --frame <abs> --near-pos 0.6,3.1,9.35,0.6 --nm build/openrecet.exe
```

## Step 3 — find WHO sets the wrong projection

The raw trace logs the `ret_va` of every `SetTransform(PROJECTION)`. Find the
setter live at the bad draw, map it to a function (port: `nm build/openrecet.exe`;
retail: the engine VA → the decompile under `docs/decompiled/by-address/`), then
objdump the retail setter to read the exact `z_far` constant/global it builds with:

```sh
objdump -d --start-address=0xVA --stop-address=0xVA+0x80 vendor/unpacked/recettear.unpacked.exe
# the D3DXMatrixPerspectiveFovRH args are fld/fstps'd to the stack before the call;
# z_far is the FIRST pushed float (cdecl right-to-left: out, fov, aspect, near, zf).
```

`z_far` ↔ `PROJ[10]` for this engine (RH, near=1.0): `z_far = p10/(1+p10)`,
`p10 = -z_far/(z_far-1)`. Larger z_far → NEARER ndcz.

## Phase divergences (anim flap / hover-bob / RNG spawn)

Different problem, same trace. The port runs Tear's companion anim through the
~1540-frame stubbed intro, so its flap/bob/RNG phase at free-roam is offset from
retail (the deferred "chase phase later"). The trace already fingerprints phase:
**prim_count = sprite anim cell count, world-Y = hover-bob, draw count = RNG-driven
spawn count.** Capture a LONG window (`--caprange <s>,120`) on both sides, then:

```sh
python3 tools/d3d_state_diff.py phase \
    runs/<x>/retail/d3d_trace.jsonl runs/<x>/port/d3d_trace.jsonl \
    --near-pos 0.6,3.1,9.35,0.6 --what pc
# prints each side's per-frame [pc / y / ndcz / count], then cross-correlates the
# chosen metric and reports "port is +N frames vs retail" — the phase offset.
```

`--what pc` aligns the wing-flap cycle, `--what y` the hover-bob, `--what count`
the RNG spawn cadence. Once you know the offset N, either (a) compare retail frame
i against port frame i−N for a phase-clean pixel diff, or (b) fix the offset at the
source (align the anim start to free-roam onset / gate the tick to the controllable
state — [[project_next_char_controller]], engine-quirks §71/§81). Per-frame
`px/py/anim/oct/rng` are also in each run's `meta.jsonl` (player-pos-log) if you
need the sim-side state rather than the draw-side fingerprint.

## Don't repeat these dead ends

- **Toggling the char/quad ZWRITE/ZFUNC/ALPHATEST.** It was a no-op for the glow
  and the dust (both already matched retail). `OPENRECET_NO_CHAR_ZWRITE` only
  un-occludes the trail sparkles, not the real divergence.
- **Hunting a 3D-mesh occluder for the dust** (the old Phase-4 theory). The dust's
  own `z_far` was wrong; there was no missing mesh.
- **Iterating on side-by-side screenshots.** Phase noise (anim/RNG) swamps the
  real diff over the body. Compute NDC-z from the trace; for pixels, phase-align
  first ([[feedback_zoom_diff_render_debug]]).

Cross-refs: `engine-quirks.md` §93, `docs/findings/scene1-tear-visual-diffs.md`,
`docs/findings/scene1-walk-dust.md`, `docs/plans/freeroam-render-depth-parity.md`,
[[feedback_zfar_depth_footgun]], [[reference_parity_trace_walk_down_dense]].
