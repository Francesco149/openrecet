# GX-00 — D3D8 method census (capture completeness)

> **Status:** STATIC + DYNAMIC census MECHANISM LANDED 2026-07-17 (roadmap
> `../plans/parity-evidence-roadmap.md` §9 GX-00 + GX-01 gate). Census
> `../schemas/d3d8-method-census-v1.json`; tool `../../tools/parity/d3d_census.py` + CLI
> `../../tools/d3d_census.py`; guard `../../tools/test_d3d_census.py` (63 checks). The
> DYNAMIC census (which forwarded methods a capture ACTUALLY calls) + the GX-01
> record-or-fail gate are built (see §"Dynamic census" below); the first live verdict on a
> real scene is pending a drive with the instrumented proxy.

## Why

A `pixels` / `render_program` pillar is only SOUND if EVERY render-affecting D3D8 call the
game makes was RECORDED — a replay reconstructs the frame from the captured command stream,
so a render-affecting call the proxy silently FORWARDS (passes to the real device without
recording) would make the replay diverge from what the game actually drew, and a pixel
PASS over such a scene would be unsound. This census makes the forward-vs-record split
explicit + fail-closed, the capture-trust prerequisite for M3.

## The split (committed proxy)

The v3 proxy (`tools/trace_studio_v3/proxy/`) wraps two interfaces; each vtable slot is a
hand-written `my_` interceptor (RECORDED) or a generated `fwd_` pass-through (FORWARDED):

| interface | slots | recorded | forwarded |
|---|---|---|---|
| IDirect3D8 | 16 | 4 | 12 |
| IDirect3DDevice8 | 97 | 25 | 72 |

Classified (113 total): **23 recorded** (draws, fixed-function state setters, Clear/Present,
SetRenderTarget/CopyRects, CreateTexture, CreateDevice params) · **6 wrapper_lifetime**
(QI/AddRef/Release) · **45 query_only** (forwarded reads — no state change) ·
**6 forwarded_irrelevant** (RegisterSoftwareDevice, ResourceManagerDiscardBytes, the Delete*
handle-frees) · **33 render_affecting_unsupported** — the RISK set.

## The RISK set — 33 forwarded, render-affecting, UNCAPTURED (fail-closed)

Grouped (`risk_subgroups`): **cursor** (SetCursorProperties/Position, ShowCursor) ·
**device_reset** (Reset, CreateAdditionalSwapChain, SetGammaRamp) · **resource_creation**
(Create{Volume,Cube}Texture, Create{Vertex,Index}Buffer, Create{RenderTarget,DepthStencilSurface,
ImageSurface}, UpdateTexture) · **fixed_function_state** (MultiplyTransform, SetViewport,
SetClipPlane, SetClipStatus, Set{Palette,CurrentTexturePalette}) · **state_blocks**
(Begin/End/Apply/Capture/CreateStateBlock) · **vertex_processing** (ProcessVertices) ·
**shaders** (Create/SetVertexShaderConstant, Create/Set/SetPixelShaderConstant) ·
**higher_order_primitives** (DrawRectPatch, DrawTriPatch).

**Fail-closed policy (GX-01):** a forwarded method that CAN alter future pixels/resources/
device state is `render_affecting_unsupported`, never `forwarded_irrelevant`, unless proven
visually inert. So this is the RISK SURFACE, not a claim any is actually hit.

## Leads

- **★ Shader-binding asymmetry — `SetVertexShader` is RECORDED, `SetPixelShader` is
  FORWARDED.** In D3D8 these handles are the FVF/programmable toggle; the proxy captures a
  vertex-shader/FVF change but would MISS a pixel-shader bind. Verify via the dynamic census
  whether `SetPixelShader` is ever called (a 2007 fixed-function title likely never binds a
  programmable PS — but the asymmetry is a real capture-completeness gap).
- **resource_creation is the GX-03/GX-04 hinge** — CreateVertexBuffer/IndexBuffer/etc. are
  forwarded, so those resources are UNWRAPPED and snapshotted late (the roadmap §1
  "snapshots unwrapped resource contents late" hole). A same-frame Lock/Unlock/CopyRects
  mutation could lose the pre-mutation content. GX-03 specifies per-draw resource versions.
- **Not a regression of existing PASSes.** The static risk does NOT mean the current pixel/
  render pillars are unsound — it means we have not PROVEN completeness for scenes that call
  these. Recettear is fixed-function DX8; most risk methods are likely never called. The
  **DYNAMIC census** (count actual calls in a capture; 0 observed ⇒ that method is safe to
  forward for this title) resolves each — the GX-00 follow-up.

## Dynamic census + GX-01 gate (mechanism)

The static census is the RISK SURFACE (which methods CAN affect pixels but are forwarded);
the dynamic census answers which a scene ACTUALLY calls — 0 observed ⇒ safe to forward for
this title/scene, >0 ⇒ an uncaptured render-affecting call ran ⇒ the replay may diverge ⇒
a `pixels`/`render_program` PASS over the scene is UNSOUND (GX-01: record-or-fail).

**Proxy counter (zero hand-edits).** The 84 FORWARDED thunks are all GENERATED
(`gen_forwarders.py`), so instrumenting is a generator change only: it emits a stable
`enum { FWD_<Iface>_<Name>, … FWD__COUNT }`, a `volatile LONG g_fwd_calls[FWD__COUNT]`, a
`g_fwd_names[]` table, and an `InterlockedIncrement(&g_fwd_calls[FWD_…])` at the top of
each `fwd_` thunk. The counter is **process-lifetime + unconditional (NOT capture-gated)**:
device state persists, so a `render_affecting_unsupported` call ANYWHERE up to the compared
window (e.g. a `SetViewport` during the load) would desync the replay — it must be caught
even if it fired before the window. The RECORDED (`my_`) methods are captured-by-
construction (their effect is in the container) so they need no counter; only the
forwarded/uncaptured set is the completeness question. The proxy rewrites
`v3cap.census.json` (`{schema_version, forwarded_calls:{"Iface.Name":count}}`) at each KEPT
frame — mirroring the container's own per-frame `fflush`, so a hard `device.kill` still
leaves the last completed frame's census on disk.

**Consumer + gate** (`tools/parity/d3d_census.py`; CLI `d3d_census.py --dynamic <sidecar>`).
`build_dynamic_report` cross-references the sidecar against the risk set:

- **SAFE** (exit 0) — every risk method present in the sidecar and 0-observed: the capture
  is COMPLETE for this scene.
- **VIOLATION** (exit 1) — a risk method fired (count>0): GX-01 record-or-fail; the
  method + call-count + risk subgroup are named.
- **INCONCLUSIVE** (exit 2) — a risk method ABSENT from the sidecar, or a sidecar key the
  census doesn't classify (drift): can't prove SAFE (fail-closed).

Query-only forwarded methods (e.g. `GetRenderState`) are shown in the informational call
profile but never trip the gate. The `SetPixelShader` asymmetry lead reads out its observed
count explicitly. This is the POST-HOC gate (run after a drive); an in-proxy hard-terminate
on the first render-affecting forwarded call (GX-01's stricter form) is a later hardening.

## Drift guard

`test_d3d_census.py` re-parses the committed `proxy_generated.h` and asserts it matches the
census exactly: every vtable method classified once, every classified method present, and
each actual mode == its class's expected mode (`mode_of_class`). So a proxy change that
starts RECORDING a risk method (GX-02) fails until the census reclassifies it to `recorded`;
a d3d8.h/gen_forwarders change that adds/removes a slot fails until the census follows. The
census can never silently rot — "wrapper and generated-forwarder lists cannot drift
unnoticed" (GX-00 acceptance).

## Tooling

`tools/parity/d3d_census.py` (static verify/report + dynamic gate) · CLI
`tools/d3d_census.py` (static: 0 match / 1 drift / 2 fatal; `--dynamic`: 0 SAFE / 1
VIOLATION / 2 INCONCLUSIVE) · census `docs/schemas/d3d8-method-census-v1.json` · sidecar
`v3cap.census.json` (proxy-emitted) · guard `tools/test_d3d_census.py` (63 checks). Next:
the first live verdict — drive a real scene with the instrumented proxy, run `--dynamic` on
each side's sidecar (thread the sidecar through the v3 cache alongside `v3cap.bin`) → the
SAFE/VIOLATION answer that grounds each pixel/render PASS; then GX-02 implements any
observed missing method (order = the risk subgroup that fired).
