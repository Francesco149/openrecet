/*
 * font_draw.h — `draw_text(x, y, str, color, scale)` (FUN_0047ca05).
 *
 * The actual consumer scenes call. Walks a SJIS string byte-by-byte,
 * routes through font_slot_alloc → font_slot_upload (for new entries),
 * binds the slot's D3D texture, and emits one quad per visible glyph
 * via render_quad. Flushes the batch at the end of the string.
 *
 * Skipped characters (engine's gates):
 *   - ASCII control (< 0x20)
 *   - Space (' ' = 0x20) — slot still gets allocated but no quad emitted
 *   - Full-width space (SJIS 0x81 0x40) — same
 *
 * SJIS lead-byte detection: any byte with the high bit set is treated
 * as a 2-byte sequence; the trailing byte is consumed without further
 * checking. (No IsDBCSLeadByte call — Win32 not needed for the simple
 * 1-byte-vs-2-byte split, and most importantly the engine's behavior
 * matches because every byte ≥ 0x80 in our data is a real lead byte.)
 *
 * Scale composition mirrors the engine math at FUN_0047ca05 line 29:
 *     fVar2 = scale * 0.65 * 0.76
 *           ≈ scale * 0.494
 * — a "pixels in atlas → pixels on screen" coefficient. The atlas was
 * rasterized at a 42px nominal font height; this scales it down to a
 * usable on-screen size.
 *
 * Glyph quad geometry, per engine constants at lines 41-49:
 *   - dst rect = (x, y, effective_width * fVar2, 42 * fVar2)
 *   - src rect = (1, 1, 41, 41) in texel coords (constant for every
 *     glyph; the engine assumes the atlas texture is ≥ 41×41 and
 *     wraps for smaller ones — we replicate to preserve layout)
 *
 * Advance after each glyph:
 *   x += (effective_width - 3) * fVar2
 */

#ifndef OPENRECET_FONT_DRAW_H
#define OPENRECET_FONT_DRAW_H

#include <stdint.h>

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * Render a string at (x, y) with diffuse color `argb` (0xAARRGGBB)
 * and rendering scale `scale`. Returns the total advance width (the
 * dx from the start x to the cursor's final x) — useful for
 * right-alignment math the way the engine uses it.
 *
 * Each per-glyph quad is emitted via render_quad_add and the whole
 * batch is flushed via render_quad_flush at the end. Caller must be
 * inside BeginScene + render_quad_state_setup.
 *
 * If g_font_atlas isn't loaded (or the atlas has no record for the
 * codepoint), the glyph is skipped silently — same as the engine's
 * FUN_0047cbcb returning NULL.
 */
float font_draw_text(struct IDirect3DDevice8 *dev,
                     float x, float y,
                     const char *str,
                     uint32_t argb,
                     float scale);

/*
 * FUN_0047d464 — the dialogue ROW drawer's per-character reveal fade.
 * Identical walk to font_draw_text, but glyph i (0-based logical index,
 * spaces and skipped bytes included) gets its diffuse alpha scaled by
 *
 *     clamp((fade_budget - i) * 0.2, ..1.0]      (DAT_005198d8 = 0.2,
 *                                                 ceil DAT_00519364 = 1.0)
 *
 * — the engine keeps the budget in a per-iteration counter (init
 * 0x47d4d4, per-char load `fildl` 0x47d528, `decl` 0x47d60e), so the
 * trailing ~5 characters of a revealing line ramp 0.2/0.4/0.6/0.8/1.0:
 * the typewriter "fade-in" gradient. Callers pass the row's char budget
 * (the same max_chars the row was truncated to), which keeps the factor
 * strictly positive — there is no lower clamp in the engine. A negative
 * fade_budget disables the fade (plain font_draw_text behaviour).
 */
float font_draw_text_fade(struct IDirect3DDevice8 *dev,
                          float x, float y,
                          const char *str,
                          uint32_t argb,
                          float scale,
                          int fade_budget);

/*
 * FUN_0047d14c — centered-text draw. Walks `str` once with
 * `font_slot_alloc` to measure total advance width using each slot's
 * `effective_width` (the same value `font_draw_text` consumes for
 * per-glyph advance), then calls `font_draw_text` at `center_x - w/2`.
 *
 * Engine quirk reproduced: the measure walk reads `effective_width`
 * from each slot BEFORE the upload populates it for newly-allocated
 * slots, so the first draw of a previously-uncached string is
 * centered against `(0 - 3) * fVar2 * char_count` instead of the real
 * width. The follow-up draw walk then sees populated values and lays
 * out per the real advance — visual mis-center on first frame only,
 * pixel-perfect thereafter. Same misalignment exists in the engine.
 *
 * The legacy "FUN_004054c0" branch of FUN_0047d14c (gated on
 * `DAT_0438b784 & 1`) is dead in vendor data (the flag is BSS-zero
 * and never set) so we don't replicate it.
 *
 * Returns the measured width (the dx the draw walk would advance by).
 */
float font_draw_text_centered(struct IDirect3DDevice8 *dev,
                              float center_x, float y,
                              const char *str,
                              uint32_t argb,
                              float scale);

/*
 * FUN_0047d2db — RIGHT-aligned text draw. Same measure walk as
 * font_draw_text_centered, but lays the string so its right edge lands
 * at `right_x` (i.e. draws at `right_x - width`). Used for the save-slot
 * picker's right-justified SCORE/LOOP value columns (FUN_0049b556). The
 * legacy FUN_004054c0 bitmap-font branch (gated on `DAT_0438b784 & 1`)
 * is dead in vendor data and not replicated. Returns the measured width.
 */
float font_draw_text_right(struct IDirect3DDevice8 *dev,
                           float right_x, float y,
                           const char *str,
                           uint32_t argb,
                           float scale);

/*
 * FUN_0047d0ea — measure-only: the total advance width of `str` at `scale`,
 * WITHOUT drawing (same per-glyph `(effective_width-3)*scale*0.494` walk the
 * draw/centre/right helpers use).  The qty-confirm box positions its quantity
 * number relative to the item-name width via this.  Glyphs are alloc+uploaded
 * so a first-seen string measures consistently with its later draw.
 */
float font_measure_text(struct IDirect3DDevice8 *dev,
                        const char *str,
                        float scale);

/*
 * FUN_00465db4 — the bubble / help-box multi-line text renderer.  Expands
 * the engine's variable macros (stubbed — see the .c), splits on <BR>, and
 * draws each line via font_draw_text_fade (FUN_0047d464) with `char_budget`
 * (DAT_09642c48*2) as a typewriter reveal consumed across all lines.
 * `scale` is the box scale (the guild bubble passes 1.0); the per-glyph
 * size folds in an extra *0.76 (engine param_5*0.76).
 */
void font_draw_text_box(struct IDirect3DDevice8 *dev,
                        float x, float y,
                        const char *str,
                        uint32_t argb,
                        float scale,
                        int char_budget);

#endif /* _WIN32 */

#endif /* OPENRECET_FONT_DRAW_H */
