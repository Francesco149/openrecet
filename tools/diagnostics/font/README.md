# Font system diagnostic probes

Persistent infrastructure for debugging the font atlas builder + glyph
rendering pipeline (`src/font_atlas.c`, `src/font_alloc.c`,
`src/font_upload.c`, `src/font_draw.c`). All scripts use Frida to drive
the unpacked retail binary alongside our port so we can compare what
each implementation actually produces.

## Layout

```
tools/diagnostics/font/
├── README.md                # this file
├── font_atlas_diff.py       # primary: byte-diff our atlas vs retail's
├── font_globals_probe.py    # secondary: read config-driven globals
├── font_face_probe.py       # secondary: hook GDI calls in retail
└── font_postprobe.py        # secondary: post-boot HDC/HFONT inspection
```

## Probes

### `font_atlas_diff.py` — atlas byte differential
Spawns retail, force-regens the atlas via the Frida agent's
`forceAtlasRegen` RPC (which raises `DAT_073dfd00` + writes the SJIS
face name into `DAT_073de168` from a hook on `FUN_0047c228` entry),
waits for retail to write `fontdata.bin` + `fontidx.bin` to its cwd,
and byte-compares against our `./font/fontdata.bin` + `./font/fontidx.bin`.

Use when: the atlas builder behavior is in doubt. A clean diff
guarantees the bug is downstream (upload / draw / scene wiring).

```bash
# our atlas must exist first
./build/openrecet.exe --max-duration-ms 3000   # writes ./font/...
nix develop --command python3 tools/diagnostics/font/font_atlas_diff.py \
    --frida-remote cutestation.soy:27042 --keep-retail-atlas
```

### `font_globals_probe.py` — config-driven runtime state
After resume + 8s settle, dumps retail's runtime values for:
- `DAT_005cbc70` (kanjioff flag — engine's `.data` init = 1)
- `DAT_005cbc74` (edgewi — dilation radius, init = 2)
- `DAT_005cbc78` (edgedel — falloff multiplier, init = 6)
- `DAT_073dddb4` (effectmode)
- `DAT_073dfd00` (atlas regen gate)
- `DAT_073de168` (loaded face name)

Use when: the parsed config values are suspect (e.g. our parser shows
`kanjioff=0` but retail shows 1 because vendor `config.idx` has
`/kanjioff:` commented and the engine's `.data` initial value sticks).

### `font_face_probe.py` — GDI call tracing
Hooks `CreateFontIndirectA`, `GetTextFaceA`, `GetGlyphOutlineA`, and
`SelectObject` in retail. Logs the LOGFONTA fields, the resolved face
after substitution, and per-codepoint `gmBlackBoxX/Y` + `gmCellIncX`
metrics for diagnostic codepoints (configurable via `DIAG_CPS` in the
script).

Use when: glyph dimensions differ between our atlas and retail's. The
GGO outputs tell us whether GDI returned different metrics OR our blit
math is wrong.

### `font_postprobe.py` — direct HDC/HFONT inspection
Resumes retail, waits 8s, then directly calls `GetTextFaceA` +
`GetTextMetricsA` on the engine's HDC (`DAT_073dde34`) via Frida
NativeFunction. Doesn't depend on hook timing — works even when the
engine itself doesn't query GDI face after `SelectObject`.

Use when: you want a one-shot, hook-timing-independent answer to
"what font + charset is retail actually using right now."

## Key findings (2026-05-22)

### Atlas size 7× difference is a locale-dependent GDI substitution
Both processes ask for `face="MS PGothic", lfCharSet=SHIFTJIS_CHARSET`.
GDI resolves this differently per-process:
- Retail's process: `face="MS Gothic", tmCharSet=0 (ANSI_CHARSET)` →
  no kanji glyphs → all kanji codepoints render as 16×16 tofu
  placeholders → atlas is 2.3 MB.
- Our process: `face="MS Gothic", tmCharSet=128 (SHIFTJIS_CHARSET)` →
  full kanji glyph set → atlas is 16.4 MB.

The face name is identical, the process default code page differs.
Likely cause: the user's "language for non-Unicode programs" registry
setting interacts with how `frida-server` launches retail vs. how
`WSLInterop` launches our mingw-built binary.

**This is not a port bug.** Our atlas has *more* glyphs (real kanji)
than retail's; the game (EN translation) doesn't use kanji at runtime
so the difference is invisible during play.

### ASCII glyph metrics differ by ±1 pixel
For most ASCII chars, ours and retail's `gmBlackBoxX` / `gmCellIncX`
differ by 1 pixel in random directions — sub-pixel positioning /
hinting noise from font rendering, not a port bug.

### Space char needs hard-coded `effective_width=24`
Engine's `FUN_0047cbcb` special-cases ASCII ' ' (codepoint 0x20) at
line 79733: `if (cVar1 == ' ') { *(undefined4 *)(iVar12 + 0x73de65c) = 0x18; }`.
Our port now mirrors this in `src/font_alloc.c::font_slot_alloc`.
Without it, space's empty glyph leaves `effective_width = 0` and the
per-character advance evaluates to `(0 - 3) * fVar2 = -3 * fVar2`
(negative), compressing rendered text.

## Pattern for future diagnostic dirs

Each runtime bug that warrants persistent probe infrastructure gets a
sibling directory under `tools/diagnostics/`. The shape:

```
tools/diagnostics/<subsystem>/
├── README.md           # what bug, what probes, what we found
├── <primary>_diff.py   # differential against retail or other oracle
└── <secondary>_probe.py × N
```

Probes that target retail use the existing Frida agent at
`tools/frida/openrecet-agent.js`. Add bug-specific RPC methods to the
agent (`forceAtlasRegen`, `dumpAtlasPtrs` exist for this set) and
keep the per-probe Python drivers thin.
