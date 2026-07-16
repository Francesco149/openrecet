# Trace Studio v3 — "capture the render program, not its output"

> **Status:** ACTIVE PARITY PLATFORM; core capture/replay/viewer landed and v2 retired
>
> **Last status correction:** 2026-07-16
>
> **Operational guide:** `../trace-workflow.md`
>
> **Open preservation-grade proof/capture gaps:**
> `parity-evidence-roadmap.md` EP and GX workstreams

This file is the detailed v3 design/build log. Early entries below retain their dated
“design/de-risking” wording as history; do not read them as current platform status.
Experiments live in `runs/studio-v3-experiments/`.

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

**P1 TAIL — full-extent MULTI-FRAME capture + content-hash dedup ✅ DONE (PORT, 2026-06-12,
`da5f601`).** The single-frame proxy is generalized to a windowed multi-frame container —
the storage model the retail-once-cached/sliced loop (P2) needs. A real HOUSE 3D free-roam
**WINDOW of 48 consecutive frames** (caprange LOADING_END+120..168, past an ~8800-frame load)
captures into **ONE 27.6 MB container** and **EVERY frame replays 0 px / 0 byte** vs its
proxy reference — **48/48 BIT-EXACT**. The dedup win is PROVEN (the "full-extent is cheap"
claim): content-hash (fnv1a-64) resource dedup keeps the resource store at ONE frame's worth
— `48 res total` stays CONSTANT across all 48 KEEP lines, so 48 frames cost 27.6 MB where the
unique resources ALONE are 26.6 MB (adding 47 frames is +1 MB of call deltas; a 480-frame
window would be ~the same resources + ~10 MB). Mechanism: `write_frame` per kept frame
snapshots its bound resources (dedup'd) + writes `[new RES][scalar preamble][calls][Present]`,
doesn't close (keeps appending), fflush per frame (kill-safe; replayer tolerates a missing
EOF); session-wide content-hash id space (dropped the ptr-dedup cache + per-frame reset);
preamble written straight to the container only for kept frames (load frames cost nothing).
GetBackBuffer keeps EVERY caprange frame (port MULTI mode); the retail capframe present-count
path stays single-frame (R2 regression BIT-EXACT). Replayer renders any kept-frame INDEX
(one pass: create all RES, issue only the target section). Committed driver
`tools/trace_studio_v3/port_capture.py` (stage proxy → scenario-test → pull from LOCALAPPDATA
→ replay every frame + assert bit-exact + report dedup); `inspect_cap.py` multi-frame aware.

**CORRECTION — port MULTI drive was leaking ~8 GB of unused BMPs/run ✅ FIXED 2026-06-18
(`ec6b494`).** The MULTI keep-trigger above ("GetBackBuffer keeps EVERY caprange frame")
piggybacks on the exe's OWN per-frame screenshot readback (`capture_backbuffer()` →
GetBackBuffer + LockRect + BMP `fopen`/write), because `scenario-test` always passes
`--capture-to`. But v3 reconstructs frames from the DRAW-CALL stream (`v3cap.bin`); the
proxy only needs the GetBackBuffer **call** (its `write_frame` runs in the hook, from the
shadow). So every port drive was dumping the whole window as v2-style BMPs (~1800/run,
1024×768×4 ⇒ ~8 GB) that nothing in v3 reads — the exact pixel-dump v3 exists to kill.
Fix: `--capture-trigger-only` (src/main.c) — `capture_backbuffer()` fires GetBackBuffer
(proxy keeps the frame) then returns, skipping the readback + write; `--capture-to` stays
(lockable backbuffer + paths unchanged). Opt-in, threaded `scenario-test` → `port_capture`
(v2/golden BMP runs untouched; `--capture-frames` spot-checks still write their handful).
Proven: `house-customer-tutorial` port drive keeps 2699 frames (139 MB vs 8097 MB of raw
pixels), replay 2699/2699 BIT-EXACT, **0 BMPs** (was 1752–1815); the bare drive is 29 s
(was an 8-min duration-ceiling kill). **Retail was already clean** (armwait +
OrV3ArmWindowAt present-window keep, no GetBackBuffer trigger).

**P1 TAIL — RETAIL present-WINDOW keep mode ✅ DONE (2026-06-12).** The single-frame retail
present-count path is generalized to a **WINDOW** `[capframe, capframe+capcount)`: the proxy
keeps EVERY present in the window into the SAME multi-frame container the port uses, drops the
rest, finalizes (EOF) after the last. This is the retail counterpart of the port's GetBackBuffer
MULTI trigger — retail does **not** read back per frame, so the window is addressed by
**present-count**, not by the app's own readback. Proven bit-exact on BOTH sides:
- **PORT (local, no Frida):** `port_capture.py --window 944:44` re-captures the SAME HOUSE 3D
  caprange frames via the present-count window (instead of GetBackBuffer) → **44/44 BIT-EXACT**,
  per-frame call-bytes CONSTANT (~42 KB — the new per-frame `cb_reset` fixes the old
  single-frame path's silent frame-0..N call accumulation), `48 res` dedup constant.
- **RETAIL (Frida spawn):** `retail_capture.py --window 120:48` captures a **48-frame TITLE
  window** into ONE **8.7 MB** container → **48/48 BIT-EXACT**, and the 48 references are **48
  DISTINCT frames** (the title animates — a real multi-frame test, not 48 static copies),
  6 UP-draws/frame CONSTANT (288 total, no accumulation), 5 textures dedup'd, backbuffer
  flags=0x0 (retail non-lockable, CopyRects readback). Finalized in **1.69 s** under turbo.

Mechanism: `capframe`/`capcount` via `v3proxy.cfg` (or env / a runtime arm); one `cb_reset` per
present (after a kept `write_frame`, or to drop a non-window frame) keeps `g_cb` to the CURRENT
frame only. Committed: the proxy WINDOW branch (`d3d8_proxy.c`), `port_capture.py --window`, and
the retail driver `tools/trace_studio_v3/retail_capture.py` (stage proxy + cfg → Frida spawn
turbo → poll for FINALIZE → pull → replay every frame + assert bit-exact). **The capture
mechanism for retail full-extent is now PROVEN.**

**P1 TAIL — ANCHOR-RELATIVE arming ✅ DONE on the title (2026-06-12).** A cfg-fixed present-count
only reaches the deterministic-early title; a post-load gameplay window's present-count is
nondeterministic (turbo load-stretch), so the harness arms the proxy LIVE at an anchor. Two
delivery paths, both proven bit-exact:
- **`OrV3ArmWindowAt(start,count)` proxy export** (WINAPI/stdcall, undecorated via `--kill-at`):
  `retail_capture.py --arm 120:48` calls it via Frida `NativeFunction` before resume → 48/48
  BIT-EXACT. Confirms the export ABI AND that d3d8.dll is a static import (findable pre-resume).
- **Agent IN-PROCESS arm** (`config.v3_arm = {anchor, offset, count}`): the agent's `sendAnchor`
  calls `OrV3ArmWindowAt(anchor_frame+offset, count)` the first time the anchor fires — zero IPC
  latency (no Python round-trip burning frames), so `offset>0` ⇒ armed well before the window
  starts. `retail_capture.py --arm-anchor BOOT+120:48` → the agent armed `BOOT@frame 0 ->
  [120,168)` → 48/48 BIT-EXACT. The agent edit is gated on `config.v3_arm` (null default) ⇒ a
  silent no-op for every v2 capture (node-syntax-checked; this run exercised it end-to-end).
**P1 TAIL — HOUSE-DRIVE retail full-extent capture ✅ DONE (2026-06-12, `b034849`). P1 IS
COMPLETE.** Drove the REAL retail exe to the HOUSE (save-virtualized + input-segtrace) and armed
the proxy at `HOUSE_FREEROAM+120` for a real post-load 3D free-roam window — combining the two
proven paths (port 3D multi-frame `da5f601` + retail single-frame R2 `fe3722a`) into one. Result:
**HOUSE_FREEROAM fired at retail present 13912** (the ~13k-frame load-stretch E3 predicted, vs the
port's 824), the agent armed `[14032,14080)` IN-PROCESS 120 frames ahead, and **all 48 frames
replay 0 px / 0 byte — 48/48 BIT-EXACT**, 29.3 MB container. Three scoped pieces, all landed:
1. **`frida_capture` `v3_arm` plumbing (gated):** a `v3_arm` field on `CaptureConfig` + `run_capture`,
   emitted into `init_cfg` (implies `anchor_trace`). None default ⇒ a silent no-op for every v2
   capture. Lets the FULL scenario machinery (save-virt, segtrace, turbo, resolution/RNG pins) carry
   the anchor-relative proxy arm the agent already supports (`config.v3_arm` → `OrV3ArmWindowAt`).
2. **Proxy `armwait=1` cfg key:** through the long pre-anchor load `g_capframe` is unset, so the
   GetBackBuffer MULTI keep-trigger would mis-keep a stray readback as a bogus load frame. `armwait`
   SUPPRESSES that trigger entirely ⇒ the proxy keeps NOTHING until the in-process arm sets the
   present-window; only the armed window survives. Port MULTI path unaffected (gated on `!armwait`;
   `port_capture.py` regression re-ran **48/48 bit-exact**). The segtrace's own `{caprange}` v2
   readbacks (if any) become harmless non-keeping reads ⇒ no trace stripping needed.
3. **`house_capture.py` driver:** load the segtrace scenario, resolve `{savefile}` → sandboxed
   `save_ref`, stage proxy + `armwait` cfg, `run_capture(... v3_arm={anchor:'HOUSE_FREEROAM',
   offset:120, count:48} ...)`, pull from `%LOCALAPPDATA%` + replay every frame + assert bit-exact.
   Reuses the `retail_capture` helpers (replay verify, localappdata, resolution). HOUSE_FREEROAM is
   the robust anchor: it fires ONCE, on the same frame as the final LOADING_END (proven: port
   anchors LOADING_END@824 == HOUSE_FREEROAM@824), so `HOUSE_FREEROAM+120` == the port's
   `LOADING_END+120..168` window. (A multi-minute retail run — the load-stretch + post-window
   over-run; the latter is killable by P2's window-aware early-exit.)
**Next — P2: the content-addressed slice cache → sync-by-identity** (capture retail's window ONCE,
key by trace-hash, slice sub-windows with zero re-drives; + window-aware early-exit to kill the
post-window over-run). Then the E3 stored-`(anchor,offset)` pairing becomes the real alignment
authority. See P2 below.

**P2 IN PROGRESS (2026-06-12) — window-aware early-exit + sync-by-identity + slice cache, all
proven on real port+retail HOUSE captures.** Four pieces landed:
1. **Window-aware early-exit (`1f54dd8`).** When the agent arms the v3 window it now schedules a
   shutdown 2 frames past the window end (the proxy present-counter and the agent frame-counter are
   the same Present clock — the bit-exact landing proves it), reusing the `max_frames_reached`
   teardown. The HOUSE drive stopped at frame 14158 instead of grinding to max_frames 22000 (~7800
   frames of E4 over-run gone) — **48/48 BIT-EXACT in 53 s** (was multi-minute). Gated on an armed
   v3 window ⇒ a no-op for every v2 capture.
2. **`orv3.py` — Python container reader + bit-exact slicer.** Parses the flat container into
   per-frame sections (present-count, byte range, resources referenced/defined) and re-emits any
   sub-window `[a,b)` as a STANDALONE container, pulling forward content-hash-dedup'd resources
   first defined before the slice. Proven: slice `[10,20)` of the retail HOUSE container → 10-frame
   standalone container, frame 0 replays **0 differing bytes** vs the original ref_010. (Slicing is
   logical for the replayer — it renders any kept index from the full container — but the re-emit
   makes a sub-window a self-contained cache unit.)
3. **`v3cache.py` — content-addressed cache + STORED identity.** Copies a finished proxy capture
   (transient `%LOCALAPPDATA%`) into a keyed persistent entry (`runs/studio-v3-cache/<scen>-<key>/
   {port,retail}/`) + a `v3meta.json` identity. The key hashes ONLY retail's pixel-determining
   inputs (trace + arm spec) so a port-side fix never invalidates the retail cache. Kept frame k's
   identity = `(anchor#occ, offset0+k)`.
4. **`orv3_sync.py` — the sync-by-identity JOIN (the v3 alignment authority).** Pairs port↔retail
   by stored identity. **Proven on the real HOUSE window: 48/48 ALIGNED, 0 honest gaps** — port
   present 619..666, retail 14108..14155, a **+13489-frame load stretch**, yet every frame pairs by
   `(HOUSE_FREEROAM, offset 120..167)`. Contrast: **naive absolute-present pairing = 0/48** (zero
   shared presents — the v2-class frame-number scheme is hopeless across the load stretch). The
   join writes `pairs.json` (computed once, read by the future diff/seek/state/marks). Both capture
   drivers (`house_capture.py`/`port_capture.py`) now auto-cache with identity on success.
5. **`orv3_slice.py` — slice-serve a cached sub-window, ZERO re-drive (the cache win).** Given a
   cached entry + a sub-window in identity-offset space (`--window 130:20`), it re-emits those
   frames as a standalone container, copies the 0-based references, writes a sub-window meta, and
   replay-verifies bit-exact — no proxy/Frida/retail. **Proven end-to-end: the full re-window loop**
   — re-window to offsets 130..149 → slice BOTH cached sides (instant) → join → **20/20 ALIGNED**
   (retail slice **20/20 bit-exact**), with **zero re-drive of either side**. A re-window that cost
   a multi-minute retail drive in v2 is now instant (+ an optional verify). This is v2's
   `--only port` generalized so a *window change* no longer forces a retail re-drive.
6. **`orv3_window.py` — the capture-once/slice-many WINDOW LOOP (auto-drive). ✅ DONE (2026-06-12).**
   The single command that ties 1–5 into the loop a human runs while iterating: `orv3_window.py
   <scenario> --window OFFSET:COUNT`. Per side, INDEPENDENTLY: ask `v3cache.find_extent` "is the
   window already in a cached full-extent for (scenario, anchor), captured from the CURRENT trace?"
   — HIT ⇒ `orv3_slice.slice_entry` (instant, zero re-drive); MISS ⇒ drive the full caprange extent
   (`house_capture.py` retail / `port_capture.py` port) then slice. Then `orv3_sync.sync_entries`
   JOINs the two sub-window slices → `pairs.json` + verdict. Two correctness guards make it
   trustworthy (kill the v2 "filenames silently lie" class): (a) **dir-key re-hash** — `find_extent`
   reconstructs the arm from the stored meta (anchor/offset0/count) and requires
   `cache_key(current_trace, arm)` to still equal the dir's key, so an edited trace can NEVER match a
   stale entry; (b) **port freshness** — a rebuilt `build/openrecet.exe` (mtime newer than the cached
   container) marks the cached PORT pixels stale ⇒ re-drive the port (retail untouched). **Proven on
   the real HOUSE cache, all paths:** re-window `130:20` → **pure cache slice, nothing re-driven,
   20/20 ALIGNED**; full-extent `120:48` → "full-extent (no slice)", 48/48; out-of-extent `110:20` →
   clean error at the caprange; `--force-port` → **"drove only: port"** (port re-driven 48/48,
   retail sliced 20/20 from cache, joined 20/20) — the v2 `--only port` loop, now immune to window
   changes too. `find_extent`/`pick_extent`/`extent_contains`/`dir_key` are hermetic-unit-tested
   (`test_orv3.py::test_extent_lookup`: containment, widest-pick, stale-key + wrong-anchor filtered).
   `slice_entry`/`sync_entries` were factored out of the CLIs (behavior-preserving) for the loop to
   reuse.
**P2 ✅ COMPLETE (2026-06-12).** Capture-once/slice-many + sync-by-identity + window-aware early-exit
+ the auto-drive loop are all PROVEN end-to-end on real port+retail HOUSE captures. Usage note:
capture a *generous* full-extent (the loop's extent = the scenario's `{caprange}`; here 48 frames
already serves a 20-frame sub-window — a wider caprange serves more re-windows). The storage win is
scene-dependent — the HOUSE binds every resource each frame so a slice keeps the full resource set;
a scene-transition session would drop out-of-window resources. **Next → P3 (the viewer).**

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
    captured/replayed bit-exact.
  - **P1 TAIL — full-extent MULTI-FRAME capture + content-hash dedup ✅ DONE (PORT,
    `da5f601`):** a 48-frame HOUSE 3D window → ONE 27.6 MB container, 48/48 BIT-EXACT,
    `48 res total` constant across all frames (resources stored once; frames ≈ free).
    Driver `port_capture.py`.
  - **P1 TAIL — RETAIL full-extent capture ✅ DONE — P1 COMPLETE.** Present-WINDOW keep mode
    (`retail_capture.py --window/--arm/--arm-anchor`, title 48/48) → anchor-relative arming via
    `OrV3ArmWindowAt` + the agent in-process arm → and finally the **HOUSE-DRIVE** (`b034849`):
    `house_capture.py` drove retail through the save-load to the house and armed at
    `HOUSE_FREEROAM+120` (fired @ retail present 13912) for a real post-load 3D window —
    **48/48 BIT-EXACT**, via `frida_capture`'s gated `v3_arm` + the proxy `armwait=1` idle-until-arm
    cfg. The retail full-extent capture mechanism is proven end-to-end on a real 3D scene. The
    content-addressed slice cache (slice-don't-re-drive) moves to P2 below.
- **P2 — Sync-by-identity + the slice/cache loop + window-aware early-exit.** Port the
  E3 prototype into the real pairing authority. Includes the **content-addressed retail slice
  cache** (capture retail's window once, key by trace-hash, slice sub-windows with zero re-drives)
  + a **window-aware early-exit** (stop the retail drive at the last in-window frame — kills E4's
  ~100 s post-window over-run the house drive currently pays).
  **✅ COMPLETE (2026-06-12) — see the "P2 IN PROGRESS" block above.** Landed + proven on real port+
  retail HOUSE captures: the **window-aware early-exit** (`1f54dd8`, 48/48 in 53 s), the
  **`orv3.py`** reader + bit-exact slicer, the **`v3cache.py`** content-addressed cache + stored
  identity, the **`orv3_sync.py`** identity JOIN (**48/48 ALIGNED vs 0/48 naive** across a
  +13489-frame load stretch), **`orv3_slice.py`** slice-serve (re-window → slice both cached sides →
  join **20/20 ALIGNED**, ZERO re-drive), and finally the **`orv3_window.py`** auto-drive loop — one
  command that slices a cached full-extent when the window is in-extent (pure-slice re-window:
  nothing re-driven, 20/20 ALIGNED) and drives ONLY what's missing/stale (`--force-port` → "drove
  only: port", retail sliced from cache), guarded by a dir-key re-hash (never serve a stale-trace
  entry) + a port-exe-mtime freshness check (auto-detect a rebuild). **Next → P3 (the viewer).**
- **P3 — Viewer. PIVOTED to a NATIVE viewer (user call, 2026-06-12) — N0/N1/N2 DONE,
  user-confirmed.** A first **web** prototype (orv3_view PNG-bake + orv3_serve + the
  preact SPA, `web/`) works and is KEPT as a fallback, but the user rejected it: the PNG
  bake reintroduces v2's pains (stale intermediates, ~150 MB of duplicated pixels, a
  ~150 ms/frame encode that caps the faster-than-realtime replay). The native viewer
  replays the container ON DEMAND — the container is the only artifact, no bake.
  - **Stack:** C++ / Dear ImGui (Win32 + DX9 backends) / mingw **i686** (32-bit so the
    process matches the real 32-bit d3d8.dll the replay core loads). d3d9 hosts the UI;
    two d3d8 replay cores (port + retail) render frames into d3d9 textures. ImGui +
    nlohmann_json pinned via `flake.nix` ($IMGUI_SRC / $NLOHMANN_JSON_INC). Headless
    `--shot out.bmp` mode renders one UI frame offscreen → BMP, so the build loop
    self-verifies with no display (pushed to the feed each step).
  - **N0 (`65bcdd7`-era spike):** ImGui+d3d9 window builds + renders headless under
    WSL→Windows. **N1:** `replay_core.{c,h}` — the proven replayer factored RESIDENT
    (device + all 26 MB resources created ONCE at open; render any frame on demand);
    replay.exe slimmed to a thin CLI over it, 48/48 HOUSE regression still BIT-EXACT.
    **PERF PROOF (settles the wiring-overhead question):** resident per-render = best
    **1.42 ms** / mean 2.95 ms (1083 calls + readback, 1024×768) vs ~620 ms cold — ~200×;
    two panels + diff + upload < one 16 ms frame ⇒ 60 fps+ scrub, faster than realtime.
    The d3d8→d3d9 texture bridge gotcha: the alpha-less backbuffer needs an **X8**R8G8B8
    texture (an A8 one draws it fully transparent). **N2:** the full 3-panel viewer
    reading `view.json` (orv3_view `--native`: the identity-join timeline + container
    paths, NO PNG bake) — port|retail|diff panels (diff CPU-computed, gt8/meanabs same law
    as pixel_diff), diff ribbon (heat/click-seek/worst-next, metrics precomputed per
    column), per-frame state table (present/draws/calls retail-vs-port, diff-highlighted —
    surfaces the 125-vs-98 draw structural divergence), scrub + ,/. arrows Home/End + 1/2/3
    toggles, honest-gap panels. **Synced ENTIRELY by stored identity** (no align/renumber/
    seam). **All three user-confirmed 2026-06-12** ("viewer looks great", "diff and state
    panel look great too").
  - **N3 — semantic diff/pick layer ✅ DONE (2026-06-13).** "Which draw/state/texture
    differs", the v2-blind divergence. Five committed units, all feed-demoed + user-praised:
    - **N3a `render_range(idx,lo,hi)`** (replay_core) — draw isolation: issue only draws in
      [lo,hi), every state/clear/scene call still issued. [0,K)=prefix (render_upto delegates),
      [J,J+1)=one draw over the clear. Full-frame regression still BIT-EXACT.
    - **N3b `orv3_draws.py`** — enumerate a frame's draws WITH the device state in effect
      (bound tex/VB/IB/FVF + the render/stage states that decide if/how each paints),
      cross-side-keyed by CONTENT hash (not the per-container id). **material_diff** verdict
      ALIGNED / BATCHING / DIVERGENT from per-texture triangle totals (batching-robust); baked
      lean into view.json (orv3_view).
    - **N3c/e viewer** — a **draw-step** slider (prefix build-up), a **solo** toggle (isolate
      one draw via render_range), a **draw-program panel** (verdict + the divergent textures),
      and **pixel→draw pick** (click a panel pixel → linear-scan the prefixes → the owning
      draw, auto-solo'd). Headless `--col/--draw-step/--solo/--pick` self-verify.
    - **N3d `orv3_window --view/--launch`** — the one-command loop: drive/slice/sync →
      view.json → open the viewer (detached). 1.2 s on the cached HOUSE window.
    - **THE HEADLINE (HOUSE, v2-invisible):** the port-98 / retail-125 draw gap on a
      pixel-bit-exact frame = **26 batching splits** (per-texture triangle totals IDENTICAL —
      retail splits what the port batches) **+ 1 genuinely-extra retail draw** (texture `ea99`,
      80 tris, drawn first, SRCALPHA blend w/ effective src-alpha 0 + ZENABLE off ⇒ **0 px**,
      proven in true isolation; the port omits it). Pixels ALIGNED, render-program DIVERGENT.
    - **Perf:** the material-diff bake was re-hashing the ~26 MB resource set per-frame ×96
      with pure-Python fnv1a (2.5+ MIN); a shared per-container C-speed blake2b `ResHash`
      → **0.41 s** (350×) — needed before N4's thousands-of-frames trace.
  - **N4 — the THOUSANDS-of-frames stress ✅ DONE (2026-06-13)** on the new
    **`guild-ui-flow`** scenario (= the v2 `merchants-guild-ui-flow` session's working
    trace promoted to `tests/scenarios/`; caprange `[330,2600]` from `LOADING_END#1`,
    multiple mid-window load seams, canonical pins). Two scale walls were predicted,
    hit, and fixed BEFORE the big drive (each regression-proven on the HOUSE cache):
    - **Hash references + resident batch verify** (`92cce65`): 2600 frames × 3 MB raw
      refs ≈ 8 GB/side and a replay.exe spawn per frame don't scale. Proxy `refhash=1`
      writes one fnv1a-64 line per kept frame to `v3refs.txt` (+ a raw every
      `refraw_every` for forensics); `replay.exe --verify-hashes` renders EVERY frame
      on the resident core in one process. 2600-frame verify = seconds, refs = 155 KB.
    - **Per-frame MULTI-ANCHOR identity + stored arm spec, meta v2** (`1e03b72`): the
      single-anchor `offset0+index` identity silently mispairs everything after the
      first mid-window seam (port suppresses load frames ⇒ kept set non-contiguous;
      retail keeps every present), and `find_extent` rebuilding the arm from the KEPT
      count meant a suppressed-load window could never match its own cache dir.
      v3meta now stores the run's full anchor stream + the arm verbatim; each kept
      frame's key resolves from its OWN present (most-recent anchor ≤ present — the
      E3 design, finally per-frame). Same-frame anchor aliases tie-break to the
      entry's BASE anchor (symmetric + legacy-compatible; the HOUSE regression caught
      0/20 → 20/20). Viewer columns are key-ordered, labeled `ANCHOR#occ+delta`.
    - **The stress numbers:** PORT 1785 kept / 58 MB / **1785/1785 bit-exact** (raw
      pixels would be 5.4 GB). RETAIL **2600/2600 kept** / 91.2 MB / **2600/2600
      bit-exact** (vs 7.8 GB), one drive, early-exit. JOIN: **1784/1785 paired**
      across a **+13,272-frame** load stretch and **63 anchor segments**; every gap
      NAMED and explained — 798 = the port's TAS replay ends before the armed window
      (the v2-known "retail triggers past the port replay end"), 18 = load screens
      the port suppresses, 7 = a one-frame anchor-alias skew at one PAUSE_CLOSE seam.
      Naive present pairing: **0/1785**. **Pairing correctness proven visually**:
      paired frames deep past many seams (LOADING_END#2+50, TEXT_ANIM_START#10+5,
      DLG_LINE_CLEAR#12+20) are **bit-identical (gt8=0)**; the menu pair shows only
      the v2-known accepted cursor-bob/sparkle phase residue. Viewer opens the
      2601-column view in ~15 s (metric precompute = 2 renders/pair).
    - **NEW v2-invisible findings (semantic layer at scale, 1581 DIVERGENT columns):**
      (1) retail draws a **fullscreen SRCALPHA/INVSRCALPHA overlay quad LAST** (tex
      `…9fd8`, 128×128 stretched to 1024×768, ZENABLE off) on ~1094 columns — the
      port omits it; net pixel effect sub-gt8 (paired diff ≈ 0). The `ea99` class,
      but persistent — RE which engine layer draws it (scene tint/fade?) and whether
      the port needs it. (2) the **port double-draws the guild background** (tex
      `…2780` 1024×512: port 2 draws/4 tris vs retail 1/2) on ~1076 columns —
      pixel-invisible overdraw, port-side cleanup lead.
    - **Live viewer at 2601 columns ✅ USER-CONFIRMED 2026-06-13** ("works perfectly
      and scrubs instantly") — the resident-replay scrub model holds at thousands of
      columns.
    - **Process findings / follow-ups:** ~~the retail drive's wall-clock is DOMINATED
      by the v2 caprange machinery (≈2.5k PNG conversions + 287 montages ≈ 5 of the
      ~13 min) — add a v3 drive flag to skip frame/montage baking~~ → **✅ DONE
      (`4f7cfed`):** a v3 drive (`run_capture(v3_arm=…)`) strips `{caprange}`/`{capstride}`
      so the agent writes ZERO v2 frames/montages (the container + hash refs are the only
      v3 artifacts); ~~the CACHED re-window loop is ~5 min at
      2600 columns~~ → **CACHED re-window loop ✅ DONE (2026-06-13):** the loop was
      already ~10 s (no-slice) once the ResHash blake2b fix landed — the "~5 min" was a
      pre-ResHash figure — but the 91+58 MB containers still re-parsed in pure Python
      ~3× per side (sync, view._side_index, view's internal sync) AND the material-diff
      bake built full Draw lists per column (hashing geometry the verdict discards). Two
      fixes, both behavior-preserving (view.json + pairs.json **byte-identical** before/
      after): (1) **parse-once container handoff** — `v3cache.LoadedSide`/`load_side`
      parses meta+container+identity-index ONCE; `orv3_window` threads the SAME object
      through `sync_entries` → `write_view_json` (which re-calls sync), each accepting a
      LoadedSide OR a Path (`as_side`, idempotent). (2) **material aggregate bake** —
      `orv3_draws.material_agg` walks a frame to `{tex_hash:[tris,draws]}` directly (no
      Draw objects, no geo_hash UP byte-loops, no rs/tss copies — all discarded by
      `material_diff`), feeding a shared `_material_report`. **Numbers (2600-col guild
      pair):** the per-column bake **6.71 s → 0.36 s (~18×)**; sync+view **compute
      8.98 s → 1.40 s (~6.4×)** (sync 0.74→0.07, view 8.24→0.62, +0.71 single parse);
      end-to-end loop 10 s → 7 s (remainder = fixed nix/python/numpy-import startup).
      Guarded by `test_material_agg` (fast bake == enumerate+material_diff over all
      frame pairs incl. UP draws, ALIGNED+DIVERGENT) + `test_load_side` (parse-once /
      idempotent). The slice path benefits too (sync+view re-parse gone, 18× bake); its
      residual cost is the replay verify, not parsing. *(A baked-draws cache was the
      alternative lever — unneeded now the bake is 0.36 s.)*
    - **Viewer metric precompute ✅ DONE (2026-06-13).** The diff-metric fill (2 resident
      renders + a px loop per column) blocked the open ~15 s at 2601 cols. Moved off the
      open path: `load_view` no longer precomputes; the interactive loop calls
      `pump_metrics(8 ms)` per UI frame (a time-budgeted background slice), so the window is
      responsive instantly and the ribbon colours in over ~1-2 s (header shows "filling diff
      metrics (N left)"). `show_column` still computes the current column eagerly, so
      scrubbing/worst-of-seen are never gated. The headless `--shot` path keeps the
      synchronous `precompute_metrics()` for a full self-verify ribbon. (replay.exe still
      cannot fopen-write `\\wsl.localhost` UNC paths — write to Windows-local scratch.)
  - **GAME-STATE PANEL + `--state` capture ✅ DONE + measured (2026-06-13).** The v2 web
    StatePanel, native + identity-keyed — the engine-state half of the divergence story the
    d3d draw-program panel can't see. `orv3_state.py` is the pillar: `--state` (on
    `house_capture`/`port_capture`/`orv3_window`) captures the 4 once-per-frame flow-trace VAs
    (rng/rngcalls, player+companion px/py/anim, title menu, dialogue) into each side's
    `call_trace.jsonl`, cached as a sidecar (`v3cache`), carried through slices (`orv3_slice`),
    re-driven when a same-key cache lacks it (`orv3_window`). `build_state_rows` keys every
    event by `meta.key_of_present(frame)` — the call-trace frame == the present-count the d3d
    frames + the join use (verified) — so state slots onto the identity timeline with ZERO new
    sync logic and composes across the load stretch. `orv3_view` bakes per-column
    `state{port,retail}` into view.json; the viewer renders a field/retail/port table,
    diff-highlighted, with a name filter + diffs-only toggle (`--state-diffs` headless). Floats
    f32-normalised in the bake (`%.9g` display) so a red row is a REAL divergence (a 1-ULP cx
    gap, the +737 rngcalls phase offset), not f32-repr noise.
    - **Perf (the measure-first ask): negligible, AND it fixed a latent waste.** The
      `{calltrace}` op window-gates the probes to the kept frames (HOUSE: 98 events / 36 KB,
      NONE in the load-stretch; +60 ms / ~1%, within load-stretch noise). Measuring exposed
      that a v3 drive was auto-loading the FULL ~1979-VA call-graph (120k events / ~11 MB,
      NEVER cached) from the trace's `{calltrace}` op — fixed: a v3 drive (`v3_arm`) no longer
      auto-enables call-trace and strips `{calltrace}` UNLESS `--state` (which keeps it to
      window-gate the 4-VA probes). So the default v3 drive is now LEANER; `--state` is far
      lighter than the old default. Opt-in per the user's lean-by-default call.
    - **RNG/phase verdict is a DROP-IN** (the user's "same level as the drill/verdict" ask):
      `flow_diff.py --verdict --align-field db054 --retail <cache>/retail/call_trace.jsonl
      --port …/port/call_trace.jsonl` runs UNCHANGED on the v3 `--state` cache — HOUSE =
      ✅ PHASE-CLEAN (rng 48/48 bit-exact, rngcalls per-frame ALIGNED, the +737 = a constant
      phase offset). The whole `flow_diff` suite (`--field-timeline`, `--rng-drill`) reads the
      v3 traces as-is — no flow_diff changes. **v2 is now fully retired as the working tool**
      (CLAUDE.md's parity-loop bullets point at v3).
  - **NOTES + crop regions ✅ DONE + USER-CONFIRMED (2026-06-13, `db28c34`+`f20b5ea`).**
    The v2 `edits.jsonl` notes loop, native — **the last v2-parity gap**, so v2 is retired
    as the working tool (user call). Note mode (m) → drag a crop box on any panel (or "note
    frame") → type a note; notes overlay as green boxes pinned to their column by **identity
    label** (stable across re-windows) + a seek/del list; pick disabled while arming.
    Persistence dodges the UNC-write limit (the Windows viewer can't fopen-write a
    `\\wsl.localhost` path): notes go to a WINDOWS-LOCAL `%LOCALAPPDATA%\openrecet\v3\notes\
    <scenario>.json` (view.json carries `notes_path`; `v3cache.notes_file` resolves it,
    pre-creating the dir). **`orv3_notes.py`** reads them on WSL: `list` prints the flags;
    `--render [--id N] [--feed]` replays the flagged frame port|retail|diff via `replay.exe
    --upto`, crops to the (padded) box, outlines the region, composes a PNG (+feed) so Claude
    SEES the flag. Cursor/font fix (`f20b5ea`): the interactive viewer sized its d3d9
    backbuffer to the WINDOW (1400x920) not the CLIENT area, so a non-integer Present
    downscale skewed the mouse (clicks landed low) + squished the pixel font — fixed by
    sizing to `GetClientRect` + a `WM_SIZE` reset. Live drag→save→read round-trip + the fix
    both user-confirmed. **The parity loop now runs on v3:** `orv3_window <scen> --window
    OFF:COUNT --launch` → drag notes → `orv3_notes.py <scen> --render`.
- **P4 — Parity-loop parity check** (v2's formal send-off): reproduce a known confirmed-1:1
  session in v3, verdict matches v2. v2 is ALREADY retired as the working tool (user call
  2026-06-13 — notes was the last gap); P4 is the belt-and-suspenders proof before the v2
  code is physically archived. The perf follow-ups above land first (the iteration loop).
  - **CORE CHECK ✅ PASSED (2026-06-13):** `flow_diff.py --verdict --align-field db054` on the
    v3-captured HOUSE cache (`house-loaded-display-pinned-26e5aec3`, `--state` call_trace) =
    **✅ PHASE-CLEAN** — all 33 house_update counters bit-exact/within-eps, rngcalls ALIGNED,
    rng **48/48 frames bit-exact**. This matches v2's verdict on the same canonical pinned
    HOUSE (PHASE-CLEAN), so the v3-captured call-trace feeds the verdict tool equivalently to
    v2 (the tool + format are unchanged from v2; only the CAPTURE source differs). The
    render-program pillar is independently exercised (the guild double-draw fix `2a2d84d` +
    the HOUSE 98-vs-125 draw finding). **Remaining for the FULL formal send-off** (deliberate,
    user-overseen, since it gates physically archiving the v2 code): a byte-level v2-capture-
    vs-v3-capture call_trace comparison on a rich gameplay session (vs the static HOUSE), then
    the archive move itself.

- **P5 — RENDER-TARGET capture (the RT blind spot) ✅ DONE 2026-06-13 — capture + replay +
  history, 240/240 bit-exact on the real pause backdrop.** The proxy/replayer/readers now record +
  reconstruct off-screen render-target effects. **What landed:**
  - **Format (v3, `orv3_format.h`):** `ORV3_RES_RT_TEX` (id,w,h,fmt,levels,usage — an RT texture,
    identified by IDENTITY not content: a DEFAULT-pool RT can't be locked, so no pixels stored, its
    content is produced by the replayed stream), `ORV3_SetRenderTarget` + `ORV3_CopyRects` (citing
    surfaces by a **SURFREF** `[kind][resid]` — NULL/BACKBUFFER/DEPTH/TEX — the replayer rebuilds the
    actual surface from the kind; the app's Get* surface handles aren't replayed).
  - **Proxy (`d3d8_proxy.c`):** `CreateTexture` custom (RT registry of `usage&RENDERTARGET`
    textures); `SetRenderTarget`/`CopyRects` custom (classify each surface ptr via the cached real
    backbuffer/depth + `IDirect3DSurface8::GetContainer` for texture-backed RTs — recovers the parent
    of a surface the app got via `GetSurfaceLevel` at init, which the proxy never saw); `snap_rt_tex`
    writes `RES_RT_TEX` once by registry; the RT-tex resid is deferred-patched like other binds. The
    proxy's OWN readback (`orv3_readback_bgra`) runs on `w->real` ⇒ never recorded.
  - **Python (`orv3.py`):** parse the 3 ops (correct byte-sizing so slice/sync don't break); an
    RT-tex SURFREF counts as a resource REFERENCE so the slicer pulls the RES_RT_TEX forward;
    `tex_info` reports RTs. `orv3_draws.py` skips the new ops (the per-column material bake survives
    RT containers). **`orv3_rt.py` (new):** dumps a frame's RT command program (SetRenderTarget /
    CopyRects / Clear / draw-runs per target, marking RT-sampling draws) — the "read the mechanism"
    tool; `--scan` lists RT-using frames.
  - **Replayer (`replay_core.c`):** create RTs (DEFAULT pool, RENDERTARGET usage); resolve SURFREFs
    (GetBackBuffer / GetDepthStencilSurface / GetSurfaceLevel) and honor SetRenderTarget + CopyRects;
    **`orv3_replay_render_history(idx)`** replays [0..idx] cumulatively so cross-frame RT content
    fills (the per-frame render shows the [0] backdrop empty). `replay.exe --history <idx>` exposes it.
  **Proof:** the re-driven retail `house-pause` (v3 RT container) **verifies 240/240 bit-exact**
  (`--verify-hashes`; the in-ORDER sweep accumulates RT content for free, so even the capture frame 40
  + composite frame 41 + every resting frame match the proxy's reference fnv64). History-replay of the
  resting menu frame 119 = the REAL darkened/blurred-house pause backdrop (the per-frame replay = empty
  garbage); the 40 non-RT house frames (0-39) in the same container stay bit-exact ⇒ the non-RT path is
  unaffected (HOUSE/title regression safe). The decoded pause capture/blur mechanism: `plans/pause-menu.md`
  M3 + quirk §123.
  **Original gap (for the record):** the proxy `fwd_`-forwarded the RT calls (pass-through, NOT
  recorded) and the op enum had no such ops, so an off-screen-RT effect — **captured-screen backdrops
  (the pause-menu [0], `DAT_073de648`), radial-blur / zoom transitions, post-processing** — replayed
  EMPTY (pause [0] tex `3e66` 1024×768 datalen=0, `replay --upto 119 1` = pure black). HOUSE/title/guild
  are bit-exact because they don't use RTs; the pause backdrop is the first RT effect we hit.
  **Why it matters:** porting these 1:1 (no-guess, verifiable) per THE PORTING LOOP needs the v3
  tools to SHOW + DIFF the RT draw program — decompile alone is the "ship render on RE alone" trap
  (cf. the sold-out-text colour miss). **Design (the extension):**
  - **Surfaces become first-class.** `GetRenderTarget`/`GetBackBuffer`/`GetSurfaceLevel`/
    `CreateRenderTarget` hand back `IDirect3DSurface8*`; the proxy must map each surface handle →
    (texture-id, level) or (backbuffer) so `SetRenderTarget`/`CopyRects` can be recorded by stable
    id, not pointer. Wrap or side-table the surfaces.
  - **New format ops (v3):** `ORV3_CreateRenderTarget` (id, w, h, fmt), `ORV3_SetRenderTarget`
    (color-surface-id, depth-surface-id), `ORV3_CopyRects` (src-surf-id, rects, dst-surf-id,
    points). RT textures created with `D3DUSAGE_RENDERTARGET` + `D3DPOOL_DEFAULT` (can't be MANAGED).
  - **Replayer:** create RTs with RENDERTARGET usage; maintain the surface→target map; honor
    SetRenderTarget (redirect) + CopyRects (surface blit) + restore the backbuffer at frame end.
  - **Cross-frame RT content = a HISTORY/cumulative render mode** (`replay --history-upto N`: replay
    sections [0..N] on the one resident device so an RT filled at the open-ramp frame is still
    populated at the rest frame N). Needed because the per-frame independent render won't have run
    the fill frame. (orv3_shot's `--history` was removed pending this.)
  - **Verify:** the HOUSE/title regressions stay bit-exact (no RTs ⇒ no behaviour change); a pause
    rest frame's [0] then replays the real captured-darkened-house instead of black, and the
    draw-program panel shows the RT draws.
  **Interim ground-truth (no extension):** the proxy's per-frame backbuffer READBACK (`--raw-refs`)
  stores the REAL composited frame (RT content included, since the menu draws the RT onto the
  backbuffer); read `v3ref_NNN.raw` directly to SEE retail's true backdrop for analysis/appearance,
  even before the replayer can re-render it. (The native viewer re-renders, so it shows black until
  P5 — a known viewer limitation for RT effects.) This is the fast path to confirm the pause
  backdrop appearance; P5 is what makes it structurally analyzable + viewer-faithful.
  **P5 viewer fidelity ✅ DONE 2026-06-13 (`5a9cd05`):** the native viewer re-rendered each column
  with the per-frame `orv3_replay_render`, which shows RT-bound samples black/garbage — so the
  pause backdrop reconstructed WRONG (a near-black transition + a stale-backbuffer "shop exterior"
  fragment), even though the port renders it right in-game (user-flagged scrubbing house-pause).
  Fix: `replay_core` now flags `has_rt` at open + exposes `orv3_replay_has_rt`; the viewer renders
  the full column (display + diff metric) via `orv3_replay_render_history` when the container has
  RT, else the cheap per-frame render (identical for RT-free, O(1) vs O(idx) — large RT-free windows
  stay fast). Confirmed: transition f162 mean 1.5→96.4, resting f119 70.8→113.6. (Draw-step
  isolation stays per-frame — inherent; an RT sample there is still black.) **Gotcha: after any
  `replay/replay_core.c` change, rebuild BOTH `replay/replay.exe` AND `viewer/viewer.exe`** — a
  stale viewer.exe fails container load with "unknown op" the moment a new op (e.g. SetRenderTarget)
  lands in the stream.
  **P5 viewer fidelity — the DEPTH-surfref reconstruction bug ✅ FIXED 2026-06-13 (`941a4ca`):**
  after the history fix the viewer STILL showed a "see-through walls, bright outside at the top"
  artifact when scrubbing to a 3D frame AFTER a pause/blur frame (user-flagged; in-game + the feed
  pushes were correct). Root cause was replay-side, not the port: the blur binds its RTs with NO
  depth (`SetRenderTarget(rt,NULL)`), and `resolve_surface` resolved the DEPTH surfref via the LIVE
  `GetDepthStencilSurface` — NULL while a no-depth RT is bound — so the blur's restore re-bound NULL
  depth and every frame replayed after it on the RESIDENT device rendered with no depth buffer (no
  occlusion). A fresh `replay.exe` process never hit it (no prior blur); the resident viewer core,
  scrubbing many columns, did. Fix: grab the auto depth-stencil ONCE at device-create (while bound)
  + resolve the DEPTH surfref to that stored surface. Reproduced/regression-tested headlessly with a
  new **`replay --hist-warm <warm> <idx>`** mode (render one column, then another, on the SAME
  device — the viewer's resident pattern; warm-vs-fresh must be gt8 0): 3D frame 73%→0, transition
  50%→0, port self-verify still 240/240. **Lesson: the resident viewer core can leak ANY device
  state a single fresh replay never exercises — test cross-render idempotency with `--hist-warm`,
  not just a fresh `--history`.**

## Honest note on "10×"
The 10× is on the **iteration loop**, not one axis: retail-caching + window-early-exit kill
the capture wait; stored-identity kills the sync whack-a-mole; the integrated semantic
viewer kills the multi-tool divergence hunt. The d3d-replay is what makes all three cheap
*together* (compact sliceable cache, stored identity, semantic content) — and it delivers
the user's "re-render exactly as the game did." The one thing it does NOT do is make the
*first* retail drive faster (that's the game's load); caching makes every *subsequent* one
free.
