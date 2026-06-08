# Frida capture crash — long captures truncate (frida-agent AV) — ✅ RESOLVED 2026-06-08

**Status: SOLVED. The AV was NOT frida-internal — it was OUR ~3 MB-per-frame
`readByteArray`+`send` shipping each screenshot over the remote Frida channel. The
intended fix (the `7b9907d` capture-local `writeRawFile` path) had silently been DEAD
since the frida-17 upgrade because `ensureWinFileFns` still called the removed global
`Module.getExportByName` and threw on the first captured frame — so EVERY capture fell
back to the heavy in-band path. Fix = (1) revive `writeRawFile` (frida-17 module API),
(2) route ALL captures (windowed caprange/pending/anchor, not just `capture_all`)
through it via `capture_local` (default on). Dense captures are now reliable AND ~8×
faster. The `{capstride}` workaround is no longer needed for reliability.**

## The fix (commits this session)
- `tools/frida/openrecet-agent.js` `ensureWinFileFns`: `Module.getExportByName('kernel32.dll',
  …)` → `Process.findModuleByName('kernel32.dll').findExportByName(…)` (the same frida-17
  migration already applied to `installShowWindowHook` / `installSaveRedirectHook`; this one
  was missed). Without it the whole capture-local fast path threw `TypeError: not a function`
  at `captureBackbuffer`→`ensureWinFileFns` on frame 1 and captured ZERO frames.
- `tools/frida_capture.py`: new `capture_local` (default True) — sets `capture_dir` for ALL
  capture paths, not just `capture_all`. `--no-capture-local` escape hatch for a true remote
  host. Also fixed an unrelated `f_log` "I/O operation on closed file" at the raw→png step
  (the log was closed one block too early).

## Proof (isolated title repro — both paths, same scenario)
A dense title capture is a fast faithful repro (the capture path is scene-independent; the
title produces Present every frame, so it stresses the transport in seconds without the long
boot-to-HOUSE nav). All `--turbo --silent-audio --hide-window --force-resolution 1024x768`:
- **In-band path** (`--no-capture-local`, `--capture-frames 0..250` = `readByteArray`+`send`):
  **`process-terminated` at frame 38**, 39 frames captured; Event Viewer logged a fresh
  `frida-agent.dll 0xc0000005 @ 0x00be64bc`. ⇒ the AV reproduces on the in-band path.
- **Capture-local path** (default, same command): **all 251 frames, clean
  `application-requested` detach, 0 agent errors.** A 400-frame `--capture-all` run likewise
  survived to frame 401 cleanly. 10×+ past the in-band death point.
- **Speed:** capture-local ~33 fps vs the in-band path's ~4 fps (which also crashed) — the
  3 MB/frame network ship was both the crash AND the bottleneck.
- Integration: `scenario-test boot-idle --target retail` 3/3 frames pass golden through the
  real `run_capture` path; the 7 trace_studio/export/save tests pass.

## Why the old framing was wrong (corrected)
The "ruled OUT: not our per-frame allocations / dominant cause is frida-internal" conclusion
below was a **false lead** — it assumed the `7b9907d` mitigation was in effect. It wasn't
(the API throw made it dead code), so the real captures kept running the exact 3 MB-churn
path the mitigation was meant to remove. The symbolization is the tell: the fault instruction
is the CRT **`memcpy`** (`rep movsb` @ `0xbe5f4e`) / **`memset`** (`rep stosb` @ `0xbe64bc`),
i.e. a bulk-copy walking off a heap made bad by sustained 3 MB alloc+send backpressure on the
remote channel — not V8 GC / Stalker / the interceptor pool. (This also explains the old
puzzles: WITHOUT call-trace died *earlier* because retail reaches the capture window faster
and floods the channel sooner; `{capstride:10}` survived because it ships ⅒ the bytes.)

---
_Historical diagnosis below (kept for the RE record; superseded by the resolution above)._

## Symptom
A long both-target trace-studio capture truncates on the **retail** leg: the retail
process dies mid-capture after a **non-deterministic** number of captured frames
(observed 69 / 87 / 117 / 126 / 132 / 193 across runs). The port leg (native, no frida)
always completes. This blocked the dense full-nav both-capture for the world-map nav
(town-map-RE.md §5b #4) — worked around with `{capstride}` (see below).

## Root cause (Windows Event Viewer — user-supplied + read via WSL interop)
It is an **access violation inside frida's own agent**, NOT retail code and NOT a clean
exit:
```
Faulting application: recettear.unpacked.exe
Faulting module:      frida-agent.dll   (frida 17.5.1)
Exception code:       0xc0000005   (access violation)
Fault offset:         0x00be5f4e   (a couple at 0x00be64bc, ~1.4 KB away)
```
The fault offset is **near-constant across dozens of crashes over 06/06–06/08** → a
specific code path in frida's runtime AVs once its heap is in a bad state. The *when*
varies (frame count) but the *where* is fixed. Read more crash rows yourself with:
`powershell.exe -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000} -MaxEvents 12 | ? {$_.Message -match 'recettear|frida'} | % {$_.Properties[7].Value}"`
(UAC auto-approves; WSL interop works — `powershell.exe`/`wevtutil.exe` are on PATH.)

## Ruled OUT (don't re-chase these)
- **frida "degradation"** — there is no such thing (user-guaranteed); see
  [[feedback_frida_server_leak]]. Each spawn AVs independently at the same address.
- **128 MiB DBus per-message cap** — that always logs a GLib/DBus warning; the agent.log
  shows NONE (user-flagged this check). Not it.
- **The call-trace hooks** — recapturing WITHOUT `--call-trace` died *earlier* (≈69 vs
  ≈117 frames). call-trace overhead also balloons the turbo load-stretch (boot→HOUSE 14852
  frames WITH ct vs 2909 WITHOUT — the load is real-time-bound), a confound, not the cause.
- **The wall-clock deadline** — trace-studio retail `duration_ceiling_ms`=600 s; runs died
  at 16–73 s elapsed. (`drive/retail.py`.)
- **The send-message volume** — more sends (call-trace) survived *longer*, not shorter.

## Mitigation LANDED (commit 7b9907d) — reduces churn, does NOT fix the AV
`tools/frida/openrecet-agent.js` `captureBackbuffer()` was re-creating, **every captured
frame**: ~5-6 `new NativeFunction` (getDesc/lockRect/unlockRect/release — each allocates an
executable trampoline), ~5 `Memory.alloc` incl. a ~3 MB readback blob, and a ~3 MB
`readByteArray` + `new File`. Fixed: cache the surface vtable NativeFunctions once (all
IDirect3DSurface8 share a vtable), reuse the blob + scratch out-pointers, and on the
capture-local path write the reused blob straight to disk via **kernel32 CreateFileA/
WriteFile** (no per-frame ArrayBuffer / File). **Frames verified bit-valid.** The AV
**persists** (still 0x00be5f4e, still non-deterministic) → the dominant remaining cause is
**frida-internal**, not our per-frame allocations.

## Working WORKAROUND — `{capstride}` (reliable today)
Thinning the screenshots below the crash threshold makes the capture reliable:
`{capstride:10}` (every 10th frame) → **port == retail == 63 frames, kept-count parity OK,
no crash**, full nav span, aligned by filename. Verified the world-map nav is **1:1 vs
retail end-to-end** this way (only the #5 tooltip PORT-DEBT differs). So: for long captures,
either capstride, or chunk the window into sub-`< ~60`-frame drills.

## Next-session investigation plan (pick one+)
1. **Symbolize 0x00be5f4e** in frida-agent.dll 17.5.1 (the DLL is on the host at
   `C:\Users\headpats\AppData\Local\Temp\re.frida.server\x86\frida-agent.dll`). Map the
   offset to frida's source (GumJS V8 GC? Stalker? Interceptor trampoline pool?) — that
   names the exact subsystem to avoid/patch.
2. **Try frida's QuickJS runtime** instead of V8 (different GC/heap) — the AV may be a V8
   issue under sustained allocation. Also try a **different frida version** (up/down from
   17.5.1) — could be a known 17.5.x regression.
3. **Lighten the per-frame hook footprint** further: the Present `Interceptor.attach` fires
   every frame; consider `Interceptor.replace`/a thinner trampoline, or moving the readback
   out of the JS callback (a pure-native C agent / CModule that writes the frame, with JS
   only orchestrating).
4. **Alternative capture mechanism** (no per-frame frida JS): a small injected native DLL
   that hooks Present and dumps the backbuffer to disk itself (frida only injects it), or a
   shared-memory ring the Python side drains. Removes frida's JS runtime from the hot path
   entirely — most likely the robust long-term fix.
5. **Robust fallback**: auto-chunk any caprange > ~60 frames into sequential drills (each a
   fresh spawn under the threshold) and stitch — make trace-studio do it transparently.

## Cross-refs
- Mitigation commit `7b9907d`; town-map nav verification: `town-map-RE.md` §5b.
- Memory: [[feedback_frida_server_leak]] (the 128MiB-is-a-per-message-cap correction).
