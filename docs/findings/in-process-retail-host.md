# In-process retail host ("retail as a library") — prototype findings

2026-10-13. Prototype: `tools/oracle/` (P1 offline oracle, P2 in-process runner).
Context/decision record: `docs/audits/2026-10-13-methodology-reset.md`.
All numbers below measured on wine 11.19 + Xvfb (Arch) and Win11 native (wslop host).

## 1. Mapping retail into a host process

- `recettear.unpacked.exe`: ImageBase 0x400000, SizeOfImage 0x9555000, relocs STRIPPED
  (Characteristics 0x10f), DllCharacteristics 0. `.text` 0x401000+0x11358e;
  `.data` 0x528000 VSize→0x964f000 (≈145 MB incl. bss); `.data1` 0x964f000+0x8e0;
  `.rsrc` 0x9650000. Imports: KERNEL32 USER32 GDI32 SHELL32 ole32 WINMM DINPUT8 d3d8
  d3dxof ADVAPI32 (164 symbols, all resolvable on wine + Windows). No steam_api.
- Plain `VirtualAlloc(0x400000, 0x9555000)` FAILS (err 487) on wine and Windows when the
  host is elsewhere: loader-time heaps/mapped NLS files land in 0x3c0000..0x8b0000.
  Fix = host linked at 0x200000 with a 0x9900000-byte `.bss` hole (`RETAIL_HOLE_DEFINE`)
  → loader reserves the range; `VirtualProtect` RWX + copy sections.
- mingw ld defaults to `--dynamicbase` ⇒ Windows ASLR-relocates the host and the hole
  moves (works on wine, fails on Windows). Link `-Wl,--disable-dynamicbase`.
- Running the game in-process needs only 2 IAT swaps: `GetModuleHandleA(NULL)`→0x400000,
  `GetModuleFileNameA(NULL)`→`<gamedir>\recettear.exe`. Resources/menu/icon load fine
  from the hand-mapped image (title menu bar File/Display/Help renders).
- Hooks = rewrite every direct `E8 rel32` whose target == VA (no trampolines; original
  stays callable). Direct-call-site counts: clock 0x47be2f=1, tick 0x47be92=1,
  input 0x47b73c=1, sim_a 0x4536cb=1, sim_b 0x49966a=1, rand 0x5041f6=1.
  (Indirect/vtable calls not covered — fine for sim fns: 14.6k direct vs 2.9k indirect,
  indirect ≈ COM.)

## 2. Determinism (the load-seam problem)

- Loader handshake (0x452cde→worker 0x45293d, 0x452d07→worker 0x452aab): creator sets
  `DAT_06a4995c/60=1`, calls `CreateThread`, stores the RETURNED HANDLE into
  `DAT_06a49950`; worker does the load, `CloseHandle([06a49950])`, zeroes
  `06a49950/5c/60`, plain `ret` (caller-cleans — wrap it as **cdecl**, a stdcall wrapper
  corrupts the worker stack).
- Under the turbo virtual clock + normal threading, on Windows the house load never
  completes inside 3000 frames (≈1 s wall @ ~3000 fps; load needs real seconds) =
  the documented "turbo load stretch". Wine finished ≈frame 1000. ⇒ load length in
  frames = wall-time × fps = nondeterministic by construction.
- `SYNC_THREADS` (run worker inside CreateThread): BROKEN — inverts the creator's
  post-CreateThread ordering (hang / exec-NULL). Don't.
- **`DEFER_THREADS` (create suspended; at next tick boundary Resume + Wait): WORKS on
  wine + Windows.** Every load = exactly 1 frame. Title→Load Game→house by frame 300.
- Residual nondeterminism after DEFER + `RNG_SEED` (DAT_006023a0 @frame0) + scripted
  input: 8 dwords / 152 MB = thread id `DAT_06a496cc` + raw wall-clock reads outside the
  game clock (`0x73dde28/40`, `0x73de63c/40`, `0x73dfc80/84`, `0x964d0f4`). With
  `VTIME` (timeGetTime/GetTickCount/QPC/QPF/GetLocalTime/GetSystemTime virtualized):
  **0 non-pointer diffs** across 2 wine runs (93 heap/COM pointer values differ —
  allocation addresses, not state). Windows run-to-run: 1 non-pointer diff
  (`0x73dfc7c`, window/GDI handle).
- RNG trajectory identical wine vs Windows (`7b2df9f6` @f1500, `5510bda9` @f3000).
  Wine↔Windows non-pointer diffs are environmental only: CRT handle table 0x603dc0..,
  path strings (0x73d8dec, 0x964d190), desktop res (0x73dfc90/94: 1920×1080 vs
  1024×768), D3D caps/heap ptrs below the heuristic threshold (0x73cbb28.., wine heap
  sits at 0x15xxxx). No gameplay field differs. ⇒ wine is a valid logic oracle;
  pixel truth still needs Windows.

## 3. x87 precision (affects every float port)

- In-game x87 CW = **0x027F (53-bit PC)** on BOTH Windows native and wine, measured on
  the main thread at every tick. Contradicts `winmain-and-bootstrap.md:176`
  ("engine accepts the DX8 default of single precision") — D3D8 on Win11 does not leave
  PC24 in effect here. Verify once in a normal (non-hosted) retail launch.
- Port build uses `-std=c11` ⇒ gcc `-fexcess-precision=standard` ⇒ on x87, `1.05f`
  literals are evaluated as `1.05L` (long double), and products are rounded to float on
  assignment. Retail (MSVC6) multiplies by the FLOAT constant and `__ftol`s the
  unrounded x87 result. P1 oracle on `haggle_decide` (FUN_00460672) over 484,605
  (ref,ask) cases: c11 → 19/14/66 mismatches in PC53/PC24/PC64 (e.g. ref=60 ask=63:
  retail REJECT, port COUNTER); **`-std=gnu11` → 484,605/484,605 in all 3 modes.**
  5,352 `f`-suffixed literals in 81 `src/*.c` files are exposed.
- Retail `haggle_decide` takes NO args (reads `DAT_0730b588` accept_ref,
  `DAT_005c6bb8` player_ask). The CC capsule fixture claiming `cdecl stack_args
  [1000,1000]` is synthetic (also `retail_build_sha256 = "aaaa…"`).

- P1b (same harness, wine AND Win11 native, identical results): `boss_id_allowed`
  FUN_00431990 4001/4001, `floor_is_checkpoint` FUN_0043195d 1776/1776, `audio_fade`
  FUN_00499583 slider 0..9 **exact** (old Frida diff allowed ±1 cb slack; retail
  `SetVolume` captured via a fake IDirectMusicAudioPath vtable) — all 3 PC modes, c11
  and gnu11. ⇒ the c11 literal bug bites float-literal×int paths (haggle), not all FP.

## 4. Shadow execution (identical-state re-run)

- `shadow_call`: memcpy .data(152 MB)→pre; run fn; post; restore pre; run again;
  page-compare. House idle (wine): sim_a writes 16 pages/frame, sim_b 1 page;
  **802 shadowed calls, 0 run1≠run2 pages.** ≈90 ms/shadow (3×memcpy+cmp; unoptimized).
- MUST NOT shadow while a retail worker thread is live (first Windows attempt restored
  .data under the running loader → 36-page divergence then crash). Gate on tracked
  CreateThread workers (or DEFER).
- `REPLACE_RAND=1` (all rand call sites → port LCG on retail's seed): end state
  identical to pure retail after 1500 frames; `REPLACE_RAND=2` (deliberate bug)
  diverges (`4eec2882` vs `7b2df9f6`).

## 5. Speed

3000-frame deterministic run (boot→title→load save→house→2700 sim frames):
**Windows native ≈3 s wall (~1000 fps)**, wine+Xvfb llvmpipe ≈26 s (~115 fps).
(Frida retail drive for comparable windows: 15–50 min re-drives, per session logs.)

## 6. Scripted input

`hook_input`: after the real poll, write mask to `DAT_073dddd0`, force tick gate
`DAT_073dfca0=1`. 0x10=A, 0x1/0x2/0x4/0x8 move the player (pos floats @0x56da1d8:
(-0.30,0,9.35)→(3.10,0,5.24)→(-6.38,0,3.10)→…). Screenshot of the headless run renders
the Day-1 house correctly.

## Open / not yet shown

- Heap objects not in the shadow snapshot (item catalog `DAT_095d3804` is heap) —
  need CRT-heap capture or a private-heap hook.
- External effects during shadow (D3D/DirectMusic/file) not yet recorded/stubbed.
- Only the house-load worker exercised under DEFER; cutscene/dungeon loaders untested.
- No retail-layout port function beyond `rand` run in-game yet.
