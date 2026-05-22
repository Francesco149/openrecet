# Font draw_text size fix — RESOLVED 2026-05-22

**Status:** Engine-faithful path landed on `master`. Commits:
- `aaa6cd9` — first pass via screen-space-rect derived from engine's
  affine. Worked for `cell_inc_x == tex_width` glyphs but stretched
  narrow glyphs (`i`, `l`, `.`) horizontally — see "Why screen-space
  didn't work" below.
- (this commit) — switched to engine-faithful cell-padded texture
  upload + verbatim engine dst/src/tex_dim block.

This file is kept for the writeup; the patch lives in
`src/font_upload.c` and `src/font_draw.c`. The dead-code
small-texture optimization path is gated behind
`FONT_USE_CELL_PADDED_TEX=0` (off by default).

## What was wrong

Original (pre-`aaa6cd9`) baseline-align used `dst.h = tex_h * fVar2`
and a synthetic `dst.x = x + origin_x * fVar2`. Neither matches the
engine. Glyphs rendered ~10% too tall + slightly mis-sized
horizontally.

## What the engine actually does (FUN_0047ca05)

```
slot[0]      = floor(cell_inc_x * 36.0 / 42.0)   ← hidden FPU math
dst          = (x, y, slot[0] * fVar2, 42.0f * fVar2)
src          = (1, 1, 41, 41)
tex_dim_UV   = (42, 42)              ← UV reference, NOT actual texture
fVar2        = scale * 0.65 * 0.76
```

- `dst.h` is **always** `42 * fVar2 = 20.748` with `scale=1.0`.
- `dst.w` is `floor(cell_inc_x * 6/7) * fVar2`. For ASCII at this font
  that's `{24, 25, 26, 27} * fVar2`.
- The actual GPU texture bound is an **`cell_inc_x × line_height`
  cell** with the glyph bitmap at `(col 0, row ascent - origin_y)` and
  zero-fill everywhere else.
- Engine then samples UV (1.5/42 → 41/42) of that cell.

## The hidden FPU math (was Ghidra-elided)

The "stored cell_inc_x is 5 less than fontidx.cell_inc_x" mystery from
the original pending doc was the FPU pattern at 0x47cd82–0x47cda6:

```asm
fild   DWORD PTR [ebp-0x8]      ; push cell_inc_x as float
fstp   DWORD PTR [ebp-0xc]
fld    DWORD PTR [ebp-0xc]
fmul   DWORD PTR ds:0x519480    ; * 36.0
fdiv   DWORD PTR ds:0x519d08    ; / 42.0
call   0x503954                 ; __ftol → truncates toward zero
mov    DWORD PTR [ebx],eax      ; slot+0 = floor(cell_inc_x * 36/42)
```

Constants at `.rdata`:
- `0x519480 = 36.0f`
- `0x519d08 = 42.0f`

So `slot[0] = floor(cell_inc_x * 6/7)`. Verified against the Frida
probe data: fontidx cell_inc_x ∈ {29,30,31,32} → stored ∈ {24,25,26,27}.

## Why screen-space-rect (commit aaa6cd9) didn't fully work

That attempt kept the small `(tex_w × tex_h)` upload and computed
the on-screen rect via the cell→dst affine, sampling the glyph
texture directly. It assumed the engine's **cell width was
`tex_width`**.

That assumption is wrong. CreateTexture at 0x47cde2 takes
`local_c = cell_inc_x` for the width (not `tex_width`). For narrow
glyphs like `i` (cell_inc_x=29, tex_width=16) the engine's cell is
**29 cols wide** with the glyph bitmap occupying the leftmost 16 and
**13 cols of transparent zero-fill on the right**. That trailing pad
is what produces the correctly-thin `i` on retail — sampling
UV(1.5/42, 41/42) of the 29-wide cell reaches 1..28 in cell texels,
so cols 16..28 sample the transparent pad.

Without uploading the pad, we have nothing to sample, so the small
glyph got stretched to fill the engine's dst width. Visible artifact:
`i` and `l` looked ~2× too wide.

## What's shipped now

**font_upload.c (`FONT_USE_CELL_PADDED_TEX=1`):**
- `CreateTexture(cell_inc_x, line_height, A8R8G8B8, MANAGED)`
- Zero-fill the whole cell.
- Write the glyph bitmap at `(col 0, row y0 = ascent - origin_y)`.
- Track `effective_width` from the glyph walk same as before.

**font_draw.c:**
- `stored_cell_inc_x = (rec->cell_inc_x * 36) / 42`
- `dst = (x, y, stored_cell_inc_x * fVar2, 42 * fVar2)`
- `src = (1, 1, 41, 41)`, `render_quad_add(..., 42, 42, argb)`
- Bind the cell texture, single quad per glyph.

The dead small-texture path lives under `#if !FONT_USE_CELL_PADDED_TEX`
in both files — opt-in if someone later wants to tune memory and can
live with the narrow-glyph stretch.

## Verified

Side-by-side comparison via
`tools/scenario-test.py title-options --target both
--frida-remote cutestation.soy:27042` (turbo+silent harness):
settings panel + title menu glyphs now match retail pixel-for-pixel
modulo a state-only `Music` slider value (retail had `5`, ours `9`).
Narrow `i`/`l` glyphs render at the correct visual width.

Memory cost: ~2× per glyph slot (32×50 cell vs 32×45 glyph for the
widest ASCII, 29×50 vs 16×45 for the narrowest). Acceptable — 200
slots × ~6.4 KB worst-case = 1.3 MB peak.
