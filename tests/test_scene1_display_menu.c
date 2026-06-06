/*
 * test_scene1_display_menu.c — A2 coverage.
 *
 * The cc04==1 display-stand remove-item menu's per-frame picker logic
 * (scene1_display_menu.c): the OPEN list-init (FUN_00468338), the UPDATE state
 * machine (FUN_00469414 — slide gate, the 6-frame confirm countdown, Z-confirm,
 * cancel), the selected-item query (FUN_00469a9f), and the inventory
 * return/remove helpers (FUN_00468d22 / FUN_00469241).  These are the
 * save-relevant + deterministic pieces validated frame-by-frame against retail
 * on the house-display-remove drive (db054 freeze at 157, grid cell 64→-1).
 */

#include "t.h"

#include <string.h>

#include "scene1_display_menu.h"
#include "sim.h"                /* g_sim_buttons[0].pressed/held */
#include "stage_load_pulse.h"   /* the slide ramp gate */
#include "save_bank.h"
#include "save_work.h"
#include "tables_item.h"        /* synthetic g_item for the list-init path */

/* Drive one input frame: set the player-1 edge mask, return its effect via the
 * caller. */
static void set_pressed(unsigned mask)
{
    g_sim_buttons[0].pressed = (uint16_t)mask;
}

/* Bring the menu to the fully-slid-in state (counter == 5) so the update accepts
 * input, with a clean BSS-zero picker + zero input. */
static void open_settled(void)
{
    display_menu_reset();
    set_pressed(0);
    display_menu_open(0, /*first_open=*/1);
    stage_load_pulse_reset_counter_to_5();   /* skip the 5-frame slide-in */
}

/* Build a one-sword inventory in slot 0 with a matching one-record item DB so
 * the open's category grouping forms a single tab: [-1 "Nothing", sword]. */
static void setup_one_sword(int item_value)
{
    memset(&g_item, 0, sizeof g_item);
    g_item.count                  = 1;
    g_item.records[0].valid       = 1;
    g_item.records[0].item_id     = item_value >> 6;   /* lookup key = value>>6 */
    g_item.records[0].category    = 0;
    g_item.records[0].subindex    = 0;
    g_item.records[0].rank        = 1;
    g_item.records[0].price       = 300;

    save_work_set_active_slot(0);
    uint32_t *bank  = save_work_dwords_at(0);
    uint32_t *items = bank + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++)
        items[i] = 0xFFFFFFFFu;
    items[0] = (uint32_t)item_value;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 1;
}

/* FUN_00468338 tail (all.c:64637): when tab 0 leads with the -1 "Nothing"
 * entry, the open cursor starts on the FIRST REAL item — retail highlights the
 * displayed item (and its description), not "Nothing".  A cursor-UP then lands
 * on the -1 entry (the removal selection). */
int test_display_menu_open_inits_none_entry(void)
{
    setup_one_sword(64);                 /* worn sword (id 1) */
    display_menu_reset();
    display_menu_open(0, 1);

    /* cursor on the first real item, not "Nothing" (the corrected behavior). */
    T_ASSERT_EQ_I(display_menu_selected(), 64);

    /* number-possessed recount picked up the one held sword. */
    T_ASSERT_EQ_I(display_menu_possessed(), 1);

    /* cursor-UP (held bit 4) walks onto the -1 "Nothing" removal entry. */
    stage_load_pulse_reset_counter_to_5();
    set_pressed(0);
    g_sim_buttons[0].held = 0x4u;
    display_menu_update(1);
    g_sim_buttons[0].held = 0;
    T_ASSERT_EQ_I(display_menu_selected(), -1);
    return 0;
}

int test_display_menu_update_returns_0_while_sliding(void)
{
    display_menu_reset();
    set_pressed(0x10u);                 /* even with Z held… */
    display_menu_open(0, 1);            /* …open leaves the slide at 0 */
    /* counter < 5 → no input accepted */
    T_ASSERT_EQ_I(display_menu_update(1), 0);
    return 0;
}

int test_display_menu_confirm_countdown_fires_after_6(void)
{
    open_settled();

    /* Z edge frame: arms the countdown, returns the pick-up code 3. */
    set_pressed(0x10u);
    T_ASSERT_EQ_I(display_menu_update(1), 3);

    /* the next 5 frames (no input) tick the countdown, return 0… */
    set_pressed(0);
    for (int i = 0; i < 5; i++)
        T_ASSERT_EQ_I(display_menu_update(1), 0);
    /* …and the 6th tick fires CONFIRM (return 1). */
    T_ASSERT_EQ_I(display_menu_update(1), 1);
    return 0;
}

int test_display_menu_cancel_returns_2(void)
{
    open_settled();
    set_pressed(0x20u);                 /* cancel button */
    T_ASSERT_EQ_I(display_menu_update(1), 2);
    return 0;
}

int test_display_menu_idle_returns_0(void)
{
    open_settled();
    set_pressed(0);                     /* no input, settled */
    T_ASSERT_EQ_I(display_menu_update(1), 0);
    return 0;
}

int test_display_menu_inventory_return_appends_and_counts(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    uint32_t *items = bank + SAVE_BANK_ITEM_TABLE_DWORD;

    /* two items then empty (-1) slots */
    items[0] = 10;
    items[1] = 20;
    items[2] = 0xFFFFFFFFu;
    items[3] = 0xFFFFFFFFu;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 2;

    display_menu_inventory_return(bank, 64);   /* return a removed sword */

    T_ASSERT_EQ_I((int)items[2], 64);          /* appended at the first empty */
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 3);

    /* -1 is a no-op (the "select none" removal returns nothing to the bag). */
    display_menu_inventory_return(bank, -1);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 3);
    return 0;
}

int test_display_menu_inventory_remove_shifts_and_counts(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    uint32_t *items = bank + SAVE_BANK_ITEM_TABLE_DWORD;

    items[0] = 10;
    items[1] = 20;
    items[2] = 30;
    items[3] = 0xFFFFFFFFu;
    bank[SAVE_BANK_FIELD_ITEM_COUNT] = 3;

    T_ASSERT_EQ_I(display_menu_inventory_remove(bank, 20), 1);
    T_ASSERT_EQ_I((int)items[0], 10);          /* 30 shifted down over 20 */
    T_ASSERT_EQ_I((int)items[1], 30);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 2);

    /* -1 + absent item are both no-ops returning 0 (the removal case). */
    T_ASSERT_EQ_I(display_menu_inventory_remove(bank, -1), 0);
    T_ASSERT_EQ_I(display_menu_inventory_remove(bank, 999), 0);
    T_ASSERT_EQ_I((int)bank[SAVE_BANK_FIELD_ITEM_COUNT], 2);
    return 0;
}
