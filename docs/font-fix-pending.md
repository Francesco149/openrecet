# Font draw_text size fix — RESOLVED 2026-05-22

**Status:** Applied. See commit on `master` after `387c114`.
**Visible result:** title menu + settings panel glyphs now match retail's
horizontal extent and cap height (within sub-pixel half-texel offsets).

This file is kept for the writeup; the patch lives in `src/font_draw.c`.

## What was wrong

Glyphs rendered ~10% too tall and slightly mis-sized horizontally
compared to retail. The previous baseline-align commit (`a8ec516`) used
`dst.h = tex_h * fVar2` and `dst.x = x + origin_x * fVar2`. Both were
inferences that don't match what the engine actually does.

## What the engine actually does (FUN_0047ca05)

```
slot[0]      = floor(cell_inc_x * 36.0 / 42.0)   ← from FUN_0047cbcb
dst          = (x, y, slot[0] * fVar2, 42.0f * fVar2)
src          = (1, 1, 41, 41)
tex_dim_UV   = (42, 42)              ← UV reference, NOT actual texture
fVar2        = scale * 0.65 * 0.76
```

- `dst.h` is **always** `42 * fVar2 = 20.748` (with `scale=1.0`).
  Never varies per glyph.
- `dst.w` is `floor(cell_inc_x * 6/7) * fVar2`. For ASCII at this
  font that's `{24, 25, 26, 27} * fVar2`.
- The actual GPU texture bound is a **`tex_width × line_height` cell**
  with the glyph bitmap at `(col 0, row ascent-origin_y)` and zero-fill
  everywhere else.
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
call   0x503954                 ; __ftol  →  truncates toward zero
mov    DWORD PTR [ebx],eax      ; slot+0 = floor(cell_inc_x * 36/42)
```

Constants at `.rdata`:
- `0x519480 = 36.0f`
- `0x519d08 = 42.0f`

So `slot[0] = floor(cell_inc_x * 6/7)`. Verified against the Frida
probe data: fontidx cell_inc_x ∈ {29,30,31,32} → stored ∈ {24,25,26,27}. ✓

## The fix shipped in src/font_draw.c

Clip the glyph's cell-row extent against the engine's UV sample window
in cell-texel space, then map the visible portion to a screen-space
rect via the cell→dst affine:

```c
const int stored_cell_inc_x = (rec->cell_inc_x * 36) / 42;
const float dw_engine = (float)stored_cell_inc_x * fVar2;
const float dh_engine = 42.0f * fVar2;

const float tex_wf  = (float)rec->tex_width;
const float line_hf = (float)rec->line_height;
const float gy0     = (float)(rec->ascent - rec->origin_y);

const float sample_x_lo = 1.5f  / 42.0f * tex_wf;
const float sample_x_hi = 41.0f / 42.0f * tex_wf;
const float sample_y_lo = 1.5f  / 42.0f * line_hf;
const float sample_y_hi = 41.0f / 42.0f * line_hf;
const float sample_y_span = sample_y_hi - sample_y_lo;

float vy_lo = gy0, vy_hi = gy0 + (float)rec->tex_height;
if (vy_lo < sample_y_lo) vy_lo = sample_y_lo;
if (vy_hi > sample_y_hi) vy_hi = sample_y_hi;

if (vy_hi > vy_lo) {
    const float dst_y =
        y + (vy_lo - sample_y_lo) / sample_y_span * dh_engine;
    const float dst_h =
        (vy_hi - vy_lo) / sample_y_span * dh_engine;
    float dst[4] = { x, dst_y, dw_engine, dst_h };
    float src[4] = {
        sample_x_lo, vy_lo - gy0,
        sample_x_hi, vy_hi - gy0,
    };
    /* ... SetTexture + render_quad_add + flush ... */
}
```

In X, glyph always fully fits inside the cell (cell_width = tex_width),
so no X clipping is needed — the visible portion is the same `[sample_x_lo,
sample_x_hi]` range that the engine sees, mapped to the full `dw_engine`
dst width.

In Y, descenders like "j"/"y"/"g" can clip at the bottom (tex_h+gy0 >
sample_y_hi). Vendor data places these so they clip by ≤1.2 cell rows —
the bottom 2-3% of the descender tip is lost on screen, matching retail.

## Verified

Side-by-side comparison via `tools/scenario-test.py title-options
--target both`: settings panel ("Music"/"Sound"/"Voice"/"Message
Speed"/"Unread Text Skip"/"Clear Save Data" rows) renders at the same
horizontal extent and cap height as retail.

Title menu items ("NEW GAME"/"ITEM ENCYCLOPEDIA"/"OPTIONS"/"EXIT") and
the "openrecet 0.1" smoke text in the corner also resized — all
post-fix goldens re-blessed under all 4 scenarios.

Sub-pixel half-texel offsets remain (render_quad_add adds 0.5 inset
on left/top only; the engine inherits the same quirk so cumulatively
neutral) — visible if you blow the comparison to 8x but invisible at
native scale.
