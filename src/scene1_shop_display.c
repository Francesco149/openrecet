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
#include "party.h"               /* the player DAT_056da1d8, facing DAT_056db05c */
#include "x87.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "save_work.h"               /* save_work_active_slot / save_work_dwords_at */
#include "save_bank.h"               /* SAVE_BANK_FIELD_DISPLAY_GRID */
#include "scene1_walker_pass_init.h" /* g_scene1_walker_phase2_* (the placed furniture) */
#include "map_collision.h"           /* g_mapcol_objs (DAT_0438bfb0.., the same memory in retail) */
#include "scene1_camera.h"           /* scene1_camera_view_mode (DAT_068dd3fc[stage]) */
#include "audio.h"                   /* audio_play_se_by_id / _file (FUN_00499519 / FUN_0049933c) */

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
static int s_cbfc;   /* BSS 0 until FUN_00436f97's house arm sets -1 */
static int s_cc00;
/* _DAT_0438cbf4 / _DAT_0438cbf8 — float render position of the highlight. */
static float s_render_x = 0.0f;
static float s_render_z = 0.0f;
/* DAT_0438bf68 — selected-cell occupancy/furniture indicator (furniture+1). */
static int s_bf68 = 0;
/* Move Tables (cc08 2): DAT_074b28d8 — the display-grid cells under the carried
 * table (the 4 dwords below the layout grid); DAT_0438bea4 — the carried table. */
static int32_t s_carry[4];
static int s_bea4;

void shop_display_reset(void)
{
    memset(s_grid, 0, sizeof s_grid);
    s_cbfc = -1;
    s_cc00 = -1;
    s_render_x = 0.0f;
    s_render_z = 0.0f;
    s_bf68 = 0;
    memset(s_carry, 0, sizeof s_carry);
    s_bea4 = 0;
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
    static const float k_pi = 3.1415927410125732f;   /* 0x51943c */
    static const float k10 = 10.0f, k8 = 8.0f, k0p5 = 0.5f, k2 = 2.0f, km2 = -2.0f;
    static const float k9 = 9.0f, k7 = 7.0f;          /* 0x5196b4, 0x5193f8 */
    /* the facing's tenth-of-π index: fld m32; fdiv m32 π; fmul m32 10; __ftol */
    const int32_t q = x87_ftol((long double)pang / k_pi * k10);
    /* 2 ahead: 0 → +z, −11..−9 → −z, 4 → +x, else −x */
    float rx = 0.0f, rz = 0.0f;
    if (q == 0) rz = k2;
    else if (q <= -9 && q >= -11) rz = km2;
    else rx = q == 4 ? k2 : km2;
    int col = x87_ftol(((long double)px + rx + k10) * k0p5);
    int row = x87_ftol(((long double)pz + rz + k8) * k0p5);
    if (col < 0) col = 0;
    if (row < 0) row = 0;

    s_bf68 = 0;
    /* retail reads DAT_074b28e8[row·20 + col] unbounded; out of the grid = 0 here */
    const int32_t cell = shop_display_grid_cell(col, row);
    if (cell < 2 || cell >= 9) {
        s_cbfc = -1;
        s_cc00 = -1;
        return;
    }
    /* FUN_004860c8: the furniture there; its work +0x2c750 flag → bf68 = index + 1 */
    const int fidx = shop_display_furniture_index(col, row);
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (fidx >= 0 && bank != NULL && bank[SHOP_DISPLAY_SUPPRESS_FLAGS + (uint32_t)fidx] != 0)
        s_bf68 = fidx + 1;
    s_cbfc = col;
    s_cc00 = row;
    const float fc = spill32((long double)col), fr = spill32((long double)row);
    s_render_x = spill32((long double)fc + fc - k9);   /* _DAT_0438cbf4 */
    s_render_z = spill32((long double)fr + fr - k7);   /* _DAT_0438cbf8 */
}

void shop_display_faced_cell(void)
{
    shop_display_highlight_update(g_party.pos[0][0], g_party.pos[0][2], PW(0x56db05cu).f);
}

void shop_display_faced_none(void)
{
    s_cbfc = -1;
}

/* ── Move Tables (cc08 2) ────────────────────────────────────────────────── */

int shop_display_move_table(void) { return s_bea4; }

const int32_t *shop_display_state_va(uint32_t va)
{
    if (va % 4)
        return NULL;
    if (va >= 0x74b28d8u && va < 0x74b28e8u)
        return &s_carry[(va - 0x74b28d8u) / 4];
    if (va >= 0x74b28e8u && va < 0x74b28e8u + 4u * SHOP_DISPLAY_GRID_CELLS)
        return &s_grid[(va - 0x74b28e8u) / 4];
    return NULL;
}

/* FUN_0048940e (0x48940e): cell = x + col + 20·(z + row); the buffer runs
 * row-major over the footprint (2×2: row·2 + col). */
void shop_display_table_carry(int table, int pickup)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL || table < 0 || table >= 10)
        return;
    const int type = g_scene1_walker_phase2_mesh_type[table];   /* DAT_0438bfcc */
    int rows, cols;
    if (type == 3)                                              { rows = 2; cols = 2; }
    else if (type == 4 && g_scene1_walker_phase2_rot_y[table] == 0.0f) { rows = 1; cols = 4; }
    else if (type == 4 || type == 6)                            { rows = 4; cols = 1; }
    else
        return;
    const int32_t *org = (const int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS) + 2 * table;
    int32_t *cells = (int32_t *)(bank + SAVE_BANK_FIELD_DISPLAY_GRID);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const int idx = org[0] + c + SHOP_DISPLAY_GRID_STRIDE * (org[1] + r);
            if (idx < 0 || idx >= SHOP_DISPLAY_GRID_CELLS)
                continue;   /* retail trusts the check (every put-down follows a 0) */
            int32_t *held = &s_carry[r * cols + c];
            if (pickup) {
                *held = cells[idx];
                cells[idx] = -1;
            } else {
                cells[idx] = *held;
            }
        }
    }
}

/* DAT_0438c0bc[i]: a table's lift — the walker's y and the collision object's */
static void shop_display_table_lift(int i, float y)
{
    g_scene1_walker_phase2_pos_y[i] = y;
    g_mapcol_objs.y[5 + i] = y;
}

/* DAT_005ce3e0: the cell a table slot's mesh sits at, [scene type][slot] (x, z).
 * The house's rooms are types 0..3; row 4 is what follows in .data (retail
 * indexes it unchecked).  FUN_00436f97 reads its own copy, DAT_005c5120. */
static const int32_t k_table_anchor[5][10][2] = {
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    {{4,3},{3,4},{5,2},{4,2},{4,3},{3,4},{4,3},{4,3},{4,3},{4,3}},
    {{5,5},{5,5},{0,5},{5,5},{5,0},{5,5},{5,25},{5,4},{5,8},{10,0}},
};

/* PORT-DEBT(simplified, FUN_0048670f): collision_house.c keeps its own copy of
 * the table origins from the house load (read only by the cc08 4 stool's
 * ground probe, collision_set_player_ground_y) — a moved table stays at its
 * old spot there until the next house entry. */
void shop_display_tables_place(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    const int st = scene1_camera_view_mode();
    if (bank == NULL || st < 0 || st > 4)
        return;
    const int32_t *org = (const int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS);
    for (int i = 0; i < 10; i++) {
        /* fild; fstp m32; fld m32; fadd st,st; fstp m32 */
        const float x = (float)(org[2 * i + 0] - k_table_anchor[st][i][0]);
        const float z = (float)(org[2 * i + 1] - k_table_anchor[st][i][1]);
        g_scene1_walker_phase2_pos_x[i] = g_mapcol_objs.x[5 + i] = x + x;
        g_scene1_walker_phase2_pos_z[i] = g_mapcol_objs.z[5 + i] = z + z;
    }
}

void shop_display_move_begin(void)
{
    s_bea4 = 0;
    shop_display_table_carry(0, 1);
}

int shop_display_move_tick(uint16_t pressed, uint16_t held)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int count = g_scene1_walker_phase2_count;                   /* DAT_0438bfb4 */
    if (count > 10)
        count = 10;
    for (int i = 0; i < count; i++)
        shop_display_table_lift(i, 0.0f);
    if (bank == NULL || count <= 0 || s_bea4 < 0 || s_bea4 >= count)
        return 0;

    int leave = 0;
    if (pressed & 0x20) {
        if (shop_display_grid_rebuild() == 0) {
            shop_display_table_carry(s_bea4, 0);
            audio_play_se_file("bin/se/00re/system/00re_sys07.bin");
            leave = 1;
        } else {
            audio_play_se_by_id(0x16a);
        }
    } else if (pressed & 0x10) {
        if (shop_display_grid_rebuild() == 0) {
            shop_display_table_carry(s_bea4, 0);
            if (++s_bea4 == count)
                s_bea4 = 0;
            shop_display_table_carry(s_bea4, 1);
            audio_play_se_by_id(0x143);
        } else {
            audio_play_se_by_id(0x16a);
        }
    } else {
        shop_display_table_lift(s_bea4, 0.5f);
        int32_t *org = (int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS) + 2 * s_bea4;
        const int32_t x0 = org[0], z0 = org[1];
        if (held & 0x2) org[0] = x0 - 1;
        if (held & 0x1) org[0]++;
        if (held & 0x4) org[1]--;
        if (held & 0x8) org[1]++;
        const uint32_t r = shop_display_grid_rebuild();
        const int refused = (r & 1u) != 0 || r == 4u;
        if (org[0] != x0 || org[1] != z0)
            audio_play_se_by_id(refused ? 0x16a : 0x146);
        if (refused) {
            org[0] = x0;
            org[1] = z0;
        }
    }
    shop_display_tables_place();
    return leave;
}

/* DIAGNOSTIC (rng-survey §21.4 ROOT 2): dump the furniture-layout grid
 * DAT_074b28e8 + its five inputs (tier / count / origins / mesh / rot) at the
 * f406 first-customer entry, so the port↔retail grid divergence that mis-counts
 * the cs-walker retarget rejection-sampling can be diffed.  Raw 300-int32 grid
 * → <dir>\grid_dump.bin; the small inputs → <dir>\grid_dump.json.  No-op if
 * dir==NULL. */
void shop_display_grid_dump(const char *dir, uint32_t frame)
{
    if (dir == NULL)
        return;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int tier = -1;
    if (bank != NULL)
        tier = (int)bank[SHOP_DISPLAY_TIER_SELECTOR];
    int count = g_scene1_walker_phase2_count;
    const int32_t *origins =
        (bank != NULL) ? (const int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS)
                       : NULL;

    char path[512];
    snprintf(path, sizeof path, "%s\\grid_dump.bin", dir);
    FILE *fb = fopen(path, "wb");
    if (fb != NULL) {
        fwrite(s_grid, sizeof(int32_t), SHOP_DISPLAY_GRID_CELLS, fb);
        fclose(fb);
    }

    snprintf(path, sizeof path, "%s\\grid_dump.json", dir);
    FILE *fj = fopen(path, "w");
    if (fj == NULL)
        return;
    fprintf(fj, "{\"frame\":%u,\"active_slot\":%d,\"tier\":%d,\"count\":%d,\n",
            (unsigned)frame, save_work_active_slot(), tier, count);
    fprintf(fj, " \"origins\":[");
    for (int i = 0; i < count && origins != NULL; i++)
        fprintf(fj, "%s[%d,%d]", i ? "," : "",
                (int)origins[i * 2 + 0], (int)origins[i * 2 + 1]);
    fprintf(fj, "],\n \"mesh\":[");
    for (int i = 0; i < count; i++)
        fprintf(fj, "%s%d", i ? "," : "",
                (int)g_scene1_walker_phase2_mesh_type[i]);
    fprintf(fj, "],\n \"rot\":[");
    for (int i = 0; i < count; i++)
        fprintf(fj, "%s%.6f", i ? "," : "",
                (double)g_scene1_walker_phase2_rot_y[i]);
    fprintf(fj, "],\n \"grid\":[");
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        fprintf(fj, "%s%d", i ? "," : "", (int)s_grid[i]);
    fprintf(fj, "]}\n");
    fclose(fj);
}
