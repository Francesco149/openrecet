# OpenRecet — Progress Log

Reverse-chronological log of meaningful changes. Auto-generation TBD once
the test harness has coverage metrics worth reporting.

## 2026-05-20 — Phase B [2/15]: `data/config.idx` parser

**Subsystems landed:**
- `src/tables_config.{c,h}` — pure-C parser for FUN_00475270 block #2.
  Five live keys (`kanjioff`, `edgewi`, `effectmode`, `edgedel`, `font`)
  + one dead key (`makefont` — the engine matches 8 bytes against the
  bare word but assigns to nothing; we mirror the dead check). The
  `font:` value is copied as raw bytes into a 256-byte fixed buffer
  with safe truncation on overlong input.
- `src/tables.c` — replaced the config.idx stub with a real loader.
  Path-mismatch quirk still sidestepped via the read-side spelling
  (`"data/config.idx"`) for both `storage_get_size` and `storage_read`.
- `docs/formats/data-text.md` — appended a config.idx section with
  full key table, dead-makefont quirk, and the line-terminator
  handling difference from buysell.txt.
- `tests/test_tables_config.c` — 7 cases: empty input, all five
  live keys parsed together, `makefont:` no-op, SJIS font name
  (`ＭＳ Ｐゴシック`), font over-length truncation at the 256-byte
  cap, comment-only file (everything `/`-prefixed → all defaults),
  vendor-shape end-to-end (only `edgewi=2 edgedel=6` active).

**Boot verification:** stderr now shows
`tables: data/config.idx — 950 bytes (kanjioff=0 edgewi=2 edgedel=6 effectmode=0 font=(default))`,
matching the shipping vendor file's active key set exactly.

**Test status:** 44 tests pass (up from 37), no fails, no skips.

## 2026-05-20 — Phase B [1/15]: `data/buysell.txt` parser

**Subsystems landed:**
- `src/tables_buysell.{c,h}` — pure-C parser for FUN_00475270 block #7.
  Mirrors the engine's "match every prefix on every non-comment line"
  structure with five key forms: `ok:` (debug flag), `客番号:` / `種類:`
  (SJIS scalars), and `msg%02d:` / `rmsg%02d:` (two 20-int arrays).
  Engine-global instance `g_buysell`; tests use the out-parameter form.
- `src/tables.c` — replaced the buysell stub with a real loader that
  storage_reads the file, calls `tables_parse_buysell`, and logs the
  three scalars to the boot trace.
- `docs/formats/data-text.md` — new format-spec doc for the
  `data/*.txt` + `idx/*.idx` group. Documents shared conventions
  (Shift-JIS, CRLF, leading-`/` comments, two format families) plus a
  full section for buysell.txt (key table with byte-level SJIS
  identification, engine-side global addresses, the
  rmsg-before-msg in-memory layout quirk, vendor file sample).
- `tests/test_tables_buysell.c` — 8 cases covering empty input,
  comment-only files, the `ok:` toggle, the two SJIS scalar keys
  (using the exact byte sequences from the engine's `.data`),
  msg/rmsg arrays at boundary indices 0 and 19, EOF-without-newline,
  embedded-`\0` early-termination, and a vendor-shape end-to-end
  fixture that reproduces the actual file's CRLF + SJIS + comment
  layout with non-zero values.

**Boot verification:** stderr now shows
`tables: data/buysell.txt — 504 bytes (debug=0 kyaku=14 kind=2)`,
matching the vendor file's expected values (debug commented, customer
14, kind "about"=2). All other 16 stubs continue to log size lines as
before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 37 tests pass (up from 29), no fails, no skips.

## 2026-05-20 — FUN_00475270 ("init indexfile ok") skeleton + Phase A discovery

**Subsystems landed:**
- `docs/findings/tables-loader.md` — discovery doc for the gameplay
  tables loader: caller context (it's the boot trace step right after
  `init render ok`), full file list with sizes and per-block C-line
  ranges, helper identities (storage_get_size / storage_read / atoi /
  atof / free), the two format families observed (`/key:value` for
  `config.idx`; CSV-with-comments for the `data/*.txt` files), and
  the proposed one-commit-per-file Phase B plan.
- `src/tables.{c,h}` — skeleton dispatcher `tables_load_all()` calling
  fourteen stub loaders (one per file) plus a tutorial-loop stub. Each
  stub exercises `storage_get_size` + `storage_read` end-to-end and
  logs the byte count to stderr; the real parsers will replace the
  printf in Phase B without touching the dispatcher.
- `src/main.c` — wired `tables_load_all()` into the boot chain at the
  TODO marker that was already pinned for `FUN_00475270`. Position
  matches the engine's `init render ok → [HERE] → init fontsys ok`
  ordering.

**Engine quirks documented this turn:**
1. `FUN_00475270` calls `storage_get_size` and `storage_read` with
   different `.data` addresses in every block — usually two interned
   copies of the same path string. For `config.idx` the developer
   accidentally typed two **different** spellings (get_size with
   `"config.idx"`, read with `"data/config.idx"`), so the original
   silently `malloc(0+10) = 10` and overruns by 940 bytes on every
   boot. Our stub uses the read-side spelling to avoid the bug.
2. Tutorial format string is `"data/tuto%d.txt"` (no underscore).

**Boot verification:** stderr trace from `openrecet.exe
--max-duration-ms 2000` shows all 17 storage reads succeed (14 fixed
+ 3 tutorials), and the loop correctly stops at `tuto4.txt`. Several
files come back larger than the lnkdatas size because they have a
bmpdata-overlay patched version (e.g. `enemy.txt`: 2801 → 3589).

**Test status:** 29 tests pass (no new tests yet — Phase B will add
per-file fixture tests as each parser lands). Boot smoke clean.

## 2026-05-20 — FUN_004341d4 bookkeeping (file-size helper)

Pinned candidate #2 closed as already-done. `FUN_004341d4` is the
trivial `fseek(0,SEEK_END); ftell; fseek(0,SEEK_SET)` file-size
helper, and it was already faithfully translated as
`storage_file_size` in `src/storage.c:139` during the
`storage_init`/`FUN_004341fe` port (with an in-file comment naming
the original). All four in-engine call sites we've ported (the ones
inside `storage_init` itself) route through it.

The other three inlined `fseek/ftell/rewind` idioms in `src/tga.c`,
`src/sprite.c`, and `src/lnkdatas_hash.c` were written by us, not
ports — they intentionally check the fseek/ftell return values
(the original doesn't). Left untouched so the defensive coverage
stays in place; promoting a 5-line static into a shared util module
would have been premature abstraction. Dropped from the
session-starter pin list.

## 2026-05-20 — lnkdatas content read + LZSS

**Subsystems landed:**
- `src/lnk_lzss.{c,h}` — port of FUN_004349e5 (the lnkdatas LZSS decoder).
  Pure C, no Win32 surface.  ~50 LOC. Stream is self-delimiting via the
  back==0 sentinel, so no input size is required.
- `src/storage.c` — extended `storage_get_size` and `storage_read` to fall
  back to the lnkdatas index when the asset isn't in the bmpdata overlay.
  Adds a 1-deep `bin/data%03d.bin` FILE* cache and a 10 MiB chunk-spanning
  reader (handles entries that straddle a `bin/data*.bin` boundary).
  Skips the original engine's 3× `Sleep(500ms)` retry loop around the
  fopen — that was robustness against transient I/O on 2007 spinning
  drives, not load-bearing for a modern Steam install.
- `tests/test_lnk_lzss.c` — 7 synthetic unit tests covering single
  literals, short / extended back-references, self-overlap RLE, the
  end-of-stream sentinel mid-control-byte, high-bit back-distances, and
  mixed flags within one control byte. Plus a vendor round-trip that
  iterates every entry in `vendor/original/lnkdatas.bin`, reads its
  slice (across chunk boundaries as needed), decompresses, and verifies
  the result length matches the declared `dsize` + that a one-byte
  output canary is intact.

**Two case-sensitivity quirks worth knowing:** the bmpdata branch of
`FUN_00434585` / `FUN_004346bf` does case-insensitive name matching
(A..Z folded to a..z) over 88 bytes; the **lnkdatas branch does a
straight byte compare** over 128 bytes — no fold. Our port mirrors
both. Callers relying on case-insensitive lookup must hit through the
bmpdata path.

**Pixel-exact validation:** rebuilt the standalone harness
`/tmp/storage_extract.exe` (built from `src/storage.c` with
`-DSTORAGE_TEST_EXTRACT`) and confirmed byte-identical output vs the
Python reference (`tools/extract/data-bin.py`) on 5 entries including
4 chunk-straddling ones (`xfile/koku_last/mahoujin.tga`,
`xfile/wall/kabe_check.bmp`, `bmp/chr/chr31.bmp`,
`bmp/worldmap_yugata.bmp`). Hashes match (SHA-256).

**Test status:** 29 tests pass (up from 21), no fails, no skips.
Sanitizer-clean. ASan caught two bugs while writing tests — both in
the *test fixtures*, not in the decoder: a mis-computed control byte
in `test_lnk_lzss_self_overlap` (0x28 should have been 0x30) and a
use-after-free on the canary value in the vendor round-trip. Good
ASan-pays-for-itself moment.

**Engine smoke:** boot scenario `tools/smoke-test.py` still exits 0
in ~4s on the rebuilt exe, debug-magenta clear color unchanged.

**Next pin (per session-start):** `FUN_00475270` is the big one —
3965 decompiled lines of `data/*.txt` parsing (item / kyaku / chara
/ enemy gameplay tables) plus `idx/stage.idx` and `idx/config.idx`.
Will likely need splitting across multiple commits.

## 2026-05-20 — Sanitizer-instrumented unit tests

**Subsystems landed:**
- `tests/Makefile` — Linux-native test harness. Host gcc +
  `-fsanitize=address,undefined -fno-sanitize-recover=all`. Run with
  `make -C tests run`.
- `tests/t.h` — dependency-free assertion macros (T_ASSERT, T_FAIL,
  T_SKIP) + UBSan-safe byte writers.
- `tests/test_main.c` — runs registered tests, supports name-substring
  filter via `argv[1]`, exits non-zero on any failure (skips don't
  count).
- `tests/test_{bmp,tga,bmp_lzw,lnkdatas_hash}.c` — 21 tests covering
  every audited code path in the four portable decoders.

**Why now:** the Win32 sprite loader just landed, and every decoder
ingests user-controlled bytes (BMP/TGA from `bmpdata.bin`, LZW slices,
CRC over the whole lnkdatas blob). Memory bugs in these can pass
pixel-equality checks while still being broken. Valgrind/ASan can't
run Win32 PE binaries, so the natural split is "Linux test target for
the portable .c files". The Win32 layer stays exercised by smoke
tests.

**Doc fix discovered during test writing:** `src/lnkdatas_hash.{c,h}`
called the engine's hash "CRC-16/CCITT-FALSE". It's *shaped* like
CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, final invert) but
the feedback step uses **subtraction** instead of XOR. The standard
CCITT-FALSE check value for "123456789" is 0x29B1; ours is 0xF5B7
because of borrow propagation. Cross-checked against
`/opt/src/recettear-repacker/crc.py`.

**Sanity check the harness catches real bugs:** temporarily injected
a 1-byte OOB read past the BMP pixel buffer and reran. ASan
pinpointed the exact `bmp.c:77` line with the heap region details.
Reverted; all 21 tests pass green.

## 2026-05-20 — `FUN_0047193c` ported — engine-style sprite loader

**Subsystems landed:**
- `src/bmp.{c,h}` — 24-/32-bit BI_RGB DIB decoder with color-key
  application (engine passes `0xFF00FF00` to D3DX → we test exact-match
  pure-green and zero the alpha). Top-down + bottom-up.
- `src/tga.c` extended — now also handles Type 10 RLE, plus an
  `tga_load_mem(buf,size,*img)` variant so the loader can decode
  storage-fetched bytes without round-tripping through disk.
- `src/sprite.c` — new `sprite_load(dev, name, w, h, *out)` entry
  point. Mirrors `FUN_0047193c`: tries `fopen(name,"rb")` first, falls
  back to `storage_read(name)`, sniffs `'BM'` → BMP-with-key vs TGA,
  decodes, uploads via the existing `sprite_create`.
- `src/main.c` — replaced `--show-tga <path>` (direct-file shim) with
  `--show-sprite <name>` that routes through the new `sprite_load`.

**Identification fix carried into `docs/findings/texture-loader.md`:**
the earlier write-up had the disk and storage calls swapped (claimed
the engine tried storage first with disk as fallback). Re-reading
`FUN_005038b0` as a thin `fopen` wrapper (forwards to `FUN_00503890`
with a `0x40` buffer-size hint) flipped it — **disk is tried first,
storage second**. The user-facing implication: external/mod overrides
on disk take precedence over the packed asset, consistent with how
`recettear-repacker` works.

**Validation (pixel-perfect, max diff 0/255 in all four cases):**
- Storage path: `--show-sprite bmp/ivent/ed_kasi11.tga` (resolves via
  `storage_read` from `bmpdata.bin`) renders byte-identical to a
  reference Python decode composited over the debug-magenta clear
  color, across all 512×32 pixels.
- Synthetic disk fixtures (built in-place, then cleaned up): a 64×64
  Type-2 TGA, a 64×64 Type-10 RLE TGA, and a 64×64 24-bit BMP with one
  half pure-green keyed and the other half opaque blue. All three
  round-trip 0 mismatches against the expected composite.

**Not yet engine-accurate:** D3DX-style resampling to `(expected_w,
expected_h)` is still skipped. Every audited asset ships at native
resolution, so this matters mainly for forward-compat / mod paths
that intentionally scale.

**Lifecycle fix (orphan-window cleanup, follow-up commit):** ad-hoc
`timeout 3 openrecet.exe …` runs kept leaving the host's Windows
side with orphan windows because `g_paused` blocks the main loop in
`WaitMessage` when the window loses focus — any deadline check
inside `tick_and_present` is never reached. Added `--max-duration-ms
<ms>` (also taken up by `tools/smoke-test.py`) that registers
`SetTimer` → `WM_TIMER` → `DestroyWindow`, which fires regardless of
pause state. Smoke harness now reaches `exit=0` gracefully instead
of falling through to SIGTERM/taskkill.

Next-milestone candidates: lnkdatas content-read path (so `storage_read`
also services `bin/data_NNN.bin` + the LZSS decompressor at
`FUN_004349e5`); `FUN_004341d4` standalone port (mostly mechanical);
diving into `FUN_00475270` (gameplay-text-table parser, 3965 lines —
needs splitting across commits).

## 2026-05-20 — `bmpdata.bin` LZW decoder + storage_read overlay path

**Subsystems landed:**
- `src/bmp_lzw.{c,h}` — 12-bit MSB-first LZW decompressor. Translation
  of `FUN_00434b32` (main loop), `FUN_00434c2c` (bit reader), and
  `FUN_00434ca9` (dict-chain walker). Dictionary frozen at 3839 entries,
  matches `recettear-repacker/bmp_unpack.py` exactly.
- `src/storage.{c,h}` extended — now also opens `bmpdata.bin`, slurps
  it into memory, validates the hash sentinel `0x21dc`, and exposes
  `storage_get_size(name)` + `storage_read(name, dst)`. Mirrors
  `FUN_00434585` (size lookup) and `FUN_004346bf` (read into buffer) for
  the bmpdata branch.

**Identification fix:** `FUN_00475270` (originally pinned as "likely
bmpdata.bin LZW loader" in PROGRESS) turned out to be the global
gameplay-text-table loader — a 3965-line parser for `data/item.txt`,
`data/chara.txt`, the `idx/stage.idx` chain, etc. The real LZW lives in
the much smaller `FUN_00434b32` + helpers, called lazily from the
storage read path. Plan annotation corrected.

**Engine deviation, by design:** the engine's `FUN_00434b32` doesn't
handle code 256 (LZW reset/EOS) — it walks past the dict base on the
sentinel and emits a few garbage bytes past the caller's `dsize`
buffer. Benign in the shipping game (callers tolerate the overrun) but
we'd rather not write past the asked-for size, so our decoder honors
256 explicitly. End result: byte-for-byte identical with
`bmp_unpack.py` output, not byte-for-byte identical with the engine's
overrun-prone output.

**Validation:** all 22 entries in the shipping Steam `bmpdata.bin`
round-trip through `storage_init → storage_read → stdout` to
byte-equal output vs the Python reference (see `/tmp/storage_diff.py`).
Boot smoke (`tools/smoke-test.py`) still green — exit signal, 3 frames
captured, no early-exit error from the now-stricter `storage_init`
(which is required to find `bmpdata.bin`).

**New format spec:** `docs/formats/bmpdata.md` — 84-byte names, 3 ×
int32 (dsize/offset/csize) per entry, 96-byte stride, 12-bit LZW
payload, hash sentinel `0x21dc`.

Next-milestone candidates (unchanged): `FUN_0047193c` (proper sprite
loader using `storage_*` with BMP + green-key + RLE-TGA — now possible
because `bmpdata` lookups work), `FUN_004341d4` standalone port, or
diving into the `FUN_00475270` gameplay-data parser.

## 2026-05-19 — Render-layer init ported (`FUN_00454e69` + `FUN_004038e4`)

**Subsystem landed:** `src/layers.{c,h}`. The "init render ok" hand-off
isn't device creation (that's step 11) — it's the engine fanning
`GetDeviceCaps` + back-buffer-desc + the live device pointer out into
its 24 per-layer state objects (each 0x2f0 bytes). Two arrays:
`g_layers_b[20]` (loop, `DAT_073da2f0` stride 0x2f0) and `g_layers_a[4]`
(unrolled in asm at `DAT_073cba20`/`+0x2f0`×3). See
`docs/findings/winmain-and-bootstrap.md` §"Render-layer init" for the
RE writeup + offset table.

**Layout corrections from the earlier guess:**
- The previous notes claimed the loop "zeros" the structs via
  `FUN_004038e4`. It doesn't — it actively writes `device` (`+0x108`),
  the back-buffer `D3DSURFACE_DESC` (`+0x10c`, 32 bytes), and a copy of
  `D3DCAPS8` (`+0x12c`, 212 bytes), then nulls `+0x200`.
- The 20-element loop is only *one* of two arrays; the 4 unrolled
  trailing calls operate on a *separate* 4-element array — easy to miss
  from the decompiler output because Ghidra strips the ECX setup before
  each thiscall.

**Skeleton wiring (`main.c`):**
- Removed the placeholder `IDirect3D8_GetDeviceCaps` from `init_render`
  — the real owner is now `layers_init`.
- `layers_init(g_d3d, g_dev)` slotted in after `input_init`, matching
  the original's `…dinput ok → init render ok` ordering (the previous
  comment had this misplaced).
- Bootstrap-order comments now mirror the actual call sequence.

**Why the struct is field-by-field (not a byte blob):** mingw's `d3d8.h`
ships `D3DCAPS8 = 212`/`D3DSURFACE_DESC = 32` — exact match to the
original's `rep movsl 0x35` and `0x12c−0x10c = 0x20`. Five
`_Static_assert`s on the known offsets + total size catch any future
header drift at build time.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--duration 4 --capture` — debug magenta `(160, 32, 96)` reads flat
across all 4 captured frames; no crash on init or shutdown.

Next-milestone candidates (unchanged): `FUN_00475270` ("init indexfile
ok" — likely `bmpdata.bin` LZW loader, cross-ref
`/opt/src/recettear-repacker/bmp_unpack.py`), `FUN_004341d4` (file-size
helper, quick mechanical port), or porting `FUN_0047193c` properly to
read assets via `storage_*` with BMP+green-key + RLE-TGA support.

## 2026-05-19 — Project bootstrap

**What landed**
- Decisions (see [`PLAN.md`](PLAN.md) §3): C + mingw-w64 32-bit, DirectX
  direct, MIT, Win32-first drop-in.
- `flake.nix` with full RE toolchain (ghidra 12, radare2, rizin, cutter,
  retdec, imhex, wine staging, frida-tools, mingw32 i686 cross compiler,
  python env with construct/scikit-image/pillow/opencv, xvfb-run, scrot,
  ffmpeg, imagemagick, pandoc).
- Directory structure: `src/`, `tests/`, `tools/`, `docs/`, `vendor/` (gi),
  `ghidra/` (gi).
- Plan, README, MIT license, `.gitignore` that aggressively protects any
  derived game data, `.editorconfig`.

**What we know about the original**
- `recettear.exe`: 32-bit PE, **SteamStub-packed** (VLV signature @0x80),
  5.6 MB on disk.
- `custom.exe`: ~462 KB config tool reading `recet.ini`.
- Assets: `bin/data###.bin` (custom archives, format TBD), `xfile/*.x` and
  `xfile2/*.x` (DirectX retained-mode `.x` text models — open spec),
  `bgm/*.wav`, `ef/effect*.dat`, `bmpdata.bin`, `lnkdatas.bin`,
  `recet_op.wmv`.
- `recet.ini` exposes: `winmode`, `fps`, `dispfps`, `usefog`, `usemipmap`,
  `usetree`, `windowpos`, `uselighttex`, `nolight`, `easydisp`, `bgnodisp`,
  `texlevel`, `toorioff`, `s_easydisp`, `sfnouse`, `pfnouse`,
  `fontmode1`/`fontmode2`, `screen`, `texmode`, `mapmode`, `demomode`, plus
  `pad##` / `skill##` key bindings and `[config] se`/`mu` audio levels.
  These are the engine's main feature toggles — each one is a hint about
  a code path we'll meet.

**Note on wine vs WSLInterop** — WSL has `WSLInterop` registered as a
binfmt handler, so `.exe` invocations run natively on Windows by default.
We use that for Steamless and for casual play. The automated test harness
still uses wine + Xvfb so that (a) the runtime is pinnable via the flake
and (b) original-vs-ours diffs share the same backend and don't surface
wine-vs-Windows differences as phantom bugs. See `PLAN.md` §6 for details.

**Tooling landed**
- `tools/setup.sh` — symlinks game, runs Steamless via WSLInterop, prints sha256s.
- `tools/ghidra-headless.sh` + `tools/ghidra-scripts/ExportDecompiledC.py` —
  batch decompile every function to `docs/decompiled/` (gitignored).
- `tools/smoke-test.py` — Xvfb+wine runner with frame capture and SSIM diff
  against a golden run.
- `tools/contact-sheet.py` — single-set or side-by-side downscaled grids, with
  optional `--zoom` full-res crop strip.
- `tools/extract/xfile.py` — DirectX `.x` text-format summarizer + tree scanner.

**First extractor result (validates pipeline)** — running
`xfile.py xfile/city/dun_city00.x` on the user's Steam install reports:

| template               | count |
|------------------------|------:|
| Material               |    58 |
| TextureFilename        |    25 |
| Frame                  |    13 |
| FrameTransformMatrix   |    13 |
| Mesh                   |    12 |
| MeshMaterialList       |    12 |
| MeshNormals            |    12 |
| MeshTextureCoords      |    12 |
| MeshVertexColors       |     1 |

All **standard DirectX 9 retained-mode** templates. No custom extensions →
`xfile/` and `xfile2/` can be parsed with stock `D3DXLoadMeshFromX*` or any
open-spec parser. (Phase 2 will scan the full tree for a definitive answer.)

**Flake quirk** — `retdec` currently fails to build in nixpkgs (capstone
sub-build error). Disabled it. Ghidra + radare2/rizin cover decompilation
cross-checks. Re-enable if upstream fixes.

**Next** (Phase 1 entry)
- User runs `./tools/setup.sh` to unpack the exe.
- Then `./tools/ghidra-headless.sh` to produce `docs/decompiled/`.
- First subsystem to map: WinMain + main loop. Find `D3D*Create*` calls in
  the decompiled output → confirms DirectX version (likely 8 or 9).

---

## 2026-05-19 — Setup ran, first findings from unpacked binary

- ✅ `tools/setup.sh` ran successfully on the user's machine — Steamless via
  WSLInterop produced `vendor/unpacked/recettear.unpacked.exe` (5.0 MB,
  down from 5.6 MB packed; 7 PE sections → 6 sections; VLV signature gone).
- Fixed `tools/ghidra-headless.sh`: nixpkgs ghidra names its binaries
  `ghidra-<tool>` (e.g., `ghidra-analyzeHeadless`), not bare `analyzeHeadless`.

**New findings — recorded in [`findings/imports-and-layout.md`](findings/imports-and-layout.md):**

- **DirectX version is 8** (d3d8.dll / d3d8d.dll / D3DERR_*). Fixed-function
  pipeline only — no shaders, no HLSL compiler required.
- **DirectX is loaded dynamically** via `LoadLibraryA` + `GetProcAddress` —
  static imports are only `KERNEL32`, `USER32`, `SHELL32`, `WINMM`,
  `ole32`, `ADVAPI32`. Six DLLs total. Very tight.
- **`DirectXFileCreate` is used** for `.x` model parsing (open DX File API).
- **No `dsound.dll` / `dinput.dll` static imports.** Audio likely via
  `WINMM` (`mciSendString` / `waveOut*`) or dynamically-loaded DSOUND;
  input likely raw `USER32` `WM_KEYDOWN` / `GetKeyState`. To be confirmed.
- **Asset layout discovered from strings:** the binary references
  `bmp/item/item%02d.bmp`, `bmp/item_win.tga`, `data/item.txt`,
  `bin/se/.../*.bin`, etc. None of these paths exist on disk — confirming
  that `bin/data###.bin` archives contain the `bmp/` (TGA/BMP textures)
  and `data/` (plain text gameplay tables) trees. Cracking this format
  unlocks all 2D art and all gameplay data.
- **All UI/dialogue strings are inline in `.rdata`** — no string table,
  no `.po` files. i18n story is "rebuild the binary".

**Next**
- ~~Run `./tools/ghidra-headless.sh`~~ Done — 2620 functions decompiled.
- ~~Locate the function that opens `bin/data000.bin` → archive format~~
  Pre-empted: spec was already cracked by UnrealPowerz/recettear-repacker.
- Locate `WinMain` and the `LoadLibraryA("d3d8.dll")` site → document
  the window+device init sequence for the skeleton in phase 3.

---

## 2026-05-19 — Ghidra working, cross-references absorbed

**Ghidra:**
- Fixed the post-script: nixpkgs Ghidra 12 isn't built with PyGhidra, so
  `.py` scripts fail. Rewrote `ExportDecompiledC` in **Java** — works
  in plain headless mode without flags.
- Also fixed a latent bug: the script had `-deleteProject` set on the
  first import, which would have wiped analysis state. Removed.
- Result: **2620 functions** decompiled into `docs/decompiled/all.c`
  (6.3 MB), `by-address/*.c`, `by-name/*.c`, `functions.csv`.

**Cross-reference projects** (cloned to `/opt/src/`):
- **UnrealPowerz/recettear-repacker** — full spec for `bin/data*.bin`
  archives. Format: 10 MiB chunks of LZSS-compressed blobs indexed by
  big-endian `lnkdatas.bin`. Custom LZSS variant with 12-bit back-distance
  + MSB-first ctrl byte. `bmpdata.bin` is a separate LZW-compressed
  update overlay.
- **ribeena/RecettearXTools** — `.x` ↔ USD Blender 4.1 converter; useful
  for double-checking our `.x` parser.
- **just-harry/FancyScreenPatchForRecettear** — runtime widescreen
  patcher; useful as a map of engine offsets we'll want to understand.

**New format spec:** [`formats/data-bin.md`](formats/data-bin.md).

**Our own extractor:** `tools/extract/data-bin.py` — clean Python
reimplementation matching the spec. Validated against upstream:
**byte-identical** output on the current Steam build (1188 files extracted).
Run `./tools/extract/data-bin.py vendor/original --validate-against
/opt/src/recettear-repacker` to re-verify.

**Newly confirmed about the engine:**
- **DirectInput 8** (`dinput8.dll`) is also dynamically loaded — found
  `DirectInput8Create` symbol at `0x4a1cc0`.
- **C++ compiled with MSVC** — `vector_constructor_iterator` /
  `vector_deleting_destructor` indicate array new/delete scaffolding.
- **MFC is statically linked** — `RFX_Text_Bulk` (MFC ODBC field exchange)
  present. Probably leakage from `custom.exe` sharing libs; possibly
  engine uses some MFC for save serialization (TBD).
- PE entry is at `0x5046c7` — MSVC `__tmainCRTStartup`. `WinMain`
  symbolic name not yet auto-resolved.

**Next investigation targets**
1. Trace `__tmainCRTStartup` → `WinMain`. Rename in the Ghidra project.
2. From `WinMain`, find the `LoadLibraryA("d3d8.dll")` call → document the
   DX8 device-creation sequence. (Skeleton for phase 3.)
3. Read `recettear-repacker/crc.py` — the engine probably uses the same
   CRC as a path hash for `bmpdata.bin` lookups. Worth porting.
4. Optional: read `FancyScreenPatchForRecettear` patch sites to find
   resolution-clamping code (a likely candidate for an early test
   subsystem since the patches are small and well-isolated).

---

## 2026-05-19 — Engine bootstrap mapped end-to-end

Full writeup in [`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md).
Highlights:

- **WinMain at `0x47bfb3`**. Identified by the standard MSVC
  `__tmainCRTStartup(hInst=GetModuleHandleA(NULL), 0, lpCmdLine, nCmdShow)`
  call signature in the PE entry.
- **Engine internal name is "Azumanga"** — that's EGS's name for their
  custom engine (also powers Chantelise). Window class is literally
  `"Azumanga Main Window"`.
- **Window title is `"RECETTEAR Ver 1.108"`** — exact version string for
  drop-in compatibility.
- **Debug logger `FUN_0047aa31` is a 1-byte `return;` stub** — all
  logging compiled out in the release build. The `s_init_*` string
  constants remain in `.rdata` as breadcrumbs, which **gave us the full
  subsystem init order for free**:
  `start → strage → print → dinput → render → indexfile → fontsys →
  daoudio → fontsystem → systemtex → savefile → titletex → main loop`.
  (Note Japanese-English typos preserved: `strage`, `daoudio`.)
- **Main loop function: `FUN_0047be92`** — the game tick.
- **`Direct3DCreate8(0xDC)` at line 77975 of `all.c`** — `0xDC = 220 =
  D3D_SDK_VERSION`. Global `IDirect3D8 *` is `DAT_073dfcb8`.
- **DirectInput 8 init: `FUN_0047af52`** — keyboard + EnumDevices for
  joysticks, with axis range ±5000 and 100-unit deadzone.
- **Storage init: `FUN_004341fe`** — tries `lnkdata.bin` (JP name) first,
  falls back to `lnkdatas.bin` (EN name). Validates the index via
  `FUN_00474f14` which must return `-0x7456` (`0xFFFF8BAA`); this is the
  engine's integrity hash, almost certainly matches
  `recettear-repacker/crc.py`.
- **WndProc `FUN_0047b2e7`** handles `WM_CREATE`, `WM_DESTROY`,
  `WM_ACTIVATE` (pause + DI un/acquire), `WM_CLOSE` (confirm dialog in
  windowed), `WM_KEYDOWN` (ESC only — rest of input via DInput).

**Process docs added:** [`AGENT-WORKFLOW.md`](AGENT-WORKFLOW.md) — codifies
the Opus-orchestrator / Sonnet-subagent split + briefing template + stop
conditions. Read at the start of every new session.

**Stop point:** engine bootstrap is mapped. Logical next milestone:
either (a) write the phase-3 skeleton drop-in `src/main.c` matching the
init order, or (b) start translating the high-value individual functions
(`FUN_0047be92` game tick, `FUN_00474f14` integrity hash, the d3d8 wrapper
that calls `Direct3DCreate8`). User to choose.

---

## 2026-05-19 — Phase 3 skeleton drop-in runs

**`src/main.c` + `src/Makefile` written and building.** The skeleton
mirrors the bootstrap chain from
[`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):
high-resolution timer setup → window class register (`"Azumanga Main
Window"`) → CreateWindowExA (`"RECETTEAR Ver 1.108"`) → LoadLibraryA
(`d3d8.dll` → `d3d8d.dll` fallback) → `Direct3DCreate8(D3D_SDK_VERSION)`
→ `IDirect3D8::CreateDevice` → message pump with `PeekMessage`/
`WaitMessage`/`tick_and_present`. Each subsystem in the original's init
order is a `TODO` comment naming the `FUN_XXX` we still need to
translate.

**Builds at 77 KB** via `i686-w64-mingw32-gcc` from inside `nix develop`.
Static libs: `-ld3d8 -ldinput8 -ldsound -lwinmm -lgdi32 -luser32
-lkernel32 -lole32 -ladvapi32 -lshell32`.

Tick path currently does `Clear → BeginScene → EndScene → Present` with
a distinctive **debug magenta** clear color (`160, 32, 96`) so a working
boot is visually obvious vs a black-screen failure.

**Test harness pivoted to WSLInterop** (see updated `PLAN.md` §6):

- Modern nixpkgs `wineWow64Packages.stagingFull` skips the 32-bit
  `syswow64/` layer → 32-bit binaries fail to load `kernel32.dll`.
- `wineWowPackages.stagingFull` (classic dual-arch) builds from source on
  every machine, slow.
- WSL2 + WSLInterop is rock solid and runs the exe natively on Windows.
  Trade-off: tests pop a window on the desktop. We'll work around this
  with self-emitting back-buffer captures inside the exe
  (`--capture-to <dir>`, not yet wired).
- Wine dropped from the flake entirely.

`tools/smoke-test.py` rewritten — no Xvfb, no wine, no scrot. Launches the
exe via WSLInterop, captures exit code + duration + stdout/stderr + sha256.
First run: `openrecet.exe` ran cleanly for 3 seconds, was killed by
timeout (exit code -15, SIGTERM), `taskkill /F /IM openrecet.exe` confirmed
clean shutdown.

**Next**
1. Wire `--capture-to <dir>` into `src/main.c` — save back-buffer as 32-bit
   BMP every N frames, into the harness's `runs/<scenario>/<id>/frames/`.
2. Translate `FUN_00474f14` (the lnkdatas integrity hash) to validate our
   `tools/extract/data-bin.py` matches the engine's expected sentinel.
3. Start filling in subsystem stubs — first target: `FUN_004341fe`
   (storage init / lnkdatas loader) so the skeleton actually opens the
   game's index file. Good Sonnet-subagent task.

---

## 2026-05-19 — All three subagent tasks landed; capture pipeline works end-to-end

**Subagent 2: CRC hash port** — `src/lnkdatas_hash.{c,h}` +
`tools/extract/lnkdatas_hash.py`. Algorithm identified as
**CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, MSB-first, final
`~crc`). Validates byte-identical against `recettear-repacker/crc.py`;
on the real `lnkdatas.bin` (sha256 `6c5b93cf…`) returns `0x8BAA`
(= `-0x7456`, the engine's "valid" sentinel).

**Subagent 3: Storage init port** — `src/storage.{c,h}`. Caught a
critical detail the first writeup missed: **`FUN_004341fe` has two
distinct format paths**. The JP build's `lnkdata.bin` (singular) has a
5-byte header skipped + a `byte' = 0x01 - byte` payload transform +
sentinel `0xC5E1`. The EN build's `lnkdatas.bin` (plural) is raw +
sentinel `0x8BAA`. Both implemented. Findings doc
[`winmain-and-bootstrap.md` §"Storage init"](findings/winmain-and-bootstrap.md)
corrected.

**Subagent 1: Frame capture** — `src/main.c` gained `--capture-to <dir>`
+ `--capture-every N` CLI flags. Renders BMPs at intervals via
`GetBackBuffer → LockRect → fwrite`. Initially silent-failed; root cause
identified as needing `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` in the present
parameters AND capturing **before** `Present()` (we use
`D3DSWAPEFFECT_DISCARD` which makes post-Present back-buffer undefined).
Both fixed. Capture now runs at ~5000 FPS in the empty-tick state
(660 frames captured in 4 seconds of un-vsync'd rendering — capture
overhead is negligible).

**Subagent integration issues** worth noting for future use of the
AGENT-WORKFLOW pattern:
- The first attempt at subagent 1 (frame capture) failed because
  `isolation: worktree` requires an existing git commit — we have none
  yet. Re-ran without isolation; safe because the other two created only
  new files. **Action:** make an initial commit before relying on
  worktree isolation.
- Subagents 2 and 3 raced on the `lnkdatas_hash` signature: subagent 2
  used `(buf, size) → int16_t`, subagent 3 assumed `(size, buf) →
  uint16_t` and inlined a fallback impl in `storage.c`. Caused a
  duplicate-symbol link error. **Action:** when subagents share an
  interface, brief them with the exact signature, not "infer it".
- Subagent 3 caught the JP/EN dual-format detail in `FUN_004341fe` that
  the orchestrator (me, Opus) had missed in the initial writeup. Good
  outcome — second-pass careful reading by a fresh agent surfaced
  something a quick first read glossed over.

**End-to-end visual confirmation:**
- User reported seeing the debug-magenta window during a manual run.
- Captured BMP frame 60 center pixel = exactly `RGB(160, 32, 96)`.
- 4-tile contact sheet via `tools/contact-sheet.py` shows all magenta.
- Pipeline: mingw32 build → `openrecet.exe` (91 KB) → WSLInterop →
  Windows 32-bit process → `storage_init()` loads 1188 lnkdatas entries
  via the EN path → DX8 device with `LOCKABLE_BACKBUFFER` → tick loop
  → BMP captures → contact sheet → visual diff ready.

**Tooling fix:** `tools/contact-sheet.py` now sets
`ImageFile.LOAD_TRUNCATED_IMAGES = True` to bypass PIL's strictness on
32-bit BI_RGB BMPs with an X-padding byte. The BMPs are structurally
valid (file size, headers, pixel layout all correct — verified manually);
PIL treats the X byte as alpha and trips a bounds check. Not actually
truncated.

**Stop point.** Skeleton boots, has working frame capture, has real
lnkdatas integrity-validated load. Logical next milestones (pick one,
or parallelize via subagents per AGENT-WORKFLOW.md):

1. **Initial git commit** so future subagents can use `isolation: worktree`.
2. **Translate `FUN_004341d4`** — the file-size helper used by storage init
   (currently we reimplemented it inline; matching the original is cleaner).
3. **Translate `FUN_0047af52`** — the DirectInput8 init chain.
4. **Translate `FUN_00475270`** — the "init indexfile ok" subsystem, almost
   certainly the `bmpdata.bin` LZW loader (cross-reference with
   `/opt/src/recettear-repacker/bmp_unpack.py`).
5. **Translate `FUN_00454e69` + surroundings** — the D3D8 device creation
   site, so we can match the original's exact present parameters and
   render-state initial values (necessary for pixel-identical diffs once we
   have real rendering).
6. **First real rendering** — load a single TGA from the extracted assets
   and draw it via a screen-aligned quad. Confirms the texture pipeline
   before we tackle any of the engine's actual draw paths.
- Wire up `tools/ghidra-headless.sh` for batch decompilation.
- Confirm DirectX version from unpacked imports.
- First extractor: `xfile.py` (validate pipeline against known format).

---

## 2026-05-19 — First real rendering: TGA + screen-aligned quad

**What landed:** `src/tga.{c,h}` (uncompressed truecolor TGA Type 2, 24/32-bit,
bottom-up or top-down → BGRA), `src/sprite.{c,h}` (`IDirect3DTexture8` via
`CreateTexture(D3DPOOL_MANAGED) + LockRect + memcpy`, screen-aligned quad
via `D3DFVF_XYZRHW | DIFFUSE | TEX1` + `DrawPrimitiveUP`, with
`SRCALPHA/INVSRCALPHA` blending and the standard half-pixel offset).
Wired into `src/main.c` behind a `--show-tga <path>` CLI flag.

**Verification.** Ran `openrecet.exe --show-tga bmp/window.tga
--capture-to <dir> --capture-every-ms 500` for 4 seconds via WSLInterop;
captured 8 BMPs showing the 64×64 `window.tga` (a rounded UI button)
correctly alpha-blended over the debug-magenta clear. Math check on
frame 4: TGA pixel `(32,32) = (23,23,47, α=133)` blended over
`(160,32,96)` predicts `(88, 27, 70)`; captured pixel reads `(89, 27,
70)` — agrees to within rounding.

**Engine-accuracy gap recorded** in
[`findings/texture-loader.md`](findings/texture-loader.md). `FUN_0047193c`
(the original's loader) hands work to `D3DXCreateTextureFromFileInMemoryEx`
(identified by 15-arg call site + D3DXERR_INVALIDDATA error path). For
BMPs it applies the green color-key `0xFF00FF00`; TGAs use native alpha.
We deliberately bypass d3dx8 (not in nixpkgs, deprecated) and will grow
our own decoders to match the engine's output. Current `tga.c` handles
Type 2 only — BMP-with-color-key and RLE-TGA come next.

**Next milestones (unchanged from prior stop point except #6 done):**

1. Translate `FUN_004341d4` (file-size helper).
2. Translate `FUN_0047af52` (DInput8 init chain).
3. Translate `FUN_00475270` (`bmpdata.bin` LZW loader; cross-ref
   `recettear-repacker/bmp_unpack.py`).
4. Translate `FUN_00454e69` + neighbours (D3D8 device creation, for
   matching the original's present parameters and initial render states).
5. Port `FUN_0047193c` properly — read assets via `storage_*`, accept
   BMPs with the green color-key, add RLE-TGA. Replaces `--show-tga`'s
   direct-file path.
6. ~~First real rendering~~ — done (this entry).

---

## 2026-05-19 — DirectInput 8 init ported (keyboard + joysticks)

**Subsystem landed:** `src/input.{c,h}` — full port of `FUN_0047af52`
("init dinput ok") plus its cleanup at `FUN_0047b0ef` and the
WM_ACTIVATE Acquire/Unacquire dance. Wired into `main.c` after
`init_render` and into `WndProc:WM_ACTIVATE` so deactivation correctly
releases device focus (matches the original's behavior).

**Pieces traced and ported:**
- `FUN_0047af52` — outer init: `DirectInput8Create`, keyboard create +
  `SetDataFormat(c_dfDIKeyboard)` + `SetCooperativeLevel(FOREGROUND|NONEXCLUSIVE)`
  + `SetProperty(DIPROP_BUFFERSIZE = 100)` + `Acquire`; then
  `EnumDevices(DI8DEVCLASS_GAMECTRL, ATTACHEDONLY)` followed by per-joystick
  `SetProperty(DIPROP_AXISMODE = ABS)` + `DIPROP_BUFFERSIZE = 100` + `Acquire`.
- `LAB_0047b167` — joystick enumeration callback. Ghidra never decompiled
  this (came up as a label, not a function); read directly from objdump on
  `vendor/unpacked/recettear.unpacked.exe`. Calls
  `IDirectInput8::CreateDevice(lpddi->guidInstance, ...)` into a 4-slot
  array, then `GetCapabilities` as a liveness probe — failure releases the
  device and zeroes the slot. Caps the joystick count at 4
  (`cmp 4; setl` — explains the static `g_joys[INPUT_MAX_JOYS]` layout).
- `FUN_0047b1f2` — per-object enum callback for `IDirectInputDevice8::EnumObjects`
  with filter `DIDFT_AXIS|DIDFT_POV`. Sets each enumerated object's
  `DIPROP_RANGE` to ±1000 via `DIPH_BYID`. (Earlier writeup said ±5000 — that
  was wrong; bytes are `0xFFFFFC18` = −1000 and `0x03E8` = 1000.)
- `FUN_0047b0ef` — symmetric shutdown: Unacquire+Release for the keyboard,
  each joystick slot, then Release the `IDirectInput8` factory.

**Other corrections to the bootstrap findings:**
- Keyboard `SetProperty` is `DIPROP_BUFFERSIZE=100`, not "DIPROP_RANGE ±5000"
  as I'd transcribed initially. The ±5000 number was never in the binary.
- The `WM_ACTIVATE` decision uses both `LOWORD(wParam)` (active/inactive)
  and `HIWORD(wParam)` (minimized flag): paused = inactive OR minimized.

**Toolchain note:** had to add `-ldxguid` to `src/Makefile` so the linker
resolves `IID_IDirectInput8A`, `GUID_SysKeyboard`, and the data-format
GUIDs that `c_dfDIKeyboard` / `c_dfDIJoystick` reference internally.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--capture` runs cleanly for 5 frames — debug-magenta still reads
`(160, 32, 96)` flat across the back-buffer, no crash on init or
shutdown, no MessageBox.

Next-milestone candidates (unchanged ordering from the session-starter
memo): `FUN_004341d4` (file-size helper), `FUN_00475270` (bmpdata.bin
LZW loader), `FUN_00454e69` ("init render ok" — post-device render-state
init), or porting `FUN_0047193c` properly to read assets through
`storage_*` and accept BMP+green-key in addition to TGA.

## 2026-05-19 — D3D8 device creation properly identified + matched

**Correction:** the bootstrap doc previously labeled `FUN_0047ac6a` as
"second-stage init" and `FUN_00454e69` as "init render ok". After
reading the `WinMain` dispatch carefully, **`FUN_0047ac6a` is the actual
D3D8 device-creation function** (`Direct3DCreate8` + `CreateDevice`,
present-params, behavior-flag fallback) and `FUN_00454e69` is post-device
render-state init that runs the "init render ok" log on completion.

**Findings updated** in
[`winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md): full
present-params field map, the unusual fullscreen=COPY+VSYNC swap-effect
choice (vs. windowed=DISCARD), the CreateDevice behavior-flag fallback
chain `0x44 (HW+MT) → 0x80 (MIXED) → 0x20 (SW)`, and the
`[setup] screen` resolution-lookup table
(0=640×480, 1=800×600 default, 2=1024×768, 3+=1280×960).

**Skeleton updated** — `src/main.c init_render()` now mirrors the
present-params layout (windowed/fullscreen split, COPY+VSYNC for
fullscreen) and walks the same `0x44 → 0x80 → 0x20` BehaviorFlags
fallback. Deliberate deviations recorded in code comments:
`hDeviceWindow=hwnd` (original leaves NULL → focus-window fallback,
behaviorally equivalent), `Flags=LOCKABLE_BACKBUFFER` only when
`--capture-to` is set (capture-only toggle), and hardcoded 800×600 until
the `recet.ini` parser lands.

**Verified:** smoke test runs cleanly with the new HW+MT-first chain;
sprite blend pixel still reads `(89,27,70)` — no regression.

Next: port `FUN_0047af52` (DInput8) — next subsystem in the bootstrap
order, contained scope.
