/*
 * scene1_dialogue_draw.c — opening-prologue dialogue RENDER pass. See the
 * header. Port of the DRAW body of FUN_0046c9a2.
 *
 * Win32-only: the body is D3D8 draw calls (via render_quad + font_draw). The
 * pure-C helpers it depends on (the mirror quad, the box open/close wobble, the
 * reveal-truncation char count) live in render_quad.c / scene1_dialogue_run.c /
 * font_draw.c so the unit suite exercises them on the host.
 *
 * Built incrementally (docs/plans/vectorized-scribbling-backus.md):
 *   Layer 1 (here): the painted 2D background. Standees / box / nameplate / text
 *   land in Layers 2-4. Assets load on the first draw of each script (the sim
 *   tick has no D3D device), keyed by the driver's script generation counter.
 */
#include "scene1_dialogue_draw.h"

#ifdef _WIN32

#include "render_quad.h"   /* render_quad_* + <d3d8.h> (IDirect3DDevice8) */
#include "sprite.h"        /* sprite_t, sprite_load, sprite_destroy        */
#include "scene1_intro_dialogue.h"
#include "scene1_dialogue_run.h"

#include <string.h>

/* ─── loaded assets (one set per running script) ─────────────────────────
 * Reloaded when the driver's script generation changes (iv1_1 → iv1_2). */
static sprite_t  g_bg[IVE_MAX_NAMES];   /* DAT_073571f0 — bg images (by slot)   */
static int       g_bg_count   = 0;
static sprite_t  g_chr[IVE_MAX_NAMES];  /* DAT_0734f9b0 — chr graphics (by slot) */
static int       g_chr_count  = 0;
static unsigned  g_loaded_gen = (unsigned)-1;

static void free_assets(void)
{
    for (int i = 0; i < g_bg_count; i++)
        sprite_destroy(&g_bg[i]);
    memset(g_bg, 0, sizeof g_bg);
    g_bg_count = 0;
    for (int i = 0; i < g_chr_count; i++)
        sprite_destroy(&g_chr[i]);
    memset(g_chr, 0, sizeof g_chr);
    g_chr_count = 0;
}

/* FUN_0046bf38 (bg subset): load each parsed bg name into its slot. The engine
 * loads ive_window.tga / chrname.tga / chr graphics here too — added with the
 * box (Layer 3) and standee (Layer 2) passes. Proprietary art, loaded from the
 * user's install via the storage layer; never redistributed. */
static void ensure_assets(IDirect3DDevice8 *dev, const struct ive_program *prog)
{
    unsigned gen = scene1_intro_dialogue_generation();
    if (gen == g_loaded_gen)
        return;
    free_assets();

    int n = prog->n_bg;
    if (n > IVE_MAX_NAMES) n = IVE_MAX_NAMES;
    for (int i = 0; i < n; i++) {
        /* Engine expects 0x400×0x200 (1024×512); sprite_load keeps the decoded
         * native size for UV normalisation (src is divided by s->width/height). */
        if (sprite_load(dev, prog->bg[i], 0x400, 0x200, &g_bg[i]))
            g_bg_count = i + 1;
    }

    /* chr standee graphics (FUN_0046bf38 third loop): each registered grp name
     * → its slot, with the script-declared W,H as the expected dims. */
    int nc = prog->n_chrname;
    if (nc > IVE_MAX_NAMES) nc = IVE_MAX_NAMES;
    for (int i = 0; i < nc; i++) {
        if (prog->chrname[i][0] == '\0') continue;
        if (sprite_load(dev, prog->chrname[i],
                        (uint32_t)prog->chr_w[i], (uint32_t)prog->chr_h[i], &g_chr[i]))
            g_chr_count = i + 1;
    }
    g_loaded_gen = gen;
}

/* ─── background (FUN_0046c9a2 lines 75-127) ─────────────────────────────── */
static void draw_background(IDirect3DDevice8 *dev,
                            const struct ive_runtime *rt,
                            const struct ive_program *prog)
{
    if (prog->n_bg <= 0)              /* DAT_073a3df0 — no bg set */
        return;
    int idx = rt->scene.bg_index;    /* DAT_073a6d90 */
    if (idx < 0 || idx >= g_bg_count || g_bg[idx].tex == NULL)
        return;
    const sprite_t *bg = &g_bg[idx];
    render_quad_bind(dev, bg);

    if (rt->scene.bg_scroll == 0 && rt->scene.bg_mode == 0) {
        /* Static: one full-screen quad, sampling the left 640×480. */
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_add(dst, src, bg->width, bg->height, 0xffffffffu);
    } else {
        /* Scroll: three 1024-wide tiles offset by bg_scroll/1000 (+ shake jitter
         * in Layer 4). PORT-DEBT(deferred): the scroll path is untested until a
         * scrolling prologue bg is exercised; the prologue caps are static. */
        int sx = rt->scene.bg_scroll / 1000;
        for (int t = -1; t <= 1; t++) {
            const float x = (float)(sx + t * 0x400);
            const float dst[4] = { x, 0.0f, 1024.0f, 480.0f };
            const float src[4] = { 0.0f, 0.0f, 1024.0f, 480.0f };
            render_quad_add(dst, src, bg->width, bg->height, 0xffffffffu);
        }
    }
    render_quad_flush(dev);
}

/* ─── character standees (FUN_0046c9a2 lines 153-209) ────────────────────── */
static void draw_standees(IDirect3DDevice8 *dev,
                          const struct ive_runtime *rt,
                          const struct ive_program *prog)
{
    for (int i = 0; i < IVE_STANDEE_COUNT; i++) {
        const struct ive_standee *s = &rt->scene.standees[i];
        if (s->field[IVE_ST_ACTIVE] == 0)              /* not displayed */
            continue;
        int g = s->field[IVE_ST_GRAPHIC];
        if (g < 0 || g >= prog->n_chrname || prog->chrname[g][0] == '\0')
            continue;                                  /* no graphic registered */
        if (g >= g_chr_count || g_chr[g].tex == NULL)
            continue;                                  /* texture failed to load */
        const sprite_t *tex = &g_chr[g];
        float w = (float)prog->chr_w[g];
        float h = (float)prog->chr_h[g];

        render_quad_bind(dev, tex);

        /* Colour: ftol the 4 channel floats, repack exactly as the engine does
         * (color = ch18<<24 | ch15<<16 | ch16<<8 | ch17). */
        int c15 = (int)ive_word_f(s->field[15]);
        int c16 = (int)ive_word_f(s->field[16]);
        int c17 = (int)ive_word_f(s->field[17]);
        int c18 = (int)ive_word_f(s->field[18]);
        uint32_t color = ((uint32_t)c18 << 24) | ((uint32_t)c15 << 16)
                       | ((uint32_t)c16 << 8)  |  (uint32_t)c17;

        /* Blend mode (field 27): >=2 = additive (SRCALPHA/ONE), else normal
         * (SRCALPHA/INVSRCALPHA); bit0 picks COLOROP ADD vs MODULATE. */
        int blend = s->field[IVE_ST_BLEND];
        if (blend >= 2) {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
        } else {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
            (blend & 1) ? D3DTOP_ADD : D3DTOP_MODULATE);

        /* dst (x = field1, y = field2; chr-shake jitter on y deferred to L4). */
        float y = (float)(int)ive_word_f(s->field[2]);
        const float dst[4] = { ive_word_f(s->field[1]), y, w + 0.5f, h + 0.5f };
        const float src[4] = { 0.0f, 0.0f, w, h };
        if (s->field[IVE_ST_MIRROR] == 1)
            render_quad_add_mirrored(dst, src, tex->width, tex->height, color);
        else
            render_quad_add(dst, src, tex->width, tex->height, color);
        render_quad_flush(dev);
    }
}

void scene1_dialogue_draw(IDirect3DDevice8 *dev)
{
    const struct ive_runtime *rt = scene1_intro_dialogue_runtime();
    if (rt == NULL)
        return;   /* no active script — nothing to draw */
    const struct ive_program *prog = scene1_intro_dialogue_program();
    if (prog == NULL)
        return;

    ensure_assets(dev, prog);

    render_quad_state_setup(dev);   /* FUN_0049b425 — 2D alpha-blend preset */
    draw_background(dev, rt, prog);
    draw_standees(dev, rt, prog);

    /* Box / nameplate / text land in Layers 3-4. */
}

#endif /* _WIN32 */
