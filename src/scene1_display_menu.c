/*
 * scene1_display_menu.c — see scene1_display_menu.h.
 *
 * The cc04==1 display-stand remove-item menu (A2).  The picker state lives
 * here; the open gate + the cc04 dispatch that acts on display_menu_update()'s
 * return value live in scene1_player_ctrl.c (the engine has both inline in
 * FUN_0048670f, but the dispatch needs the player/cbfc/cc00 state, so the glue
 * stays with the controller).
 *
 * Engine globals mirrored (all BSS-zero defaults):
 *   DAT_0731f598[i*2]  s_list[]        — list item ids (stride 2: id, count)
 *   DAT_0731f408[tab]  s_tab_base[]    — per-tab base index into the list
 *   DAT_07337850[tab]  s_tab_cursor[]  — per-tab cursor (selected index)
 *   DAT_073376c0[tab]  s_tab_scroll[]  — per-tab scroll offset (render window)
 *   DAT_07337210[tab]  s_tab_count[]   — per-tab entry count
 *   DAT_0734b968       s_cur_tab       — current tab
 *   DAT_0731f404       s_num_tabs      — tab count
 *   DAT_0734b9a8       s_mode          — menu mode (open param_1; 0 = display)
 *   DAT_0734b994       s_confirm_ctr   — 6-frame confirm countdown
 *   DAT_0734b998       s_highlight     — highlighted item id (render/recount)
 *   DAT_0734b990       s_window_flag   — window-type flag (render; default 0)
 * The slide ramp DAT_0734b98c/9a0 (FUN_004693e3) is owned by stage_load_pulse.
 */

#include "scene1_display_menu.h"

#include <string.h>

#include "sim.h"                 /* g_sim_buttons[0].pressed (edge mask = DAT_073dddd4) */
#include "stage_load_pulse.h"    /* the slide ramp (FUN_004693e3 = DAT_0734b98c/9a0) */
#include "save_bank.h"           /* SAVE_BANK_ITEM_TABLE_DWORD / _FIELD_ITEM_COUNT */

/* ── picker state (engine globals above) ──────────────────────────────── */

#define DISPLAY_MENU_MAX_TABS  100   /* DAT_07337210[100] etc. */
#define DISPLAY_MENU_MAX_LIST  200   /* DAT_0731f598 stride-2 entries (100 ids) */

static int s_list[DISPLAY_MENU_MAX_LIST];
static int s_tab_base[DISPLAY_MENU_MAX_TABS];
static int s_tab_cursor[DISPLAY_MENU_MAX_TABS];
static int s_tab_scroll[DISPLAY_MENU_MAX_TABS];
static int s_tab_count[DISPLAY_MENU_MAX_TABS];
static int s_cur_tab     = 0;
static int s_num_tabs    = 0;
static int s_mode        = 0;
static int s_confirm_ctr = 0;   /* DAT_0734b994 */
static int s_highlight   = -1;  /* DAT_0734b998 */
static int s_window_flag = 0;   /* DAT_0734b990 */

int display_menu_slide(void) { return stage_load_pulse_get_counter(); }

void display_menu_reset(void)
{
    memset(s_list,       0, sizeof s_list);
    memset(s_tab_base,   0, sizeof s_tab_base);
    memset(s_tab_cursor, 0, sizeof s_tab_cursor);
    memset(s_tab_scroll, 0, sizeof s_tab_scroll);
    memset(s_tab_count,  0, sizeof s_tab_count);
    s_cur_tab     = 0;
    s_num_tabs    = 0;
    s_mode        = 0;
    s_confirm_ctr = 0;
    s_highlight   = -1;
    s_window_flag = 0;
}

/* ── FUN_00468338 — OPEN / list init (A2: minimal) ────────────────────── */
void display_menu_open(int mode, int first_open)
{
    /* per-open reset (all.c:64184-64193): mode, countdown, slide active. */
    s_mode        = mode;          /* DAT_0734b9a8 = param_1 */
    s_confirm_ctr = 0;             /* DAT_0734b994 */
    s_highlight   = -1;            /* DAT_0734b998 = 0xffffffff */
    s_window_flag = 0;             /* DAT_0734b990 (FUN_004681ec default) */

    /* the slide (DAT_0734b9a0=1 active, DAT_0734b98c=0): FUN_004693e3 ramps it
     * 0 → 5 (slide in).  Owned by stage_load_pulse (already ticked each frame
     * in sim.c). */
    stage_load_pulse_reset();          /* DAT_0734b98c = 0, DAT_0734b9a0 = 0 */
    stage_load_pulse_set_active(1);    /* DAT_0734b9a0 = 1 */

    /* PORT-DEBT(A3, FUN_00468338-population): the full inventory list build (the
     * param_1=0 scan of DAT_044e37b0 + the item-DB category filter + the
     * FUN_0045526a sort) is deferred to the render chip.  The removal drive only
     * ever has the cursor on the index-0 "select none" entry, so A2 inits just
     * that single-tab, single-entry list — enough for FUN_00469a9f()==-1 and the
     * `tab_count >= 1` confirm gate. */
    s_list[0]   = -1;              /* DAT_0731f598[0] = none (all.c:64396) */
    s_num_tabs  = 1;               /* DAT_0731f404 */
    s_cur_tab   = 0;               /* DAT_0734b968 (clamped to 0) */
    s_tab_base[0]  = 0;            /* DAT_0731f408[0] */
    s_tab_count[0] = 1;            /* DAT_07337210[0] = the none entry (uVar4=1) */
    if (first_open) {              /* param_2 != 0 → zero the per-tab cursor/scroll */
        s_tab_cursor[0] = 0;       /* DAT_07337850[0] */
        s_tab_scroll[0] = 0;       /* DAT_073376c0[0] */
    }

    /* NOTE: the engine's FUN_00468338 tail snaps the SHARED menu cursor
     * (FUN_00435693 → DAT_0438b150 = 1).  The port must NOT drive that here: its
     * save-dialog window render is (incompletely) gated on DAT_0438b150 alone,
     * so raising it spuriously paints the save/load window panel over HOUSE (the
     * "broken white HUD").  The display menu has its OWN cursor + panel
     * (FUN_0046b00a), ported with the menu render below — the shared-cursor snap
     * lands there, after the save-dialog gate is scene-qualified. */
}

/* ── FUN_00469a9f — selected list item ────────────────────────────────── */
int display_menu_selected(void)
{
    int idx = (s_tab_cursor[s_cur_tab] + s_tab_base[s_cur_tab]) * 2;
    if (idx < 0 || idx >= DISPLAY_MENU_MAX_LIST)
        return -1;
    return s_list[idx];
}

/* ── FUN_00469414 — per-frame update ──────────────────────────────────── */
int display_menu_update(int param)
{
    /* slide gate (all.c:65237): no input is accepted until the window has fully
     * slid in (DAT_0734b98c == 5). */
    if (stage_load_pulse_get_counter() < 5)
        return 0;

    /* confirm countdown (all.c:65240-65247): once armed by a Z-press, count 6
     * frames then fire CONFIRM (return 1).  The brief delay is the pick-up
     * animation window. */
    if (s_confirm_ctr != 0) {
        s_confirm_ctr++;
        if (s_confirm_ctr > 6) {
            s_confirm_ctr = 0;
            return 1;               /* CONFIRM */
        }
        return 0;
    }

    /* PORT-DEBT(A3): the message-hold countdowns DAT_0734b97c / b988 / b96c
     * (all.c:65248-65263) are only armed by the place-an-item "full inventory"
     * path, never by the removal — left BSS-zero, so they fall through. */

    uint16_t pressed = g_sim_buttons[0].pressed;   /* DAT_073dddd4 edge mask */

    if (pressed & 0x10u) {          /* Z / confirm (all.c:65264) */
        /* mode-6 auto-sort path (DAT_0734b9a8==6) is the counter menu, never the
         * display stand (mode 0) — skip to the else-branch (all.c:65348). */
        if (s_tab_count[s_cur_tab] < 1)
            return 0;               /* empty tab: no confirm */
        if (param != 0)
            s_confirm_ctr = 1;      /* LAB_004696bf: arm the countdown */
        return 3;                   /* pick-up arm (iVar14 = 3) */
    }

    if (pressed & 0x20u)            /* cancel (all.c:65355) */
        return 2;                   /* CANCEL */

    /* PORT-DEBT(A3): the in-list navigation (cursor up/down, quantity left/right
     * via the DAT_073dddd6 auto-repeat mask) + the FUN_00468246 highlight
     * recount (DAT_005c6ee4) below LAB_0046999a are render-only and unreachable
     * without nav input during the removal drive — deferred to the render chip
     * with the full list population. */
    s_highlight = display_menu_selected();   /* DAT_0734b998 = list[cursor] */
    return 0;
}

/* ── FUN_00468d22 — return an item to inventory ───────────────────────── */
void display_menu_inventory_return(uint32_t *bank, int item)
{
    if (bank == NULL || item == -1)
        return;

    /* PORT-DEBT(FUN_0049126b): the item-category-"seen" marker the engine calls
     * first (DAT_0450b170 records + the FUN_0049f012 unlock gate) is idempotent
     * for an item that was already on display (its category was marked seen when
     * first stocked), so it does not change the save bytes for the removal. */

    uint32_t *items = bank + SAVE_BANK_ITEM_TABLE_DWORD;   /* DAT_044e37b0 */
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        if (items[i] == 0xFFFFFFFFu) {
            items[i] = (uint32_t)item;
            bank[SAVE_BANK_FIELD_ITEM_COUNT]++;   /* DAT_0450f2b0 */
            return;
        }
    }
}

/* ── FUN_00469241 — remove an item from inventory ─────────────────────── */
int display_menu_inventory_remove(uint32_t *bank, int item)
{
    if (bank == NULL || item == -1)
        return 0;                   /* no-op on -1 (the removal case) */

    uint32_t *items = bank + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        if ((int)items[i] == item) {
            /* shift the tail down over the found slot, blank the last slot. */
            for (int j = i; j < SAVE_BANK_ITEM_TABLE_COUNT - 1; j++)
                items[j] = items[j + 1];
            items[SAVE_BANK_ITEM_TABLE_COUNT - 1] = 0xFFFFFFFFu;
            bank[SAVE_BANK_FIELD_ITEM_COUNT]--;   /* DAT_0450f2b0 */
            return 1;
        }
    }
    return 0;
}

