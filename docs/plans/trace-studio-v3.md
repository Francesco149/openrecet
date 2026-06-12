# Trace Studio v3 — "capture the render program, not its output"

**Status:** DESIGN / de-risking (2026-06-12). Built in isolation; **v2 stays the
working tool until v3 is proven and archived** (user constraint). This doc is the
canonical v3 plan. Experiments live in `runs/studio-v3-experiments/`.

**P0 ✅ GO (2026-06-12, `65bcdd7`) — the load-bearing risk (R1) is CLEARED.** A
shared proxy `d3d8.dll` loaded into the real port, captured the full call stream +
dedup'd resources into a binary container (`tools/trace_studio_v3/`), and a separate
`replay.exe` re-rendered a real title frame through real D3D8 **bit-identically** (0
differing bytes / 0 pixels / max delta 0, vs both the proxy reference AND the port's
own screenshot; 4884 calls / 666 draws / 4 textures replayed). Capturing the d3d
command stream + resources IS sufficient to re-render frames exactly as the game did.
**Remaining de-risks:** R2 retail-side proxy loadability (SteamStub-unpacked exe +
Frida spawn); the VB/IB-backed 3D path (P0c was 2D UP draws only); cross-frame
content-hash dedup at session scale; then the P1–P4 build.

**P1 ✅ DONE (2026-06-12) — capture-at-scale works; a 3D frame past a long load replays
BIT-EXACT.** A real HOUSE 3D free-roam frame captured **8797 prologue/load frames deep**
(55 VB/IB indexed + 31 UP draws, 46 resources, ~26 MB) replays **0 px / 0 byte** vs the
proxy reference. Two fixes got there: (1) **deferred-snapshot two-section container** —
per-frame calls buffer in memory + drop every Present; resources snapshot ONLY at finalize
(target frame) ⇒ the load costs zero snapshot work (963 MB balloon + throttle gone) with no
stale/pointer-reuse bug; (2) **device-state-shadow preamble** — R4 (inherited state) was
real for 3D after all (the frame inherits lighting/blend state ⇒ replayed overbright +
black-blended), fixed by shadowing every scalar Set and emitting it at each frame boundary.
Title regression still bit-exact. Details in the P1 phase entry below.

**R2 ✅ DONE (2026-06-12, `fe3722a`) — the LAST de-risk before P2 is cleared: the SAME
proxy d3d8.dll captures BOTH sides.** R2a (loadability): the Windows loader picks up the
app-local proxy d3d8.dll for `vendor/unpacked/recettear.unpacked.exe` — even loaded from a
`\\wsl.localhost` UNC path under Frida spawn — and wraps `Direct3DCreate8`+`CreateDevice`;
**SteamStub/the unpack does NOT interfere**. R2b: the retail **title screen** (frame 120,
1024×768, 5 res, 52 calls, 6 draws) replays **0 px / 0 byte** vs the proxy reference (port
regression still bit-exact). Two findings (matter for P2 diff align): (1) retail's OWN
`recet.ini` read **fails over the `\\wsl.localhost` UNC path** — `GetPrivateProfileIntA` can't
read it (plain `fopen` can; that's why the proxy's `v3proxy.cfg` over UNC works), so `screen`
falls to its default 0 ⇒ 640×480. Retail is **pinned to 1024×768 to match the port via the
agent's `force_resolution` hook** (patches `DAT_005cbc04/08` on `FUN_0047a474` exit — the same
mechanism scenario-test uses for v2 retail; my first R2 probe just hadn't enabled it). (2)
retail **ships its backbuffer NON-lockable** (flags=0x0; port 0x1) ⇒ readback bounces through
a sysmem surface via CopyRects (now a shared helper used by proxy + replayer, the same path
the v2 agent uses). Config now via a `v3proxy.cfg` next to the dll (capframe/out — env vars
don't cross to the Frida-spawned exe); proxy log made unbuffered (kill-safe). Driver:
`tools/trace_studio_v3/r2_retail_probe.py` (`--hook-ini` = the GetPrivateProfileIntA probe
that found the UNC read failure).
**Next: retail FULL-EXTENT capture + content-addressed cache (P1 tail) → P2 sync-by-identity.**

**3D/multi-scene stress test (2026-06-12) — surfaced the P1 capture-at-scale work
(not a flaw in the bet).** Tried capturing a 3D HOUSE frame via `scenario-test`. Two
learnings: (1) **present-count can't target a post-load frame** (turbo load-stretch is
nondeterministic) — fixed by triggering the capture finalize on the app's own
`GetBackBuffer` readback, which aligns capture to the harness's `--capture-frames` in
sim-frame space (landed in the proxy; the first such frame in a Continue/Load trace is
the first house frame). (2) **"capture from frame 0 + replay all" is impractical for a
long (post-load) target**: recording the multi-thousand-frame load-stretch ballooned the
container to **963 MB** and the 9p-mount writes throttled the port so it never reached the
target frame. → P1 MUST add: **windowed capture** (record a window, not from 0) + a
**full device-state snapshot at the window start** (R4: GetRenderState-all + transforms +
tss + current bindings, so the window replays without prior frames); **local-disk
container writes** (not 9p, mirroring D2); **content-hash resource dedup** (pointer-dedup
also breaks across scene transitions when a freed texture's pointer is reused — a window
sidesteps it, content-hash closes it). WSLENV note: WSL env vars don't cross to the
Windows exe unless in `WSLENV`; the proxy reads config next to the dll instead.

## Mandate (user, 2026-06-12)
- **Radical, order-of-magnitude better — not a slight improvement over v2** ("otherwise
  it's not worth thousands of lines"). Try radical ideas; the d3d-state-replay is the
  exemplar.
- **Capture ALL d3d state + ALL textures + ALL frames**, dedup'd (each unique resource
  stored ONCE), **sufficient to re-render every frame EXACTLY as the real game rendered
  it.**
- **Don't break the existing Frida harness.** A separate dedicated agent or a **custom
  DLL** is sanctioned if it gives better performance/functionality.
- **Preserve the v2 UX** (3-panel scrub, diff ribbon, marks/crops, state table) — make it
  faster + more robust, don't redesign it.
- **One tool** that captures *and* analyzes everything needed to pinpoint divergences and
  port them.

## The three pains v3 must kill (with root causes)
1. **Captures are slow** (1h+/session on re-captures). Root cause (E4, measured): the
   cost is **retail EXECUTION**, not pixel readback. Retail turbo-executes ~18.5k frames
   to reach a 3.7k-frame window, then grinds on to `max_frames=45k` because the segtrace
   early-exit never fires → **80% of the 172s is boot-stretch + post-window over-run
   waste**, and it's **re-paid on every recapture** (no retail cache).
2. **Sync whack-a-mole.** Root cause (E3, measured): frames are paired by a *reconstructed
   global label* (`window_start + cumulative kept-index`). A non-deterministic load makes
   the two sides keep different frame counts → the index drifts → everything after a load
   seam mispairs. Raw global-index pairing on the live guild session is **2.1% correct**;
   v2 only works because of heavy bolt-on patches (anchor-rebase, honest-holes,
   per-panel-seek).
3. **Not robust** (diffs out of sync / stale / diffing different frames). Root cause: the
   alignment contract is **re-derived from anchors every capture and re-implemented in 4+
   places** (segments.py / align.mjs / convert.py / model.mjs / apply.py / state.py).
   Identity is *implied by a filename*, never *stored*. Any input drift → the filenames
   silently lie. (15+ historical sync fixes to the same 3 files.)

## Experiment results (the evidence base)

| # | Question | Result |
|---|---|---|
| **E2** | How dedup-able is the d3d call stream? | **99% of each frame's calls are byte-identical to the previous frame** (retail mixed scene); **1.9–2.5% of all call records are unique** (40–54× repeat); gzip 24–27×. A 41 MB / 932-frame retail trace has **6,345 unique call records**. JSONL parse is slow (0.26 M rec/s = 1.3 s) → a binary/columnar format is ~100× faster to process. |
| **E2** | What does today's d3d trace capture? | **Nothing replayable**: 0 tex names, 0 tex contents, 0 geometry (VB-backed draws store only an opaque pointer; UP-draw verts off by default). 18,860 VB-backed draws/run with no geometry. |
| **E3** | Does stored-identity pairing beat global-index? | On the live guild seam (port 3860 / retail 3740, **13,492-frame** prologue load-stretch): **stored `(anchor#occ, offset-since-anchor)` join pairs 94% cleanly** (3642/3860) with honest explicit gaps; **v2 global-index would mispair 98%** without the rebase patch. |
| **E4** | Where does capture wall-clock go? | Retail = **172 s**; executes 18.5k frames for a 3.7k window then runs to 45k. **~60% (~100 s) is post-window over-run** (killable by a window-aware early-exit); the rest is boot+load-stretch (killable by **caching** retail). Pixel readback is NOT the bottleneck. |
| **E1** | Is full exact-replay capture affordable? | Yes, because of E2's dedup: resources are reused 40–54× and the unique set is tiny. Est. **textures few MB + geometry sub-MB–few MB per session, dumped ONCE** — **smaller than v2's 460 MB of mp4** while being richer + replayable. The unknown to *prove*: real-D3D8 replay reproducing pixels **bit-exactly**. |

**Headline:** a frame is ~99% a repeat of the prior frame, and a whole session reduces to
a few thousand unique render commands + a few MB of unique resources. The d3d stream is a
*better, smaller, richer* representation of a session than 460 MB of screenshots — and it
is the natural unit for solving sync (stored identity) and analysis (semantic diff).

## The thesis
**Stop treating a frame as an image; treat it as a deterministic render program.** Capture
the exact D3D8 command stream + every unique resource (textures, geometry, state) on both
sides, dedup'd into a compact binary container. Then:
- **Display & oracle** = *replay* the program (pixel-exact, validated) instead of storing
  screenshots → tiny storage, render at any zoom.
- **Sync** = join on a **stored per-frame identity** `(anchor#occ, offset)` → seam bugs
  structurally impossible (E3).
- **Analysis** = the captured program IS the semantic explanation → click a divergent
  frame and see *which draw / which state / which texture* differs, with draw-call picking
  and isolation, all in the one viewer.

This is RenderDoc-for-Recettear with port|retail side-by-side and frame-exact sync.

## Architecture

### 1. Capture — a shared proxy `d3d8.dll` (the radical mechanism)
A drop-in `d3d8.dll` that wraps the real one (standard proxy-DLL; app-dir loads before
system32), used by **BOTH** the port exe and the retail unpacked exe. It wraps
`Direct3DCreate8` → `IDirect3D8` → `IDirect3DDevice8` and every resource
(`CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer`/`Lock`/`UpdateTexture`/…),
recording into an in-process **binary trace** written straight to disk:
- **All state-setting + draw calls** (the 12 we hook today + the missing ones: `Clear`,
  `SetViewport`, `SetLight`/`LightEnable`, `SetClipPlane`, render-target/depth binds,
  `BeginScene`/`EndScene`/`Present`, palette).
- **All resource contents, content-hashed and stored ONCE** (texture mips, VB/IB lock
  ranges). A bind = a reference to a hash; a re-lock with the same bytes = no new storage.
- **Initial/inherited device state** at window start (so a sliced window replays exactly).

Why a shared proxy DLL (vs today's split port-C-macros + retail-Frida-JS):
- **ONE capture implementation for both sides** → byte-identical capture semantics →
  trustworthy diffing, and it kills the port-vs-retail hook-drift maintenance burden.
- **In-process, no Frida IPC for the heavy data** → fast, no 128 MiB cap, no backpressure
  (the crash class that forced capture-local).
- **Doesn't touch the existing Frida harness.** Frida keeps doing orchestration only
  (spawn, turbo clock, input forcing, anchors, save-virtualization); the d3d+frame data
  moves to the DLL. (Per the user's explicit sanction.)

### 2. Storage — a compact binary container (the dedup win)
Per session: a **resource store** (unique textures/geometry by content hash, dumped once)
+ a **per-frame command log** (columnar/delta-encoded; E2 says ~99% is a repeat of the
prior frame, so delta encoding is near-free). Memory-mappable, no JSON parse → ~100×
faster to process than v2's JSONL. Target: **tens of MB/session vs v2's ~460 MB.**

### 3. Replay — pixel-exact, validated (the #1 risk, de-risked first)
A replayer re-issues the captured program through the **same real `d3d8.dll` on the same
GPU** → deterministic fixed-function output. **The load-bearing unproven assumption:
replay reproduces the real frame BIT-exactly.** Mitigation, built FIRST: capture a sample
of real screenshots alongside the program and assert `replay(frame) == screenshot(frame)`
to 0 LSB. If it holds → replay IS each side's ground truth (the parity oracle replays both
sides and diffs). If a driver nondeterminism (dither/MSAA) costs a LSB → keep sampled real
screenshots as the oracle and use replay for analysis/zoom (still a full win on sync +
analysis + storage). **We do not bet the project's 1-LSB rigor on replay until it's
proven.**

### 4. Sync — stored identity as the ONE authority (E3-proven)
Every captured frame stores its identity `(most-recent-anchor#occurrence,
frames-since-that-anchor)`. Pairing = a **join** on that key — computed once, stored, read
by the diff, the video seek, the state table, and marks alike. No renumber, no rebase, no
per-panel seek, no `window_start==0` no-op trap. Loads can stretch one side arbitrarily;
the next gameplay segment re-syncs at its anchor by construction. Gaps are explicit and
honest (E3: 218 port-only / 98 retail-only, vs 3660 silently-wrong in v2).

**Kill the sync push-pull dance (first-class goal, user 2026-06-12).** The v2 pain isn't
just slow captures — it's the manual whack-a-mole of lining both sides up frame-by-frame,
fighting load/wait-time differences. v3 dissolves it by design, not by disciplining the
human: (1) **never frame-match by hand** — anchor *semantic events* (the engine already
emits LOADING_END / HOUSE_FREEROAM / dialogue anchors) and JOIN on `(anchor, offset)`; you
never reproduce a loading time, because loads live *between* anchors and may differ freely,
only gameplay offsets (deterministic under turbo) must match. (2) **Sync once, reuse
forever** — retail captured + joined once; a port-side fix re-slices the SAME cached+joined
retail (no re-dance, no re-drive). (3) **Replay both sides** → frames reproduced
deterministically, removing GPU/driver/timing variance from the comparison. (4) **Honest
gaps, never silent mispairing** — where the join genuinely can't pair, the tool SHOWS the
hole and names the anchor; the human's only job becomes "do the anchors fire on the same
events?", which the tool checks for them.

### 5. Capture speed — retail captured ONCE, then sliced
- **Window-aware early-exit**: stop retail when the last in-window frame is captured (kills
  E4's ~100 s post-window over-run).
- **Content-addressed retail cache**: capture retail's run for a trace ONCE (the compact
  binary makes a *full-extent* capture cheap), key by (trace-hash); any sub-window is a
  **slice of the cache** → **zero retail re-drives** across re-windowing and port-fix
  loops. The port (fast — no load-stretch) is the only thing re-driven. This turns the
  3–4 min full recapture into a ~30–60 s port-only loop. (This is v2's `--only port`
  generalized so a *window change* no longer forces a retail re-drive.)

### 5b. Retail turbo throughput — measure + uncap (investigate, user 2026-06-12)
Retail execution is the dominant capture cost (E4), so its turbo frame-rate is the lever.
**Initial read from the guild run:** 172 s for ~45,081 executed frames ≈ **262 fps avg** —
but that average is dominated by the cheap post-window over-run (no capture); the *windowed*
frames carry frame-readback + call-trace (tens-of-k events/frame, the likely heaviest knob)
+ per-frame Frida IPC. **To do:** measure turbo fps in controlled modes — bare run vs
+frame-capture vs +call-trace vs +d3d-trace — to isolate each overhead, and hunt hidden
throttles (Present vsync/`INTERVAL`, residual real `Sleep`, Frida per-frame message cost,
audio/DirectShow). **The proxy is the ideal instrument + fix:** it can timestamp every
`Present` in-process (precise fps histogram, no IPC skew) AND force
`FullScreen_PresentationInterval = IMMEDIATE` at `CreateDevice` to kill any vsync cap — a
natural v3 win that also serves the retail-once-cached capture. (262 fps > 60 ⇒ not a hard
60-Hz vsync cap, but windowed present cost is still worth checking.)

### 6. Viewer — preserve v2 UX, add a semantic layer
Keep the htm/preact SPA UX verbatim in feel: 3 lockstep panels (port|retail|diff) +
filmstrip + diff ribbon + scrub + marks/crops + state table. Frames are served by the
**replayer** (render-on-demand, any zoom) instead of mp4. New on top: click a divergent
frame → **semantic diff** (which draw/state/texture differs, `render_diff` +
`d3d_state_at_draw` integrated), **draw-call picking** (click a pixel → the draw that
painted it), **draw isolation/wireframe**. This is the "one tool" — capture + pixel diff +
semantic diff + state, no command-composing between tools.

## Tricks toolbox — dodge the load-stretch + auto-sync (brainstorm, user 2026-06-12)
Ranked by leverage. The first three are the highest-value; compose them.

**Dodging the load-stretch** (the multi-thousand-frame turbo load that bloats captures,
scrambles frame-addressing, and dominates retail time):
- **T-A · Anchor-gated capture (do first).** Stay fully idle (no capture, no resource
  snapshot) until `LOADING_END` fires, then capture the post-load window. The load still
  runs but costs nothing to capture; frame-addressing is anchor-relative
  (`HOUSE_FREEROAM+N`), never present-count. This alone fixes the 3D-test throttle and the
  targeting problem. The proxy reads the engine's `loading_active` flag (already computed)
  or the harness signals the anchor.
- **T-B · Skip-render during load.** While `loading_active`, the proxy no-ops the
  nowloading draws + Present (sim still pumps — keep it deterministic, only skip GPU work)
  → the load-pump runs at max speed. Cuts the load's *wall-clock*, not just its capture.
- **T-C · Process snapshot/restore past the load (radical, biggest win).** Boot+load ONCE,
  snapshot the writable state at `HOUSE_FREEROAM` (the phase-census already dumps
  .data/.bss), then RESTORE it to skip boot+load entirely on every subsequent capture —
  emulator save-state for iteration. Pairs with "retail captured once, cached." Risk:
  non-memory state (live D3D device, file handles) — restore sim memory while keeping the
  live device; validate against a cold-boot capture. Could collapse a 3-min retail drive to
  seconds.
- **T-D · Patch out the cosmetic load-wait.** If the nowloading duration is a counter/timer
  (not genuine work), collapse it to 1 frame while preserving the deterministic warmup
  (NPC RNG etc.). Surgical; needs the load-pump RE.

**Auto-syncing traces** (so the human never frame-matches):
- **T-E · d3d-stream hash as a content sync key.** Hash each frame's command stream (or a
  downsampled replay). Identical hash ⇒ identical render ⇒ same moment — pairs frames with
  ZERO anchors, and *validates* the `(anchor,offset)` join (mismatch = a real desync, named
  by frame). The d3d stream IS the frame's identity; this is the strongest secondary key.
- **T-F · Gameplay-only sim-frame counter as the universal clock.** Find a counter that
  advances 1/frame ONLY during gameplay (frozen through loads) — identical on both sides at
  the same logical moment regardless of load timing. Join on it directly. Census-discoverable
  (db054 is load-phase-dependent; the right counter may not be). The cleanest join key if it
  exists.
- **T-G · Anchor-relative auto-windowing.** The human writes `HOUSE_FREEROAM+120..168`; the
  tool resolves it independently on each side. No absolute frames ever cross the human's
  desk — kills the "lack of discipline reproducing timings" pain by construction.

## What stays from v2 (reuse, don't rewrite)
Frida orchestration (spawn/turbo/input/anchors/save-virt), the anchor system, the pin ops
(`{phasepin}`/`{rngseed}`/`{tutloadpin}`) + lint + auto-pin, the trace/scenario model, the
audio-diff pillar, the SPA UX, flow_diff/verdict. v3 replaces the **capture mechanism**,
the **storage format**, the **alignment authority**, and **adds replay + semantic viewing**.

## Risks / open questions
- **R1 (critical): bit-exact replay.** Prove first (phase 1). If it fails, fall back to
  screenshot-oracle + replay-for-analysis.
- **R2 ✅ SOLVED (2026-06-12, `fe3722a`): proxy-DLL loadability + capture for retail.**
  The loader picks up the app-local proxy d3d8.dll for the unpacked exe (even from a
  `\\wsl.localhost` UNC path under Frida spawn); SteamStub/the unpack does NOT interfere.
  A retail title frame captures + replays bit-exact. Surfaced: retail's UNC recet.ini read
  fails (defaults to 640×480) ⇒ pinned to 1024×768 via force_resolution to match the port; and
  a NON-lockable backbuffer (CopyRects-via-sysmem readback, now shared by proxy + replayer).
- **R3: resource-capture overhead** (hashing every lock). Measure; expected cheap given
  dedup, but Lock-heavy frames need a fast hash + a "dirty range" shortcut.
- **R4 ✅ SOLVED (2026-06-12): inherited state at a sliced window start.** Title was
  self-contained so P0/P1 downgraded this — but 3D scenes inherit lighting/ambient/blend
  state, so a sliced single frame replayed overbright + black-blended. The **state-shadow
  preamble** (track every scalar Set; emit at each frame boundary) supplies the inherited
  state; proven bit-exact on the HOUSE 3D frame. Resource *bindings* are NOT
  inherited-relevant (every draw re-binds), so the shadow is scalar-only + cheap.

## Phased build (de-risk first; commit in logical units; `/clear` at boundaries)
- **P0 — Replay-fidelity spike (riskiest first).** Minimal proxy d3d8.dll that logs the
  full call+resource stream for the PORT only, for a few frames, + a replayer + a
  screenshot-equality check. **Acceptance: one real frame replays bit-exact.** Go/no-go on
  the whole replay bet. *(Also answers R2 if extended to retail.)*
- **P1 — Capture-at-scale ✅ DONE (2026-06-12, `f0147a8` + the two-section/shadow chip).**
  Landed: local-disk writes, `GetBackBuffer`-aligned frame targeting, **single-frame
  capture**, the **two-section container**, and the **device-state-shadow preamble**.
  - **Two-section = a DEFERRED-snapshot variant (better than snapshot-once-persistent).**
    Per-frame calls accumulate in an in-memory buffer dropped every Present; resource
    snapshots are DEFERRED to finalize, so ONLY the target frame's bound resources are ever
    read back. The multi-thousand-frame load costs **ZERO snapshot work** (the 963 MB
    balloon + throttle are gone), AND there's no stale-data / pointer-reuse-across-transition
    bug — snapshotting at finalize reads the target frame's *current* pointers + contents, so
    the content-hash concern the plan flagged simply dissolves for single-frame capture.
    Finalize writes `[resources][preamble][calls]` (resources first → streaming replayer
    still sees every id defined before use).
  - **R4 RE-UPGRADED then SOLVED.** The title is self-contained (re-sets its own state) so
    P0/P1 thought a device-state snapshot was unneeded — but a 3D scene is NOT: it inherits
    lighting/ambient/blend state set in an *earlier* frame, so a sliced single frame replayed
    **overbright + with black-blended quads** (user-confirmed: "everything 3d is overbright
    and washed out … black rectangle around the tapestry"). Fix = a **state shadow**: track
    every scalar Set the game makes (render states / TSS / transforms / material / FVF) and
    emit it as a preamble at each frame boundary, so the kept frame begins at its exact
    inherited state. Only states ACTUALLY set are emitted (no GetRenderState-all, no
    invalid-enum risk — "track the state, cheap + bullet-proof", user). Resource *bindings*
    are NOT shadowed (every draw re-binds its own texture/stream/indices ⇒ frame-start
    bindings never matter, and shadowing them would resurrect the load-time snapshot cost).
  - **PROVEN bit-exact** on a real HOUSE 3D free-roam frame captured **past 8797
    prologue/load frames** (55 VB/IB `DrawIndexedPrimitive` + 31 UP draws, 46 resources →
    **0 px / 0 byte / max-delta-0** vs the proxy reference) — closes the VB/IB path +
    scene-transition + R4 questions together. Title regression (self-contained, 56 calls)
    still bit-exact. Capture run 23 s, no throttle.
  - **Resource volume (closes E1):** ~**26 MB** for a full HOUSE frame's 46 unique resources
    (38 tex / 4 VB / 4 IB, all D3DPOOL_MANAGED ⇒ lockable, 0 empty), dumped once.
  - Container analyzer: `tools/trace_studio_v3/inspect_cap.py` (structured JSON: dev params,
    resource store, call-op histogram, self-contained-vs-inherited state signals).
  - **R2 ✅ DONE (`fe3722a`):** retail-side proxy loadability + a retail title frame
    captured/replayed bit-exact. **Still TODO in P1:** retail FULL-EXTENT capture +
    content-addressed cache (the slice-don't-re-drive cache that unblocks P2).
- **P2 — Sync-by-identity + the slice/cache loop + window-aware early-exit.** Port the
  E3 prototype into the real pairing authority.
- **P3 — Viewer**: replay-served panels + preserved UX + the semantic diff/pick layer.
- **P4 — Parity-loop parity check**: reproduce a known confirmed-1:1 session in v3, verdict
  matches v2. **Then** archive v2.

## Honest note on "10×"
The 10× is on the **iteration loop**, not one axis: retail-caching + window-early-exit kill
the capture wait; stored-identity kills the sync whack-a-mole; the integrated semantic
viewer kills the multi-tool divergence hunt. The d3d-replay is what makes all three cheap
*together* (compact sliceable cache, stored identity, semantic content) — and it delivers
the user's "re-render exactly as the game did." The one thing it does NOT do is make the
*first* retail drive faster (that's the game's load); caching makes every *subsequent* one
free.
