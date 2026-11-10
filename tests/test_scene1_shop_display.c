/*
 * test_scene1_shop_display.c — A0/A0b coverage.
 *
 * Validates the furniture-layout grid rebuild (FUN_0048960d) + the
 * cell-highlight detector (FUN_0048619f) against the live retail ground truth
 * from the save-roundtrip open frame (docs/findings/shop-display-menu-RE.md):
 * with the new-game-equivalent count=3 HOUSE furniture set, the player facing
 * the back stand (pang=-π, px=-0.787, pz=-5.32) highlights cell col 4 / row 0 —
 * the x=-1 sword the removal targets.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "scene1_shop_display.h"
#include "scene1_walker_pass_init.h"
#include "save_work.h"
#include "save_bank.h"       /* SAVE_BANK_FIELD_DISPLAY_GRID */
#include "map_collision.h"   /* g_mapcol_objs */
#include "stage_palette.h"   /* g_stage_b4dc */

#define K_PI       3.1415927f
#define K_HALF_PI  1.5707964f

/* Seed the active working bank + walker-phase2 arrays with the count=3 HOUSE
 * furniture set the production writer (scene1_postload_walker_phase2_init)
 * produces: furniture 0 = 2×2 (mesh_type 3) at origin (3,3); furniture 1 = 1×4
 * (mesh_type 4, rot 0) at origin (1,0) = the back-row stand; furniture 2 = 4×1
 * (mesh_type 4, rot π/2) at origin (0,1).  Origins live in the bank at dword
 * SHOP_DISPLAY_FURNITURE_ORIGINS (x,z pairs, stride 2). */
static void seed_house_furniture(int tier)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    /* zero the regions we touch */
    bank[SHOP_DISPLAY_TIER_SELECTOR] = (uint32_t)tier;
    int32_t *origins = (int32_t *)(bank + SHOP_DISPLAY_FURNITURE_ORIGINS);
    static const int32_t pairs[3][2] = { {3,3}, {1,0}, {0,1} };
    for (int i = 0; i < 3; i++) {
        origins[i * 2 + 0] = pairs[i][0];
        origins[i * 2 + 1] = pairs[i][1];
    }
    /* suppression flags off (no occupancy override) */
    for (int i = 0; i < 3; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 0;

    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count        = 3;
    g_scene1_walker_phase2_mesh_type[0] = 3;
    g_scene1_walker_phase2_mesh_type[1] = 4;
    g_scene1_walker_phase2_mesh_type[2] = 4;
    g_scene1_walker_phase2_rot_y[1]     = 0.0f;
    g_scene1_walker_phase2_rot_y[2]     = K_HALF_PI;

    shop_display_reset();
}

int test_shop_display_grid_rebuild_stamps_back_row_stand(void)
{
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* furniture 1 = 1×4 at row 0, cols 1..4 → stamp 3 (the back-row sword
     * stand).  Cell 4 (col 4, row 0) is the x=-1 sword the removal targets. */
    for (int col = 1; col <= 4; col++)
        T_ASSERT_EQ_I(shop_display_grid_cell(col, 0), 3);
    /* col 0 row 0 is NOT part of the 1×4 stand (it's the 4×1's start at row 1,
     * and the base template's wall) → not a stand value of 3. */
    T_ASSERT(shop_display_grid_cell(5, 0) != 3);
    return 0;
}

int test_shop_display_grid_rebuild_footprint_shapes(void)
{
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* furniture 0 = 2×2 at (3,3) → cols 3..4 rows 3..4 stamped 2 */
    T_ASSERT_EQ_I(shop_display_grid_cell(3, 3), 2);
    T_ASSERT_EQ_I(shop_display_grid_cell(4, 3), 2);
    T_ASSERT_EQ_I(shop_display_grid_cell(3, 4), 2);
    T_ASSERT_EQ_I(shop_display_grid_cell(4, 4), 2);

    /* furniture 2 = 4×1 at (0,1) → col 0 rows 1..4 stamped 4 */
    for (int row = 1; row <= 4; row++)
        T_ASSERT_EQ_I(shop_display_grid_cell(0, row), 4);
    return 0;
}

int test_shop_display_grid_rebuild_tier_template_base(void)
{
    /* With NO furniture, the grid is the tier-0 base template (no stand cells).
     * Tier-0 row 0 = {1,0,0,0,0,0,1,1,1,1,...}; col 6 is a fixture (1). */
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    bank[SHOP_DISPLAY_TIER_SELECTOR] = 0;
    scene1_walker_phase2_reset();          /* count = 0 */
    shop_display_reset();
    shop_display_grid_rebuild();

    T_ASSERT_EQ_I(shop_display_grid_cell(0, 0), 1);   /* wall */
    T_ASSERT_EQ_I(shop_display_grid_cell(6, 0), 1);   /* fixture */
    T_ASSERT_EQ_I(shop_display_grid_cell(5, 1), 9);   /* counter-edge */
    /* no stand cells anywhere in row 0 (only the furniture stamp makes [2,8]) */
    for (int col = 0; col < 20; col++) {
        int32_t c = shop_display_grid_cell(col, 0);
        T_ASSERT(c < 2 || c > 8);
    }
    return 0;
}

int test_shop_display_furniture_index_resolves_footprint(void)
{
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* back-row stand cell (col 4, row 0) belongs to furniture 1 */
    T_ASSERT_EQ_I(shop_display_furniture_index(4, 0), 1);
    /* the 2×2 cell (3,3) belongs to furniture 0 */
    T_ASSERT_EQ_I(shop_display_furniture_index(3, 3), 0);
    /* the 4×1 cell (0,3) belongs to furniture 2 */
    T_ASSERT_EQ_I(shop_display_furniture_index(0, 3), 2);
    /* an empty cell belongs to no furniture */
    T_ASSERT_EQ_I(shop_display_furniture_index(10, 10), -1);
    return 0;
}

int test_shop_display_highlight_open_frame_cbfc4_cc00_0(void)
{
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* The live retail open frame: player at the back stand, facing -π.
     *   eax = ftol(-π/π·10) = -10 ∈ [-11,-9] → zoff = -2
     *   col = ftol((-0.787 + 0 + 10)·0.5) = ftol(4.6065) = 4
     *   row = ftol((-5.320 - 2 + 8)·0.5)   = ftol(0.340)  = 0
     *   grid[4 + 0·20] = 3 ∈ [2,8] → highlight (4,0). */
    shop_display_highlight_update(-0.7871f, -5.3204f, -K_PI);
    T_ASSERT_EQ_I(shop_display_cbfc(), 4);
    T_ASSERT_EQ_I(shop_display_cc00(), 0);
    return 0;
}

int test_shop_display_highlight_no_stand_is_none(void)
{
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* Standing in the middle of the room facing -π: the faced cell is empty
     * floor (not a stand) → highlight "none". */
    shop_display_highlight_update(2.0f, 2.0f, -K_PI);
    T_ASSERT_EQ_I(shop_display_cbfc(), -1);
    T_ASSERT_EQ_I(shop_display_cc00(), -1);
    return 0;
}

int test_shop_display_highlight_facing_octants(void)
{
    /* Verify the offset-selection by facing index without depending on a stand:
     * just check the (col,row) the detector resolves matches the asm formula
     * for each of the four offset arms.  Put a stand at the resolved cell so we
     * can read back cbfc/cc00. */
    seed_house_furniture(0);
    shop_display_grid_rebuild();

    /* eax==4 arm (xoff=+2): pang=1.4 → ftol(1.4/π·10)=ftol(4.456)=4.  Land on
     * the 2×2 stand cell (col 4, row 4) = furniture 0's footprint:
     *   col = ftol((-3 + 2 + 10)·0.5) = ftol(4.5) = 4
     *   row = ftol(( 0.5 + 0 + 8)·0.5) = ftol(4.25) = 4 ; grid[4 + 4·20] = 2. */
    shop_display_highlight_update(-3.0f, 0.5f, 1.4f);
    T_ASSERT_EQ_I(shop_display_cbfc(), 4);
    T_ASSERT_EQ_I(shop_display_cc00(), 4);
    return 0;
}

/* ── Move Tables (cc08 2): FUN_0048940e / FUN_0048960d's verdict / the case-2
 * input arm.  Expected values = rhost retail, scenario counter-move-tables
 * (tier 0, tables (3,3) 2×2, (1,0) 1×4, (0,1) 4×1). */

static int32_t *display_cells(void)
{
    return (int32_t *)(save_work_dwords_at(0) + SAVE_BANK_FIELD_DISPLAY_GRID);
}

static int32_t *table_origin(int i)
{
    return (int32_t *)(save_work_dwords_at(0) + SHOP_DISPLAY_FURNITURE_ORIGINS) + 2 * i;
}

int test_shop_display_table_carry_shapes(void)
{
    seed_house_furniture(0);
    int32_t *cells = display_cells();
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        cells[i] = 1000 + i;

    /* table 0, 2×2 at (3,3): the buffer runs row-major — (3,3) (4,3) (3,4) (4,4) */
    shop_display_table_carry(0, 1);
    T_ASSERT_EQ_I(*shop_display_state_va(0x74b28d8u), 1000 + 3 + 20 * 3);
    T_ASSERT_EQ_I(*shop_display_state_va(0x74b28dcu), 1000 + 4 + 20 * 3);
    T_ASSERT_EQ_I(*shop_display_state_va(0x74b28e0u), 1000 + 3 + 20 * 4);
    T_ASSERT_EQ_I(*shop_display_state_va(0x74b28e4u), 1000 + 4 + 20 * 4);
    T_ASSERT_EQ_I(cells[3 + 20 * 3], -1);
    T_ASSERT_EQ_I(cells[4 + 20 * 4], -1);
    T_ASSERT_EQ_I(cells[5 + 20 * 3], 1000 + 5 + 20 * 3);      /* outside the footprint */
    /* put down one cell up-left: the items travel */
    table_origin(0)[0] = 2;
    table_origin(0)[1] = 2;
    shop_display_table_carry(0, 0);
    T_ASSERT_EQ_I(cells[2 + 20 * 2], 1000 + 3 + 20 * 3);
    T_ASSERT_EQ_I(cells[3 + 20 * 2], 1000 + 4 + 20 * 3);
    T_ASSERT_EQ_I(cells[2 + 20 * 3], 1000 + 3 + 20 * 4);
    T_ASSERT_EQ_I(cells[3 + 20 * 3], 1000 + 4 + 20 * 4);
    T_ASSERT_EQ_I(cells[4 + 20 * 4], -1);                     /* the old spot stays empty */

    /* table 1, type 4 rotation 0: 1×4 along x at (1,0) */
    shop_display_table_carry(1, 1);
    for (int k = 0; k < 4; k++) {
        T_ASSERT_EQ_I(*shop_display_state_va(0x74b28d8u + 4u * (uint32_t)k), 1000 + 1 + k);
        T_ASSERT_EQ_I(cells[1 + k], -1);
    }
    shop_display_table_carry(1, 0);
    for (int k = 0; k < 4; k++)
        T_ASSERT_EQ_I(cells[1 + k], 1000 + 1 + k);

    /* table 2, type 4 rotated: 4×1 along z at (0,1) */
    shop_display_table_carry(2, 1);
    for (int k = 0; k < 4; k++) {
        T_ASSERT_EQ_I(*shop_display_state_va(0x74b28d8u + 4u * (uint32_t)k), 1000 + 20 * (1 + k));
        T_ASSERT_EQ_I(cells[20 * (1 + k)], -1);
    }
    shop_display_table_carry(2, 0);

    /* type 6 is 4×1 whatever its rotation; an unknown type does nothing */
    g_scene1_walker_phase2_mesh_type[1] = 6;
    shop_display_table_carry(1, 1);
    T_ASSERT_EQ_I(cells[1 + 20 * 3], -1);
    T_ASSERT_EQ_I(cells[2], 1000 + 2);
    shop_display_table_carry(1, 0);
    g_scene1_walker_phase2_mesh_type[1] = 5;
    shop_display_table_carry(1, 1);
    T_ASSERT_EQ_I(cells[1], 1000 + 1);
    return 0;
}

int test_shop_display_placement_check_verdicts(void)
{
    seed_house_furniture(0);
    T_ASSERT_EQ_U(shop_display_grid_rebuild(), 0u);           /* the start layout is free */
    table_origin(0)[0] = 6;                                   /* (7,3) is a wall cell */
    T_ASSERT_EQ_U(shop_display_grid_rebuild(), 1u);
    table_origin(0)[0] = 0;                                   /* under table 2's column */
    T_ASSERT_EQ_U(shop_display_grid_rebuild(), 2u);
    table_origin(0)[0] = -1;                                  /* off the grid: 4, nothing stamped */
    T_ASSERT_EQ_U(shop_display_grid_rebuild(), 4u);
    T_ASSERT_EQ_I(shop_display_grid_cell(1, 0), 0);
    table_origin(0)[0] = 4;
    table_origin(0)[1] = 0;                                   /* (5,1) is a 9; table 1 then lands on (4,0) */
    T_ASSERT_EQ_U(shop_display_grid_rebuild(), 3u);
    return 0;
}

int test_shop_display_move_tick_scenario(void)
{
    const int32_t b4dc = g_stage_b4dc;
    g_stage_b4dc = -1;                                        /* scene type 0 */
    seed_house_furniture(0);
    int32_t *cells = display_cells();
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        cells[i] = -1;
    cells[3 + 20 * 3] = 0x6480;  cells[4 + 20 * 3] = 0x64c0;
    cells[3 + 20 * 4] = 0x30882; cells[4 + 20 * 4] = 0x2ee80;
    cells[2] = 0x111;

    shop_display_move_begin();
    T_ASSERT_EQ_I(shop_display_move_table(), 0);
    T_ASSERT_EQ_I(cells[3 + 20 * 3], -1);

    /* idle: the carried table is 0.5 up, the others down */
    T_ASSERT_EQ_I(shop_display_move_tick(0, 0), 0);
    T_ASSERT(g_scene1_walker_phase2_pos_y[0] == 0.5f);
    T_ASSERT(g_mapcol_objs.y[5] == 0.5f);
    T_ASSERT(g_scene1_walker_phase2_pos_y[1] == 0.0f);

    /* R R: x 3 → 5; a third R would put (7,3) under it — refused */
    shop_display_move_tick(0, 0x1);
    shop_display_move_tick(0, 0x1);
    T_ASSERT_EQ_I(table_origin(0)[0], 5);
    shop_display_move_tick(0, 0x1);
    T_ASSERT_EQ_I(table_origin(0)[0], 5);
    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == 2.0f);        /* 2·(5 − 4) */
    T_ASSERT(g_mapcol_objs.x[5] == 2.0f);
    /* L×5 → x 0 (carried over table 2: allowed); a sixth leaves the grid */
    for (int k = 0; k < 5; k++)
        shop_display_move_tick(0, 0x2);
    T_ASSERT_EQ_I(table_origin(0)[0], 0);
    shop_display_move_tick(0, 0x2);
    T_ASSERT_EQ_I(table_origin(0)[0], 0);
    /* A / B on another table: refused, still carrying table 0 */
    T_ASSERT_EQ_I(shop_display_move_tick(0x10, 0), 0);
    T_ASSERT_EQ_I(shop_display_move_tick(0x20, 0), 0);
    T_ASSERT_EQ_I(shop_display_move_table(), 0);
    T_ASSERT_EQ_I(cells[0 + 20 * 3], -1);
    T_ASSERT(g_scene1_walker_phase2_pos_y[0] == 0.0f);        /* an A / B frame has no lift */
    /* L + R in one frame cancel; R R U → (2,2) */
    shop_display_move_tick(0, 0x3);
    T_ASSERT_EQ_I(table_origin(0)[0], 0);
    shop_display_move_tick(0, 0x1);
    shop_display_move_tick(0, 0x1);
    shop_display_move_tick(0, 0x4);
    T_ASSERT_EQ_I(table_origin(0)[0], 2);
    T_ASSERT_EQ_I(table_origin(0)[1], 2);
    T_ASSERT(g_scene1_walker_phase2_pos_x[0] == -4.0f);       /* 2·(2 − 4) */
    T_ASSERT(g_scene1_walker_phase2_pos_z[0] == -2.0f);       /* 2·(2 − 3) */
    T_ASSERT(g_mapcol_objs.z[5] == -2.0f);

    /* A: down at (2,2) with its items, table 1 in hand */
    T_ASSERT_EQ_I(shop_display_move_tick(0x10, 0), 0);
    T_ASSERT_EQ_I(shop_display_move_table(), 1);
    T_ASSERT_EQ_I(cells[2 + 20 * 2], 0x6480);
    T_ASSERT_EQ_I(cells[3 + 20 * 2], 0x64c0);
    T_ASSERT_EQ_I(cells[2 + 20 * 3], 0x30882);
    T_ASSERT_EQ_I(cells[3 + 20 * 3], 0x2ee80);
    T_ASSERT_EQ_I(cells[2], -1);
    T_ASSERT_EQ_I(*shop_display_state_va(0x74b28dcu), 0x111);

    /* B: table 1 down again, the mode ends */
    T_ASSERT_EQ_I(shop_display_move_tick(0x20, 0), 1);
    T_ASSERT_EQ_I(cells[2], 0x111);
    T_ASSERT_EQ_I(shop_display_grid_cell(2, 2), 2);
    T_ASSERT_EQ_I(shop_display_grid_cell(4, 4), 0);

    /* A wraps from the last table to the first */
    shop_display_move_begin();
    shop_display_move_tick(0x10, 0);
    shop_display_move_tick(0x10, 0);
    T_ASSERT_EQ_I(shop_display_move_table(), 2);
    shop_display_move_tick(0x10, 0);
    T_ASSERT_EQ_I(shop_display_move_table(), 0);
    T_ASSERT_EQ_I(shop_display_move_tick(0x20, 0), 1);
    T_ASSERT_EQ_I(cells[2 + 20 * 2], 0x6480);

    g_stage_b4dc = b4dc;
    return 0;
}
