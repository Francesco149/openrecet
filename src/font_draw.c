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

float font_draw_text_fade(struct IDirect3DDevice8 *dev_,
                          float x, float y,
                          const char *str,
                          uint32_t argb,
                          float scale,
                          int fade_budget)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_;
    if (!dev || !str) return 0.0f;
    if (!g_font_atlas.fontidx) return 0.0f;

    const float start_x = x;
    const float fVar2 = scale * 0.65f * 0.76f;   /* engine constant chain */
    int ci = 0;   /* logical char index — the engine's decremented budget
                   * (0x47d4d4 init / 0x47d60e decl) expressed as i: char i
                   * fades by clamp((fade_budget - i)·0.2, ..1.0]. Counts
                   * EVERY walked char (spaces + skips), like the engine. */

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
         * (SJIS lead bytes). Still consumes a fade index — the engine
         * iterates (and decrements its budget for) every char. */
        if (b0 < 0x20) {
            p++;
            ci++;
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

            /* Per-char reveal fade (FUN_0047d464 0x47d528-0x47d551):
             * factor = (budget − i) · 0.2, clamped to ≤ 1.0 (no lower
             * clamp — callers truncate the string to the budget, keeping
             * it positive); alpha byte = ftol(input_alpha · factor). */
            uint32_t col = argb;
            if (fade_budget >= 0) {
                float f = (float)(fade_budget - ci) * 0.2f;
                if (f > 1.0f) f = 1.0f;
                uint32_t a = (uint32_t)((float)(argb >> 24) * f);
                col = (a << 24) | (argb & 0xffffffu);
            }

            void *tex = g_font.textures[slot];
            if (!skip && tex) {
                uint32_t record_id = (uint32_t)rec_id;
                if (record_id < g_font_atlas.fontidx_count) {
                    struct font_atlas_record *rec =
                        &g_font_atlas.fontidx[record_id];

                    /* Engine geometry (FUN_0047ca05 + the
                     * elided-by-Ghidra FPU math at FUN_0047cbcb:0x47cd92):
                     *
                     *   slot[0] = floor(cell_inc_x * 36.0 / 42.0)
                     *   dst     = (x, y, slot[0] * fVar2, 42 * fVar2)
                     *   src     = (1, 1, 41, 41)
                     *   tex_dim = (42, 42)        ← UV reference, not actual
                     *
                     * Engine binds a (cell_inc_x × line_height) cell
                     * texture with the glyph bitmap placed at
                     * (col 0, row ascent - origin_y) and zero-fill
                     * everywhere else (transparent right-pad +
                     * above/below-pad rows). Sampling src=(1,1,41,41)
                     * against a 42×42 UV reference reaches the central
                     * ~94% of the cell.
                     *
                     * Under FONT_USE_CELL_PADDED_TEX=1 (default),
                     * font_upload uploads cells the same way; here we
                     * just reproduce the engine's dst/src/tex_dim block
                     * verbatim. */
                    const int stored_cell_inc_x =
                        (rec->cell_inc_x * 36) / 42;
                    float dst[4] = {
                        x,
                        y,
                        (float)stored_cell_inc_x * fVar2,
                        42.0f * fVar2,
                    };
                    float src[4] = { 1.0f, 1.0f, 41.0f, 41.0f };

                    IDirect3DDevice8_SetTexture(
                        dev, 0, (IDirect3DBaseTexture8 *)tex);
                    render_quad_add(dst, src, 42u, 42u, col);
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
        ci++;
    }

    return x - start_x;
}

float font_draw_text(struct IDirect3DDevice8 *dev_,
                     float x, float y,
                     const char *str,
                     uint32_t argb,
                     float scale)
{
    return font_draw_text_fade(dev_, x, y, str, argb, scale, -1);
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

float font_draw_text_right(struct IDirect3DDevice8 *dev_,
                           float right_x, float y,
                           const char *str,
                           uint32_t argb,
                           float scale)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_;
    if (!dev || !str) return 0.0f;
    if (!g_font_atlas.fontidx) return 0.0f;

    const float fVar2 = scale * 0.65f * 0.76f;
    float width = 0.0f;

    /* Same alloc-and-upload-if-new measure walk as the centered variant
     * (keeps every glyph's texture non-NULL for the follow-up draw). */
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

    font_draw_text(dev_, right_x - width, y, str, argb, scale);
    return width;
}

/*
 * font_draw_text_box — FUN_00465db4 (0x465db4): the bubble / help-box
 * multi-line text renderer.  The engine first expands its
 * <S>/<I>/<Y>/<D…>/<T> variable macros into a working buffer, then splits
 * the result on <BR> and draws each line through the per-character reveal
 * row drawer (FUN_0047d464 = font_draw_text_fade).
 *
 * PORT-DEBT(box-text-macros): the macro substitution buffers (engine
 * DAT_0730xxxx) are unported; the engine's own `DAT_0730b300==0` etc.
 * guards make an unset buffer expand to nothing, which is exactly the
 * state we model — every <S>/<I>/<Y>/<D…>/<T> tag is dropped.  The
 * Merchant's Guild bubbles use none of these macros (literal text + <BR>
 * only; the store-expansion %d is pre-substituted via FUN_005038ff before
 * this call), so for the guild this pass is a verbatim copy.  Wire in the
 * real substitution if a future caller needs it.
 *
 * `char_budget` (engine param_6 = DAT_09642c48*2) is the typewriter
 * reveal: consumed across ALL lines (one per visible char; an SJIS lead
 * byte does not decrement on its own), truncating the text mid-reveal.
 * Each line's fade gradient uses the budget remaining at that line's
 * start.  Line spacing is param_3(0,0x28,0x50,…) * scale * 0.76 (the
 * engine's 40-unit step at the box scale); rows stop at param_3 == 400.
 */
void font_draw_text_box(struct IDirect3DDevice8 *dev,
                        float x, float y,
                        const char *str,
                        uint32_t argb,
                        float scale,
                        int char_budget)
{
    if (!dev || !str) return;
    if (char_budget <= 0) return;   /* engine: if (0 < param_6) */

    /* Pass 1 (FUN_00465db4 0x465dde-0x465f2c): macro expansion into a
     * 256-byte working buffer.  <BR> is NOT one of the expanded tags (its
     * '<' falls through as a literal), so it survives for pass 2. */
    char buf[256];
    {
        int dst = 0, src = 0;
        for (int n = 0; n < 256 && dst < 255; n++) {
            char c = str[src];
            buf[dst] = c;
            if (c == '\0') break;
            if (c == '<') {
                char t = str[src + 1];
                if (t == 'S' || t == 'I' || t == 'Y' || t == 'T') { src += 2; continue; }
                if (t == 'D') { src += 3; continue; }   /* <D1>/<D…> — stubbed */
                /* else (e.g. <BR>): keep the literal '<' for pass 2. */
            }
            dst++; src++;
        }
        buf[dst] = '\0';
    }

    /* Pass 2 (FUN_00465db4 0x465f2d-0x465fa2): split on <BR>, apply the
     * typewriter budget, draw each line. */
    int budget  = char_budget;     /* param_6 — runs across lines */
    int src_idx = 0;
    int line_p3 = 0;               /* param_3 — y-step accumulator */
    int done    = 0;              /* local_c */
    do {
        const int line_start_budget = budget;   /* iVar6 */
        char line[260];
        int j = 0, k = src_idx, lead = 0;        /* iVar2, iVar8, local_10 */
        for (int cnt = 0; cnt < 0x100; cnt++) {
            char c = buf[k];
            line[j] = c;
            if (c == '\0') { done = 1; src_idx = k; break; }
            if (c == '<' && buf[k + 1] == 'B' && buf[k + 2] == 'R') {
                line[j] = '\0'; src_idx = k + 4; break;   /* skip "<BR>" */
            }
            /* Budget: an SJIS lead byte (high bit set) does not decrement
             * on its own; the trailing byte does.  ASCII decrements every
             * char. */
            if ((signed char)buf[k] < 0 && lead == 0) {
                lead = 1;
            } else {
                budget--;
                lead = 0;
                if (budget < 1) { line[j + 1] = '\0'; done = 1; break; }
            }
            j++; k++;
        }
        const float ly = (float)line_p3 * scale * 0.76f + y;
        /* Glyph scale is the box scale (param_5) — the engine passes param_5,
         * NOT param_5*0.76, to FUN_0047d464 (the *0.76 the decompile shows on
         * the scale arg is Ghidra FPU mis-grouping; it belongs to the line-
         * spacing y above).  font_draw_text_fade folds in the 0.65*0.76 chain. */
        font_draw_text_fade(dev, x, ly, line, argb, scale, line_start_budget);
        line_p3 += 0x28;
    } while (!done && line_p3 != 400);
}

#endif /* _WIN32 */
