/*
 * scene1_chr_prepass.c — Cchr.2e: the records / people sprite pre-pass.
 * See scene1_chr_prepass.h for the chip writeup.  Engine FUN_0045672a.
 */

#include "scene1_chr_prepass.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ── engine FUN_0045526a — the index co-sort (host-tested, portable) ────── */

void chr_prepass_sort(int32_t *keys, int32_t *idx, int n)
{
    /* Bounded bubble sort: for each pass p in [0, n-1), bubble the smallest
     * key in keys[p..n-1] down to keys[p], swapping idx[] in lockstep.  The
     * engine's inner loop runs (n-1-p) compares from the top down, comparing
     * keys[j] against keys[j-1] (objdump @ 0x45527e: `if (piVar3[1] < *piVar3)
     * swap`). */
    for (int p = 0; p < n - 1; p++) {
        for (int j = n - 1; j > p; j--) {
            if (keys[j] < keys[j - 1]) {
                int32_t tk = keys[j]; keys[j] = keys[j - 1]; keys[j - 1] = tk;
                int32_t ti = idx[j];  idx[j]  = idx[j - 1];  idx[j - 1]  = ti;
            }
        }
    }
}

/* ── Win32 render path (engine FUN_0045672a, full) ──────────────────────── */
#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "math3d.h"
#include "scene1_records.h"      /* g_scene1_records_a/b + counts + offsets */
#include "scene1_emit_record.h"  /* scene1_emit_record (FUN_00455191)       */
#include "scene1_chr_sprite.h"   /* scene1_chr_sprite_render (FUN_0045a56f)  */
#include "scene1_camera.h"       /* g_scene1_camera_orient (= DAT_0438cdf8)  */

/* ── engine float constants (objdump @ 0x45672a / .rdata 2026-05-29) ──────
 *   0x5198d8 = 0.2     (records scale multiplier, sections A+B)
 *   0x51935c = 0.5     (records_b >=0x46 scale-z multiplier)
 *   0x519c2c = -0.5    (records_b >=0x46 scale-x multiplier)
 *   0x519c7c =  0.14 / 0x519d78 = 0.04 / 0x519d74 = -0.14
 *                      (records_b <0x46 fixed scale: sx=-0.14, sy=0.04, sz=0.14)
 *   0x519630 = 255.0   (people alpha: ·255/255, a verbatim no-op)
 *   0x5198f8 = 0.05    (people sprite scale = desc[+0x44]·0.05)               */
#define PREPASS_RECORDS_SCALE_MUL  0.2f
#define PREPASS_RB_FIXED_SX       (-0.14f)
#define PREPASS_RB_FIXED_SY        0.04f
#define PREPASS_RB_FIXED_SZ        0.14f
#define PREPASS_PEOPLE_SCALE_MUL   0.05f

/* records_b size field the AGE-branch scale reads — engine piVar5[0x1c] with
 * piVar5 anchored at OFF_AGE (38), i.e. record dword 38+0x1c = 66.  Past the
 * named SCENE1_RECORDS_B_OFF_* set but within the 0x49-dword stride. */
#define SCENE1_RECORDS_B_OFF_PREPASS_SIZE 66

/* Reinterpret an int32 record dword as the float the engine stores there. */
static float prepass_f(const int32_t *rec, int dword)
{
    float f;
    memcpy(&f, &rec[dword], sizeof f);
    return f;
}

/* ── Section A/B shared one-time D3D envelope (engine FUN_00456c4f) ──────── */
static void chr_prepass_ab_setup(IDirect3DDevice8 *dev)
{
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff000000u); /* 0x8b */
    IDirect3DDevice8_LightEnable(dev, 0, TRUE);                       /* +0xb8 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,       TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE,  TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF,      0);     /* 0x18 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);   /* 4=2 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);      /* 0x10=2 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);      /* 0x11=2 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE2X);   /* 1=7 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);  /* 0x13=2 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);  /* 0x14=2 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);        /* 0x1c=0 */
}

/* ── Section C (people) one-time D3D envelope (engine @ 0x456a76) ───────── */
static void chr_prepass_people_setup(IDirect3DDevice8 *dev)
{
    IDirect3DDevice8_SetVertexShader(dev, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1); /* 0x142 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);  /* 4=4 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   8);                /* 1=8 ADDSIGNED */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);    /* 5=2 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);    /* 6=0 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);    /* 0x10=1 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);    /* 0x11=1 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
}

/* ── Section C dormant accessors (people table + sprite-desc table) ───────
 * The 128-entry people table (engine DAT_0076b970, stride 0xba4) and the
 * sprite-descriptor table (DAT_005c23f0, stride 0x68) have no port-side
 * storage yet — their populator is unported, exactly like the walker's NPC
 * pass.  Until then chr_prepass_people_base() returns NULL and Section C is
 * skipped whole.  When the people table ports, point these at it and the
 * body below fires verbatim. */
#define CHR_PREPASS_PEOPLE_COUNT   128
#define CHR_PREPASS_PEOPLE_STRIDE  0xba4   /* bytes */
#define CHR_PREPASS_DESC_STRIDE    0x68    /* bytes */

/* people record byte offsets (engine, relative to DAT_0076b970). */
#define PEOPLE_OFF_SORT_KEY  0x450   /* DAT_0076bdc0 — float depth key       */
#define PEOPLE_OFF_DESC_IDX  0x424   /* DAT_0076bd94 — sprite-descriptor idx */
#define PEOPLE_OFF_ACTIVE    0x428   /* DAT_0076bd98 — active flag (0 = skip) */
#define PEOPLE_OFF_ALPHA     0x3dc   /* DAT_0076bd4c — alpha byte (0xff skip) */
#define PEOPLE_OFF_ALPHA_MUL 0xaf8   /* per-entry alpha multiplier (float)   */
#define PEOPLE_OFF_POS_X     0x3f0   /* DAT_0076bd60 */
#define PEOPLE_OFF_POS_Y     0x3f4
#define PEOPLE_OFF_POS_Z     0x3f8
/* sprite-descriptor byte offsets (engine, relative to DAT_005c23f0). */
#define DESC_OFF_GATE        0x20    /* DAT_005c2410 — draw only if 0        */
#define DESC_OFF_CHAR_ID     0x24    /* leaf char id / texture-table index   */
#define DESC_OFF_SCALE       0x44    /* sprite scale (× 0.05)                */

static const uint8_t *chr_prepass_people_base(void) { return NULL; }  /* DAT_0076b970 */
static const uint8_t *chr_prepass_desc_base(void)   { return NULL; }  /* DAT_005c23f0 */
/* texture for a descriptor's char id — DAT_073a9b18[id*0x10].  Dormant. */
static void *chr_prepass_sheet_texture(int char_id) { (void)char_id; return NULL; }

/* __ftol — truncate toward zero, i.e. the C (int) cast (engine FUN_00503954). */
static int prepass_ftol(float f) { return (int)f; }

void scene1_chr_prepass_render(struct IDirect3DDevice8 *dev_in)
{
    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* The A/B envelope is applied lazily on the first drawn slot of EITHER
     * section (engine local_10, shared across both loops). */
    int ab_setup_done = 0;

    /* ── Section A: records_b slots with TYPE == 0x61 ('a') ─────────────── */
    for (int i = 0; i < g_scene1_records_b_count; i++) {
        const int32_t *rec = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        int32_t type = rec[SCENE1_RECORDS_B_OFF_TYPE];
        if (type == 0 || type != 0x61)
            continue;
        if (!ab_setup_done) { ab_setup_done = 1; chr_prepass_ab_setup(dev); }

        float size = prepass_f(rec, SCENE1_RECORDS_B_OFF_PREPASS_SIZE)
                     * PREPASS_RECORDS_SCALE_MUL;   /* engine local_8 */
        float world[16], scale[16];

        if (rec[SCENE1_RECORDS_B_OFF_AGE] < 0x46) {
            /* young: y forced to 0, a fixed small scale. */
            mat4_translation(world, prepass_f(rec, SCENE1_RECORDS_B_OFF_POS_X),
                             0.0f, prepass_f(rec, SCENE1_RECORDS_B_OFF_POS_Z));
            mat4_scaling(scale, PREPASS_RB_FIXED_SX, PREPASS_RB_FIXED_SY,
                         PREPASS_RB_FIXED_SZ);
        } else {
            /* mature: full position + size-driven scale. */
            mat4_translation(world, prepass_f(rec, SCENE1_RECORDS_B_OFF_POS_X),
                             prepass_f(rec, SCENE1_RECORDS_B_OFF_POS_Y),
                             prepass_f(rec, SCENE1_RECORDS_B_OFF_POS_Z));
            mat4_scaling(scale, size * -0.5f, size, size * 0.5f);
        }
        mat4_mul(world, scale, world);                 /* world = scale × T   */
        {   /* engine multiplies by Scaling(1,1,1) — a verbatim no-op. */
            float ident[16];
            mat4_scaling(ident, 1.0f, 1.0f, 1.0f);
            mat4_mul(world, ident, world);
        }
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);
        scene1_emit_record(dev_in, NULL);              /* FUN_00455191(&DAT_073a9658) */
    }

    /* ── Section B: records_a slots with TYPE == 0x97 ───────────────────── */
    for (int i = 0; i < g_scene1_records_a_count; i++) {
        const int32_t *rec = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        int32_t type = rec[SCENE1_RECORDS_A_OFF_TYPE];
        if (type == -1 || type != 0x97)
            continue;
        if (!ab_setup_done) { ab_setup_done = 1; chr_prepass_ab_setup(dev); }

        float size = prepass_f(rec, SCENE1_RECORDS_A_OFF_SCALE)
                     * PREPASS_RECORDS_SCALE_MUL;
        float world[16], scale[16], roty[16];

        mat4_translation(world, prepass_f(rec, SCENE1_RECORDS_A_OFF_POS_X),
                         prepass_f(rec, SCENE1_RECORDS_A_OFF_POS_Y),
                         prepass_f(rec, SCENE1_RECORDS_A_OFF_POS_Z));
        mat4_scaling(scale, -size, size, size);
        mat4_mul(world, scale, world);                 /* world = scale × T   */
        mat4_rotation_y(roty, prepass_f(rec, SCENE1_RECORDS_A_OFF_ROT_X));
        mat4_mul(world, roty, world);                  /* world = rotY × …     */
        {   /* engine Scaling(1,1,1) no-op. */
            float ident[16];
            mat4_scaling(ident, 1.0f, 1.0f, 1.0f);
            mat4_mul(world, ident, world);
        }
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);
        scene1_emit_record(dev_in, NULL);              /* FUN_00455191(&DAT_073a9658) */
    }

    /* ── Section C: the depth-sorted people billboards ──────────────────── */
    const uint8_t *people = chr_prepass_people_base();
    const uint8_t *descs  = chr_prepass_desc_base();
    if (people == NULL || descs == NULL)
        return;                       /* dormant: no people table today */

    int32_t keys[CHR_PREPASS_PEOPLE_COUNT];
    int32_t idx[CHR_PREPASS_PEOPLE_COUNT];
    for (int i = 0; i < CHR_PREPASS_PEOPLE_COUNT; i++) {
        float key;
        memcpy(&key, people + (size_t)i * CHR_PREPASS_PEOPLE_STRIDE
               + PEOPLE_OFF_SORT_KEY, sizeof key);
        keys[i] = prepass_ftol(key);
        idx[i]  = i;
    }
    chr_prepass_sort(keys, idx, CHR_PREPASS_PEOPLE_COUNT);

    int people_setup_done = 0;
    int last_tex_char = -1;
    for (int s = 0; s < CHR_PREPASS_PEOPLE_COUNT; s++) {
        const uint8_t *rec = people + (size_t)idx[s] * CHR_PREPASS_PEOPLE_STRIDE;

        int32_t active, alpha_byte, desc_idx;
        memcpy(&active,     rec + PEOPLE_OFF_ACTIVE,   sizeof active);
        memcpy(&alpha_byte, rec + PEOPLE_OFF_ALPHA,    sizeof alpha_byte);
        memcpy(&desc_idx,   rec + PEOPLE_OFF_DESC_IDX, sizeof desc_idx);
        if (active == 0 || alpha_byte == 0xff)
            continue;

        const uint8_t *desc = descs + (size_t)desc_idx * CHR_PREPASS_DESC_STRIDE;
        int32_t desc_gate;
        memcpy(&desc_gate, desc + DESC_OFF_GATE, sizeof desc_gate);
        if (desc_gate != 0)
            continue;

        if (!people_setup_done) {
            people_setup_done = 1;
            chr_prepass_people_setup(dev);
        }

        /* alpha = (int)((float)alpha_byte · 255/255) — the ·255/255 is a
         * verbatim engine no-op; then scaled by the per-entry multiplier. */
        float alpha_mul;
        memcpy(&alpha_mul, rec + PEOPLE_OFF_ALPHA_MUL, sizeof alpha_mul);
        int final_alpha = prepass_ftol((float)alpha_byte * alpha_mul);

        float desc_scale;
        memcpy(&desc_scale, desc + DESC_OFF_SCALE, sizeof desc_scale);
        float scale_f = desc_scale * PREPASS_PEOPLE_SCALE_MUL;

        float px, py, pz;
        memcpy(&px, rec + PEOPLE_OFF_POS_X, sizeof px);
        memcpy(&py, rec + PEOPLE_OFF_POS_Y, sizeof py);
        memcpy(&pz, rec + PEOPLE_OFF_POS_Z, sizeof pz);

        float world[16], scale[16];
        mat4_translation(world, px, py, pz);
        mat4_scaling(scale, scale_f, scale_f, scale_f);
        mat4_mul(world, scale, world);                       /* S × T          */
        mat4_mul(world, g_scene1_camera_orient, world);      /* base × (S×T)   */

        int32_t char_id;
        memcpy(&char_id, desc + DESC_OFF_CHAR_ID, sizeof char_id);
        if (last_tex_char != char_id) {
            last_tex_char = char_id;
            IDirect3DDevice8_SetTexture(
                dev, 0, (IDirect3DBaseTexture8 *)chr_prepass_sheet_texture(char_id));
        }

        uint32_t color = ((uint32_t)final_alpha << 24) | 0x7f7f7fu;
        scene1_chr_sprite_render(dev_in, (const int32_t *)rec, char_id,
                                 world, color, 0, 0);
    }
}

#endif /* _WIN32 */
