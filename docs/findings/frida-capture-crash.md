# Frida capture crash — long captures truncate (frida-agent AV) — OPEN

**Status: root identified, partial mitigation landed, NOT solved. Next-session task
(user, 2026-06-08): make these captures reliable — either fix frida or find an
alternative capture mechanism.**

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
