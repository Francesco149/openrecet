/*
 * scene1_chr_sprite.c — Cchr.2b: HOUSE character sprite leaf renderer.
 * See scene1_chr_sprite.h for the chip writeup.  Engine FUN_0045a56f.
 */

#include "scene1_chr_sprite.h"

#include <math.h>

#include "chr_sprite_meta.h"

/* ── engine constants (objdump @ 0x45a56f, decoded 2026-05-29) ──────────
 *   0x519364 = 1.0   0x519368 = 100.0   0x51935c = 0.5 (half-texel)
 *   0x519474 = 32.0  0x5198d8 = 0.2     0x519434 = π/2  0x519520 = 20.0
 */
#define CHR_CELL_PX        32.0f
#define CHR_HALF_TEXEL     0.5f
#define CHR_SHIMMER_AMP    0.2f
#define CHR_SHIMMER_HALFPI 1.5707963705062866f   /* 0x3fc90fdb */
#define CHR_SHIMMER_FRAMES 20.0f

/* The 8-direction facing tables the leaf indexes by param_1[6]
 * (DAT_005c5a54 / DAT_005c5a74, dumped 2026-05-29).  Eight facings fold
 * onto five sprite banks (bank = within-frame dword offset, 0..4) plus a
 * horizontal mirror flag — the classic 8-dir-from-5-sheets scheme. */
#define CHR_NUM_FACINGS 8
static const int chr_facing_bank[CHR_NUM_FACINGS] = { 0, 2, 4, 3, 1, 3, 4, 2 };
static const int chr_facing_flip[CHR_NUM_FACINGS] = { 1, 1, 1, 1, 1, 0, 0, 0 };

/* formdata.bin stores its tables big-endian (engine CONCAT byte assembly). */
static uint32_t chr_be_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static int chr_be_u16(const uint8_t *p)
{
    return ((int)p[0] << 8) | (int)p[1];
}

int chr_sprite_build_quads(chr_sprite_vertex *out, int out_max,
                           const int32_t *actor, int char_id,
                           uint32_t color,
                           const uint8_t *formdata, size_t formdata_sz,
                           int tex_w, int tex_h)
{
    if (out == NULL || actor == NULL || formdata == NULL)
        return 0;
    if (tex_w <= 0 || tex_h <= 0)
        return 0;

    /* facing → bank (LUT field) + horizontal-mirror flag */
    int facing = actor[CHR_ACTOR_FACING];
    int bank = 0, flip = 0;
    if (facing >= 0 && facing < CHR_NUM_FACINGS) {
        bank = chr_facing_bank[facing];
        flip = chr_facing_flip[facing];
    }

    /* LUT cell for (anim, frame, facing-bank) → formdata frame entry. */
    int32_t cell = chr_meta_lut(char_id, actor[CHR_ACTOR_ANIM],
                                actor[CHR_ACTOR_FRAME], bank);
    if (cell < 0)
        return 0;

    /* base = big-endian u32 at formdata[char_id*4]; the +0x400/+0x600/
     * +0x800 sub-tables are all relative to it. */
    if ((size_t)char_id * 4 + 4 > formdata_sz)
        return 0;
    uint32_t base = chr_be_u32(formdata + (size_t)char_id * 4);

    size_t cell_off = base + (size_t)cell * 2;
    if (cell_off + 0x602 > formdata_sz)
        return 0;
    int ncells = chr_be_u16(formdata + cell_off + 0x400);  /* local_18 */
    int start  = chr_be_u16(formdata + cell_off + 0x600);  /* local_1c */
    if (ncells <= 0)
        return 0;

    int   sheet_w  = chr_meta_sheet_w(char_id);            /* iVar3 */
    int   y_origin = chr_meta_y_origin(char_id);
    float scale    = (float)chr_meta_scale_x100(char_id) / 100.0f;  /* fVar5 */
    int   cells_per_row = (sheet_w >= 32) ? sheet_w / 32 : 1;        /* iVar11 */
    int   atlas_cols = (int)((float)tex_w / CHR_CELL_PX);            /* iVar12 = ftol */
    if (atlas_cols <= 0)
        atlas_cols = 1;

    /* spawn-age shimmer (dormant for a standing actor, age == 0). */
    int   age = actor[CHR_ACTOR_AGE];
    float shimmer = 0.0f;
    if (age > 0) {
        shimmer = (float)sheet_w * CHR_SHIMMER_AMP;
        if (age < 0x14)
            shimmer = sinf((float)age * CHR_SHIMMER_HALFPI / CHR_SHIMMER_FRAMES)
                      * (float)sheet_w * CHR_SHIMMER_AMP;
    }

    /* color/alpha gate on param_1[7..9] (engine, just before the verts). */
    if (actor[CHR_ACTOR_FLAG7] >= 1) {
        color |= 0xffffffu;
    } else if (actor[CHR_ACTOR_FLAG8] > 0 && actor[CHR_ACTOR_FLAG9] == 0) {
        color = (color & 0xff9f209fu) | 0x9f209fu;
    }

    int emitted = 0;
    for (int i = 0; i < ncells; i++) {
        int atlas_idx = start + i;                          /* local_1c, running */

        size_t pos_off = base + (size_t)atlas_idx * 2 + 0x800;
        if (pos_off + 2 > formdata_sz)
            break;
        int sheet_pos = chr_be_u16(formdata + pos_off);     /* uVar16 */

        int col = sheet_pos % cells_per_row;
        int row = sheet_pos / cells_per_row;                /* local_28 */
        float px = (float)col * CHR_CELL_PX;
        float x0 = (float)(sheet_w / -2) + px;              /* fVar6 */
        float py = (float)y_origin - (float)row * CHR_CELL_PX;  /* fVar7 */

        /* atlas UVs walk the texture linearly from `start`. */
        int   arow = (atlas_idx / atlas_cols) << 5;
        int   acol = atlas_idx % atlas_cols;
        float v_top = ((float)arow + CHR_HALF_TEXEL) / (float)tex_h;
        float v_bot = ((float)arow + CHR_CELL_PX - CHR_HALF_TEXEL) / (float)tex_h;
        float u_left  = ((float)(acol * 32) + CHR_HALF_TEXEL) / (float)tex_w;
        float u_right = (((float)acol + 1.0f) * CHR_CELL_PX - CHR_HALF_TEXEL)
                        / (float)tex_w;

        float topY = py * scale;
        float botY = (py - CHR_CELL_PX) * scale;

        /* X edges, with the facing flip + shimmer.  edge_a pairs with the
         * u_right texels, edge_b with u_left (so a flipped sprite mirrors
         * by swapping which world X the right texels land on). */
        float edge_a, edge_b;
        if (flip == 0) {
            float p = (cells_per_row / 2 < row) ? (x0 + shimmer) : (x0 - shimmer);
            edge_a = (p + CHR_CELL_PX) * scale;
            edge_b = p * scale;
        } else {
            float p = (float)(sheet_w / 2) - px;
            p = (cells_per_row / 2 < row) ? (p + shimmer) : (p - shimmer);
            edge_a = (p - CHR_CELL_PX) * scale;
            edge_b = p * scale;
        }

        if (emitted + 6 <= out_max) {
            chr_sprite_vertex v0 = { edge_a, topY, 0.0f, color, u_right, v_top };
            chr_sprite_vertex v1 = { edge_a, botY, 0.0f, color, u_right, v_bot };
            chr_sprite_vertex v2 = { edge_b, botY, 0.0f, color, u_left,  v_bot };
            chr_sprite_vertex v3 = { edge_b, topY, 0.0f, color, u_left,  v_top };
            out[emitted + 0] = v0;
            out[emitted + 1] = v1;
            out[emitted + 2] = v2;
            out[emitted + 3] = v3;
            out[emitted + 4] = v0;
            out[emitted + 5] = v2;
        }
        emitted += 6;
    }

    return ncells;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

/* g_chr_formdata / g_chr_formdata_size come from chr_sprite_meta.h above. */

#define CHR_SPRITE_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)  /* 0x142 */

void scene1_chr_sprite_render(struct IDirect3DDevice8 *dev_in,
                              const int32_t *actor, int char_id,
                              const float world[16], uint32_t color,
                              int tex_w, int tex_h)
{
    if (dev_in == NULL || actor == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* SetTransform(D3DTS_WORLD, world) — engine vtable [+0x94]. */
    if (world != NULL)
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);

    static chr_sprite_vertex buf[CHR_SPRITE_MAX_VERTS];
    int ncells = chr_sprite_build_quads(buf, CHR_SPRITE_MAX_VERTS, actor,
                                        char_id, color,
                                        g_chr_formdata, g_chr_formdata_size,
                                        tex_w, tex_h);
    if (ncells <= 0)
        return;

    /* The engine leaf inherits FVF + the bound sheet texture from the
     * walker; set the FVF here for the standalone MVP path. */
    IDirect3DDevice8_SetVertexShader(dev, CHR_SPRITE_FVF);

    /* Draw-tail branch (engine L176-L179 / objdump @ 0x45a9bb):
     *   (actor[8] < 1 || actor[9] != 0) && actor[7] > 0
     *     → COLOROP=7 ; DrawPrimitiveUP ; COLOROP=8
     *   else
     *     → single DrawPrimitiveUP
     * The COLOROP values (7/8) are verbatim from objdump; pending Frida
     * A/B confirmation of their visual intent. */
    if (((actor[CHR_ACTOR_FLAG8] < 1) || (actor[CHR_ACTOR_FLAG9] != 0)) &&
        (actor[CHR_ACTOR_FLAG7] > 0)) {
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, 7);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST,
                                         (UINT)(ncells * 2), buf,
                                         sizeof(chr_sprite_vertex));
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, 8);
    } else {
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST,
                                         (UINT)(ncells * 2), buf,
                                         sizeof(chr_sprite_vertex));
    }
}

#endif /* _WIN32 */
