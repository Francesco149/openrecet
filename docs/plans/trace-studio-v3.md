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

### 6. Viewer — preserve v2 UX, add a semantic layer
Keep the htm/preact SPA UX verbatim in feel: 3 lockstep panels (port|retail|diff) +
filmstrip + diff ribbon + scrub + marks/crops + state table. Frames are served by the
**replayer** (render-on-demand, any zoom) instead of mp4. New on top: click a divergent
frame → **semantic diff** (which draw/state/texture differs, `render_diff` +
`d3d_state_at_draw` integrated), **draw-call picking** (click a pixel → the draw that
painted it), **draw isolation/wireframe**. This is the "one tool" — capture + pixel diff +
semantic diff + state, no command-composing between tools.

## What stays from v2 (reuse, don't rewrite)
Frida orchestration (spawn/turbo/input/anchors/save-virt), the anchor system, the pin ops
(`{phasepin}`/`{rngseed}`/`{tutloadpin}`) + lint + auto-pin, the trace/scenario model, the
audio-diff pillar, the SPA UX, flow_diff/verdict. v3 replaces the **capture mechanism**,
the **storage format**, the **alignment authority**, and **adds replay + semantic viewing**.

## Risks / open questions
- **R1 (critical): bit-exact replay.** Prove first (phase 1). If it fails, fall back to
  screenshot-oracle + replay-for-analysis.
- **R2: proxy-DLL loadability** for the retail unpacked exe (does the loader pick the
  local d3d8.dll? does SteamStub/the unpack interfere?). Validate in phase 1.
- **R3: resource-capture overhead** (hashing every lock). Measure; expected cheap given
  dedup, but Lock-heavy frames need a fast hash + a "dirty range" shortcut.
- **R4: capturing inherited state at a sliced window start** (device state is persistent;
  a window must replay the accumulated state). Snapshot full device state at window open.

## Phased build (de-risk first; commit in logical units; `/clear` at boundaries)
- **P0 — Replay-fidelity spike (riskiest first).** Minimal proxy d3d8.dll that logs the
  full call+resource stream for the PORT only, for a few frames, + a replayer + a
  screenshot-equality check. **Acceptance: one real frame replays bit-exact.** Go/no-go on
  the whole replay bet. *(Also answers R2 if extended to retail.)*
- **P1 — Capture-at-scale (the 3D-test learnings).** LANDED: local-disk writes,
  `GetBackBuffer`-aligned frame targeting, **single-frame capture** (rewind per frame; keep
  only the trigger frame). **Verified bit-exact on the title with NO 0→N history (44 calls,
  fresh device) → frames are self-contained, so a device-state snapshot is likely
  UNNEEDED** (R4 downgraded). The remaining blocker for capturing a frame *past a long
  load*: per-frame **resource snapshotting** throttles the engine through the
  multi-thousand-frame load. **Fix = two-section container** — capture each frame's calls
  cheaply (rewinding), snapshot each resource ONCE into a persistent cache (not per frame,
  not rewound), and at finalize write `[resources][calls]` so the streaming replayer still
  sees resources first. THEN re-run the 3D HOUSE + multi-scene tests to bit-exact (proves
  the VB/IB path + scene transitions). Measure real resource volume (closes E1). Retail
  full-extent capture + cache.
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
