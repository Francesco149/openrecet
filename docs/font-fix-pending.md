# Font draw_text size fix — pending implementation

**Status:** Paused 2026-05-22 to grind on input-injection harness work.
**Resume by:** applying the engine-faithful dst-rect math in `src/font_draw.c`.

## What's wrong

Glyphs render ~7–10% too tall and slightly too wide compared to retail —
visible side-by-side on the title settings panel (BGM/SE/Voice/Message
Speed/etc.). Captured by user 2026-05-22 in `font-issue2.png`.

The previous baseline-align commit (`a8ec516`) folded `origin_y` /
`origin_x` offsets into the dst rect but kept dst dims as
`tex_w × tex_h × fVar2`. That's not what the engine does.

## What the engine actually does (FUN_0047ca05)

For every glyph:

```
dst = (x, y, cell_inc_x * fVar2, 42.0f * fVar2)   // dst.h FIXED at 42
src = (1, 1, 41, 41)                              // dst-window FIXED at 40×40
tex_dim_for_UV = 42 × 42                          // UV reference, NOT actual texture
```

`fVar2 = scale * 0.65 * 0.76`. With `scale=1.0` that's `0.494`.

The cell texture the engine uploads is `cell_inc_x_padded × line_height`
with the glyph at `(col 0, row ascent-origin_y)`. With our small
`(tex_w × tex_h)` upload approach we skip the cell pad, so we have to
fold the equivalent geometry into the dst rect.

## Verified empirically via Frida probe

`tools/diagnostics/font/font_drawrect_probe.py` hooks `FUN_00404efc`
(inner quad-add) at entry, filters by src=(1,1,41,41) to catch only
draw_text calls, dumps the dst rect pre-screen-scale.

Probe output on retail settings panel (2026-05-22):

- `dst.h` is **always** `20.748` (= `42 * 0.494`). Never varies with glyph.
- `dst.w` varies in `{11.856, 12.35, 12.844, 13.338}` — i.e. `{24, 25,
  26, 27} * fVar2`. Matches `cell_inc_x * fVar2` for the per-glyph
  `cell_inc_x` value stored at slot+0 by `FUN_0047cbcb`.
- `tex_dim` always `(42, 42)` (the `&DAT_073b18b8` UV reference block).

## The unresolved mystery

The probe's retail captured `dst.w / fVar2 ∈ {24..27}`, but our atlas
(verified identical to retail's via `font_inspect_retail.py` reading
`DAT_073dde30 + cp*40 + 0x08`) has `cell_inc_x ∈ {29, 30, 31, 32}` for
ASCII characters in the same range.

So retail's draw_text path is using stored `cell_inc_x` values 4–5
smaller than the fontidx record's `cell_inc_x` field. The store-side
code at FUN_0047cbcb:146 is:

```c
local_c = (uint)local_24;          // local_24 = (short)piVar4[2] (cell_inc_x)
local_10 = (byte *)(float)(int)local_c;
local_74[1] = iVar11;              // ascent (red herring?)
uVar5 = __ftol();                  // pops FPU top → int
*(undefined4 *)pcVar8 = uVar5;     // slot+0 = result
```

Ghidra has at least one hidden FPU op here. Possible explanations:

- `__ftol` is reading something other than the cell_inc_x I think it
  is — maybe an intermediate value involving ascent or origin_x.
- There's an implicit scale applied somewhere I missed (font:size in
  config.idx? scale arg from a non-1.0 caller?).
- The slot's stored value is computed from cell_inc_x AND another
  metric (e.g. `cell_inc_x * scale_factor` where scale comes from
  GetTextMetrics's tmAveCharWidth).

The cleanest way to settle this: get a `font_slots_retail.py` working
that reads slot+0 directly while the settings panel is open. The
draft script in `/tmp/font_slots_retail.py` works in principle but
needs a frida-spawned retail the user can interact with — currently
the Frida-launched window isn't user-focusable.

Workarounds for that:
- Attach to an already-running retail (`device.attach(pid)` with the
  pid of a user-launched retail). Bypasses the spawn-focus issue.
- State-force the scene to settings via Frida writes (need scene
  state offset + transition handler — likely an evening of RE).

Either way, this is the BLOCKER on resolving the mystery; the fix
below works regardless because it uses **our** atlas's `cell_inc_x`.

## The fix to apply

Replace the dst/src computation in `src/font_draw.c::font_draw_text`
(the block starting `float dst[4] = { ... }` around line 128) with:

```c
const float scale_x = (float)rec->cell_inc_x * fVar2 / 40.0f;
const float scale_y = 42.0f * fVar2 / 40.0f;

/* Engine renders a (cell_inc_x × 42)*fVar2 dst quad with src=(1,1,41,41)
 * on a cell-padded texture (cell_inc_x_padded × line_height). The glyph
 * lives at (col 0, row ascent-origin_y) within that cell. With our small
 * (tex_w × tex_h) upload we have no cell padding, so we map the visible
 * glyph portion (clipped by the engine's src window) into the equivalent
 * dst sub-rect. */

const int glyph_y0 = rec->ascent - rec->origin_y;
const int src_y_top = 1, src_y_bottom = 41;
const int src_x_left = 1, src_x_right = 41;
const int glyph_x0 = 0;   /* engine places glyph at cell col 0 */

const int clip_top    = (glyph_y0 < src_y_top)
                       ? (src_y_top - glyph_y0) : 0;
const int clip_bottom = (glyph_y0 + rec->tex_height > src_y_bottom)
                       ? (glyph_y0 + rec->tex_height - src_y_bottom) : 0;
const int clip_left   = (glyph_x0 < src_x_left)
                       ? (src_x_left - glyph_x0) : 0;
const int clip_right  = (glyph_x0 + rec->tex_width > src_x_right)
                       ? (glyph_x0 + rec->tex_width - src_x_right) : 0;

const int visible_rows = rec->tex_height - clip_top  - clip_bottom;
const int visible_cols = rec->tex_width  - clip_left - clip_right;

if (visible_rows > 0 && visible_cols > 0) {
    float dst[4] = {
        x + (float)((glyph_x0 - src_x_left) + clip_left) * scale_x,
        y + (float)((glyph_y0 - src_y_top) + clip_top ) * scale_y,
            (float)visible_cols * scale_x,
            (float)visible_rows * scale_y,
    };
    float src[4] = {
        (float)clip_left,
        (float)clip_top,
        (float)(rec->tex_width  - clip_right),
        (float)(rec->tex_height - clip_bottom),
    };
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex);
    render_quad_add(dst, src,
                    (uint32_t)rec->tex_width,
                    (uint32_t)rec->tex_height,
                    argb);
    render_quad_flush(dev);
}
```

The per-character advance line below (`(eff_w - 3) * fVar2`) stays
unchanged — engine uses the same formula.

## Verification protocol

1. Build, run `./tools/scenario-test.py title-z-press --target openrecet`
   to make sure nothing else regressed.
2. Build, navigate to settings panel manually (via openrecet, not
   retail). Capture a screenshot.
3. Compare against the user's `font-issue2.png` right-half (retail).
   The "Music" / "Sound" / "Voice" rows should match retail's
   horizontal extent and cap height.
4. If still off, the cell_inc_x mystery (above) is the next thing to
   chase — our atlas's cell_inc_x is too big for the canonical render.

## Scene anchor for testing

`scene_title.c` already renders the settings panel inline at title;
the user reaches it by pressing A on "Options" from the title menu.
The render path goes through `settings_render_panel` in `scene_title.c`
which calls `font_draw_text_centered` + `font_draw_text` with `scale=1.0`.
