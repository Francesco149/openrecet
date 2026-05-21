# OpenRecet — Project Plan

> Living document. Update as decisions change. Linked from `PROGRESS.md`.
> Last revised: 2026-05-19.

## 1. Goal

Produce a **drop-in replacement** for `recettear.exe` (the main game executable
of *Recettear: An Item Shop's Tale*, EasyGameStation 2007 / Carpe Fulgur 2010)
that is **behaviorally indistinguishable** from the original for a user who
owns a legitimate copy of the game.

Educational reverse-engineering + game preservation. Not byte-identical.
Not redistributed. No piracy enablement.

## 2. Hard constraints

1. **Never** redistribute game assets. The Steamless-unpacked exe, extracted
   asset bytes, golden screenshots produced from the original — all live
   under `vendor/` or sibling gitignored paths.
2. The repo ships only: source code, format specifications (text), test
   harness, build tooling, documentation. Anyone cloning the repo gets
   nothing they couldn't write themselves.
3. Behavioural fidelity > byte fidelity. Save file compatibility is required;
   pixel-perfect rendering is not (but is the default goal).

## 3. Tech stack (decided 2026-05-19)

| concern              | choice                                         |
|----------------------|------------------------------------------------|
| language             | C (C11)                                        |
| build toolchain      | mingw-w64 i686 cross compiler (32-bit Win32)   |
| graphics             | DirectX 8 or 9 (TBD post-unpack), called direct |
| audio                | DirectSound / dsound.lib (TBD)                  |
| input                | DirectInput (TBD)                              |
| portability          | phase 1: Win32 only; phase 2: abstract backend  |
| license              | MIT                                             |

Why C over C++/Rust/Zig: matches decompiler output (Ghidra emits C), minimal
translation friction. We can always uplift later.

Why DirectX direct rather than SDL2: the original calls DirectX. Wrapping
DirectX-for-DirectX makes the test harness simple — same render path, same
output, simple frame diffs. SDL2 would force us to tolerate rasterization
differences from day one.

## 4. Target binary — what we know

The original `recettear.exe`:

- 32-bit PE (Machine 0x14c = i386).
- **SteamStub** packed: `VLV` signature at offset 0x80. Imports, strings, and
  most executable content are obfuscated until unpacked. **Steamless v3.1.0.5
  (atom0s)** can unpack it.
- 5.6 MB on disk (highly compressed/encrypted prior to unpack).
- DOS stub followed by PE at 0x110 ("PE\0\0L\x01\x07\x00").

Game data:

| path                  | contents                                              |
|-----------------------|-------------------------------------------------------|
| `recettear.exe`       | main game binary (SteamStub-packed)                   |
| `custom.exe`          | config tool, reads/writes `recet.ini`                 |
| `recet.ini`           | display + control settings                            |
| `bin/data###.bin`     | custom packed archives (12+ files; format TBD)        |
| `bin/se/*`            | SFX (need to inspect format)                          |
| `bgm/*.wav`           | music as plain WAV                                    |
| `xfile/*/*.x`         | DirectX `.x` text-format 3D models (`xof 0303txt 0032`)|
| `xfile2/*/*.x`        | same — boss/enemy models                              |
| `ef/effect*.dat`      | particle/effect scripts (format TBD)                  |
| `bmpdata.bin`         | bitmap atlas? sprite data? (format TBD)               |
| `lnkdatas.bin`        | linker/index file? (format TBD)                       |
| `recet_op.wmv`        | opening movie (standard Windows Media Video)          |
| `manual/manual.htm`   | HTML manual                                           |

The `xfile/` directory naming reveals area/asset organization:
- `city`, `shop`, `sougen` (meadow), `tani` (valley), `mori_dun` (forest dun),
  `cave_dun`, `g_dungeon`, `koku_last`, `ruri_last`, `tree_dun`, `turibasi`
  (bridge), `door`, `jihanki` (vending machine), `jutan` (carpet), `table`,
  `tree`, `iseki` (ruins), `dun_box`, `etc`, `uturikomi00` (reflection?).
- `xfile2/`: `boss_omu`, `crystal_gorem`, `d_golem_g`, `d_kani` (crab),
  `d_kurage` (jellyfish), `g_lat`, `golem_g`, `kani`, `kurage`, `maou`
  (demon king), `vector`.

These are vanilla DirectX retained-mode models (open spec), so geometry is
trivially extractable. The interesting RE work is **how the engine indexes,
loads, and renders them** — i.e., the code path inside `recettear.exe`, not
the file format.

## 5. Phased roadmap

### Phase 0 — Bootstrap (THIS PHASE)
- [x] Decisions: language, target, license.
- [x] Flake with full toolchain.
- [x] Directory structure, gitignore, license, readme.
- [ ] `tools/setup.sh` — game symlink + Steamless unpack workflow.
- [ ] `tools/ghidra-headless.sh` — batch decompilation pipeline.
- [ ] `tools/contact-sheet.py` — visual diff helper.
- [ ] `tools/smoke-test.py` — wine+Xvfb run harness skeleton.
- [ ] `tools/extract/xfile.py` — first format extractor (DirectX .x).

### Phase 1 — Surface mapping
Goal: understand the binary's overall structure without writing replacement
code yet.

- Steamless-unpack the exe.
- Run Ghidra auto-analysis. Export full decompiled C tree to
  `docs/decompiled/` (gitignored).
- Identify entry point, WinMain, and the main loop.
- Enumerate imported DLLs and resolve which DirectX version is targeted.
- Map high-level subsystems: window/init, input, archive loader, renderer,
  audio, scripting, save system.
- Document each subsystem under `docs/findings/<subsystem>.md`.

### Phase 2 — File format extractors
Goal: parse every game asset format from outside the engine. Each
extractor is independent code + a spec doc + tests against the user's
game files.

Priority order:
1. `bin/data###.bin` — almost certainly contains scripts/strings; cracking
   this unlocks everything else.
2. `bmpdata.bin` / `lnkdatas.bin`.
3. `ef/effect*.dat` (particles).
4. `xfile/*.x` (already a known format — easy first win to validate the
   tooling pipeline).
5. SFX inside `bin/se/`.

Each format gets a `docs/formats/<name>.md` spec and `tools/extract/<name>.py`.

### Phase 3 — Skeleton drop-in
Goal: a `openrecet.exe` that opens a window, initializes DirectX, exits
cleanly. Boot smoke test matches original behavior up through the title
screen splash.

- WinMain + classic Win32 message pump.
- Read `recet.ini` (must be byte-compatible with original — same keys).
- DirectX device creation matching original's call sequence (traced via
  Frida and/or wine's `+d3d` debug channel).
- Splash + title screen rendering.

### Phase 4 — Subsystem-by-subsystem fill-in
Translate Ghidra-decompiled C into hand-tuned C for each subsystem.
Replace one at a time; the rest can fall back to stubs that abort or
diff against the original via "harness" interception.

Order driven by user-facing milestones:
1. Title screen → main menu → settings → exit.
2. Save/load on the title screen — must read original save files.
3. Item shop scene (the eponymous loop).
4. Dungeon scene (combat, AI).
5. Cutscenes / scripted sequences.

### Phase 5 — Conformance & cleanup
- Full behavioral test suite: golden frame diffs, save/load round-trip,
  scripting input replay, audio mixing snapshots.
- Document open quirks / known bugs.

### Phase 6 (deferred) — Portability
Abstract DirectX behind a thin backend; add SDL2/Vulkan or OpenGL3 path.
Not in scope until phase 5 is solid.

## 6. Workflow & autonomy

The whole project is designed so the agent can do most of the mechanical
work without a human in the loop.

### Windows `.exe` execution policy under WSL2

WSL2 has `WSLInterop` registered as a `binfmt_misc` handler for `MZ`-prefixed
files, so any `.exe` invocation is transparently handed off to the Windows
host. **We use WSLInterop for everything** — setup, exploration, *and* the
automated test harness.

**Why not wine** (revised 2026-05-19 after first attempt):

- Modern nixpkgs wine for 32-bit Win32 is fragile:
  `wineWow64Packages.stagingFull` builds cleanly but its new wow64 mode
  skips the 32-bit `syswow64/` layer, so 32-bit Recettear fails to load
  `kernel32.dll`. The deprecated `wineWowPackages.stagingFull` (classic
  dual-arch) works but is no longer pre-built in the binary cache — would
  build from source per machine, slow and fragile.
- WSL2 + WSLInterop is rock solid; the host Windows is always available.
- The trade-off — host runtime isn't pinnable per flake — is acceptable
  because "original vs ours" diffs run on the **same host in the same
  session**, so the runtime is held constant per-run.

**Trade-offs of dropping wine:**

- ❌ Can't run tests on pure-Linux CI without a Windows host.
- ❌ Tests pop a window on the user's desktop (we work around this by
  capturing back-buffer frames *inside the exe* once the
  `--capture-to <dir>` flag is wired into `src/main.c`, instead of
  scrot-grabbing the visible desktop).
- ✅ Zero setup cost.
- ✅ Real Windows DirectX 8 (the runtime EGS shipped against).
- ✅ Sub-second harness invocation (no Xvfb startup, no wineprefix
  bootstrap).

### Frame capture strategy under WSLInterop

The exe itself will save back-buffer frames to disk when invoked with
`--capture-to <win_path>`. That's how we get pixel-deterministic frames
(exact back-buffer contents, no window decorations, no compositor jitter)
even though the exe runs on the user's actual Windows desktop.

For the **original** (which can't be modified): we'll use a Frida hook
on `IDirect3DDevice8::Present` to grab the back buffer, when we need
golden frames. Implemented opt-in via `tools/smoke-test.py --frida-capture`.

### Tooling layers

```
   Original recettear.exe              Our openrecet.exe
            │                                 │
            ▼                                 ▼
        Steamless              ┌──── mingw32 (i686-w64-mingw32-gcc)
            │                  │
            ▼                  ▼
     unpacked recettear.exe   openrecet.exe
            │                                 │
            ├─────────► Ghidra headless ──────┤
            │           (batch decompile)     │
            │                                 │
            ▼                                 ▼
         wine + Xvfb ─────────► frame capture (scrot/ffmpeg)
            │                                 │
            └─────► contact-sheet.py (side-by-side downscaled grid)
                              │
                              ▼
                       pytest assertions
                       (SSIM, structural diff)
```

### Subagent / model split

- **Sonnet** (most work): translate one Ghidra function to C, write/run one
  extractor against the user's game files, run the smoke harness, build a
  contact sheet, file findings to `docs/findings/`.
- **Opus** (orchestrator + hard problems): planning, decoding obfuscation,
  reading test failures, choosing what to do next.

When delegating, brief Sonnet agents with:
- Exact paths and addresses.
- The decompiled C snippet (or instructions to fetch from `docs/decompiled/`).
- The target file in `src/`.
- The test to run before reporting done.
- A length cap on the response.

### Visual verification protocol

For every screen / scene change OpenRecet renders:

1. Run original under `tools/smoke-test.py --target original --scene <name>`.
2. Run ours under `tools/smoke-test.py --target openrecet --scene <name>`.
3. `tools/contact-sheet.py --left tests/golden/<scene>/ --right runs/<id>/<scene>/`.
4. Inspect the side-by-side sheet. Don't accept on pixel diff alone — eyes
   on the contact sheet for at least one frame per scene.

For contact sheets, default to 320×240 per tile. Add `--zoom <rect>` to
crop a region at full resolution when small differences need inspection.

Ranked harness-improvements plan lives in `docs/harness-roadmap.md`
(auto contact-sheet on smoke runs, per-pixel diff overlays,
`--audio-trace` JSON, retail-side Frida instrumentation including
state-forcing hooks for deterministic golden frames without an
interactive play-through).

### Always-reproducible

Every artifact derived from the original game (decompiled C, unpacked exe,
golden frames) is regenerable from the user's local game install via a
single script. Nothing in the repo depends on a derived file living
on disk.

## 7. Risk register

| risk                                       | mitigation                                                     |
|--------------------------------------------|----------------------------------------------------------------|
| Steamless fails on this version            | Try Steamless v3.1.0.5 first; fallback Steamless 3.2 or manual `IDA + scyllaHide` style approach. The DRM is well-documented. |
| `bin/data###.bin` is heavily obfuscated    | Approach via Ghidra: find the loader function, follow XOR/key derivation, write extractor from that — no need to guess. |
| Original requires Windows-specific DirectX behavior wine reproduces incorrectly | Test target: original-under-wine vs ours-under-wine. They share wine quirks. For cross-checks user can run both natively on Windows. |
| Ghidra decompiler output too noisy         | Use radare2/retdec for cross-checks. Manual cleanup with patience. |
| Engine uses inline assembly / SSE intrinsics | C with `__m128` intrinsics, or guarded inline asm when needed. Cross-check with retdec. |
| Project scope creep into Chantelise port   | Out of scope. Note if engine code is shared but don't pursue.    |
| Time sink on opening movie codec           | `recet_op.wmv` plays via Windows Media Player COM. Stub with a black-frame timer in phase 1; revisit later. |
| Wine version drift                         | Pin via flake (currently wine 11.0). Bump deliberately, retest. |

## 8. Open questions

- ~~Which DirectX version?~~ **DirectX 8** — confirmed 2026-05-19. See
  [`findings/imports-and-layout.md`](findings/imports-and-layout.md).
  Loaded dynamically via `LoadLibraryA("d3d8.dll")` with fallback to
  `d3d8d.dll`. Fixed-function pipeline (no shaders).
- Does the engine use DirectInput, DirectSound, or newer APIs?
  **DirectInput 8 confirmed** (2026-05-19) — `DirectInput8Create` symbol
  resolved in decompiled output. DSOUND still TBD — likely also dynamic,
  or audio routes through `WINMM` (`waveOut*` / `mciSendString` for the
  WMV opening movie).
- Is `bin/data###.bin` a single archive split across files, or independent
  files? (Header bytes look scrambled but consistent — likely XOR with a
  rolling key.)
- Is there a fixed-function vs shader split? Recettear is 2007 — likely
  fixed-function pipeline.
- Are there COM interfaces to anything besides DirectX (e.g., a custom
  scripting engine)?

## 9. References

- Ghidra: https://ghidra-sre.org/
- Steamless: https://github.com/atom0s/Steamless
- DirectX `.x` file format: well-documented in legacy DirectX SDK docs.
- Recettear Wiki (for naming/lore context only): https://recettear.fandom.com/
- PCGamingWiki Recettear page: https://www.pcgamingwiki.com/wiki/Recettear

Sources cited come from public preservation/RE community resources; none
contain proprietary EasyGameStation engine details (the engine has not been
publicly reverse-engineered prior to this project, as far as is known).
