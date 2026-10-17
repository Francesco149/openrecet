/*
 * dungeon_hud.c — see dungeon_hud.h.
 */
#include "dungeon_hud.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdio.h>
#include <string.h>

#include "call_trace.h"
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "chara_skills.h"        /* DAT_0438b7dc, DAT_0438b7fc */
#include "party.h"               /* the gauges DAT_056db0c4.. */
#include "render_quad.h"
#include "save_work.h"
#include "scene1_hud.h"          /* DAT_0438b1dc */
#include "scene1_top_hud.h"      /* FUN_00406a60 */
#include "sysassets.h"
#include "x87.h"

/* .rdata m32 operands */
static const float k0 = 0.0f, k0p1 = 0.10000000149011612f, k0p5 = 0.5f, k0p8 = 0.800000011920929f;
static const float k1 = 1.0f, k2pi = 6.2831854820251465f, k19 = 19.0f, k32 = 32.0f, k60 = 60.0f;
static const float k64 = 64.0f, k80 = 80.0f, k96 = 96.0f, k128 = 128.0f, k196 = 196.0f, k256 = 256.0f;
static const float k304 = 304.0f, k320 = 320.0f, k352 = 352.0f, k376 = 376.0f, k640 = 640.0f;
static const float k9p6 = 9.600000381469727f, k10p4 = 10.40000057220459f, k12p8 = 12.800000190734863f;
static const float k19p2 = 19.200000762939453f, k25p6 = 25.600000381469727f, k28p8 = 28.80000114440918f;
static const float k36p8 = 36.79999923706055f, k38p4 = 38.400001525878906f, k51p2 = 51.20000076293945f;
static const float k53p6 = 53.60000228881836f, k70p4 = 70.4000015258789f, k76p8 = 76.80000305175781f;
static const float k102p4 = 102.4000015258789f, k112 = 112.0f, k115p2 = 115.20000457763672f;
static const float k153p6 = 153.60000610351562f, k156p8 = 156.8000030517578f;

#define DIFFUSE_HALF 0xff7f7f7fu   /* full brightness under MODULATE2X */

static void quad(const sprite_t *s, float x, float y, float w, float h,
                 float l, float t, float r, float b, uint32_t color)
{
    const float dst[4] = { x, y, w, h }, src[4] = { l, t, r, b };
    render_quad_add(dst, src, s->width, s->height, color);
}

/* FUN_00406c64: "cur/max" in hpmp_base's small digits ("%2d/%2d" from 10,
 * "%3d/%3d" from 100), 9.6 apart, spaces skipped (the bound texture is the
 * caller's) */
static void hpmp_digits(float x, float y, int cur, int max)
{
    const sprite_t *s = &g_sysassets.hpmp_base_tga;     /* DAT_073cc920 */
    char buf[256];
    if (max >= 10)
        snprintf(buf, sizeof buf, max >= 100 ? "%3d/%3d" : "%2d/%2d", cur, max);
    else
        snprintf(buf, sizeof buf, "%d/%d", cur, max);
    for (const char *p = buf; *p != 0; p++) {
        if (*p != ' ') {
            const int idx = (*p == '/') ? 10 : *p - '0';
            quad(s, x, y, k19p2, k19p2, (float)(24 * idx + 193), 456.0f,
                 (float)(24 * idx + 217), 480.0f, 0xffffffffu);
        }
        x = spill32((long double)x + k9p6);
    }
}

static int16_t rec_i16(const uint8_t *r, int off) { int16_t v; memcpy(&v, r + off, 2); return v; }
static int32_t rec_i32(const uint8_t *r, int off) { int32_t v; memcpy(&v, r + off, 4); return v; }

void dungeon_hud_party_panel(struct IDirect3DDevice8 *dev_in, float x, float y,
                             int chara, int full)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    CALL_TRACE_ENTER(0x4072f5u);
    const sprite_t *base = &g_sysassets.hpmp_base_tga;  /* DAT_073cc920 */
    const sprite_t *exp  = &g_sysassets.hpmp_exp_tga;   /* DAT_073cc900 */
    const uint8_t *w = save_work_bank_at(save_work_active_slot());
    if (w == NULL || base->tex == NULL) return;
    const uint8_t *rec = w + 0x2ceb0 + 0x6c * chara;    /* &DAT_04510648 + bank·0x2dfc8 + 0x6c·chara */

    const float y0 = spill32((long double)y + k376);
    /* the pulse: |ftol(sin(b0cc · 0.1) · 64)| — red as HP falls, white as it rises */
    const float t = spill32((long double)spill32((float)PW(0x56db0ccu).i) * k0p1);
    const float sn = spill32(x87_crt_sin((double)t));
    int32_t a = x87_ftol((long double)sn * k64);
    if (a < 0) a = -a;
    const uint32_t r8 = (uint32_t)(a + 127);
    uint32_t g8 = (uint32_t)(127 - a), b8 = (uint32_t)(127 - a);
    if (PW(0x56db0d0u).i != 0) g8 = b8 = r8;
    const uint32_t pulse = ((((r8 | 0xffffff00u) << 8) | g8) << 8) | b8;

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    render_quad_bind(dev, base);
    quad(base, x, y0, k256, k102p4, 0.0f, 0.0f, k320, k128, pulse);
    render_quad_flush(dev);

    /* the EXP fan: 21 vertices (centre + 20 rim) straight into DAT_00605208,
     * TRIANGLEFAN of 19, on hpmp_exp */
    if (exp->tex) IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)exp->tex);
    IDirect3DDevice8_SetVertexShader(dev, RENDER_QUAD_FVF);
    {
        render_quad_vtx_t *v = render_quad_vbuf();
        const int32_t e0 = rec_i32(rec, 0x48);
        const float sw = spill32((float)render_quad_screen_w_int());
        v[0].diffuse = DIFFUSE_HALF;
        v[0].x = spill32(((long double)x + k60) * sw / k640);
        const float cy = spill32((long double)y0 + k51p2);
        v[0].y = spill32((long double)cy * sw / k640);
        v[0].u = k0p5;
        v[0].v = k0p5;
        const float prog = spill32((float)(rec_i32(rec, 0x44) - e0));
        float span = spill32((float)(rec_i32(rec, 0x4c) - e0));
        if (span < k1) span = k1;
        const float ratio = spill32((long double)prog / span);
        for (int k = 0; k < 20; k++) {
            const float ang = spill32((long double)spill32((float)k) * ratio * k2pi / k19);
            const double ad = (double)ang;
            const float s = spill32(x87_crt_sin(ad));
            const float c = spill32(x87_crt_cos(ad));
            render_quad_vtx_t *q = &v[1 + k];
            q->x = spill32((((long double)s * k51p2 + x) + k60) * sw / k640);
            q->y = spill32(((long double)cy - (long double)c * k51p2) * sw / k640);
            q->diffuse = DIFFUSE_HALF;
            q->u = spill32(((long double)s + k1) * k0p5);
            q->v = spill32((long double)k0p5 - (long double)c * k0p5);
        }
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLEFAN, 19, v, sizeof(render_quad_vtx_t));
    }
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)base->tex);

    /* HP / SP bars: the eased gauges over max (base + bonus, ≥ 1) */
    float hp_max = spill32((float)((int32_t)rec_i16(rec, 0x3c) + rec_i16(rec, 0x3e)));
    float hp = PW(0x56db0c4u).f;
    if (full) hp = hp_max;
    if (hp_max < k1) hp_max = k1;
    float ratio = spill32((long double)hp / hp_max);
    const float bx = spill32((long double)x + k80);
    quad(base, bx, spill32((long double)y0 + k10p4), spill32((long double)ratio * k156p8), k38p4,
         0.0f, k256, spill32((long double)ratio * k196), k304, DIFFUSE_HALF);
    float sp_max = spill32((float)((int32_t)rec_i16(rec, 0x40) + rec_i16(rec, 0x42)));
    float sp = PW(0x56db0c8u).f;
    if (full) sp = sp_max;
    if (sp_max < k1) sp_max = k1;
    ratio = spill32((long double)sp / sp_max);
    quad(base, bx, spill32((long double)y0 + k53p6), spill32((long double)ratio * k156p8), k38p4,
         0.0f, k304, spill32((long double)ratio * k196), k352, DIFFUSE_HALF);
    /* the panel overlay, then the "HP" / "SP" labels */
    quad(base, x, y0, k256, k102p4, 0.0f, k128, k320, k256, pulse);
    const float hp_y = spill32((long double)y0 + k12p8);
    quad(base, spill32((long double)x + k115p2), hp_y, k36p8, k19p2, 193.0f, 432.0f, 239.0f, 456.0f, DIFFUSE_HALF);
    const float sp_y = spill32((long double)y0 + k70p4);
    quad(base, spill32((long double)x + k112), sp_y, 36.0f, k19p2, 239.0f, 432.0f, 284.0f, 456.0f, DIFFUSE_HALF);
    render_quad_flush(dev);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    const float nx = spill32((long double)x + k153p6);
    hpmp_digits(nx, hp_y, x87_ftol(hp), x87_ftol(hp_max));
    hpmp_digits(nx, sp_y, x87_ftol(sp), x87_ftol(sp_max));

    /* the portrait: 80×80 cells, 6 a row, from (0, 352) */
    {
        const float pl = spill32((float)(80 * (chara % 6)));
        const float pt = spill32((long double)spill32((float)(80 * (chara / 6))) + k352);
        quad(base, spill32((long double)x + k28p8), spill32((long double)y0 + k19p2), k64, k64,
             pl, pt, spill32((long double)pl + k80), spill32((long double)pt + k80), 0xffffffffu);
    }
    /* the skill icons (DAT_0438b7fc[chara], DAT_0438b7dc[chara] of them): 32×32
     * cells, 5 a row, from (320, 96 + 32·chara); the selected one (DAT_056db0d8)
     * lit */
    {
        const int32_t n = chara_skills_get_count(chara);
        const float row = spill32((float)(32 * chara));
        const float iy = spill32((long double)y0 + k38p4);
        for (int32_t k = 0; k < n; k++) {
            const int32_t slot = chara_skills_get_slot(chara, k);
            const uint32_t color = (k == PW(0x56db0d8u).i) ? 0xffffffffu : 0xff9b9b9bu;
            const float sl = spill32((long double)spill32((float)(32 * (slot % 5))) + k320);
            const float st = spill32((long double)spill32((float)(32 * (slot / 5))) + row + k96);
            quad(base, spill32((long double)spill32((float)(128 + 32 * k)) * k0p8 + x), iy, k25p6, k25p6,
                 sl, st, spill32((long double)sl + k32), spill32((long double)st + k32), color);
        }
    }
    render_quad_flush(dev);

    /* the level, +1, right-anchored */
    const int32_t lvl = rec_i32(rec, 0);
    const float lx = spill32((long double)x + (lvl >= 10 ? k38p4 : k32));
    scene1_top_hud_draw_number(dev_in, lx, spill32((long double)y0 + k76p8), lvl + 1, 0, 0xffffffffu, 1);
    (void)k0;
}

/* FUN_00407cac */
void dungeon_hud_render(struct IDirect3DDevice8 *dev)
{
    CALL_TRACE_ENTER_STUB(0x407cacu);
    dungeon_hud_party_panel(dev, 0.0f, spill32((long double)scene1_hud_slide() * k128),
                            chara_equip_get_current_chara(), 0);
    /* PORT-DEBT(stub, FUN_00407cac): the rest of the floor HUD — the boss bar
     * (chara 30 / 42), the combo / pick counters, the floor-event plates, the
     * minimap (map.bmp) and the enemy HP digits. */
}

#endif /* _WIN32 */
