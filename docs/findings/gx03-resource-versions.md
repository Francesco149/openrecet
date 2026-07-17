# GX-03 — per-draw resource versions (spec + probe verdict)

> **Status:** SPEC + PROBE LANDED 2026-07-17 (roadmap `../plans/parity-evidence-roadmap.md`
> §9 GX-03/GX-04). Resolves the GX-00 arrprobe VB/IB VIOLATION
> (`gx00-d3d-method-census.md`). Probe = `resource_binds` sidecar (proxy
> `d3d8_proxy.c`, kept-frames-only); design = wrap VB/IB + freeze-at-bind versioning
> (GX-04, implemented below). Not "probably static" — **provably complete**.

## The hole (GX-00 §Leads restated)

`CreateVertexBuffer`/`CreateIndexBuffer` are FORWARDED (unwrapped). VB/IB CONTENT is
snapshotted at FRAME-END (`snap_vb`/`snap_ib` in `write_frame`): one snapshot per
distinct POINTER, patched into every `SetStreamSource`/`SetIndices` of that pointer in
the frame. So a buffer bound at draw A (content X), re-mutated (Lock/Unlock), bound at
draw B (content Y) in the SAME frame gets ONE frame-end snapshot (Y) for BOTH binds ⇒
draw A replays with Y ≠ X. The frame-end deferral is a PERF optimization (only kept
frames snapshot; the multi-thousand-frame load costs 0) — sound ONLY if no bound buffer
is mutated between its bind and frame-end. GX-00 fired a fail-closed VIOLATION because
that invariant was unproven.

## Probe verdict (2026-07-17) — the risk surface is ACTIVE but well-scoped

`resource_binds` sidecar (proxy counts per KEPT frame, 0 cost on load frames): binds,
MULTIBIND (a ptr bound >1×/frame = a reused buffer), SNAPFAIL (id==-1: bound buffer
unlockable at frame-end). arrprobe re-drive (cache `524f2c58`, 80/80 JOIN_COMPLETE):

| side   | kept | vb_binds     | vb_multibind (max) | ib_multibind (max) | snapfail |
|--------|------|--------------|--------------------|--------------------|----------|
| retail | 1500 | 84000 (56/f) | 4500 (**3**/f)     | 4500 (**3**/f)     | **0/0**  |
| port   | 1394 | 181220(130/f)| 5576 (**4**/f)     | 4182 (**3**/f)     | **0/0**  |

- **Reuse is real:** 3–4 VB + 3 IB pointers are bound MULTIPLE times per frame on both
  sides (the hazard's precondition — a shared buffer used by >1 draw).
- **0 snapfail:** every bound buffer is alive + lockable at frame-end (NOT the
  transient-release pattern; the pool retains them). Cross-ref: retail stores only **5
  distinct `RES_VB` / 5 `RES_IB` contents** across 84000 binds ⇒ the reused buffers are
  near-certainly STATIC shared templates (re-BOUND, not re-FILLED); the dynamic geometry
  goes through `DrawPrimitiveUP` (105424 inline draws — already fully captured).
- **But NOT proven.** The frame-end/dedup view CANNOT see a mid-frame mutation that is
  overwritten by frame-end (bind X → mutate Y → draw → mutate back X → frame ends X;
  RES_VB shows only X, draw-of-Y is lost). Fail-closed rigor (the whole point of GX-00)
  forbids concluding "static" from frame-end evidence. ⇒ need Lock/Unlock visibility.

## The completeness key (why wrapping is airtight, not heuristic)

**In D3D8, `IDirect3DDevice8::CreateVertexBuffer(Length,Usage,FVF,Pool,ppVB)` has NO
initial-data parameter** (unlike D3D9). A VB/IB's bytes can be written ONLY by:
1. `IDirect3DVertexBuffer8::Lock`/`Unlock` (the app writes into the locked range), or
2. `IDirect3DDevice8::ProcessVertices` (dest buffer) — a DEVICE method, census risk set,
   **0-observed** on arrprobe (GX-00 dynamic census; the gate keeps it 0 or FAILs).

So if the proxy WRAPS every buffer and intercepts every `Lock`/`Unlock`, it observes
EVERY content version — a PROVABLY complete capture, gated on ProcessVertices staying
0-observed. This mirrors the already-`recorded` `CreateTexture` (intercepted → `snap_tex`
captures content); VB/IB just also need the wrapper because their mutator (`Lock`) lives
on the BUFFER interface, not the device.

## Decision — wrap VB/IB + freeze-at-bind versioning (GX-04)

Resolves each GX-03 spec item:

- **Resource identity:** CONTENT-hash (the existing `dedup_or_write` fnv1a over
  `[type][body]`). A resource IS its bytes — pointer-independent, so a freed pointer
  reused for a new buffer is distinguished by content, and two versions of one pointer get
  two ids. No pointer identity stored.
- **Generation:** each wrapped buffer carries `gen`, bumped on each WRITABLE `Unlock`
  (`!(flags & D3DLOCK_READONLY)`). A bind captures the content of the gen CURRENT AT BIND.
- **Freeze-at-bind (the mechanism):** the wrapper maintains a SHADOW of the buffer's
  current bytes, updated at each writable `Unlock` (memcpy from the app's still-valid
  locked pointer — no re-Lock of `real`, can't stall/fail). `SetStreamSource`/`SetIndices`
  FREEZE the current shadow into a per-frame content ARENA (`g_rc`, reset per frame like
  `g_cb`) and record the frozen range in the pending entry. `write_frame` (kept only)
  hashes+dedups+writes the FROZEN bytes → resid → patch. Distinct binds with distinct
  content → distinct resids; identical → one (dedup). Zero cost on dropped frames (arena
  reset, no `g_cap` writes). No frame-end re-Lock (more robust than the old path).
- **Dirty regions / partial updates:** `Lock(offset,size,flags)` writes only
  `[offset,offset+size)` (offset==0&&size==0 = whole buffer); the shadow is updated only
  in that sub-range at `Unlock`, so successive partial writes ACCUMULATE in the whole-buffer
  shadow, and freeze-at-bind captures the accumulated result. No sub-range tracking needed
  in the container (the whole buffer is the unit).
- **Lock flags:** `D3DLOCK_READONLY` (0x10) ⇒ no write ⇒ don't touch shadow/gen.
  `DISCARD`(0x2000)/`NOOVERWRITE`(0x1000)/`NOSYSLOCK`/etc. ⇒ still writable ⇒ shadow
  update + gen bump (DISCARD's fresh content is captured post-Unlock).
- **Pointer reuse:** each `CreateVertexBuffer` makes a NEW wrapper (fresh shadow); a reused
  address = new wrapper. Content-dedup handles cross-frame aliasing regardless.
- **Render-target writes:** N/A to VB/IB (RTs are textures, handled by `snap_rt_tex` +
  replayed draw stream, unchanged).
- **Lifetime/release:** the wrapper is a COM object with its own refcount; app
  `AddRef`/`Release` drive it, `Release`→0 releases `real` + frees the wrapper + shadow.
  Frozen bind-bytes live in the per-FRAME arena (not tied to buffer lifetime), so a buffer
  released mid-frame after its draw still has its bind content captured (though snapfail=0
  says this isn't hit on arrprobe). QueryInterface returns the wrapper for
  IID_IDirect3DVertexBuffer8/Resource/Unknown (identity preserved).

**Census reclassification:** `CreateVertexBuffer`/`CreateIndexBuffer`
render_affecting_unsupported → **recorded** (their effect — buffer content — is now
captured via the wrapper, exactly as `CreateTexture`). The two buffer interfaces
(`IDirect3DVertexBuffer8`, `IDirect3DIndexBuffer8`) are ADDED to the census with
`Lock`/`Unlock` recorded, the rest query_only/wrapper_lifetime, so the drift guard
(generic over `census["interfaces"]`) protects the wrapper vtables too.

## Acceptance

- **GX-03 (mechanism):** a two-draw frame with one intervening mutation yields TWO
  distinct bound versions + a deterministic (content-addressed) container repr. Proven by
  the freeze→dedup unit test (distinct bytes → distinct ids; identical → one) + the real
  arrprobe re-drive (the wrapper sees the reused buffers; if any mutation occurs it
  produces >5 contents, else confirms static).
- **GX-04 (wrap):** same-frame mutation, partial lock, pointer reuse, reset, and the
  existing HOUSE/title replay all pass (same-side hash-verify stays bit-exact ⇒ wrapping
  is transparent); arrprobe census dynamic verdict VIOLATION → SAFE (or catches a real
  mutation).

## GX-04 LANDED 2026-07-17 — wrap VB/IB + freeze-at-bind (commits `403ae49`+`9c3d298`)

**Implementation** (`d3d8_proxy.c`, `gen_forwarders.py`, `Makefile`): `gen_forwarders`
now generates the two buffer-interface vtables (custom = QI/AddRef/Release/Lock/Unlock);
`my_CreateVertexBuffer`/`my_CreateIndexBuffer` wrap the real buffer in a `WrapVB`/`WrapIB`
(`{real, refs, size, fvf/fmt, gen, shadow, lock_*}`). `my_..._Lock` records the lock
range + app pointer; `my_..._Unlock` (if writable) memcpies the written range from the
app's still-mapped pointer into `shadow`, bumps `gen`. `SetStreamSource`/`SetIndices`
UNWRAP (pass `real` to the device; a vtable-identity `as_wrap_vb/ib` guards a raw buffer
round-tripped via GetStreamSource) and FREEZE the current shadow into a per-frame arena
`g_rc` (`cb_resref_frozen`); `write_frame` snaps the frozen bytes (`snap_vb_bytes`,
body-identical to `snap_vb` ⇒ a static buffer dedups to the same id, replay unchanged).
Census: `CreateVertexBuffer`/`CreateIndexBuffer` → `recorded`; +the two buffer interfaces
(Lock/Unlock recorded); drift guard follows generically (141 methods, 31 risk; +9 buffer
checks = 72).

**VALIDATED — both paths:**
- **Transparent-on-static (arrprobe re-drive, cache `30d6b861`):** BOTH sides 80/80
  bit-exact (the frozen-at-bind content == what the game drew, else replay would
  diverge), JOIN_COMPLETE. Census dynamic **VIOLATION → SAFE** (31/31 risk 0-observed).
  `resource_binds`: `vb/ib_fallback=0` (every bind frozen via the wrapper — no buffer
  escaped it), `vb_multibind` 3/frame (the reuse persists). **RES_VB stays 5** with FULL
  Lock/Unlock visibility ⇒ EMPIRICALLY PROVES the 3 reused buffers/frame are static (any
  mid-frame mutation would have produced >5 contents) — the completeness the frame-end
  view could only assume is now proven.
- **Split-on-mutation (positive fixture `gx04_fixture.exe` + `test_gx04_fixture.py`, 6
  checks):** one VB, three binds A,B,A → **exactly 2 RES_VB**, bind resids `[0,1,0]`
  (SPLIT A≠B into two versions; DEDUP the re-bind of A to id 0). The old frame-end
  snapshot stored ONE record for all three ⇒ the B-draw would have replayed as A — the
  bug GX-04 closes. `vb_fallback=0`. SKIPs cleanly without a live D3D8 device.

**Net:** arrprobe's capture-completeness is closed for VB/IB — GX-00's honest FAIL loses
the resource-creation mechanism (the b494 render_program FAIL is a separate captured-draw
difference, untouched). Residual: making the census a HARD pixels/render_program
precondition in `parity_prove` (GX-01-full) is the remaining R3 policy step (the risk set
is now 31, all 0-observed on arrprobe).

## GX-05 LANDED 2026-07-17 — dedup byte-compare + reader corruption-safety

> Roadmap §9 GX-05 ("Harden deduplication and corruption detection"). Depends GX-04 ✓.
> Both acceptance criteria MET + both proven DECISIVE (fail on the pre-GX-05 code).

**Decision — hash-plus-byte-compare, NOT SHA-256.** The GX arc's ethos is *provably*
complete, not *probably*. Byte-compare is collision-PROOF (not merely collision-resistant),
needs no crypto ported into 3 languages (proxy C + replay C + Python), and is the ONLY
option that makes "forced hash collision fixture remains distinct" a CONSTRUCTIBLE, decisive
test (a real FNV-64 collision is infeasible to build; pure SHA-256 would still trust the
hash, so a forced collision couldn't be tested at all).

**(1) Dedup byte-compare (Parts 1+2, `d3d8_proxy.c`).** `dedup_or_write` now treats the
FNV-64 hash as a BUCKET: a hash hit is CONFIRMED by `memcmp` of the full retained body
(+`type`+`len`) before dedup'ing, so a collision yields a NEW id (both kept distinct), never
a false dedup. `type`/`len` compared explicitly ⇒ size/type/format are in the identity
domain (already folded into the hashed body, so a truncated body can't alias a longer one of
the same prefix). Distinct bodies retained in RAM (malloc'd copy) — bounded by the DISTINCT
set (dedup keeps it = the container's resource section; process-lifetime like `g_cb`/`g_bl`/
`g_rc`). New `g_res_collisions` (invariant 0) + `g_res_retained` surfaced per-drive in the
census sidecar's **`dedup` block** (`{distinct,collisions,retained_bytes,force_collision}`) —
a permanent soundness signal (a real drive MUST show `collisions:0`; the sidecar parser
ignores the extra key, census unaffected, `test_d3d_census` 91/0). Test seam
`OPENRECET_V3_TEST_FORCE_COLLISION` (env, read once at capture init, loud `proxy_log`) forces
`fnv1a`→const so the byte-compare path is exercised deterministically.
- **Acceptance (`gx05_fixture.exe` + `test_gx05_fixture.py`, 6 checks):** one VB, four binds
  A,B,A,C under the forced-collision seam ⇒ **3 distinct RES_VB, resids [0,1,0,2]** (split
  A|B, DEDUP re-bind A, split C past BOTH hash-equal entries); census `collisions:2
  force_collision:1 retained_bytes:216`. Hash-only under the seam would collapse to 1
  RES_VB/[0,0,0,0]. Two independent VACUOUS-PASS guards (seam-active + collisions≥2, since
  normal hashing gives 0 collisions) — they CAUGHT the first cut (a stale DLL left the seam
  off; the fixture's `_putenv_s` sets it, but the log opens at DllMain BEFORE that ⇒ read the
  census `dedup` block, co-located in `out/`, not the log). GX-04 unregressed (6/6).

**(2) Reader corruption-safety (Part 3, `replay_core.c` — the AUTHORITATIVE reader: viewer +
pixel producer).** `cu()` already EOF-clamped the 4-byte scalar reads; the gap was the
variable-length spans + `count*elem` (which integer-OVERFLOWS 32-bit `size_t`). Added
`cspan(n)` / `cspan_n(count,elem)` (division-domain, overflow-safe) that return NULL + POISON
the cursor to `end` on overflow ⇒ the next `cu()` returns EOF ⇒ the walk terminates; every
span USE is guarded on the non-NULL return + a fit check (`dl<=size` VB/IB clamp, `lh<=ld/lrb`
tex rows, `up_fits`/`idxup_fits` UP draws) so no memcpy/D3D-draw reads OOB. Unknown opcode
already returned a sentinel the callers FAIL on.
- **Acceptance (`corrupt_fuzz.exe` + `test_corrupt_reader.py`):** `#include`s `replay_core.c`,
  drives the REAL `step()` with do_res=do_calls=0 (pure parse, no device ⇒ headless) over
  crafted truncated / corrupt-length / integer-overflow / unknown-op inputs + **40000
  deterministic fuzz** inputs, asserting the cursor NEVER escapes `[buf,end]` and every walk
  terminates. **DECISIVE:** against the pre-GX-05 reader it FAILS ("cursor escaped" on
  RES_IB/CopyRects/SetTransform/SetMaterial + the fuzz). **Transparent on valid data**
  (guards are no-ops on well-formed input): `replay --verify-hashes` on real cached
  containers — `title-encyclopedia` **120/120 BIT-EXACT**; `house-pause-save-commit/port`
  byte-identical to HEAD (its DIVERGENT is pre-existing, NOT a regression).

**(3) Python readers (Part 3, `orv3.py` primary Container parser + `orv3_draws.py`
render_program reader).** Bounds-checked `u()`/`i()`/`span()` raise an EXPLICIT `ValueError`
on any past-end read (not a raw `struct.error` or a silently-clamped short slice → bogus
geo_hash) + a DEV_PARAMS-block guard. `test_orv3.test_corrupt` (truncation sweep + short
DEV_PARAMS) is DECISIVE (the raw `u()` blows `struct.error` on a mid-record cut). Opcode
validation (`else: raise`) was already present in both.

**Wiring:** the 3 GX acceptance tests (gx04, gx05, corrupt_reader) registered in
`tools/run_python_tests.py` (each SKIPs cleanly without a mingw exe / D3D8 device).
