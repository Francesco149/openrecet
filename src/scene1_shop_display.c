/*
 * scene1_shop_display.c — see scene1_shop_display.h.
 *
 * The in-house display-stand interaction prerequisites: the furniture-layout
 * grid (DAT_074b28e8) + the cell-highlight detector that the cc04==1 "remove
 * item" menu opens off of.  All three engine functions are decoded from
 * vendor/unpacked asm; constants verified against the live retail open frame
 * (pang=-π → faced cell col 4, row 0 = the back-row x=-1 sword).  Full RE:
 * docs/findings/shop-display-menu-RE.md.
 */

#include "scene1_shop_display.h"

#include <math.h>
#include <string.h>

#include "save_work.h"               /* save_work_active_slot / save_work_dwords_at */
#include "scene1_walker_pass_init.h" /* g_scene1_walker_phase2_* (the placed furniture) */

/* DAT_005cd104 base furniture-layout templates, 4 HOUSE shop tiers x 300
 * cells (15 rows x 20 cols, row-major: cell = col + row*20).  1=wall/fixture,
 * 9=counter-edge, 0=floor; STANDS are not here (stamped by the furniture loop).
 * Extracted from vendor/unpacked @ .data 0x5cd104 + tier*0x4b0. */
static const int32_t k_display_base_template[4][300] = {
    {
        1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,9,9,1,1,1,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,9,9,1,1,1,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
        1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    },
    {
        1,0,0,0,0,0,0,9,9,1,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,9,9,0,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    },
    {
        1,0,0,0,0,0,0,9,9,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,9,9,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,
        1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,
        1,1,1,1,1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    },
    {
        1,0,0,0,0,0,0,9,9,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,9,9,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,
        1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,
        1,0,0,0,0,1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    },
};

/* DAT_074b28e8 — the per-frame furniture-layout grid. */
static int32_t s_grid[SHOP_DISPLAY_GRID_CELLS];

/* DAT_0438cbfc / DAT_0438cc00 — the highlighted display cell col / row. */
static int s_cbfc = -1;
static int s_cc00 = -1;
/* _DAT_0438cbf4 / _DAT_0438cbf8 — float render position of the highlight. */
static float s_render_x = 0.0f;
static float s_render_z = 0.0f;
/* DAT_0438bf68 — selected-cell occupancy/furniture indicator (furniture+1). */
static int s_bf68 = 0;

void shop_display_reset(void)
{
    memset(s_grid, 0, sizeof s_grid);
    s_cbfc = -1;
    s_cc00 = -1;
    s_render_x = 0.0f;
    s_render_z = 0.0f;
    s_bf68 = 0;
}

int     shop_display_cbfc(void) { return s_cbfc; }
int     shop_display_cc00(void) { return s_cc00; }

float   shop_display_render_x(void) { return s_render_x; }
float   shop_display_render_z(void) { return s_render_z; }
int     shop_display_bf68(void)     { return s_bf68; }

int32_t shop_display_grid_cell(int col, int row)
{
    if (col < 0 || row < 0)
        return 0;
    int idx = col + row * SHOP_DISPLAY_GRID_STRIDE;
    if (idx < 0 || idx >= SHOP_DISPLAY_GRID_CELLS)
        return 0;
    return s_grid[idx];
}

void shop_display_highlight_clear(void)
{
    s_cbfc = -1;
    s_cc00 = -1;
}

/* Per-furniture footprint shape (engine FUN_0048960d / FUN_004860c8): the
 * mesh_type==3 piece is a 2×2 (stamp 2); a mesh_type!=3 piece is 1×4 (stamp 3)
 * when its rotation is 0, else 4×1 (stamp 4). */
static void shop_display_footprint(int mesh_type, float rot,
                                   int *rows, int *cols, int *stamp)
{
    if (mesh_type == 3)       { *rows = 2; *cols = 2; *stamp = 2; }
    else if (rot == 0.0f)     { *rows = 1; *cols = 4; *stamp = 3; }
    else                      { *rows = 4; *cols = 1; *stamp = 4; }
}

uint32_t shop_display_grid_rebuild(void)
{
    uint32_t overlap = 0;

    /* base template copy (FUN_0048960d L88317-88321): the per-record shop-tier
     * selector indexes the 4 HOUSE templates.  Clamp out-of-range to tier 0. */
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int tier = 0;
    if (bank != NULL)
        tier = (int)bank[SHOP_DISPLAY_TIER_SELECTOR];
    if (tier < 0 || tier > 3)
        tier = 0;
    memcpy(s_grid, k_display_base_template[tier], sizeof s_grid);

    /* furniture stamps (FUN_0048960d L88322-88410): one footprint per placed
     * furniture, origins from the bank, footprint class from the walker-phase2
     * arrays.  No furniture (count==0) or no bank → template only. */
    int count = g_scene1_walker_phase2_count;
    if (count <= 0 || bank == NULL)
        return overlap;
    if (count > SCENE1_WALKER_PHASE2_MAX)
        count = SCENE1_WALKER_PHASE2_MAX;

    const int32_t *origins =
        (const int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS);

    for (int fi = 0; fi < count; fi++) {
        int ox = origins[fi * 2 + 0];   /* DAT_045105a8 (x / col origin) */
        int oz = origins[fi * 2 + 1];   /* DAT_045105ac (z / row origin) */
        /* engine bounds guard (each branch: `if (origin < 0) return 4;`). */
        if (ox < 0 || oz < 0)
            return 4;

        int rows, cols, stamp;
        shop_display_footprint(g_scene1_walker_phase2_mesh_type[fi],
                               g_scene1_walker_phase2_rot_y[fi],
                               &rows, &cols, &stamp);

        for (int dr = 0; dr < rows; dr++) {
            for (int dc = 0; dc < cols; dc++) {
                int idx = ox + (oz + dr) * SHOP_DISPLAY_GRID_STRIDE + dc;
                if (idx < 0 || idx >= SHOP_DISPLAY_GRID_CELLS)
                    continue;   /* defensive: engine trusts the data */
                int32_t existing = s_grid[idx];
                if (existing == 1 || existing == 9)
                    overlap |= 1;
                else if (existing != 0)
                    overlap |= 2;
                s_grid[idx] = stamp;
            }
        }
    }
    return overlap;
}

int shop_display_furniture_index(int col, int row)
{
    int result = -1;

    int count = g_scene1_walker_phase2_count;
    if (count <= 0)
        return -1;
    if (count > SCENE1_WALKER_PHASE2_MAX)
        count = SCENE1_WALKER_PHASE2_MAX;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return -1;
    const int32_t *origins =
        (const int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS);

    for (int fi = 0; fi < count; fi++) {
        int ox = origins[fi * 2 + 0];
        int oz = origins[fi * 2 + 1];
        int rows, cols, stamp;
        shop_display_footprint(g_scene1_walker_phase2_mesh_type[fi],
                               g_scene1_walker_phase2_rot_y[fi],
                               &rows, &cols, &stamp);
        for (int dr = 0; dr < rows; dr++)
            for (int dc = 0; dc < cols; dc++)
                if (ox + dc == col && oz + dr == row)
                    result = fi;   /* last match wins (engine has no break) */
    }
    return result;
}

void shop_display_highlight_update(float px, float pz, float pang)
{
    /* facing-octant index (asm 0x4861a4-0x4861cb): ftol(pang / π * 10). */
    int eax = (int)(pang / 3.1415927f * 10.0f);

    /* facing-direction reach offset (asm 0x4861d0-0x48620e), constants
     * 0x519314=2.0 / 0x519908=-2.0: facing ±z (eax 0 / [-11,-9]) reaches in z;
     * facing ±x (eax 4 / else) reaches in x. */
    float xoff = 0.0f, zoff = 0.0f;
    if (eax == 0)                       zoff =  2.0f;
    else if (eax >= -11 && eax <= -9)   zoff = -2.0f;
    else if (eax == 4)                  xoff =  2.0f;
    else                                xoff = -2.0f;

    /* faced cell (asm 0x486211-0x48625e): col/row = ftol((pos + off + C) * 0.5),
     * clamped >= 0.  C: +10@0x5194f0 (col), +8@0x519378 (row). */
    int col = (int)((px + xoff + 10.0f) * 0.5f);
    int row = (int)((pz + zoff +  8.0f) * 0.5f);
    if (col < 0) col = 0;
    if (row < 0) row = 0;

    s_bf68 = 0;
    int32_t cell = shop_display_grid_cell(col, row);
    if (cell >= 2 && cell <= 8) {
        /* occupancy indicator (asm 0x48627f-0x486293): if the faced furniture's
         * suppression flag is set, store furniture+1 in bf68. */
        int fidx = shop_display_furniture_index(col, row);
        if (fidx >= 0) {
            uint32_t *bank = save_work_dwords_at(save_work_active_slot());
            if (bank != NULL && bank[SHOP_DISPLAY_SUPPRESS_FLAGS + (uint32_t)fidx] != 0)
                s_bf68 = fidx + 1;
        }
        s_cbfc = col;
        s_cc00 = row;
        s_render_x = (float)(col * 2) - 9.0f;   /* _DAT_0438cbf4 */
        s_render_z = (float)(row * 2) - 7.0f;   /* _DAT_0438cbf8 */
    } else {
        s_cbfc = -1;
        s_cc00 = -1;
    }
}
