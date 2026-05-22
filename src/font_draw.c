/*
 * font_draw.c — `draw_text` (FUN_0047ca05) Win32 implementation.
 *
 * Pure-C parts of the per-string walking logic don't exist as a
 * standalone module — the iteration is wound up with D3D calls. The
 * SJIS-lead-byte advance + per-char skip masks are simple enough to
 * inline.
 *
 * On Linux this whole file is empty — no entry points are exposed
 * without _WIN32.
 */

#include "font_draw.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "font.h"
#include "font_alloc.h"
#include "font_atlas.h"
#include "font_upload.h"
#include "render_quad.h"

float font_draw_text(struct IDirect3DDevice8 *dev_,
                     float x, float y,
                     const char *str,
                     uint32_t argb,
                     float scale)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_;
    if (!dev || !str) return 0.0f;
    if (!g_font_atlas.fontidx) return 0.0f;

    const float start_x = x;
    const float fVar2 = scale * 0.65f * 0.76f;   /* engine constant chain */

    /* Engine sets LINEAR filter on texture stage 0 before drawing
     * text — D3DTSS_MAGFILTER=0x10 and D3DTSS_MINFILTER=0x11 both = 2. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    /* Defensive: clear any texture-transform state and address mode
     * that prior render paths might have left set. UV is normalized
     * 0..1, no scaling, clamp at the edges. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        unsigned char b0 = p[0];

        /* Skip ASCII control bytes (< 0x20) but allow high-bit chars
         * (SJIS lead bytes). */
        if (b0 < 0x20) {
            p++;
            continue;
        }

        int is_double_byte = (b0 & 0x80) != 0;
        unsigned char b1 = is_double_byte ? p[1] : 0;

        int rec_id, is_new;
        int slot = font_slot_alloc((uint8_t)b0, (uint8_t)b1, &rec_id, &is_new);

        if (slot != FONT_SLOT_NONE) {
            /* On new allocation: upload glyph texture for the slot. */
            if (is_new) {
                if (!font_slot_upload(slot, dev)) {
                    /* Upload failed — slot is allocated but no texture.
                     * Don't emit a quad for this glyph; do still advance
                     * the cursor by the engine's amount so layout is
                     * preserved. */
                }
            }
            /* Reset age — matches engine line 38 `piVar4[6] = 0`. The
             * find_existing path already does this for matches, but the
             * new-alloc path needs it too since the slot started life
             * with age=0 and might have been bumped between alloc and
             * draw_text in some future call-site. Cheap idempotent
             * write. */
            g_font.slots[slot].age = 0;

            uint8_t skip = 0;
            if (b0 == 0x20)                          skip = 1;
            if (b0 == 0x81 && b1 == 0x40)            skip = 1;

            void *tex = g_font.textures[slot];
            if (!skip && tex) {
                /* Engine constants. The src rect is a fixed 40×40
                 * window starting at (1,1) in texel space. UV math
                 * inside render_quad_add divides by tex dimensions —
                 * for smaller-than-41 textures the engine relies on
                 * the default WRAP addressing; we preserve that. */
                uint32_t record_id = (uint32_t)rec_id;
                if (record_id < g_font_atlas.fontidx_count) {
                    struct font_atlas_record *rec =
                        &g_font_atlas.fontidx[record_id];

                    /* The slot's `effective_width` was populated by
                     * the upload pass for this glyph (or by a prior
                     * upload, if we hit the find_existing path). Falls
                     * back to fontidx.cell_inc_x when the slot was
                     * matched by an existing entry but the upload
                     * never ran (e.g. empty glyph). */
                    uint32_t eff_w = g_font.slots[slot].effective_width;
                    if (eff_w == 0) eff_w = (uint32_t)rec->cell_inc_x;

                    float dst[4] = {
                        x,
                        y,
                        (float)eff_w * fVar2,
                        42.0f       * fVar2,
                    };
                    /* Engine uses a fixed [1,1,41,41] src rect that
                     * wraps when the texture is smaller than 41×41
                     * (default ADDRESSU/V is WRAP). Glyph textures
                     * here are typically 16..40 px wide / 8..50 px
                     * tall — wrap with sub-pixel inset doesn't quite
                     * track the glyph cleanly. For our port we use
                     * the full texture extent so the glyph maps 1:1
                     * to the dst quad regardless of texture size. */
                    float src[4] = { 0.0f, 0.0f,
                                     (float)rec->tex_width,
                                     (float)rec->tex_height };

                    IDirect3DDevice8_SetTexture(
                        dev, 0, (IDirect3DBaseTexture8 *)tex);
                    render_quad_add(dst, src,
                                    (uint32_t)rec->tex_width,
                                    (uint32_t)rec->tex_height,
                                    argb);
                    /* Flush per-glyph: each glyph uses a different
                     * texture, and render_quad batches assume a single
                     * texture binding per flush. Engine's FUN_00405354
                     * also fires per-glyph. */
                    render_quad_flush(dev);
                }
            }

            /* Per-engine advance. Uses the slot's effective_width (set
             * by upload) — the rightmost column where both glyph alpha
             * and edge halo were non-zero, gives a snug kerning. */
            uint32_t eff = g_font.slots[slot].effective_width;
            float adv = ((float)(int)eff - 3.0f) * fVar2;
            x += adv;
        }

        /* Advance past the codepoint bytes in the input string. */
        p += is_double_byte ? 2 : 1;
    }

    return x - start_x;
}

float font_draw_text_centered(struct IDirect3DDevice8 *dev_,
                              float center_x, float y,
                              const char *str,
                              uint32_t argb,
                              float scale)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_;
    if (!dev || !str) return 0.0f;
    if (!g_font_atlas.fontidx) return 0.0f;

    const float fVar2 = scale * 0.65f * 0.76f;
    float width = 0.0f;

    /* Walk the string with font_slot_alloc + immediate font_slot_upload
     * on `is_new`. The engine's FUN_0047cbcb is atomically
     * alloc-and-upload-if-new; our pure-C split lets the upload step
     * lag, so doing it explicitly here keeps the measure walk's
     * effective_width reads consistent with the follow-up draw walk's
     * advance — and, more visibly, keeps every glyph's texture pointer
     * non-NULL so font_draw_text actually emits the quad. Without this,
     * glyphs first-seen by font_draw_text_centered (and not by an
     * earlier font_draw_text caller in the same frame) render as
     * invisible because the draw walk sees is_new=0 and skips upload. */
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        unsigned char b0 = p[0];
        if (b0 < 0x20) { p++; continue; }
        int is_double_byte = (b0 & 0x80) != 0;
        unsigned char b1 = is_double_byte ? p[1] : 0;

        int rec_id, is_new;
        int slot = font_slot_alloc(b0, b1, &rec_id, &is_new);
        if (slot != FONT_SLOT_NONE) {
            if (is_new) {
                font_slot_upload(slot, dev);
            }
            uint32_t eff = g_font.slots[slot].effective_width;
            width += ((float)(int)eff - 3.0f) * fVar2;
        }
        p += is_double_byte ? 2 : 1;
    }

    font_draw_text(dev_, center_x - width * 0.5f, y, str, argb, scale);
    return width;
}

#endif /* _WIN32 */
