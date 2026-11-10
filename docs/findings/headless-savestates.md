# rhost headless devices, savestates, live sessions (2026-11-10)

How retail became an L2 target (step + snapshot + search) for agent-driven probing. Tool
reference: `tools/drive/README.md`; rhost flags: `tools/rhost/README.md`. This file = design
facts + evidence.

## 1. Headless devices (`rh_dev.c`, `--headless`)

Retail's device imports are three IAT entries: `Direct3DCreate8`, `CoCreateInstance`
(DirectMusic performance + loader, the DirectShow filter graph), `DirectInput8Create`. Headless
replaces all three with fake COM objects whose every byte lives in the **DEV region**
`0x40000000..0x50000000` (carved from the arena's LARGE range; retail's large heap peaks at
50 MB — dungeon3-deep, 294k frames). DEV has its own allocator, metadata in-band, blocks zeroed.

- **Vtables from the headers.** `gen_dev.py` parses mingw's d3d8 / dmusici / strmif / control /
  dinput headers → per method a counting S_OK stub with the right stdcall size, the vtables, and
  a `_Static_assert` on each hand-written method's type (178 checked). `dev: defaulted …` in
  rhost.log lists the stubs retail really called.
- **Adapter answers = the reference machine's.** `--d3d-probe` logs the real Direct3D 8's display
  mode, caps (HAL / REF / device), CheckDeviceFormat matrix (2 × 4 × 4 × 42), depth-stencil
  matches, multisample types → `gen_dev_ref.py` → `rh_dev_ref.h`. D3DX and the game then pick
  formats as on the real device. CreateDevice without FPU_PRESERVE leaves CW 0x007F (quirk §138).
- **Draws are no-ops; state setters store; resources are real memory** (textures, VB / IB,
  surfaces: D3DX reads its own writes back for mip filtering; CopyRects / UpdateTexture copy).
- **Window = message-only** (`HWND_MESSAGE`): wine without X refuses any other window; retail's
  WndProc and queue still work (the ESC key posts). The window handle is kept out of DEV.
- **DirectMusic:** everything succeeds, nothing sounds, `DAT_096430fc` = 1. This was the one
  wine ≠ Windows cause (no DirectMusic under headless wine ⇒ flag 0 ⇒ no SE-path RNG draws).
- **DirectShow** (opening / attract movie): `WaitForCompletion(0)` never completes — the movie
  ends by input, as in every trace (a real graph ends after wall-clock seconds).
- **DirectInput:** setup only; `Acquire` = E_ACCESSDENIED like the real keyboard for rhost's
  never-activated window. The poll was already replaced by the scripted mask.

Retail facts met on the way (both device modes):
- `FUN_00499200` passes the VALUE of its segment-state slot (0) as QueryInterface's out pointer
  after PlaySegmentEx; real DirectMusic answers E_POINTER, so `DAT_09642e24[i]` stays 0. A fake
  QI that writes through the pointer crashes: fakes return E_POINTER for NULL.
- The 110 SE segments come from `FindResourceA(NULL, …)` = the PROCESS image. Under rhost that
  is rhost.exe: no SE resource is ever found, `DAT_096430fc` is still set. Same in every rhost
  mode (and unlike the shipped exe, where they load).

**Evidence.** `rhost.py headless-check` = a headless replay vs the Windows real-device golden:
end frame + RNG, every tick's draw count + LCG state, every anchor's frame, the resolved inputs,
save.dat bytes.
- wine, no X, 2026-11-10: **100/100 IDENTICAL, 1,670,506 frames** (441 s, -j14).
- `headless-check --real --target win` (the restructured rhost on real devices vs the goldens):
  **100/100 IDENTICAL** (372 s).
- So: headless ≡ real device, wine ≡ Windows, new tick hook ≡ old.

## 2. The tick hook: host code off the game stack

Retail's one `call VA_TICK` site lands in `hook_tick_entry` (naked). Host work runs as a coroutine
on its own stack (`host_main` → `rh_step()`): boundary work → `rh_call_tick` (retail's tick with
ESP exactly where the unhooked call would put it, retail's callee-saved registers restored —
`sub_47BE92` is `__usercall` on ebx / ebp) → post-tick work → `rh_yield` (back into retail's
message pump) → resumed by the next call of the site. Consequences:
- stack addresses retail stores in globals no longer depend on rhost's frames;
- a savestate restore can rewrite the game stack `[g_gesp, 0x59000000)` while it runs;
- a live session / a search is straight-line C that calls `rh_step()` in a loop.

## 3. Savestates (`rh_snap.c`)

Tracked: retail `.data` / `.bss` (37,160 pages), the committed heap arena, DEV — ~210 MB.
- **anchor:** full base copy (288 MB buffer, ~140 ms) + every tracked page read-only. First write
  to a page → VEH → dirty list D, page writable until the next anchor.
- **take:** per page of D: share the parent savestate's 4 KB slab if equal, "= base" if equal to
  the base, else a new slab. **restore:** per page of D: copy slab or base. Pool: 960 MB of slabs.
- plus: the game stack's live bytes, registered host blocks (frame, clock, mask, tick regs, trace
  cursor, anchors, movie head), heap allocator metadata, the fake-handle table (numbering must
  replay), the trace ops' `fired` flags, screen text, x87 CW / MXCSR, and the overlay's file
  version (save.dat's 18.8 MB captured once per version, written back when a restore crosses one).
- **Kernel-mode writes do not fault** into the tracker (a ReadFile into a read-only heap page
  fails; wine: the syscall returns STATUS_ACCESS_VIOLATION). 24 imports through which the system
  writes retail memory call `rh_snap_touch()` first (ReadFile, GetGlyphOutlineA, GetClientRect,
  PeekMessageA, RegQueryValueExA, …).

Measured (wine, maximus):
- restore 0.2 ms; `snap` 0.1–3 ms; 5 ticks after an anchor in the idle house = 17 slabs (68 KB)
  per savestate; D after ~2,900 house ticks = 3,405 pages; a 300k-frame dungeon run 24,664.
- **`rhost.py snap-check` = 99/99 PASS** (every scenario ≥ 200 frames; savestate at 30%, at 70%
  hash + restore + replay, twice; state hash + DEV hash + RNG equal each pass; end frame / RNG /
  save.dat equal to the golden's). The save-commit scenarios exercise the file restore.
- plain headless replay vs the same with `--snap-test`: `state_sem` and `state_dev` equal
  (write tracking is transparent).

## 4. Live sessions (`rh_live.c`, `--live PORT`)

Text protocol on 127.0.0.1 (header comment = the command list). Postfix programs over memory
reads for predicates / samples; `run` steps with until / avoid / per-tick samples; the **movie**
(mask changes, ESC posts, rngseed ops since boot) is a persistent list whose head is savestate
state; `explore` = the Kirby v2 search in C on savestates.

- **Screen text:** retail's text draws end in `FUN_0047ca05(x, y, str, color, scale)` (197 call
  sites incl. centred / right-aligned) or `FUN_0047d464` (dialogue rows + reveal budget, 2 sites).
  Their call sites go through thunks that note the arguments on a private stack and jump on; no
  retail memory or stack is written (`rd verify` MATCH with the hooks active). Sprite-drawn UI
  (title, pause main page, HUD digits) has no text.
- **Speed** (one process, wine): dialogue / menus ~5,000 ticks/s; house / shop 700–900;
  `--profile F` (EIP samples) in the house: `FUN_00414ee2` 48% (overlay particle dispatcher: a
  4096-row scan per texture group, render), `FUN_0040fb3a` 14% (table-A tick, sim), `0x4adef4`
  12%, `FUN_00459847` 9% (mesh walker, render), non-retail 2.4%. Render ≈ 70% of a tick with no
  device behind it.

## Open

- Skip render functions proven pure w.r.t. tracked memory (the write tracker can prove it per
  call: zero dirtied pages) — `FUN_00414ee2` alone would double 3D-scene speed. Not safe wholesale:
  render writes sim-read state (FUN_0046602e's hint slot, DAT_0438b760, the model anchors).
- Pictures without a movie replay: emit one tick's draw stream from the fake device in the v3
  capture format and replay it on a real device.
- Savestates across processes (needs the remaining per-process values — HWND, GDI handles —
  relocated): worker sessions could then load a state instead of replaying the movie.
- `--live` on the Windows box (session 0 works for headless; needs a port forward): +24 threads.
