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
  (`0x73dfc7c`, window/GDI handle). That dword is now excluded from
  `semantic_hash`, so two Win11 demo runs give an identical hash (`a4c15035…`).
- RNG trajectory identical wine vs Windows (`7b2df9f6` @f1500, `5510bda9` @f3000).
  Wine↔Windows non-pointer diffs are environmental only: CRT handle table 0x603dc0..,
  path strings (0x73d8dec, 0x964d190), desktop res (0x73dfc90/94: 1920×1080 vs
  1024×768), D3D caps/heap ptrs below the heuristic threshold (0x73cbb28.., wine heap
  sits at 0x15xxxx). No gameplay field differs. ⇒ wine is a valid logic oracle;
  pixel truth still needs Windows.

## 3. x87 precision (affects every float port)

- In-game x87 CW = **0x027F (53-bit PC)** on BOTH Windows native and wine, measured on
  the main thread at every tick. Contradicts `winmain-and-bootstrap.md:176`
  ("engine accepts the DX8 default of single precision"). Resolved 2026-10-14: D3D8
  CreateDevice DOES set PC24, and retail's WinMain resets PC53 right after
  (engine-quirks §138). Hosted or not, retail ticks at 0x027F.
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
the Day-1 house correctly. **Same script on Win11 native → identical player pos + rng at
every 100-frame checkpoint** (f500 (3.10,0,5.24) aae821a0; f700 (-7.44,0,0.25) e2a868f9;
f1000 (-4.45,0,3.10) 4307f8b8) ⇒ gameplay (incl. float movement) is frame-exact
cross-platform. Demo: `ssh wslop 'bash /mnt/c/orc-oracle/demo.sh'`.

## Open / not yet shown

- Heap objects not in the shadow snapshot (item catalog `DAT_095d3804` is heap) —
  need CRT-heap capture or a private-heap hook.
- External effects during shadow (D3D/DirectMusic/file) not yet recorded/stubbed.
- Only the house-load worker exercised under DEFER; cutscene/dungeon loaders untested.
- No retail-layout port function beyond `rand` run in-game yet.

## 7. First use as an RE oracle: the `DAT_095d3728` save-sweep contradiction (resolved)

Question (audit §1.2): 6c9c85d3 says retail's `FUN_004901c2` verify sweep is gated off
on save-load because `FUN_004902fe` sets `DAT_095d3728`; `src/save_io.h:18-22` says the
normal-size bucket does NOT set it. Two host tests lock opposite values.

- IDA (`all_ida.c`): the only writes of `dword_95D3728` are in `FUN_004902fe`'s two
  LEGACY buckets (size 0x11EFCE0 / 0xF30AE0). Normal 0x11F7530 bucket = memcpy →
  `FUN_00499583` → `FUN_004901c2` (sweep runs when the gate is 0).
- rhost live: after boot-loading fa7c82 (normal size) `DAT_095d3728 == 0` (wine + Win).
- rhost controlled experiment (crafted save: bank 7 valid magic, word100=0x12345678,
  checksum 0): **retail RESETS bank 7** via `FUN_0049001c`; the result is byte-identical
  (0 differing dwords) to the seed's bank 0, i.e. the canonical reset pattern: full
  memset + field init + catalog-derived key/count (`DAT_005c80ac`=571 loaded at boot),
  checksum LEFT 0 (calc 0x345e7bcf).
- ⇒ The save-pillar seed's banks 1–99 (magic ok, stored ck 0, calc 0x345e7bcf) ARE that
  reset pattern, so retail's sweep re-resets them idempotently ⇒ retail == seed. The
  port diverged because `save_bank_init_one` ≠ `FUN_0049001c` (it zeroed key+count and
  STAMPED a checksum), not because of a skip gate.
- 6c9c85d3's fix (`save_bank_set_skip_verify(1)` on the normal bucket,
  `src/save_io.c:~200`) passes that seed by coincidence and diverges for any stale bank
  whose content isn't the reset pattern (retail resets it, port keeps it).
- Reproduced end-to-end: `house-pause-save-commit` replayed in rhost (Win11, ~2 s);
  retail's committed `save.dat` vs seed: header 0 diffs, banks 1–99 0 diffs (seed
  banks: stored ck 0, calc 0x345e7bcf = the reset-pattern checksum above), bank 0
  (active) 3 dwords. Consistent with "sweep ran, reset was idempotent".
- **Fix APPLIED 2026-10-13 (owner OK):** normal bucket no longer gates the sweep (legacy
  buckets still do); `save_bank_init_one` == `FUN_0049001c`: category run-length
  key/count over the item catalog (@0x4900ee) + NO checksum stamp. Evidence: port reset
  bank (real item.txt 571 items + chara.txt) vs retail-written seed fa7c82 banks
  1/2/50/99: **0 / 47,090 dwords differ** (pre-fix: 66 — key/counts + stamped checksum
  0x345e76b1 vs 0). Side effect, also toward retail: a load that resets N stale banks now
  consumes 8·N `rand()` like retail (the gated port consumed none). Host tests: 2 flipped
  + 1 added.

## 8. Existing scenario corpus replays inside rhost (P3)

`tools/oracle/p3_run.c` replays `tests/scenarios/*/trace.jsonl` unchanged:
- The port's own `src/input_segtrace.c` provides the parser and state machine.
- The anchor predicates are ported from the Frida agent's `anchorTick`.
- `{rngseed}` is applied to `DAT_006023a0`; `{esc}` is posted as
  `WM_KEYDOWN VK_ESCAPE`.
- The other pins are ignored: DEFER loads + VTIME make them unnecessary for a
  retail-only run.
- Each scenario runs with its own save (`_saves/` are gitignored; they lived only
  on wslop and are now copied locally too).

Win11 native, 67 scenario dirs:
- **61 complete**, in **125 s total wall** (≈2 s each), including the 65-segment
  `house-customer-tutorial`, `guild-*`, all `house-pause-*`, `intro-*` and
  `title-*`.
- **Not complete:**
  - The `cad868` first-customer family (`house-firstcust-arrprobe`,
    `-cutscene-day2`, `-cutscene-day2-full`, plus the `_firstcust-skip-probe` and
    `_probe-dayend-memsnap` probes). All stall at seg 24, waiting for a
    PAUSE_OPEN after timed shop-menu navigation.
    - These traces rely on `{bgnpcpin}` and Frida-exact `{cs,primary,tut}loadpin`
      semantics. The opt-in `LOAD_PINS=1` approximation (hold N frames from
      worker creation) gets less far, reaching seg 15.
    - Fix: re-record them natively in rhost, where no pins are needed.
  - `_probe-dayend-rngcs` uses a Frida-only `{rngcs}` op that the port parser
    rejects.
- Live `haggle_decide` shadow over the corpus: 12 retail calls, 0 mismatches for
  either port build (no call hit the ×1.05 boundary).
- Wine replay of `house-customer-tutorial` matches Win11: same 85 anchors, same
  haggle calls, final rng `e3341701`.

## 9. Function coverage of retail under the scenario corpus

Method: `COVERAGE` arms a one-shot INT3 at each of IDA's 3,550 function entries; a
VEH handler logs the first-hit frame and restores the byte. The run is otherwise
unperturbed (same completion). Join: `tools/oracle/coverage_join.py`. "Game"
excludes IDA FLIRT libs + the decomp's `vendorlib.tsv` (d3dx8/libc) ⇒ 1,597
functions, 640 KB.

66 scenarios (Win11 native, ~6 min):
- **912 functions hit; 614 game functions = 388 KB = 61% of game code bytes.**
  The not-hit 39% is consistent with the unported dungeon / endings / adventurers.
- Port inventory of the hit game bytes: source-referenced 274.6 KB + instrumented
  98.9 KB = **96%**. 211 hit-but-unreferenced functions = 14.3 KB.
- Lead: `0x47df4d` / `0x47d622` / `0x432473` / `0x42df55` / `0x47ddc2` (decomp names:
  dungeon floor build/maze/spawn) run in 57 house scenarios, but the port never
  references them. Either the house reuses the floor/room builder (the port
  reimplemented it differently, which is a parity lead), or the decomp's
  agent-invented names are wrong. Check before trusting either.

## 10. Productized rhost: residual nondeterminism, wine vs Windows (2026-10-13)

`tools/rhost/` (README) adds, on top of DEFER + VTIME + seed:
- fixed-address heap arena (LFH placed ~280 pointers differently per run);
- fake kernel handles (CRT `__pioinfo` osfhnd 0x380 vs 0x388);
- fixed stacks (CRT `__NLG_Notify` stores EBP at 0x602374);
- fixed command line / environment / startup info / std handles (the CRT copied the
  `--run` path into its heap: `"det_a"` vs `"det_b"`);
- deterministic temp names; file overlay; no real DInput poll; WaitMessage never parks;
- game controllers hidden from enumeration;
- DInput gets the host module as hinst: retail passes WinMain's hInstance
  (`DAT_073de628`, `FUN_0047af52`) = rhost's fake 0x400000, which Windows dinput8
  rejects (wine accepts). Until 2026-10-13 every Windows run had no DInput objects
  (`--record` read no keys). Turning it on changed no rng/anchor/scene
  (`house-idle`); +2 COM slots `073dfcc0`/`073dfcc4` in `volatile.txt`.

**Windows, run to run:** only system-DLL/COM pointer slots differ (`volatile.txt`, 358
slots calibrated over 12 scenarios incl. the 40k-frame tutorial-end → Day 2). `det` PASS
everywhere.

**Lesson:** mask by address, never by value. A value-classified "external pointer" mask
masked 580k dwords of heap pixel data differently per run.

**Wine vs Windows** (`house-idle`):
- identical until the new-game frame f71;
- there, the first 807 `rand()` calls match, then Windows makes one extra;
- first-hit coverage shows two environment differences:
  1. wine enumerated a game controller (now hidden for hermeticity — not the cause);
  2. only on Windows does retail run its sound-effect path (`EnableSoundEffectByIndex`
     0x4994f3, `PlaySoundEffectSegmentByIndex` 0x499c63 from f12). On headless wine the
     audio-ready flag `DAT_096430fc` stays 0 (DirectMusic init fails), so those calls
     return early.
- ⇒ **retail's RNG trajectory depends on audio availability** (the extra draw sits on an
  audio-dependent path; exact consumer not yet pinned).
- Wine is a valid oracle for pure logic and for determinism within wine. RNG-exact retail
  ground truth is Windows native (or a wine with working DirectMusic: open item).
- Stack scrubbing (zeroing below ESP each tick) made no difference — not stack garbage.

## 11. First pin-free port↔retail equality: house-pause-save-commit (2026-10-13)

Retail ran under rhost with the anchor trace, `rhost.py flatten` wrote the resolved
frame-absolute trace (no pins, only inputs + the trace's own `{rngseed}`), and the port
replayed that flat trace on Windows with `--det-loads --save-override <fa7c82 seed>`.
The committed `save.dat` came out **byte-identical (18,838,832 bytes)**.

With the legacy anchor trace the port instead applies the Frida-era `{playtimepin}`
(rhost ignores pins), and the saves differ in exactly 5 bytes (bank 0
occupied_playtime + checksum).

This validates end-to-end:
- the `FUN_0049001c` reset-pattern fix (banks 1–99);
- `--det-loads`;
- the flat-trace replay;
- playtime-counter alignment.

## 12. Whole-flow equality: day2-save-commit (2026-10-13)

`rhost.py portsave day2-save-commit` drives the whole tutorial (customers, haggles, the
end-of-tutorial cutscene, the day advance) to a Day-2 save, 33k frames, with no pins.
The diff went 106 → 88 → **0 bytes** (IDENTICAL) after two real port fixes:

1. `FUN_0046c320` END handler (ret 3): marks script (group, sub) viewed in work bank
   +0x2d3a0 and save header +0x20. The port never did.
2. `FUN_0048ffd9` furniture seed: the port wrote it through `save_arena_base() +
   slot*stride` (the save-file arena WITHOUT its 0xb10 header, so it landed in file
   bank 0) instead of the active work record.

The port also reached `SAVE_PICKER_READY` on the same frame as retail (33199). The save
file is a strong whole-state checksum: every committed scenario is now a regression
gate (`rhost.py portsave`).
