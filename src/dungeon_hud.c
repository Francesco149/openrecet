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

#include "actor_ai.h"            /* the popups DAT_044e28f0, the view DAT_095d3730 */
#include "actor_table.h"
#include "call_trace.h"
#include "chara_equip.h"         /* DAT_0438b7d8 */
#include "chara_skills.h"        /* DAT_0438b7dc, DAT_0438b7fc */
#include "dungeon_flow.h"         /* the floor events DAT_0438b938 / b96c, b954, b4d4 */
#include "floor_layout.h"         /* the rooms DAT_073e03ac, the marks DAT_0438b434 */
#include "map_collision.h"        /* the record's maptype */
#include "scene.h"                /* DAT_0438b1c0 */
#include "font_draw.h"            /* FUN_0047d2db */
#include "record_hit.h"           /* DAT_0438b90c */
#include "scene1_combat_sm.h"     /* DAT_0438b4c8, DAT_0438b904 / 908 */
#include "scene1_particles_tick.h" /* DAT_056da1d8 */
#include "scene1_render.h"        /* FUN_00490c78 */
#include "sprite.h"
#include "stage_gate.h"           /* DAT_0438b4cc, FUN_0043195d */
#include "stage_post_load.h"      /* DAT_0438b4d0 */
#include "party.h"               /* the gauges DAT_056db0c4.. */
#include "render_quad.h"
#include "save_work.h"
#include "scene1_hud.h"          /* DAT_0438b1dc */
#include "scene1_top_hud.h"      /* FUN_00406a60 */
#include "sysassets.h"
#include "x87.h"
#include "dungeon_party.h"       /* DAT_00648290 / 94 / 50 */
#include "tables_item.h"         /* FUN_004681f6 */

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

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
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

/* ─── the floor assets FUN_00407cac reads ─────────────────────────────── */

static sprite_t s_map;            /* DAT_073d8768: bmp/map.bmp (256×512) */
static sprite_t s_face[83];       /* DAT_073ac878: the enemy faces (64×64) */

/* off_005c7e2c: the face file of each enemy kind (the table runs 100 slots in
 * retail; 83.. are other strings no live kind reaches) */
static const char *const k_face_file[83] = {
    "sraim_g", "sraim_r", "sraim_b", "sraim_y", "sraim_p", "sraim_m",
    "alyman", "alyman_g", "alyman_b", "alyman_r",
    "killer_bee", "killer_bee", "killer_bee", "kinoko", "kinoko", "kabocha", "kobolt",
    "g_ropper", "r_ropper", "b_ropper", "y_ropper", "p_ropper",
    "yukiusagi", "boslim", "jijo", "syarm", "yukiusagi", "tiel", "grif",
    "yukiusagi", "yukiusagi", "yukiusagi", "yukiusagi", "yukiusagi", "foe",
    "moai_g", "moai_s", "bom_r", "bom_b", "bom_g", "bom_y", "aruma", "yukiusagi", "yukiusagi",
    "bule_knight", "nomal_knight", "gold_knight", "green_knight", "red_knight", "brack_knight",
    "skelton_n", "skelton_y", "ghost_ore", "ghost_nov", "boss_aly", "boss_kabo",
    "yukiusagi", "yukiusagi", "yukiusagi", "jijo", "jijo",
    "gorem", "gorem", "gorem", "gorem", "gorem", "gorem", "bossomu", "kani", "kani", "kurage", "kurage",
    "maou_ude", "aru_gorem", "aru_gorem", "aru_goremude", "aru_goremude",
    "yukiusagi", "yukiusagi", "yukiusagi", "yukiusagi", "yukiusagi", "yukiusagi",
};

void dungeon_hud_load_map(struct IDirect3DDevice8 *dev)
{
    if (s_map.tex == NULL)
        (void)sprite_load((IDirect3DDevice8 *)dev, "bmp/map.bmp", 256, 512, &s_map);
}

void dungeon_hud_load_faces(struct IDirect3DDevice8 *dev, const int32_t present[100])
{
    char path[64];
    for (int t = 0; t < 83; t++) {
        if (!present[t] || s_face[t].tex != NULL) continue;
        snprintf(path, sizeof path, "bmp/en/%s_mini.tga", k_face_file[t]);
        (void)sprite_load((IDirect3DDevice8 *)dev, path, 64, 64, &s_face[t]);
    }
}

/* DAT_073b1ab8, texture group 15: FUN_00473c15's tail loads the floor's title
 * card — the dungeon's title (1024 x 256), or on a boss floor the boss's name
 * (512 x 256), by DAT_0438b4c8 / DAT_0438b4cc. */
static sprite_t s_title;

static const char *title_card(int32_t dun, int32_t flo, int *boss)
{
    static const char *const k_rush[29] = {   /* dungeon 5 (the Boss Rush), floors 30..58 */
        "kingslim", "jijo", "charme", "kingahriman", "omu", "kani", "kingslim", "tiers",
        "kabocha", "kurage", "kingahriman", "gorem", "griffe", "omu", "monster", "griffe2",
        "kabocha", "jijo", "kingahriman", "omu", "kingslim", "kabocha", "kurage", "arma",
        "monster", "jijo", "gorem", "kani", "arma2",
    };
    static const struct { int32_t dun, flo; const char *name; } k_boss[] = {
        { 1,  4, "kingslim" }, { 1,  9, "jijo" }, { 1, 14, "charme" },
        { 2,  4, "tiers" }, { 2,  9, "kingslim" }, { 2, 14, "kani" }, { 2, 19, "monster" },
        { 2, 24, "omu" }, { 2, 29, "kingahriman" },
        { 3,  4, "monster" }, { 3,  9, "gorem" }, { 3, 14, "kingahriman" }, { 3, 19, "kurage" },
        { 3, 24, "kabocha" }, { 3, 29, "griffe" }, { 3, 34, "griffe2" }, { 3, 39, "monster" },
        { 3, 44, "omu" }, { 3, 49, "monster" },
        { 4,  4, "kingslim" }, { 4,  9, "omu" }, { 4, 14, "kingahriman" }, { 4, 19, "jijo" },
        { 4, 24, "kabocha" }, { 4, 29, "monster" }, { 4, 34, "arma" }, { 4, 39, "monster" },
        { 4, 44, "kurage" }, { 4, 49, "kabocha" }, { 4, 54, "monster" }, { 4, 59, "monster" },
        { 4, 64, "gorem" }, { 4, 69, "monster" }, { 4, 74, "jijo" }, { 4, 79, "omu" },
        { 4, 84, "arma2" }, { 4, 89, "monster" }, { 4, 94, "kani" }, { 4, 99, "monster" },
    };
    static const char *const k_title[6] = {
        "dungeon_title1", "dungeon_title2", "dungeon_title3",
        "dungeon_title4", "dungeon_title5", "dungeon_title6",
    };
    if (dun < 0 || dun > 5) return NULL;
    *boss = 1;
    if (dun == 5 && flo >= 30 && flo <= 58) return k_rush[flo - 30];
    for (size_t i = 0; i < sizeof k_boss / sizeof k_boss[0]; i++)
        if (k_boss[i].dun == dun && k_boss[i].flo == flo) return k_boss[i].name;
    *boss = 0;
    return k_title[dun];
}

void dungeon_hud_load_title(struct IDirect3DDevice8 *dev)
{
    if (s_title.tex) sprite_destroy(&s_title);   /* FUN_004746fc: group 15 */
    int boss = 0;
    const char *n = title_card(g_scene1_combat_stage_id, stage_gate_get_next(), &boss);
    if (n == NULL) return;
    char path[64];
    snprintf(path, sizeof path, boss ? "bmp/title/bossname_%s.tga" : "bmp/title/%s.tga", n);
    (void)sprite_load((IDirect3DDevice8 *)dev, path, boss ? 512 : 1024, boss ? 128 : 256, &s_title);
}

/* FUN_00406d50's tail (DAT_0438b1c0 == 1, a dungeon stage): the floor-title
 * banner while DAT_00648270 runs — kind 1 (the dungeon title) first flashes
 * white (system.bmp over the screen, opaque to tick 60 then 735 - 8t), the
 * card zooms in from 2x (2 - t / 40, floored at 1), fades in by 4t and out over
 * the last 64 ticks, its height closing as it fades */
void dungeon_hud_title_banner(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (dev == NULL || g_scene_state != SCENE_STATE_INGAME || mapcol_maptype() < 1) return;
    const int32_t t = g_title_banner, len = g_title_banner_len;
    if (t <= 0) return;
    if (g_title_banner_kind == 1) {
        int32_t a = 255;
        if (t <= 60 || (a = 735 - 8 * t) > 0) {
            const sprite_t *sys = &g_sysassets.system_bmp;   /* DAT_073aa188 */
            render_quad_bind(dev, sys);
            const float src[4] = { 0.0f, 0.0f, 7.0f, 7.0f };
            const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
            render_quad_add(dst, src, sys->width, sys->height, (uint32_t)a << 24);
            render_quad_flush(dev);
        }
    }
    int32_t a = 4 * t;
    if (a > 255) a = 255;
    if (t >= len - 64) a = a + 4 * (len - t) - 256;
    if (a < 0) a = 0;
    render_quad_bind(dev, &s_title);
    float sy = spill32(2.0L - (long double)(float)t * 0.025f);
    float sx = sy;
    if (sx < 1.0f) { sx = 1.0f; sy = 1.0f; }
    if (t >= len - 64)
        sy = spill32((long double)sy - (long double)(float)(t - len + 64) / 64.0f);
    const uint32_t col = 0xffffffu | ((uint32_t)a << 24);
    if (g_title_banner_kind == 1) {
        const float src[4] = { 0.0f, 0.0f, 640.0f, 240.0f };
        const float dst[4] = { spill32(320.0L - (long double)sx * 320.0f),
                               spill32(224.0L - (long double)sy * 128.0f),
                               spill32((long double)sx * 640.0f), spill32((long double)sy * 240.0f) };
        render_quad_add(dst, src, s_title.width, s_title.height, col);
    } else {
        const float src[4] = { 0.0f, 0.0f, 512.0f, 128.0f };
        const float dst[4] = { spill32(320.0L - (long double)sx * 256.0f),
                               spill32(224.0L - (long double)sy * 64.0f),
                               spill32((long double)sx * 512.0f), spill32((long double)sy * 128.0f) };
        render_quad_add(dst, src, s_title.width, s_title.height, col);
    }
    render_quad_flush(dev);
}

/* FUN_00407ab4: the "LEVEL UP!" pop at (x, y) while DAT_0438b920 runs — it
 * grows in over 10 ticks, flashes white 20..35 and fades out after 60 */
void dungeon_hud_levelup_pop(struct IDirect3DDevice8 *dev_in, float x, float y)
{
    static const float kpi = 3.1415927410125732f, k10 = 10.0f, k15 = 15.0f, k20 = 20.0f;
    static const float k0p3 = 0.30000001192092896f, k127 = 127.0f, k48 = 48.0f, k40 = 40.0f, k39 = 39.0f;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    const int32_t t = scene1_top_hud_levelup_timer();      /* DAT_0438b920 */
    if (t <= 0) return;
    int32_t a = 255;
    float dy = -10.0f, grow = k1;
    if (t > 60) a = 1215 - 16 * t;
    if (t < 10) {
        const float ft = spill32((float)t);
        grow = spill32(x87_crt_sin((double)spill32((long double)ft * kpi / k10)));
        grow = spill32((long double)grow * k0p3 + (long double)ft * k0p1);
        const float sn = spill32(x87_crt_sin((double)spill32((long double)ft * kpi / k10)));
        dy = spill32((long double)sn * -k20 - (long double)ft);
    }
    if (a <= 0) return;
    int32_t c = 127;
    if (t > 20 && t < 35) {
        const float u = spill32(x87_crt_sin((double)spill32((long double)spill32((float)(t - 20)) * kpi / k15)));
        c = x87_ftol(((long double)u + k1) * k127);
    }
    const sprite_t *win = &g_sysassets.item_win_tga;      /* DAT_073d8748 */
    if (win->tex == NULL) return;
    render_quad_bind(dev, win);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    quad(win, spill32((long double)x + 8.0f),
         spill32((long double)dy + y - k48 + k40 - (long double)grow * k40), 192.0f,
         spill32((long double)grow * k39), 640.0f, 416.0f, 832.0f, 455.0f,
         ((uint32_t)a << 24) | ((uint32_t)c << 16) | ((uint32_t)c << 8) | (uint32_t)c);
    render_quad_flush(dev);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* FUN_0040c962: the "got an item" notice while DAT_00648294 runs (120 ticks,
 * FUN_0040c923 opens it) — item_win.tga's banner strip (64, 640)-(96, 672)
 * unrolling from the top right (x 160 + 32·t to 496, sliding off past 100,
 * alpha 25·t closing 12 a tick past 100), the item's name ("%s+%d" with its
 * grade; a rare one walks back to the last record priced −1) at 0.6 while
 * DAT_00648250 is set, then its icon on the left */
void dungeon_hud_got_item(struct IDirect3DDevice8 *dev_in)
{
    static const float k160 = 160.0f, k496 = 496.0f, k104 = 104.0f, k12 = 12.0f, k24 = 24.0f;
    static const float k28 = 28.0f;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    const int32_t on = g_got_item_on;
    if (on == 0) return;
    float f = spill32((long double)spill32((float)on) * k0p1);
    int32_t a = 25 * on;
    if (f >= 1.0f) f = 1.0f;
    if (a > 255) a = 255;
    float x = spill32((long double)spill32((float)(32 * on)) + k160);
    if (x > k496) x = k496;
    if (on > 100) {
        a += 12 * (100 - on);
        x = spill32((long double)spill32((float)(8 * on - 800)) + x);
    }
    const sprite_t *win = &g_sysassets.item_win_tga;      /* DAT_073d8748 */
    const uint32_t col = 0xffffffu | ((uint32_t)a << 24);
    const float y = spill32(k104 - (long double)f * k12), h = spill32((long double)f * k24);
    render_quad_bind(dev, win);
    quad(win, spill32((long double)x - 4.0f - k24), y, 168.0f, h, 64.0f, k640, 96.0f,
         spill32((long double)f * k32 + k640), col);
    render_quad_flush(dev);
    const int32_t item = g_got_item;
    int32_t rec = tables_item_find_slot_by_id(&g_item, item >> 6);   /* FUN_004681f6 */
    if (rec < 0) return;
    if (g_got_item_bubble == 1) {
        int32_t plus = item & 0xf;
        if ((item & 0x10) != 0) {
            plus = 0;
            if (g_item.records[rec].price != -1)
                while (--rec > 0 && g_item.records[rec].price != -1) {}
        }
        char buf[256];
        if (plus > 0) snprintf(buf, sizeof buf, "%s+%d", g_item.records[rec].singular, plus);
        else          snprintf(buf, sizeof buf, "%s", g_item.records[rec].singular);
        font_draw_text(dev_in, spill32((long double)x + k28 - k24), 98.0f, buf, 0xffffffffu, 0.6f);
        rec = tables_item_find_slot_by_id(&g_item, item >> 6);
    }
    render_quad_state_setup(dev);                         /* FUN_0049b425 */
    const int32_t cat = g_item.records[rec].category;
    if (cat < 0 || cat >= SYSASSETS_ITEM_CATEGORIES) return;
    const sprite_t *page = &g_sysassets.item_icons[cat];  /* DAT_073d8778 */
    if (page->tex == NULL) return;
    int32_t tile = g_item.records[rec].subindex;
    if ((item & 0x10) != 0) tile = 0;
    const float u = spill32((long double)spill32((float)(tile % 8)) * k32);
    const float v = spill32((long double)spill32((float)(tile / 8)) * k32);
    render_quad_bind(dev, page);
    quad(page, spill32((long double)x - k24), y, k24, h, u, v, spill32((long double)u + k32),
         spill32((long double)v + k32), col);
    render_quad_flush(dev);
}

/* a pulse colour: the byte b in R, G and B over full alpha */
static uint32_t grey(int32_t b)
{
    return ((((uint32_t)(uint8_t)b | 0xffffff00u) << 8 | (uint32_t)b) << 8) | (uint32_t)b;
}

/* "%Nd" digits of the HUD's text quads */
static int fmt_int(char *buf, size_t n, const char *f, int v) { return snprintf(buf, n, f, v); }

/* FUN_00407cac 0x407cfd..: chara 30's ammo rows (DAT_056daff4, 45 a tier) */
static void gauge_chara30(IDirect3DDevice8 *dev, const sprite_t *base, float slide)
{
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)base->tex);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    int32_t n = PW(0x56daff4u).i;
    if (n > 225) n = 225;
    const float y = spill32((long double)slide + 360.0f);
    for (int row = 0; row < 2; row++) {
        int32_t cnt = row == 0 ? 12 : n % 45 / 5;
        int32_t tier = row == 0 ? 5 - n / 45 : 4 - n / 45;
        if (tier < 0) tier = 0;
        for (int32_t k = 0; k < cnt; k++)
            quad(base, spill32((long double)spill32((float)(7 * k)) + 16.0f), y, 15.0f, 23.0f,
                 (float)(16 * (tier + 13)), 256.0f, (float)(16 * tier + 223), 279.0f, DIFFUSE_HALF);
    }
    quad(base, 100.0f, spill32((long double)slide + 362.0f), 63.0f, 15.0f, 256.0f, 304.0f, 319.0f, 319.0f, DIFFUSE_HALF);
    render_quad_flush(dev);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* FUN_00407cac 0x407e2b..: chara 42's charge pips (DAT_056db0e4 of them,
 * DAT_056db0e0 / 10 filled, pulsing while DAT_056db0ec runs), its label, and
 * the DAT_056db048 == 4 gauge (DAT_056db0f0 of 10) */
static void gauge_chara42(IDirect3DDevice8 *dev, const sprite_t *base, float slide)
{
    static const float kpi = 3.1415927410125732f, k30 = 30.0f, k64f = 64.0f, k127 = 127.0f;
    const sprite_t *win = &g_sysassets.item_win_tga;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)base->tex);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    const int32_t e4 = PW(0x56db0e4u).i, e0 = PW(0x56db0e0u).i, ec = PW(0x56db0ecu).i;
    const float y = spill32((long double)slide + 360.0f);
    int32_t pulse = 127;
    if (ec > 10) {
        int32_t t = ec - 10;
        if (t > 30) t = 30;
        pulse = x87_ftol((long double)spill32(x87_crt_sin((double)spill32((long double)spill32((float)t) * kpi / k30)))
                         * k64f + k127);
    }
    for (int32_t k = 0; k != e4; k++) {
        const float x = spill32((long double)spill32((float)(7 * k)) + 16.0f);
        quad(base, x, y, 15.0f, 23.0f, 240.0f, 320.0f, 255.0f, 343.0f, DIFFUSE_HALF);
        if (e0 / 10 > k) {
            const int lit = ec > 0 && ec < 11;
            quad(base, x, y, 15.0f, 23.0f, lit ? 224.0f : 208.0f, 320.0f, lit ? 239.0f : 223.0f, 343.0f, grey(pulse));
        }
    }
    const float ly = spill32((long double)slide + 346.0f);
    if (ec >= 1 && ec <= 10) {
        quad(base, 16.0f, ly, 63.0f, 15.0f, 256.0f, 320.0f, 319.0f, 335.0f, 0x7f7f7fu | (uint32_t)(0x19000000 * ec));
    } else if (ec > 10) {
        int32_t t = ec - 10;
        if (t > 30) t = 30;
        const int32_t c = x87_ftol((long double)spill32(x87_crt_sin((double)spill32((long double)spill32((float)t) * kpi / k30)))
                                   * k64f + k127);
        quad(base, 16.0f, ly, 63.0f, 15.0f, 256.0f, 336.0f, 319.0f, 351.0f, grey(c));
    }
    render_quad_flush(dev);
    if (PW(0x56db048u).i == 4 && PW(0x56db0f0u).i > 0 && win->tex) {
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);
        quad(win, 208.0f, 216.0f, 224.0f, 48.0f, 704.0f, 976.0f, 928.0f, 1024.0f, DIFFUSE_HALF);
        int32_t n = PW(0x56db0f0u).i;
        if (n > 10) n = 10;
        for (int32_t k = 0; k < n; k++) {
            const float o = spill32((float)(8 * k));
            quad(win, spill32((long double)o + 236.0f), 219.0f, 8.0f, 40.0f, 928.0f, 976.0f, 936.0f, 1016.0f, DIFFUSE_HALF);
            quad(win, spill32(395.0f - (long double)o), 219.0f, 8.0f, 40.0f, 928.0f, 976.0f, 936.0f, 1016.0f, DIFFUSE_HALF);
        }
        if (PW(0x56db0f0u).i >= 10)
            quad(win, 256.0f, 216.0f, 128.0f, 48.0f, 576.0f, 896.0f, 704.0f, 928.0f, DIFFUSE_HALF);
        render_quad_flush(dev);
    }
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* the minimap's world → map offset: 39 − (p + 40) · 0.4 − 1 + origin */
static float map_off(float p, float origin)
{
    return spill32(39.0L - ((long double)p + 40.0f) * 0.4000000059604645f - 1.0L + origin);
}

/* FUN_00407cac 0x408495..: the minimap (map.bmp) of a floor bigger than 3×3 —
 * the visited rooms' tiles, the player, the live enemies (the 29..31 kinds
 * pink), the exit (cyan) and the found marks (yellow) */
static void minimap(IDirect3DDevice8 *dev, float slide2)
{
    if (g_floor_w == 3 && g_floor_h == 3) return;
    if (s_map.tex == NULL) return;
    const float ox = spill32(620.0f - (long double)spill32((float)(16 * g_floor_w)));
    const float oy = spill32(432.0L - spill32((float)(16 * g_floor_h)) + slide2 - 16.0f);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s_map.tex);
    const float sc = spill32((long double)spill32((float)render_quad_screen_w_int()) / 640.0f);
    for (int32_t r = 0; r < g_floor_h; r++) {
        for (int32_t c = 0; c < g_floor_w; c++) {
            const floor_room_t *cell = &g_floor_rooms[r * FLOOR_GRID_STRIDE + c];
            if (cell->type == -1 || cell->pad == 0) continue;
            int32_t pat = cell->pat;
            const int32_t var = cell->var;
            if (var >= 5) pat += 4;
            const float u = spill32((float)(48 * (var % 5))), v = spill32((float)(48 * pat));
            const float l = spill32((long double)u + 0.5f), t = spill32((long double)v + 0.5f);
            const float rr = spill32((long double)u + 47.5f), b = spill32((long double)v + 47.5f);
            const float x0 = spill32((long double)sc * spill32((long double)spill32((float)(16 * (c + 1))) + ox));
            const float y0 = spill32((long double)sc * spill32((long double)spill32((float)(16 * (r + 1))) + oy));
            const float wh = spill32((long double)sc * 16.0f);
            const float x1 = spill32((long double)wh + x0), y1 = spill32((long double)wh + y0);
            const float ul = spill32((long double)l / 256.0f), ur = spill32((long double)rr / 256.0f);
            const float vt = spill32((long double)t / 512.0f), vb = spill32((long double)b / 512.0f);
            render_quad_vtx_t *q = render_quad_reserve(6);
            if (q == NULL) continue;
            const float xs[6] = { x1, x0, x1, x0, x0, x1 }, ys[6] = { y1, y1, y0, y1, y0, y0 };
            const float us[6] = { ur, ul, ur, ul, ul, ur }, vs[6] = { vb, vb, vt, vb, vt, vt };
            for (int k = 0; k < 6; k++) {
                q[k].x = xs[k]; q[k].y = ys[k]; q[k].diffuse = 0x9bffffffu;
                q[k].u = us[k]; q[k].v = vs[k];
            }
        }
    }
    render_quad_flush(dev);
    quad(&s_map, map_off(g_scene1_actor_pos[0][0], ox), map_off(g_scene1_actor_pos[0][2], oy), 4.0f, 4.0f,
         0.0f, 384.0f, 8.0f, 392.0f, 0xffffffffu);
    for (int i = 0; i < ACTOR_COUNT; i++) {
        const actor_t *a = &g_actor[i];
        if (act_geti(a, ACT_INUSE) == 0 || act_geti(a, 0x43c) == 0) continue;
        const int32_t kind = act_geti(a, ACT_TYPE);
        const uint32_t col = (kind == 29 || kind == 30 || kind == 31) ? 0xffff7fffu : 0xffff0000u;
        quad(&s_map, map_off(act_getf(a, ACT_POS), ox), map_off(act_getf(a, ACT_POS + 8), oy), 4.0f, 4.0f,
             24.0f, 384.0f, 32.0f, 392.0f, col);
    }
    if (!stage_gate_floor_is_checkpoint() && g_floor_extra_n != -1) {
        const int32_t *found = &g_floor_marks[0][0] + 21;     /* DAT_0438b488.. */
        for (int32_t k = 1; k != g_floor_extra_n + 2; k++) {
            if (found[k - 1] == 0) continue;
            const float mx = map_off(spill32((long double)spill32((float)-g_floor_marks[k][0]) * 40.0f), ox);
            const float my = map_off(spill32((long double)spill32((float)-g_floor_marks[k][1]) * 40.0f), oy);
            if (k == 1) quad(&s_map, mx, my, 4.0f, 4.0f, 8.0f, 384.0f, 16.0f, 392.0f, 0xff00ffffu);
            else        quad(&s_map, mx, my, 4.0f, 4.0f, 16.0f, 384.0f, 24.0f, 392.0f, 0xffffff00u);
        }
    }
    render_quad_flush(dev);
}

/* FUN_00407cac 0x40878b..: the floor plate — the frame, the dungeon's name
 * strip (Boss Rush 5: its own), and on a regular run the floor number + "F" */
static void floor_plate(IDirect3DDevice8 *dev, const sprite_t *win, float slide)
{
    int32_t dun = g_scene1_combat_stage_id, flo = stage_gate_get_next();
    if (stage_post_load_get_mode_b4d0() >= 0) dun = stage_post_load_get_mode_b4d0();
    if (g_dat_0438b4d4 >= 0) flo = g_dat_0438b4d4;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);
    const float y = spill32((long double)slide + 436.0f);
    quad(win, 460.0f, y, 180.0f, 40.0f, 448.0f, 480.0f, 628.0f, 520.0f, 0xffffffffu);
    if (dun == 5)
        quad(win, 468.0f, spill32((long double)y + 4.0f), 96.0f, 32.0f, 640.0f, 352.0f, 736.0f, 384.0f, 0xffffffffu);
    else
        quad(win, 468.0f, y, 180.0f, 40.0f, 448.0f, (float)(8 * (5 * dun + 65)), 628.0f,
             (float)(8 * (5 * dun + 70)), 0xffffffffu);
    if (g_dat_0438b4d4 < 0) {
        char buf[256];
        fmt_int(buf, sizeof buf, "%2d", flo + 1);
        int32_t o = 0;
        for (int k = 0; k < 3 && buf[k] != 0; k++) {
            if (buf[k] == ' ') continue;
            const int32_t d = 24 * (buf[k] - '0');
            quad(win, spill32((long double)spill32((float)o) + 560.0f), spill32((long double)y + 2.0f), 24.0f, 32.0f,
                 (float)(d + 704), 864.0f, (float)(d + 728), 896.0f, 0xffffffffu);
            o += 16;
        }
        quad(win, spill32((long double)spill32((float)o) + 600.0f - 40.0f), spill32((long double)y + 2.0f), 24.0f, 32.0f,
             944.0f, 864.0f, 968.0f, 896.0f, 0xffffffffu);
    }
    render_quad_flush(dev);
}

/* FUN_00407cac 0x408a9b..: the kill combo (DAT_0438b90c) — "%3d" digits and
 * the label */
static void kill_combo(IDirect3DDevice8 *dev, const sprite_t *win)
{
    if (g_kill_combo <= 0) return;
    char buf[256];
    fmt_int(buf, sizeof buf, "%3d", g_kill_combo);
    for (int k = 0; k < 3; k++) {
        if (buf[k] == ' ') continue;
        const int32_t d = buf[k] - '0';
        quad(win, spill32((long double)spill32((float)(24 * k)) + 16.0f), 160.0f, 32.0f, 40.0f,
             (float)(32 * d), 960.0f, (float)(32 * (d + 1)), 1000.0f, 0xffffffffu);
    }
    quad(win, 96.0f, 160.0f, 96.0f, 40.0f, 320.0f, 960.0f, 416.0f, 1000.0f, 0xffffffffu);
    render_quad_flush(dev);
}

/* FUN_00490c78: through the render's view / projection DAT_073de29c / 2dc —
 * at the HUD's draw that is this frame's camera (the sim's DAT_095d3730 copy
 * lags a frame: the popups sat a few pixels off while the camera moved) */
static void project(const int32_t *p, float *sx, float *sy, float *vz)
{
    float x, y, z;
    memcpy(&x, &p[0], 4); memcpy(&y, &p[1], 4); memcpy(&z, &p[2], 4);
    scene1_project_world_mat(scene1_render_view_matrix(), scene1_render_proj_matrix(), x, y, z, sx, sy, vz);
}

/* FUN_00407cac 0x408c56..: the word popups (kind >= 10: "Side Attack!" …,
 * item_win rows 24·(kind + 22)), stacked by their value, fading their last
 * 16 ticks */
static void popups_words(IDirect3DDevice8 *dev, const sprite_t *win)
{
    for (int i = 0; i < 32; i++) {
        const int32_t *p = g_popup[i];
        if (p[3] <= 0 || p[6] < 10) continue;
        float sx, sy, vz;
        project(p, &sx, &sy, &vz);
        if (!(vz < 0.0f)) continue;
        int32_t a = 255;
        if (p[4] - p[3] > p[4] - 16) a = 16 * p[3] - 1;
        const float y = spill32((long double)spill32((float)(p[3] / 4)) + sy - 32.0f
                                - spill32((float)(16 * p[5])));
        quad(win, spill32((long double)sx - 64.0f), y, 128.0f, 24.0f,
             704.0f, (float)(8 * (3 * p[6] + 66)), 832.0f, (float)(8 * (3 * p[6] + 69)),
             0x7f7f7fu | ((uint32_t)a << 24));
    }
    render_quad_flush(dev);
    (void)dev;
}

/* FUN_00407cac 0x408dc3..: the number popups (kind < 10) on hpmp_base —
 * 0 damage (bouncing), 1 / 6 HP / SP gain with their label, 2 a percentage,
 * 3 the "Critical" frame, 4 purple, 5 the growing red one */
static void popups_numbers(IDirect3DDevice8 *dev, const sprite_t *base)
{
    static const float kpi = 3.1415927410125732f, k16 = 16.0f, k32f = 32.0f;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)base->tex);
    for (int i = 0; i < 32; i++) {
        const int32_t *p = g_popup[i];
        if (p[3] <= 0 || p[6] >= 10) continue;
        float sx, sy, vz;
        project(p, &sx, &sy, &vz);
        if (!(vz < 0.0f)) continue;
        char buf[256];
        fmt_int(buf, sizeof buf, "%3d", p[5]);
        const int32_t ticks = p[3], el = p[4] - p[3], kind = p[6];
        int32_t a = 255;
        if (el > p[4] - 16) a = 16 * (p[4] - el) - 1;
        const uint32_t al = (uint32_t)a << 24;
        uint32_t col = 0x7f7f7fu | al;
        float sc = k1;
        if (kind == 4) col = 0x9b3f9bu | al;
        if (kind != 5) sx = spill32((long double)sx - 18.0f);
        const float yl = spill32((long double)spill32((float)(ticks / 4)) + sy - k16);
        if (kind == 3) {
            col = 0xc3e9fbu | al;
            sx = spill32((long double)sx + k16);
            quad(base, spill32((long double)sx - 72.0f), yl, 80.0f, 32.0f, 37.0f, 576.0f, 117.0f, 608.0f, col);
            quad(base, sx, yl, 16.0f, 32.0f, 117.0f, 576.0f, 133.0f, 608.0f, col);
            if (p[5] > 99) sx = spill32((long double)sx + 12.0f);
        }
        if (kind == 1 || kind == 6) {
            col = al | 0x37a5ffu;
            quad(base, spill32((long double)sx - 44.0f + k16), yl, 44.0f, 24.0f,
                 kind == 1 ? 193.0f : 241.0f, 432.0f, kind == 1 ? 237.0f : 285.0f, 456.0f, col);
            quad(base, spill32((long double)sx + k16), yl, 16.0f, 24.0f, 461.0f, 464.0f, 477.0f, 488.0f, col);
            if (p[5] > 9) sx = spill32((long double)sx + 12.0f);
            if (p[5] > 99) sx = spill32((long double)sx + 24.0f);
        }
        if (kind == 5) {
            sc = spill32(5.0L - (long double)spill32((float)el) / 2.0f);
            if (sc < 2.0f) sc = 2.0f;
            col = 0xbf1f1fu | al;
        }
        if (kind == 2) {
            col = 0xffcd37u | al;
            sx = spill32((long double)sx - 8.0f);
        }
        if (kind == 5)
            sx = spill32((long double)sx - (long double)sc * (p[5] < 10 ? 8.0f : 24.0f));
        for (int k = 0; k < 3; k++) {
            const char ch = buf[k];
            if (ch == ' ') { sx = spill32((long double)sx + 12.0f); continue; }
            const int32_t idx = ch == '/' ? 10 : ch - '0';
            float y = sy;
            if (kind == 5) {
                y = spill32((long double)sy - (long double)sc * k16);
            } else if (kind == 1 || kind == 6 || kind == 2 || kind == 3) {
                y = yl;
            } else {
                const int32_t t = 56 + 2 * k - ticks;
                if (t > 0 && t < 16) {
                    const float s = spill32(x87_crt_sin((double)spill32((long double)spill32((float)t) * kpi / k16)));
                    y = spill32((long double)sy - (long double)s * k32f);
                }
            }
            const float u = spill32((float)(20 * idx + 288));
            quad(base, sx, y, spill32((long double)sc * 19.0f), spill32((long double)sc * 24.0f),
                 spill32((long double)u + 0.5f), 432.0f, spill32((long double)u + 19.5f), 456.0f, col);
            sx = spill32((long double)sc * 12.0f + sx);
        }
        if (kind == 2)
            quad(base, sx, yl, 16.0f, 32.0f, 133.0f, 576.0f, 149.0f, 608.0f, col);
    }
    render_quad_flush(dev);
}

/* FUN_00407cac 0x40927f..: the struck enemy's bar while DAT_0438b908 runs —
 * ene_hp2's frame and bar (HP2 / max), its face, "Lv" + level, "HP" cur/max */
static void enemy_bar(IDirect3DDevice8 *dev, const sprite_t *base, float slide)
{
    if (g_scene1_combat_dat_0438b908 <= 0) return;
    const int32_t idx = g_scene1_combat_dat_0438b904;
    if (idx < 0 || idx >= ACTOR_COUNT) return;
    const actor_t *a = &g_actor[idx];
    const sprite_t *eh = &g_sysassets.ene_hp2_tga;      /* DAT_073cc910 */
    if (eh->tex == NULL) return;
    float mx = act_getf(a, ACT_HP_MAX);
    if (mx < k1) mx = k1;
    const float ratio = spill32((long double)act_getf(a, ACT_HP2) / mx);
    const float y = spill32((long double)scene1_hud_slide() * -128.0f);
    (void)slide;
    render_quad_bind(dev, eh);
    const float by = spill32((long double)y + 20.0f);
    quad(eh, 416.0f, by, 207.0f, 19.0f, 56.0f, 148.0f, 263.0f, 167.0f, 0xffffffffu);
    quad(eh, 416.0f, by, spill32((long double)ratio * 207.0f), 19.0f, 56.0f, 84.0f, 263.0f, 103.0f, 0xffffffffu);
    quad(eh, 360.0f, y, 280.0f, 64.0f, 0.0f, 0.0f, 280.0f, 64.0f, 0xffffffffu);
    render_quad_flush(dev);
    const int32_t kind = act_geti(a, ACT_TYPE);
    if (kind >= 0 && kind < 83 && s_face[kind].tex != NULL) {
        render_quad_bind(dev, &s_face[kind]);
        quad(&s_face[kind], 364.0f, spill32((long double)y + 28.0f - 32.0f), 64.0f, 64.0f,
             0.0f, 0.0f, 64.0f, 64.0f, 0xffffffffu);
        render_quad_flush(dev);
    }
    render_quad_bind(dev, eh);
    const float ly = spill32((long double)y + 32.0f);
    quad(eh, 404.0f, ly, 24.0f, 24.0f, 0.0f, 192.0f, 24.0f, 216.0f, 0xffffffffu);
    int32_t lvl = act_geti(a, ACT_LEVEL) + 1;
    if (lvl > 99) lvl = 99;
    char buf[256];
    fmt_int(buf, sizeof buf, "%d", lvl);
    for (int k = 0; k < 7 && buf[k] != 0; k++) {
        const int32_t d = buf[k] - '0';
        quad(eh, spill32((long double)spill32((float)(12 * k)) + 418.0f), ly, 24.0f, 24.0f,
             (float)(8 * (3 * d + 3)), 192.0f, (float)(8 * (3 * d + 6)), 216.0f, 0xffffffffu);
    }
    render_quad_flush(dev);
    render_quad_bind(dev, base);
    const float hy = spill32((long double)y + 40.0f);
    quad(base, 456.0f, hy, k38p4, k19p2, 199.0f, 432.0f, 247.0f, 456.0f, 0xffffffffu);
    hpmp_digits(488.0f, hy, x87_ftol(act_getf(a, ACT_HP)), x87_ftol(act_getf(a, ACT_HP_MAX)));
    render_quad_flush(dev);
}

/* FUN_00407cac */
void dungeon_hud_render(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    CALL_TRACE_ENTER(0x407cacu);
    const sprite_t *base = &g_sysassets.hpmp_base_tga;
    const sprite_t *win = &g_sysassets.item_win_tga;
    const float b1dc = scene1_hud_slide();
    const float slide = spill32((long double)b1dc * k128);
    dungeon_hud_party_panel(dev_in, 0.0f, slide, chara_equip_get_current_chara(), 0);
    if (base->tex == NULL || win->tex == NULL) return;
    const int32_t chr = PW(0x56da1ccu).i;
    if (chr == 30) gauge_chara30(dev, base, slide);
    if (chr == 42 && PW(0x56db0e4u).i > 0) gauge_chara42(dev, base, slide);

    /* the floor events, right-aligned, sliding in by DAT_0438b954 */
    for (int32_t k = 0; k < g_floor_event_n; k++) {
        char buf[256];
        size_t o = 0;
        for (const char *q = g_floor_event_text[k]; *q != 0 && o + 1 < sizeof buf; q++) {
            if (q[0] == '%' && q[1] == '%') q++;
            buf[o++] = *q;
        }
        buf[o] = 0;
        float x = spill32(960.0f - (long double)spill32((float)(16 * g_dat_0438b954)));
        if (x < 632.0f) x = 632.0f;
        font_draw_text_right(dev_in, x, spill32((long double)b1dc * k256 + 420.0f), buf, 0xc8ffffffu,
                             0.8250000476837158f);
    }
    minimap(dev, spill32((long double)b1dc * k256));
    floor_plate(dev, win, slide);
    kill_combo(dev, win);

    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    if (PW(0x56db00cu).i != 0) {
        quad(win, 280.0f, 120.0f, 80.0f, 80.0f, 288.0f, 608.0f, 368.0f, 688.0f, DIFFUSE_HALF);
        render_quad_flush(dev);
    }
    popups_words(dev, win);
    popups_numbers(dev, base);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    enemy_bar(dev, base, slide);
    dungeon_hud_levelup_pop(dev_in, 16.0f, 400.0f);
}

#endif /* _WIN32 */
