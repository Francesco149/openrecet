# Plan: HOUSE free-roam RENDER/DEPTH structural parity

> **Status update (2026-06-03f): Phases 0–2 DONE; dust occlusion STILL OPEN → Phase 4 is the
> live work.** Phase 0 landed (`tools/d3d_state_diff.py` + port `--d3d-trace` caprange
> windowing); the per-draw contract is ground-truthed on both sides. Phase 1 found the char
> render-state owner is **FUN_004552d0** (port `sw_pass_light`), NOT the leaf/prepass, and that
> the **free-roam player IS drawn by sw_pass_light** (the chr_walker attribution was wrong).
> Phase 2 = the port now carries retail's exact char contract (ZWRITE=1 / ZFUNC=LE / ALPHATEST
> ref0 GREATER); a clean pixel A/B proved the char Z-write is a **0/24 dust no-op**
> (constant-depth) — so the char is **ruled OUT** as the dust occluder, but the **dust bug is
> NOT solved** (retail occludes the dust; the port does not). The occluder must be a **3D mesh**
> drawn with ZWRITE=1 before the dust → **Phase 4** (per-pixel mesh depth at the dust footprint,
> port vs retail). Phase 3 (Tear): her WORLD position actually MATCHES retail ((0.6,3.06,9.35)
> both) — not a position bug; any Tear divergence is anim-phase/orientation, check on synced
> state only. See `docs/findings/scene1-walk-dust.md` §2026-06-03f. Original plan below.
>
> A focused deepening of
> [`freeroam-structural-parity.md`](freeroam-structural-parity.md) (the 77-fn master
> list), scoped to the **render + depth-buffer chain**. Driven out of the walk-dust
> occlusion investigation, which closed every *particle-math* door and localized the
> remaining gap to how the port builds the free-roam render pipeline + depth state.
> Goal: recover the engine's actual render structure (≈ original source) so depth/
> occlusion emerges the way it does in retail, instead of ad-hoc per-pass state.

## Why this plan exists (what the dust hunt proved)

See `docs/findings/scene1-walk-dust.md` §2026-06-03d/e for the full evidence. Summary:

- The foot-dust **emit, spawn, and tick are bit-exact** vs retail — triple-confirmed
  (disassembly = captured GT = port C, + a seed-sweep diff `runRetailDustSpawn`). The
  dust's world position matches retail. **The particle math is NOT the problem.**
- The visible "dust draws in front of the body in the port, behind in retail" is a
  **render/depth-chain** difference, not a position/RNG difference.
- The char sprite is a **constant-depth billboard** (verified through retail's actual
  WORLD·VIEW·PROJ: ndcz constant for all local-Y; engine-quirks §92). Dust↔char depth
  is a **near-tie** (~0.001 ndcz).
- The real free-roam char path is **`scene1_chr_walker_render`** (default), which draws
  the char with **ZWRITEENABLE=FALSE** (chr_walker.c L195). **Retail's char draw
  (FUN_0045a56f, ret_va 0x45aa31) carries ZWRITE=1, ZFUNC=LESSEQUAL, ALPHATEST ref0
  GREATEREQUAL.** That render-state divergence on the real path is the lead.

### "What makes ZWRITE cause problems for us" (the actual question)

Enabling the char Z-write today *occludes Tear's own wing-glow* (the b1acf7c regression).
That is **not a ZWRITE bug** — it is a **symptom of two structural shortcuts**:

1. **Tear is not 1:1** ([[project_confirmed_parity_ledger]]: Tear position CONFIRMED off;
   animation phase also drifts). Her char billboard lands at a *wrong depth*, so when it
   writes Z it occludes her glow (drawn later, z-tested). In retail her depth is correct,
   so the glow passes the LESSEQUAL test. → ZWRITE only "breaks" because the thing it
   writes (Tear's quad depth) is wrong.
2. **Render state is set ad-hoc by the wrong owner.** The port scatters depth state across
   `chr_walker` (sets ZWRITE=FALSE for the whole pass) and `scene1_chr_sprite_render` (sets
   *no* depth state). Retail sets it inside the real leaf (FUN_0045a56f) per its own
   contract. Until the port mirrors *where* and *what* state the engine sets, toggling
   ZWRITE in one place has side effects the engine never has.

So "fix the ZWRITE problem" = (a) port the char draw's render-state ownership faithfully,
and (b) close Tear's position/anim parity so her depth is correct. Both are structural.

## The retail free-roam render pipeline (target structure)

Draw order ground truth (runs/retail-bw-d3d4, every frame):
```
3D meshes (DrawIndexedPrimitive, FUN_004adfe2/…)   write Z
  → player/companion ground shadow (FUN_0045aa36)  ZEN1 ZWR0 ZERO/SRCCOLOR
  → furniture/object shadows ×6 (FUN_0046f648)     ZEN1 ZWR0 ZERO/SRCCOLOR  (STUBBED in port)
  → CHAR sprites (FUN_0045a56f, 0x45aa31) ×7       ZEN1 ZWR1 ZFUNC≤ AREF0 GE  SRCALPHA/INVSRCALPHA
  → more 3D meshes
  → CHAR ×1
  → wing-glow (FUN_004176ff 0x41e165) ×8           ZEN1 ZWR0 ONE/ONE
  → dust (FUN_004176ff 0x41e97b) ×2                ZEN1 ZWR0 SRCALPHA/INVSRCCOLOR
  → 2D HUD (0x405396/0x4063bc)                     ZEN0
```

The character-sprite chain (the focus): **FUN_0045672a (prepass/index depth-SORT,
FUN_0045526a co-sort) → FUN_00456f56 (per-actor world build) → FUN_0045a56f (leaf:
sets render state + DrawPrimitiveUP)**. Port today: `scene1_chr_prepass.c` (FUN_0045672a),
`scene1_chr_walker.c` (FUN_00456f56), `scene1_chr_sprite.c` (FUN_0045a56f leaf). Decompiles
exist: `docs/decompiled/by-address/{45672a,456f56,45a56f}.c`.

## Phases

### Phase 0 — Establish the per-draw RENDER CONTRACT (ground truth)
Capture the COMPLETE ordered draw list **with per-draw device state** for retail free-roam
(bottomwall + an open walk). For every draw record: ZENABLE, ZWRITEENABLE, ZFUNC,
ALPHATESTENABLE/FUNC/REF, SRC/DEST blend, CULL, the WORLD/VIEW/PROJ, and the FVF/stride.
- Tooling: the d3d-trace agent already logs ops; extend it to snapshot the *full* live RS
  block at each Draw (it currently logs some). Land a `tools/d3d_state_diff.py` that diffs
  a **port** d3d-trace against the **retail** one draw-by-draw (align by ret_va + draw index).
- Port-side d3d-trace now works via `run-openrecet.sh --d3d-trace <file> --d3d-trace-frames`
  (path-rewrite added this session). Map port ret_vas → functions via the port symbol table.
- **Acceptance:** a single table "retail draw N: state … | port draw N: state …" with the
  deltas flagged. This is the master checklist the rest of the plan drives to zero.

### Phase 1 — Map + verify the char-draw call graph (prepass → walker → leaf)
Decompile-verify the three functions against the disassembly and the live call order:
- **FUN_0045672a (prepass)** — what does it sort by (depth key?), and does it set any
  device/depth state for the pass? Confirm `scene1_chr_prepass` reproduces the sort + state.
- **FUN_00456f56 (walker)** — per-actor world-matrix build (incl. the +0.02 Z), the
  ZWRITE/blend it sets around the loop. Confirm `scene1_chr_walker` matches (today it sets
  ZWRITE=FALSE at L195 + ZWRITE=TRUE at L311 — verify that's where/what the engine does).
- **FUN_0045a56f (leaf)** — the actual DrawPrimitiveUP + **its render-state setup**. Find
  exactly which RS the leaf sets (ZWRITE? ZFUNC? ALPHATEST?). The port `scene1_chr_sprite_
  render` sets NONE — likely the structural hole. (Note retail draws 14-prim FAN, port emits
  per-cell 4-vert quads — reconcile the primitive topology too.)
- **Acceptance:** a written "who-sets-what" map for the char depth/blend state, with the
  exact engine owner of ZWRITE=1 / ZFUNC / ALPHATEST identified, cross-checked to the port.

### Phase 2 — Port the char render-state ownership faithfully
Move the depth/alpha state to the engine-faithful owner (leaf vs walker per Phase 1),
removing the ad-hoc `chr_walker` ZWRITE=FALSE override. The char draw must carry retail's
**ZWRITE=1, ZFUNC=LESSEQUAL, ALPHATEST ref0 GREATEREQUAL, SRCALPHA/INVSRCALPHA**, with the
pass restoring ZWRITE=0 afterward for the glow/dust passes (mirror the bg-NPC pattern in
`scene1_bg_npc.c` L383/L432, already proven).
- **Acceptance:** a port d3d-trace shows the char draw with ZWRITE=1 matching retail 45/45
  frames; Phase-0 diff for the char draw goes to zero. (Expect Tear's glow to break here if
  Phase 3 isn't done — that's the signal, not a failure.)

### Phase 3 — Close Tear's not-1:1 (the ZWRITE prerequisite)
The char ZWRITE only renders correctly once Tear's billboard depth matches retail. Pin
**position** (FUN_0048a4d1 spring-follow, [[project_next_char_controller]]) and **animation
phase** 1:1 so her quad depth == retail. This is the lever that makes ZWRITE stop "causing
problems."
- Verify with the depth: with ZWRITE=1, Tear's wing-glow must SURVIVE (glow depth == Tear
  quad depth → LESSEQUAL passes), exactly as retail.
- **Acceptance:** ZWRITE=1 char draw + glow visible + dust occlusion matching retail, on the
  bottomwall + open-walk traces; pixel-diff [retail|us|white-diff] clean at Tear + the glow.

### Phase 4 — Verify the 3D-mesh depth contract (the OTHER dust occluder)
The dust z-tests against the HOUSE meshes drawn before it (bottom wall / counter / furniture).
If the char ZWRITE alone doesn't reproduce retail's occlusion (it's a near-tie), the real
occluder is a 3D mesh whose depth the port renders differently. Audit `scene1_render_meshes`
(the ~14 walker stubs) for depth-write parity around the player's footprint; compare per-pixel
mesh depth port vs retail at the dust location.
- **Acceptance:** the dust occlusion matches retail regardless of which occluder dominates;
  no port-only show-through.

### Phase 5 — Validate + regression-guard
Re-run bottomwall + open-walk on `--target both`; confirm dust occlusion, wing-glow, ground
shadow, furniture shadows, sprite z-order, and HUD all match retail. Guard the deferred
furniture dynamic shadow pass (FUN_00470385/FUN_0046f648), the bg-NPC z-order, and the
companion phase.

## Tooling inventory (built this session — keep)
- `--dust-log` (port per-particle world pos + NDC-z + actor anchors): `scene1_walk_dust.c`,
  `main.c`, `run-openrecet.sh`, `export_trace.py --dust-log`.
- `runRetailDustSpawn` agent RPC + port host seed-sweep — bit-exact particle-param diff
  (extend to any spawn type / the char leaf if it becomes callable).
- Port `--d3d-trace` path-rewrite in `run-openrecet.sh` (Phase 0).
- Deterministic retail replay: `distill_trace --anchor-segments
  runs/recordings/retail-bottomwall.raw.jsonl` → `frida_capture --d3d-trace /
  --dump-records-b` (player pinned at the wall; jitter-immune).
- Data: runs/retail-bw-d3d3/4 (matrices), runs/retail-bw-recA/2 (vel/age/pos),
  runs/trace-export/bw-dustlog, /tmp/dustdbg/*sweep*.

## Do-NOT-redo (closed doors)
Emit jitter, spawn velocity/rot/scale/RNG-order, and the type-0xe tick drift are **bit-exact**
— do not re-investigate. The char-sprite Z-write at `sw_pass_light` is the WRONG path (no-op).
A naive char Z-write "fix" without Phase 3 will resurface the Tear-glow regression — that is
expected and is the whole reason this is a structural plan, not a one-line change.

## Cross-references
[[project_freeroam_smoke_effect]] · [[project_next_char_controller]] ·
[[project_confirmed_parity_ledger]] · `freeroam-structural-parity.md` ·
`docs/findings/scene1-walk-dust.md` · engine-quirks §92.
