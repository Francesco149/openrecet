# GX-00 — D3D8 method census (capture completeness)

> **Status:** STATIC + DYNAMIC census LANDED 2026-07-17 + FIRST LIVE VERDICT (roadmap
> `../plans/parity-evidence-roadmap.md` §9 GX-00 + GX-01 gate). Census
> `../schemas/d3d8-method-census-v1.json`; tool `../../tools/parity/d3d_census.py` + CLI
> `../../tools/d3d_census.py`; guard `../../tools/test_d3d_census.py` (63 checks). The DYNAMIC
> census + GX-01 gate are built (§"Dynamic census") and RUN on the real
> `house-firstcust-arrprobe` M0 scene → **VIOLATION on both sides** (§"First live verdict"):
> the capture's VB/IB CREATION is forwarded-uncaptured (the GX-03/GX-04 hinge). Not the
> expected SAFE — the census caught a real completeness gap on our most-confirmed scene.
>
> **✅ RESOLVED 2026-07-17 by GX-04** (`gx03-resource-versions.md`; commits
> `403ae49`+`9c3d298`): VB/IB are now WRAPPED — CreateVertexBuffer/CreateIndexBuffer
> intercepted, every Lock/Unlock seen, content FROZEN at each bind. arrprobe re-drive:
> census dynamic **VIOLATION → SAFE** (31/31 risk 0-observed), 80/80 bit-exact both sides.
> The section below is the ORIGINAL first verdict (kept for the record).

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

## First live verdict — arrprobe (2026-07-17)

Drove `house-firstcust-arrprobe` `HOUSE_FREEROAM#1 [1,80]` (`orv3_window … --view`) with the
instrumented proxy — the new proxy hash re-keyed the EP-08 cache ⇒ both sides re-drove; 80/80
bit-exact, JOIN_COMPLETE. Ran `d3d_census.py --dynamic` on each side's cached
`v3cap.census.json`:

| side   | verdict   | risk methods fired | count |
|---|---|---|---|
| retail | VIOLATION | CreateVertexBuffer, CreateIndexBuffer | 130× each |
| port   | VIOLATION | CreateVertexBuffer, CreateIndexBuffer | 13× each |

**31 of 33 risk methods 0-observed on BOTH sides** — no Reset, SetViewport, state-blocks,
shaders, cursor, palettes, ProcessVertices. The completeness gap is SURGICALLY
resource-creation (VB/IB) only; `SetPixelShader`=0 (the asymmetry lead is moot here). The
query-only forwards (retail GetDeviceCaps 450×, GetDirect3D 420×, GetDisplayMode 160×,
GetViewport 35×) correctly do NOT trip the gate — the query-only-ignored logic validated on
real data.

**Meaning — the GX-03/GX-04 hinge, exactly as the static Leads predicted.**
CreateVertexBuffer/CreateIndexBuffer are FORWARDED ⇒ the proxy does NOT wrap/version the
resource. BUT `write_frame` DOES snapshot each bound VB/IB's *content* at end-of-kept-frame
(`snap_vb`/`snap_ib`, content-hash dedup'd). So the replay reconstructs the geometry correctly
**iff each buffer's end-of-frame content equals what every draw in that frame used** — i.e. no
same-frame re-mutation of a reused buffer (the GX-03 invariant). Fail-closed, we have NOT
proven that ⇒ VIOLATION. arrprobe's human-confirmed 1:1 pixels are EVIDENCE the invariant
holds on this scene, not proof; GX-03 (per-draw resource versions) + GX-04 (wrap/version VB/IB)
resolve it (or catch a real same-frame mutation).

**Honest scope.** This SHARPENS arrprobe's M0 honest-FAIL: our most human-confirmed scene is
visually 1:1 yet its capture is not PROVEN complete — now with a concrete mechanism (unwrapped
VB/IB creation), not just ±1 sub-perceptual pixel noise. It does NOT say the pixels are wrong,
and is DISTINCT from the known render_program FAIL (the b494 80-tri retail-only strip — a
captured DRAW difference, not a completeness gap).

**Count magnitude is NOT a parity signal.** retail 130 vs port 13 is EXPECTED: the counter is
process-lifetime and retail runs the full title→intro→house load (more scenes ⇒ more buffers)
the port skips. Completeness asks only "does the risk method fire at all" (both: yes); count
EQUALITY would need a window-scoped counter + a different (equality) gate. A lead, not a fault.

**NOT auto-wired into `parity_prove`.** The census is a standalone per-side gate today. Making
capture-completeness a hard PRECONDITION on the pixels/render_program pillars (⇒ arrprobe's
pixel/render → INCONCLUSIVE until GX-04) is an R3 policy step with wide blast radius (every 3D
scene creates VB/IB) — deferred to GX-01-full / GX-03 / GX-04.

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
`v3cap.census.json` (proxy-emitted, threaded through the v3 cache alongside `v3cap.bin`) ·
guard `tools/test_d3d_census.py` (63 checks). Next: **GX-03/GX-04** (per-draw resource
versions + wrap/version VB/IB) — the one risk subgroup the first verdict fired, the resolution
that turns arrprobe's capture-completeness VIOLATION → SAFE (or catches a real same-frame
re-mutation). Then the R3 policy call on wiring the census as a hard pixels/render_program
precondition in `parity_prove` (GX-01-full).
