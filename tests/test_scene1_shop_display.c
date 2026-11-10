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
#include "scene1_display_menu.h"   /* the item window (the vender menu's list) */
#include "scene_adv_guild.h"       /* DAT_0438b684 */
#include "sim.h"                   /* g_sim_buttons */
#include "stage_load_pulse.h"      /* the item window's slide */
#include "tables_item.h"           /* g_item */
#include "title_save_dialog.h"     /* the hand cursor */

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

/* FUN_004864fd — the shop re-layout the guild's store Expansion runs right
 * after the tier bump (retail trace guild-sell-expansion f900: tier 0 → 1,
 * items 31 → 41, DAT_0438bfb4 3 → 4, table 0's origin x 3 → 4): every item on
 * display back into the inventory in grid order, the grid empty, the per-table
 * shelf lists returned + their flags cleared, the origins = the tier's
 * template (FUN_0048ffd9), the table count by tier. */
int test_shop_display_relayout_expansion(void)
{
    seed_house_furniture(1);                       /* the tier is already the new one */
    uint32_t *bank = save_work_dwords_at(0);
    bank[SAVE_BANK_FIELD_DECO_WALL] = bank[SAVE_BANK_FIELD_DECO_FLOOR] = 0;
    bank[SAVE_BANK_FIELD_DECO_CARPET] = bank[SAVE_BANK_FIELD_DECO_TABLE] = 0;

    /* 31 items in the bag (handles with bit 0x10: no encyclopedia entry to mark) */
    int32_t *items = (int32_t *)(bank + SAVE_BANK_ITEM_TABLE_DWORD);
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        items[i] = i < 31 ? ((100 + i) << 6 | 0x10) : -1;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 31;

    /* ten on display, scattered; the bag gets them row-major (row·20 + col) */
    int32_t *cells = display_cells();
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        cells[i] = -1;
    static const int at[10] = { 3 + 20 * 4, 1, 4, 20 * 2, 4 + 20 * 3, 2, 3 + 20 * 3, 20 * 3, 4 + 20 * 4, 3 };
    for (int k = 0; k < 10; k++)
        cells[at[k]] = (500 + k) << 6 | 0x10;
    static const int order[10] = { 1, 5, 9, 2, 3, 7, 6, 4, 0, 8 };   /* by cell index */

    /* table 1's shelf list holds one item; every table's flag dword is set */
    int32_t *shelf = (int32_t *)(bank + SHOP_DISPLAY_SHELF_ITEMS);
    for (int i = 0; i < 10 * SHOP_DISPLAY_SHELF_SLOTS; i++)
        shelf[i] = -1;
    shelf[1 * SHOP_DISPLAY_SHELF_SLOTS + 7] = 900 << 6 | 0x10;
    for (int i = 0; i < 10; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 1;

    shop_display_relayout(bank);

    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 4);            /* tier 1: 4 tables */
    T_ASSERT_EQ_I(g_mapcol_objs.n2, 4);
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        T_ASSERT_EQ_I(cells[i], -1);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 42);
    T_ASSERT_EQ_I(items[30], (130 << 6) | 0x10);               /* the bag as it was */
    for (int k = 0; k < 10; k++)
        T_ASSERT_EQ_I(items[31 + k], ((500 + order[k]) << 6) | 0x10);
    T_ASSERT_EQ_I(items[41], (900 << 6) | 0x10);               /* then the shelf list's */
    T_ASSERT_EQ_I(items[42], -1);
    T_ASSERT_EQ_I(shelf[1 * SHOP_DISPLAY_SHELF_SLOTS + 7], -1);
    for (int i = 0; i < 4; i++)
        T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i], 0);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 4], 1);   /* only the tier's tables */
    /* the tier-1 template: table 0 one cell right of tier 0's, the rest as before */
    static const int32_t tier1[10][2] = {
        {4, 3}, {1, 0}, {0, 1}, {9, 1}, {10, 3}, {11, 0}, {3, 6}, {6, 6}, {9, 6}, {12, 6},
    };
    for (int i = 0; i < 10; i++) {
        T_ASSERT_EQ_I(table_origin(i)[0], tier1[i][0]);
        T_ASSERT_EQ_I(table_origin(i)[1], tier1[i][1]);
    }

    /* the count by tier: 0 → 3, 2 → 6, 3 → 10 (the template's rows 2 / 3 move table 3) */
    bank[SHOP_DISPLAY_TIER_SELECTOR] = 0;
    shop_display_relayout(bank);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 3);
    T_ASSERT_EQ_I(table_origin(0)[0], 3);
    bank[SHOP_DISPLAY_TIER_SELECTOR] = 2;
    shop_display_relayout(bank);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 6);
    T_ASSERT_EQ_I(table_origin(3)[0], 14);
    bank[SHOP_DISPLAY_TIER_SELECTOR] = 3;
    shop_display_relayout(bank);
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 10);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 42);  /* nothing left to return */

    bank[SHOP_DISPLAY_TIER_SELECTOR] = 0;
    shop_display_relayout(bank);
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        items[i] = -1;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 0;
    for (int i = 0; i < 10; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 0;
    g_mapcol_objs.n2 = 0;
    return 0;
}

/* ── vending machines (FUN_0048670f: the cc04 1 confirm's 5201..5299 arm, the
 * cc04 2 menu).  Expected values = rhost retail, scenario vending-level16 (the
 * placement on table 0 at f5465, the stock of slot 1 at f5577) and the arm's
 * disasm 0x488dda..0x489153 for what that trace does not press. */

#define VEND0  (5201 << 6)            /* 0x51440, Old Vender */
#define IT(n)  (((n) << 6) | 0x10)    /* a handle the encyclopedia skips */

static void vend_press(unsigned pressed, unsigned held)
{
    g_sim_buttons[0].pressed = (uint16_t)pressed;
    g_sim_buttons[0].held    = (uint16_t)held;
}

static int vend_tick(unsigned pressed, unsigned held)
{
    vend_press(pressed, held);
    const int r = shop_vender_menu_tick((uint16_t)pressed, (uint16_t)held);
    vend_press(0, 0);
    return r;
}

/* tier-0 house, the player facing cell (4,3) of table 0 (2×2 at (3,3)); the bag
 * holds the vender, two of item 100 and one 101 */
static void seed_vender_shop(int level)
{
    seed_house_furniture(0);
    uint32_t *bank = save_work_dwords_at(0);
    ((uint8_t *)bank)[0x2bd11] = 1;                /* the encyclopedia's first-entry event: done */
    bank[SAVE_BANK_FIELD_MERCHANT_LEVEL] = (uint32_t)level;

    memset(&g_item, 0, sizeof g_item);
    static const int ids[3] = { 100, 101, 5201 };
    g_item.count = 3;
    for (int i = 0; i < 3; i++) {
        g_item.records[i].valid    = 1;
        g_item.records[i].item_id  = ids[i];
        g_item.records[i].category = ids[i] == 5201 ? 52 : 1;
        g_item.records[i].rank     = 1;
        g_item.records[i].price    = 100;
    }

    int32_t *items = (int32_t *)(bank + SAVE_BANK_ITEM_TABLE_DWORD);
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        items[i] = -1;
    items[0] = VEND0; items[1] = IT(100); items[2] = IT(100); items[3] = IT(101);
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 4;

    int32_t *cells = display_cells();
    for (int i = 0; i < SHOP_DISPLAY_GRID_CELLS; i++)
        cells[i] = -1;
    int32_t *shelf = (int32_t *)(bank + SHOP_DISPLAY_SHELF_ITEMS);
    for (int i = 0; i < 10 * SHOP_DISPLAY_SHELF_SLOTS; i++)
        shelf[i] = -1;
    for (int i = 0; i < 10; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 0;

    shop_display_grid_rebuild();
    shop_display_highlight_update(-1.0f, 1.0f, -K_PI);       /* 2 ahead along −z: cell (4,3) */
    display_menu_reset();
    title_save_dialog_reset();
    vend_press(0, 0);
}

int test_shop_vender_place_level_gates(void)
{
    seed_vender_shop(13);
    uint32_t *bank = save_work_dwords_at(0);
    T_ASSERT_EQ_I(shop_display_cbfc(), 4);
    T_ASSERT_EQ_I(shop_display_cc00(), 3);

    /* merchant level dword < 14: refused, nothing moves */
    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_LEVEL);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0], 0);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 4);

    /* 14..20 with three machines among the DAT_0438bfb4 tables: the limit */
    bank[SAVE_BANK_FIELD_MERCHANT_LEVEL] = 20;
    for (int i = 0; i < 3; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 1;
    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_LIMIT);
    T_ASSERT_EQ_I(display_cells()[4 + 20 * 3], -1);
    /* a machine on a table past the count is not counted */
    bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 2] = 0;
    bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 3] = 1;
    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_PLACED);

    /* from 21 there is no limit */
    seed_vender_shop(21);
    for (int i = 0; i < 3; i++)
        bank[SHOP_DISPLAY_SUPPRESS_FLAGS + i] = 1;
    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_PLACED);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0], VEND0);
    return 0;
}

int test_shop_vender_place_swaps_the_table(void)
{
    seed_vender_shop(15);
    uint32_t *bank = save_work_dwords_at(0);
    int32_t *items = (int32_t *)(bank + SAVE_BANK_ITEM_TABLE_DWORD);
    int32_t *cells = display_cells();
    cells[3 + 20 * 3] = IT(200); cells[4 + 20 * 3] = IT(201);
    cells[3 + 20 * 4] = IT(202); cells[4 + 20 * 4] = IT(203);
    cells[1] = IT(300);                                      /* table 1's: stays */

    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_PLACED);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0], VEND0);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 1], 0);
    T_ASSERT_EQ_I(shop_display_table_flag(0), VEND0);
    /* the faced cell holds the vender's handle, the rest of the table is empty */
    T_ASSERT_EQ_I(cells[4 + 20 * 3], VEND0);
    T_ASSERT_EQ_I(cells[3 + 20 * 3], -1);
    T_ASSERT_EQ_I(cells[3 + 20 * 4], -1);
    T_ASSERT_EQ_I(cells[4 + 20 * 4], -1);
    T_ASSERT_EQ_I(cells[1], IT(300));
    /* the four items came back row-major after the bag's, then the vender left
     * slot 0 (retail f5465: count 33 → 36, the tail shifted down) */
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 7);
    static const int want[8] = { IT(100), IT(100), IT(101), IT(200), IT(201), IT(202), IT(203), -1 };
    for (int i = 0; i < 8; i++)
        T_ASSERT_EQ_I(items[i], want[i]);
    return 0;
}

int test_shop_vender_menu_rows_and_paging(void)
{
    seed_vender_shop(15);
    uint32_t *bank = save_work_dwords_at(0);

    /* the last row by the machine's kind: 6 / 8 / 10 slots after the two entries */
    bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0] = 0x51480 | 3;
    shop_vender_menu_open(0, 1);
    T_ASSERT_EQ_I(shop_vender_menu_last(), 9);
    bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0] = 0x514c0;
    shop_vender_menu_open(0, 0);
    T_ASSERT_EQ_I(shop_vender_menu_last(), 11);
    bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0] = VEND0;
    shop_vender_menu_open(0, 0);
    T_ASSERT_EQ_I(shop_vender_menu_last(), 7);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 2);                /* the first slot */
    T_ASSERT_EQ_I(shop_vender_menu_top(), 0);
    T_ASSERT_EQ_I(shop_vender_menu_pick(), 0);
    T_ASSERT_EQ_I(shop_vender_menu_table(), 0);

    /* down: the window of six follows from row 6 */
    static const int down[6][2] = { {3, 0}, {4, 0}, {5, 0}, {6, 1}, {7, 2}, {7, 2} };
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(vend_tick(0, 0x8), 0);
        T_ASSERT_EQ_I(shop_vender_menu_row(), down[i][0]);
        T_ASSERT_EQ_I(shop_vender_menu_top(), down[i][1]);
    }
    /* left: six rows up, both clamped at 0 */
    vend_tick(0, 0x2);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 1);
    T_ASSERT_EQ_I(shop_vender_menu_top(), 0);
    vend_tick(0, 0x2);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 0);
    T_ASSERT_EQ_I(shop_vender_menu_top(), 0);
    vend_tick(0, 0x2);                                        /* at row 0: nothing */
    T_ASSERT_EQ_I(shop_vender_menu_row(), 0);
    /* right: six rows down, the row clamped at the last, the top at last − 5 */
    vend_tick(0, 0x1);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 6);
    T_ASSERT_EQ_I(shop_vender_menu_top(), 2);
    vend_tick(0, 0x1);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 7);
    T_ASSERT_EQ_I(shop_vender_menu_top(), 2);
    /* up keeps the window while the row is inside it */
    vend_tick(0, 0x4);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 6);
    T_ASSERT_EQ_I(shop_vender_menu_top(), 2);
    /* up wins over down, down over left / right (one else-if chain) */
    vend_tick(0, 0x4 | 0x8 | 0x1);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 5);

    /* B closes: the item window slides out */
    T_ASSERT_EQ_I(vend_tick(0x20, 0), 1);
    T_ASSERT_EQ_I(stage_load_pulse_get_active(), 0);
    return 0;
}

int test_shop_vender_menu_stock_detail_revert(void)
{
    seed_vender_shop(15);
    uint32_t *bank = save_work_dwords_at(0);
    int32_t *items = (int32_t *)(bank + SAVE_BANK_ITEM_TABLE_DWORD);
    int32_t *shelf = (int32_t *)(bank + SHOP_DISPLAY_SHELF_ITEMS);
    int32_t *cells = display_cells();
    int *brought = adv_guild_bag_brought();
    for (int k = 0; k < 4; k++)
        brought[k] = 7000 + k;

    T_ASSERT_EQ_I(shop_vender_place(bank, VEND0), SHOP_VENDER_PLACED);   /* bag: 100, 100, 101 */
    shop_vender_menu_open(0, 1);
    stage_load_pulse_reset_counter_to_5();

    /* Y on an empty slot does nothing; A on slot 2 (row 3) hands over to the list */
    vend_tick(0, 0x8);
    T_ASSERT_EQ_I(vend_tick(0x40, 0), 0);
    T_ASSERT_EQ_I(display_menu_detail(), 0);
    T_ASSERT_EQ_I(vend_tick(0x10, 0), 0);
    T_ASSERT_EQ_I(shop_vender_menu_pick(), 1);

    /* the list: A arms the 6-frame confirm, then the pick lands in the slot */
    T_ASSERT_EQ_I(display_menu_selected(), IT(100));
    vend_tick(0x10, 0);
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(shelf[1], -1);
        vend_tick(0, 0);
    }
    T_ASSERT_EQ_I(shelf[1], IT(100));
    T_ASSERT_EQ_I(shop_vender_menu_pick(), 0);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 2);
    T_ASSERT_EQ_I(items[0], IT(100));
    T_ASSERT_EQ_I(items[1], IT(101));
    T_ASSERT_EQ_I(stage_load_pulse_get_counter(), 5);        /* FUN_004682e3 */
    /* retail's stray write: DAT_0438b67c[row 3] = DAT_0438b684[1] */
    T_ASSERT_EQ_I(brought[0], 7000);
    T_ASSERT_EQ_I(brought[1], IT(100));
    T_ASSERT_EQ_I(brought[2], 7002);

    /* the rows again: the slot's item is highlighted; Y opens its detail, which
     * swallows everything until A / B / Y closes it */
    vend_tick(0, 0);
    T_ASSERT_EQ_I(display_menu_highlight_item(), IT(100));
    vend_tick(0x40, 0);
    T_ASSERT_EQ_I(display_menu_detail(), 1);
    vend_tick(0, 0x8);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 3);
    T_ASSERT_EQ_I(vend_tick(0x20, 0), 0);                    /* closes the detail, not the menu */
    T_ASSERT_EQ_I(display_menu_detail(), 0);

    /* B in the list goes back to the rows */
    vend_tick(0x10, 0);
    T_ASSERT_EQ_I(shop_vender_menu_pick(), 1);
    vend_tick(0x20, 0);
    T_ASSERT_EQ_I(shop_vender_menu_pick(), 0);
    T_ASSERT_EQ_I(shelf[1], IT(100));

    /* "Nothing" into the stocked slot: its item returns to the bag */
    vend_tick(0x10, 0);
    while (display_menu_selected() != -1)
        vend_tick(0, 0x4);
    vend_tick(0x10, 0);
    for (int i = 0; i < 6; i++)
        vend_tick(0, 0);
    T_ASSERT_EQ_I(shelf[1], -1);
    T_ASSERT_EQ_I(brought[1], -1);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 3);
    T_ASSERT_EQ_I(items[2], IT(100));

    /* row 1 "Done Stocking": closes, the machine stays */
    shelf[4] = IT(101);
    vend_tick(0, 0x4);
    vend_tick(0, 0x4);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 1);
    T_ASSERT_EQ_I(display_menu_highlight_item(), -1);
    T_ASSERT_EQ_I(vend_tick(0x10, 0), 1);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0], VEND0);
    T_ASSERT_EQ_I(shelf[4], IT(101));

    /* row 0 "Revert To Table": the vender (the handle in its display cell) and
     * the stock go back into the bag, the table is a table again */
    shop_vender_menu_open(0, 0);
    vend_tick(0, 0x2);
    T_ASSERT_EQ_I(shop_vender_menu_row(), 0);
    T_ASSERT_EQ_I(vend_tick(0x10, 0), 1);
    T_ASSERT_EQ_I((int)bank[SHOP_DISPLAY_SUPPRESS_FLAGS + 0], 0);
    T_ASSERT_EQ_I(cells[4 + 20 * 3], -1);
    T_ASSERT_EQ_I(shelf[4], -1);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 5);
    T_ASSERT_EQ_I(items[3], VEND0);
    T_ASSERT_EQ_I(items[4], IT(101));

    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        items[i] = -1;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 0;
    for (int k = 0; k < 4; k++)
        brought[k] = -1;
    display_menu_reset();
    title_save_dialog_reset();
    return 0;
}
