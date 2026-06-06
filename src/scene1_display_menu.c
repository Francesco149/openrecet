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

#include <stdio.h>
#include <string.h>

#include "sim.h"                 /* g_sim_buttons[0].pressed/held (DAT_073dddd4/d6) */
#include "stage_load_pulse.h"    /* the slide ramp (FUN_004693e3 = DAT_0734b98c/9a0) */
#include "save_bank.h"           /* SAVE_BANK_ITEM_TABLE_DWORD / _FIELD_ITEM_COUNT */
#include "save_work.h"           /* save_work_dwords_at / save_work_active_slot */
#include "tables_item.h"         /* g_item DB (FUN_004681f6 record lookup) */
#include "scene1_chr_prepass.h"  /* chr_prepass_sort (FUN_0045526a co-sort) */
#include "title_save_dialog.h"   /* the SHARED hand cursor (FUN_00435693/710/747) */
#include "audio.h"               /* audio_play_se_by_id (nav SE, no RNG) */

/* ── picker state (engine globals above) ──────────────────────────────── */

#define DISPLAY_MENU_MAX_TABS  100    /* DAT_07337210[100] etc. */
/* DAT_0731f598..DAT_0732341c = (0x3e84/8) ≈ 2000 stride-2 (id,count) entries. */
#define DISPLAY_MENU_MAX_LIST  4000   /* 2000 (id,count) pairs */

static int s_list[DISPLAY_MENU_MAX_LIST];               /* DAT_0731f598 (id,count)×N */
static int s_tab_base[DISPLAY_MENU_MAX_TABS];           /* DAT_0731f408 */
static int s_tab_cursor[DISPLAY_MENU_MAX_TABS];         /* DAT_07337850 */
static int s_tab_scroll[DISPLAY_MENU_MAX_TABS];         /* DAT_073376c0 */
static int s_tab_count[DISPLAY_MENU_MAX_TABS];          /* DAT_07337210 */
static int s_tab_first_item[DISPLAY_MENU_MAX_TABS];     /* DAT_073373a0 (render tab icon) */
static int s_cur_tab     = 0;   /* DAT_0734b968 */
static int s_num_tabs    = 0;   /* DAT_0731f404 */
static int s_mode        = 0;   /* DAT_0734b9a8 */
static int s_confirm_ctr = 0;   /* DAT_0734b994 */
static int s_highlight   = -1;  /* DAT_0734b998 */
static int s_window_flag = 0;   /* DAT_0734b990 */
static int s_possessed   = -1;  /* DAT_005c6ee4 (-1 = recount-needed sentinel) */

/* scratch for the inventory scan + co-sort (DAT_0730b60c keys / DAT_07323418
 * items / DAT_073379e0 indices — all 20000-wide in the engine). */
static int s_scan_keys[SAVE_BANK_ITEM_TABLE_COUNT];
static int s_scan_items[SAVE_BANK_ITEM_TABLE_COUNT];
static int s_scan_idx[SAVE_BANK_ITEM_TABLE_COUNT];

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
    s_possessed   = -1;
}

/* "Number possessed" recount (FUN_00469414 LAB_0046999a, all.c:65468-65478):
 * count the working-bank inventory entries whose item id (value>>6) equals the
 * highlighted item's.  -1 highlight ("Nothing") counts nothing.  Cached in
 * s_possessed; recomputed when the cursor moves / a result fires / the sentinel
 * is -1. */
static void display_menu_recount(void)
{
    s_possessed = 0;
    if (s_highlight == -1)
        return;
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    const uint32_t *inv = bank + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        if (inv[i] != 0xFFFFFFFFu &&
            ((inv[i] ^ (uint32_t)s_highlight) & 0xFFFFFFC0u) == 0)
            s_possessed++;
    }
}

int display_menu_possessed(void) { return s_possessed; }

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

    /* ── FUN_00468338 population (param_1==0 display-stand path) ──────────
     * Scan the working-bank inventory, group by item-DB category into tabs,
     * each tab led by a -1 "select none/remove" entry, items deduped with a
     * running count.  Matches retail's "place an item" list (the sword
     * category shows the player's held swords). */
    memset(s_list,          0, sizeof s_list);
    memset(s_tab_count,     0, sizeof s_tab_count);
    memset(s_tab_first_item,0, sizeof s_tab_first_item);
    if (first_open) {                            /* param_2 != 0 */
        memset(s_tab_cursor, 0, sizeof s_tab_cursor);
        memset(s_tab_scroll, 0, sizeof s_tab_scroll);
    }

    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int n = 0;
    if (bank != NULL) {
        const int32_t *inv = (const int32_t *)(bank + SAVE_BANK_ITEM_TABLE_DWORD);
        for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
            int item = inv[i];
            if (item == -1)
                continue;
            /* sort key (all.c:64308-64315): group by category(=id/100), then
             * rank, with the raw item value in the low bits. */
            int rec  = tables_item_find_slot_by_id(&g_item, item >> 6);
            int cat100 = (rec >= 0) ? (g_item.records[rec].item_id / 100) * 100 : 0;
            int rank   = (rec >= 0) ? g_item.records[rec].rank : 0;
            uint32_t key = (uint32_t)((cat100 + 10 + rank) * 0xc80) + (uint32_t)item;
            if (key & 0x10u)
                key = (((key >> 6) + 0x10u) * 0x40u) | (key % 0x10u);
            s_scan_keys[n]  = (int)key;
            s_scan_items[n] = item;
            s_scan_idx[n]   = n;
            n++;
        }
    }
    chr_prepass_sort(s_scan_keys, s_scan_idx, n);   /* FUN_0045526a co-sort */

    /* tab grouping (all.c:64405-64591).  Each tab's count starts at 1 (the -1
     * "none" entry); each unique item appended bumps it. */
    for (int t = 0; t < DISPLAY_MENU_MAX_TABS; t++)
        s_tab_count[t] = 1;
    int cur_tab  = -1;     /* local_420 (becomes 0 on the first item) */
    int cur_cat  = -1;     /* local_404 */
    int list_pos = 0;      /* local_40c (stride-2 entry index) */
    for (int k = 0; k < n; k++) {
        int item = s_scan_items[s_scan_idx[k]];
        if (item == -1)
            continue;
        int rec = tables_item_find_slot_by_id(&g_item, item >> 6);
        int cat = (rec >= 0) ? g_item.records[rec].category : -1;   /* DAT_095d3808 */
        if (cat != cur_cat) {                       /* new category → new tab */
            cur_tab++;
            cur_cat = cat;
            if (cur_tab >= DISPLAY_MENU_MAX_TABS)
                break;
            s_tab_base[cur_tab] = list_pos;          /* DAT_0731f408 */
            /* lead with the -1 "select none/remove" entry (all.c:64448). */
            if (list_pos * 2 + 1 < DISPLAY_MENU_MAX_LIST) {
                s_list[list_pos * 2]     = -1;
                s_list[list_pos * 2 + 1] = 0;
            }
            list_pos++;
        }
        /* dedup within the list built so far (all.c:64452-64463): same raw item
         * value → bump its count; else append id with count 1 + tab_count++. */
        int found = 0;
        if ((item & 0x10) == 0) {
            for (int e = 0; e < list_pos; e++) {
                if (s_list[e * 2] == item) {
                    s_list[e * 2 + 1]++;
                    found = 1;
                    break;
                }
            }
        }
        if (!found && list_pos * 2 + 1 < DISPLAY_MENU_MAX_LIST) {
            s_list[list_pos * 2]     = item;
            s_list[list_pos * 2 + 1] = 1;
            s_tab_count[cur_tab]++;                   /* DAT_07337210[tab]++ */
            list_pos++;
        }
    }
    s_num_tabs = cur_tab + 1;                         /* DAT_0731f404 */
    if (s_num_tabs <= 0)
        s_num_tabs = 1;
    if (s_num_tabs <= s_cur_tab)
        s_cur_tab = -1;
    if (s_cur_tab < 0)
        s_cur_tab = 0;
    /* per-tab "first real item" for the render's category-icon (all.c:64594:
     * DAT_073373a0[tab] = DAT_0731f5a0[tab_base*2] = the entry AFTER the -1). */
    for (int t = 0; t < s_num_tabs; t++) {
        int e = s_tab_base[t] + 1;                    /* skip the -1 none entry */
        s_tab_first_item[t] = (e * 2 + 0 < DISPLAY_MENU_MAX_LIST) ? s_list[e * 2] : -1;
    }

    /* open-cursor init (FUN_00468338 tail, all.c:64635-64639, the param_2!=0
     * full-open branch): reset to tab 0 and, when tab 0 leads with the -1
     * "Nothing" entry, START the cursor on the first REAL item (index 1).  That
     * is why retail's freshly-opened menu highlights the displayed item (and its
     * description), not "Nothing".  (The param_2==0 re-open clamp loop, all.c:
     * 64597-64633, only matters for re-opens with a saved cursor — PORT-DEBT;
     * the removal drive is always a first-open.) */
    if (first_open) {
        s_cur_tab = 0;
        if (s_tab_count[0] > 1 && s_list[s_tab_base[0] * 2] == -1)
            s_tab_cursor[0] = 1;
    }
    s_highlight = display_menu_selected();             /* DAT_0734b998 */
    display_menu_recount();                             /* DAT_005c6ee4 */

    /* snap the SHARED hand cursor to the highlighted row (FUN_00435693, all.c:
     * 64641): x = 280 (0x438c0000), y = (cursor - scroll)·36 + 96.  Safe in
     * HOUSE: g_cursor_visible only feeds title_save_dialog_cursor_render, which
     * HOUSE invokes from display_menu_render — the title/pause save WINDOW is a
     * separate scene_title render not called here. */
    title_save_dialog_cursor_snap(
        280.0f,
        (float)((s_tab_cursor[s_cur_tab] - s_tab_scroll[s_cur_tab]) * 0x24) + 96.0f);
}

/* ── FUN_00469a9f — selected list item ────────────────────────────────── */
int display_menu_selected(void)
{
    int idx = (s_tab_cursor[s_cur_tab] + s_tab_base[s_cur_tab]) * 2;
    if (idx < 0 || idx >= DISPLAY_MENU_MAX_LIST)
        return -1;
    return s_list[idx];
}

/* slide the shared hand cursor to the current row (FUN_0046939a → FUN_00435710,
 * all.c:65176): x = 280, y = (cursor - scroll)·36 + 96. */
static void display_menu_cursor_to_row(void)
{
    title_save_dialog_cursor_slide(
        280.0f,
        (float)((s_tab_cursor[s_cur_tab] - s_tab_scroll[s_cur_tab]) * 0x24) + 96.0f);
}

/* ── FUN_00469414 — per-frame update ──────────────────────────────────────
 * Faithful port of the cc04 display-stand menu update.  Returns 0 = idle,
 * 1 = CONFIRM (after the 6-frame countdown), 2 = CANCEL, 3 = pick-up arm,
 * 4 = mode-6 auto-sort (counter menu — PORT-DEBT, never reached for mode 0). */
int display_menu_update(int param)
{
    int ret   = 0;
    int moved = 0;
    uint16_t pressed, held;
    int item_under, handled = 0;

    /* slide gate (all.c:65234): no input until the window has fully slid in. */
    if (stage_load_pulse_get_counter() < 5)
        return 0;

    /* confirm countdown (all.c:65240): once armed by a Z-press, count 6 frames
     * then fire CONFIRM.  The brief delay is the pick-up animation window. */
    if (s_confirm_ctr != 0) {
        s_confirm_ctr++;
        if (s_confirm_ctr > 6) {
            s_confirm_ctr = 0;
            return 1;               /* CONFIRM (recount not needed — closing) */
        }
        handled = 1;                /* skip input; fall to recount */
    }

    /* PORT-DEBT(A3): the message-hold countdowns DAT_0734b97c / b988 / b96c
     * (all.c:65248-65263) are only armed by the place-an-item "full inventory"
     * / "sell?" prompts, never by the removal — left BSS-zero, so they fall
     * through. */

    pressed = g_sim_buttons[0].pressed;   /* DAT_073dddd4 edge mask */
    held    = g_sim_buttons[0].held;      /* DAT_073dddd6 auto-repeat */

    if (!handled && (pressed & 0x10u)) {   /* Z / confirm (all.c:65264) */
        /* PORT-DEBT(A3): the mode-6 (DAT_0734b9a8==6) counter auto-sort confirm
         * (all.c:65266-65345) is the shop counter, never the house display
         * stand — fall to the normal arm (all.c:65348). */
        if (s_tab_count[s_cur_tab] >= 1) {
            if (param != 0)
                s_confirm_ctr = 1;  /* LAB_004696bf: arm the countdown */
            ret = 3;                /* pick-up arm (iVar14 = 3) */
        }
        handled = 1;
    }

    if (!handled && (pressed & 0x20u)) {   /* X / cancel (all.c:65355) */
        ret = 2;
        handled = 1;
    }

    item_under = display_menu_selected();   /* uVar2 = list[cursor] */

    /* PORT-DEBT(A3): the (DAT_073dddd4 & 0x40) "use / sell this item" sub-path
     * (all.c:65451-65461) — only the use-item / counter-sell menus.  For the
     * removal the cursor's item is -1, or 0x40 is never pressed, so we always
     * take the navigation branch (all.c:65316-65450). */
    if (!handled &&
        ((pressed & 0x40u) == 0 || item_under == -1 || s_tab_count[s_cur_tab] < 1)) {
        /* tab switch (all.c:65318): held-left (bit 2) prev, held-right (bit 1)
         * next, wrapped mod num_tabs. */
        if (s_num_tabs > 0) {
            int do_tab = 1, delta = 0;
            if (held & 0x2u)      delta = s_num_tabs - 1;   /* prev */
            else if (held & 0x1u) delta = s_num_tabs + 1;   /* next */
            else                  do_tab = 0;
            if (do_tab) {
                moved = 1;
                s_cur_tab = (delta + s_cur_tab) % s_num_tabs;
                if (s_num_tabs > 1)
                    audio_play_se_by_id(0x146);             /* nav SE (no RNG) */
            }
        }

        int count   = s_tab_count[s_cur_tab];
        int visible = (count < 7) ? count : 7;

        /* cursor UP (bit 4, all.c:65383). */
        if ((held & 0x4u) != 0 && s_tab_cursor[s_cur_tab] > 0) {
            moved = 1;
            audio_play_se_by_id(0x146);
            s_tab_cursor[s_cur_tab]--;
            if (s_tab_cursor[s_cur_tab] - s_tab_scroll[s_cur_tab] < 0)
                s_tab_scroll[s_cur_tab]--;
        }
        /* cursor DOWN (bit 8, all.c:65395). */
        if ((held & 0x8u) != 0 && s_tab_cursor[s_cur_tab] < count - 1) {
            moved = 1;
            audio_play_se_by_id(0x146);
            s_tab_cursor[s_cur_tab]++;
            if (s_tab_cursor[s_cur_tab] - s_tab_scroll[s_cur_tab] > visible - 1)
                s_tab_scroll[s_cur_tab]++;
        }

        /* single-tab page jump (all.c:65405): with one tab, held-left/right page
         * the list by 6. */
        if (s_num_tabs == 1) {
            int *cur = &s_tab_cursor[s_cur_tab];
            int *scr = &s_tab_scroll[s_cur_tab];
            if ((held & 0x2u) != 0) {              /* page up */
                int prev = *cur;
                if (*cur > 0) {
                    audio_play_se_by_id(0x146);
                    *cur -= 6;
                    if (*cur - *scr < 0) *scr -= 6;
                    prev = *cur;
                    moved = 1;
                }
                if (prev < 0) *cur = 0;
                if (*scr < 0)  *scr = 0;
            } else if ((held & 0x1u) != 0) {       /* page down */
                int last = count - 1;
                if (*cur < last) {
                    audio_play_se_by_id(0x146);
                    *cur += 6;
                    if (*cur - *scr > visible - 1) *scr += 6;
                    moved = 1;
                    if (last <= *cur) *cur = last;
                } else {
                    *cur = last;
                }
                if (count - visible <= *scr) *scr = count - visible;
            }
        }
    }

    /* highlight = list[cursor] (all.c:65462); slide the cursor if it moved. */
    if (moved)
        display_menu_cursor_to_row();

    /* "number possessed" recount (all.c:65468): on a move, a fired result, or
     * the -1 sentinel. */
    s_highlight = display_menu_selected();
    if (s_possessed == -1 || moved || ret != 0)
        display_menu_recount();
    return ret;
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

/* ── FUN_0046b00a — menu RENDER (C4b) ─────────────────────────────────────
 * Win32 only (uses render_quad + g_sysassets textures + font_draw).  Ported
 * from FUN_0046b00a(0,0) for the cc04==1 display-stand path (DAT_0734b9a8 != 6).
 * C4b-1: the item_win parchment panels (main + category frame + scroll arrows).
 * The item rows (icons + names/counts), category-header text, selected-row
 * pulse and the hand cursor land in C4b-2..4. */
#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"   /* render_quad_add / _flush / _state_setup */
#include "sysassets.h"     /* g_sysassets.item_win_tga / item_icons[] */
#include "font_draw.h"     /* font_draw_text* */

/* ── FUN_00469b3a — the bottom description panel (C4b-4a) ──────────────────
 * Drawn at the tail of FUN_0046b00a (all.c:66837) with (param_1,param_2)=(0,0).
 * The parchment bg is always drawn; for a real highlighted item it adds the two
 * description lines, the base price (comma-formatted) and "Number possessed".
 * The -1 ("Nothing") highlight draws only the bg (all.c:65617).  All text is
 * white (DAT_005c7184=0xffffffff) at scale 0.8 (0x3f4ccccd).  PORT-DEBT: the
 * price-status line (Price Up/Down/…) + the b1c0==6 counter price multipliers
 * need FUN_004361b2 (item price-trend) — that reads the daily-market region
 * pricing tables, not yet ported; skipping it == the type-0 (no-trend) path. */
static void display_menu_description_render(IDirect3DDevice8 *dev)
{
    const sprite_t *win = &g_sysassets.item_win_tga;   /* DAT_073d8748 */
    if (win->tex == NULL)
        return;

    /* panel bg: item_win src(0,320,640,480) dst(0,332,640,160) (all.c:65592). */
    render_quad_state_setup(dev);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);
    {
        const float dst[4] = { 0.0f, 332.0f, 640.0f, 160.0f };
        const float src[4] = { 0.0f, 320.0f, 640.0f, 480.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    }
    render_quad_flush(dev);

    /* -2 / -6 specials (Equip-optimum / fusion) are counter-menu only; -1
     * "Nothing" draws nothing more (all.c:65604-65619). */
    if (s_highlight < 0)
        return;

    int rec = tables_item_find_slot_by_id(&g_item, s_highlight >> 6);
    if (rec < 0)
        return;

    /* PORT-DEBT: the (highlight>>4 & 1) sub-item walk-back (all.c:65635-65641)
     * resolves a variant glyph to its base record; the displayed swords are
     * base items, so we render the resolved record directly. */
    const item_record_t *r = &g_item.records[rec];

    /* description lines (all.c:65660-65661), white scale 0.8. */
    if (r->desc_line1[0])
        font_draw_text(dev, 80.0f, 368.0f, r->desc_line1, 0xffffffffu, 0.8f);
    if (r->desc_line2[0])
        font_draw_text(dev, 80.0f, 394.0f, r->desc_line2, 0xffffffffu, 0.8f);

    /* base price = DB price · 1.0 (_DAT_005c6ee8) → comma-formatted (FUN_00469abb)
     * → "Base Price: %s" at (80,420) (all.c:65665-65701). */
    {
        int price = r->price;
        char num[32], line[64];
        if (price < 1000)
            snprintf(num, sizeof num, "%d", price);
        else if (price < 1000000)
            snprintf(num, sizeof num, "%d,%03d", price / 1000, price % 1000);
        else
            snprintf(num, sizeof num, "%d,%03d,%03d",
                     price / 1000000, (price / 1000) % 1000, price % 1000);
        snprintf(line, sizeof line, "Base Price- %s", num);  /* s_…_005c75f0 */
        font_draw_text(dev, 80.0f, 420.0f, line, 0xffffffffu, 0.8f);
    }

    /* "Number possessed: %d" at (304,420), max(possessed,0) (all.c:65722-65728). */
    {
        int n = display_menu_possessed();
        if (n < 0)
            n = 0;
        char line[48];
        snprintf(line, sizeof line, "Number possessed- %d", n);  /* s_…_005c7638 */
        font_draw_text(dev, 304.0f, 420.0f, line, 0xffffffffu, 0.8f);
    }
}

void display_menu_render(struct IDirect3DDevice8 *dev_in)
{
    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* FUN_0046b00a L20: the window is closed (fully retracted) → draw nothing. */
    int slide = stage_load_pulse_get_counter();        /* DAT_0734b98c */
    if (slide == 0)
        return;

    const sprite_t *win = &g_sysassets.item_win_tga;   /* DAT_073d8748 */
    if (win->tex == NULL)
        return;

    /* slide-in: the window starts 640px right of home and slides 128px/step
     * (DAT_0734b98c << 7).  param_1 = param_2 = 0 (the HOUSE call site). */
    const float x0  = (0.0f + 640.0f) - (float)(slide << 7);   /* fVar1 */
    const float xL  = x0 + 240.0f;                             /* local_18 */
    float       y   = 0.0f + 40.0f;                            /* local_40 */

    int tab     = s_cur_tab;
    int count   = s_tab_count[tab];
    int scroll  = s_tab_scroll[tab];
    int visible = (count < 7) ? count : 7;                     /* local_3c */

    render_quad_state_setup(dev);                              /* MODULATE + alpha blend */
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);

    /* main panel — src(0,0,400,320) dst(xL,40,400,320) (all.c:66499-66508). */
    {
        const float dst[4] = { xL, y, 400.0f, 320.0f };
        const float src[4] = { 0.0f, 0.0f, 400.0f, 320.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    }
    render_quad_flush(dev);

    y += 40.0f;                                                /* local_40 = 80 */

    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);

    /* category frame (mode != 6) — src y depends on tab count (all.c:66517-66532).
     * <2 tabs → src(448,813,688,890); else src(448,736,688,813). dst(xL+80,10,240,77). */
    {
        const float st = (s_num_tabs < 2) ? 813.0f : 736.0f;
        const float sb = (s_num_tabs < 2) ? 890.0f : 813.0f;
        const float dst[4] = { xL + 80.0f, 10.0f, 240.0f, 77.0f };
        const float src[4] = { 448.0f, st, 688.0f, sb };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    }
    /* scroll-up arrow if scrolled (all.c:66534-66544). */
    if (scroll > 0) {
        const float dst[4] = { xL + 56.0f, 40.0f, 64.0f, 32.0f };
        const float src[4] = { 448.0f, 896.0f, 512.0f, 944.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    }
    /* scroll-down arrow if more rows below the window (all.c:66545-66555). */
    if (scroll + visible < count) {
        const float dst[4] = { xL + 56.0f, 312.0f, 64.0f, 32.0f };
        const float src[4] = { 512.0f, 896.0f, 576.0f, 944.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
    }
    render_quad_flush(dev);

    /* ── category header text (C4b-3) ──────────────────────────────────────
     * Centered category name on the green banner (all.c:66587 → FUN_0047d14c):
     * center_x = xL + const[0x519e54]=204, y = const[0x5194d4]=40, scale 0.8. */
    {
        int hcat = -1;
        int hfirst = s_tab_first_item[tab];     /* DAT_073373a0 (entry after -1) */
        if (hfirst >= 0) {
            int hrec = tables_item_find_slot_by_id(&g_item, hfirst >> 6);
            if (hrec >= 0)
                hcat = g_item.records[hrec].category;
        }
        if (hcat >= 0 && hcat < ITEM_CATEGORY_COUNT) {
            const char *cname = g_item.categories[hcat].singular;
            if (cname && cname[0])
                font_draw_text_centered(dev, xL + 204.0f, 40.0f, cname,
                                        0xffffffffu, 0.8f);
        }
    }

    /* ── item rows (C4b-2): per visible row, an icon + name/count text ──────
     * Row geometry (objdump FUN_0046b00a): text at (xL+120, row*0x22+y+12)
     * scale 0.8; icon at (xL+72, row*0x22+y+12-6) 32×32 from item_icons[cat].
     * (y here = local_40 = 80.) */
    const int   base   = s_tab_base[tab];
    const float row_h  = 34.0f;          /* 0x22 */
    const float text_x = xL + 120.0f;    /* const[0x519444] */
    const float icon_x = xL + 72.0f;

    /* icons first (their own bind per row). */
    for (int r = 0; r < visible; r++) {
        int e    = base + scroll + r;
        int item = s_list[e * 2];
        if (item < 0)
            continue;                    /* the -1 "none" row has no icon */
        int rec = tables_item_find_slot_by_id(&g_item, item >> 6);
        if (rec < 0)
            continue;
        int cat  = g_item.records[rec].category;
        int idx  = ((item >> 4) & 1) ? 0 : g_item.records[rec].subindex;
        if (cat < 0 || cat >= SYSASSETS_ITEM_CATEGORIES)
            continue;
        const sprite_t *icon = &g_sysassets.item_icons[cat];
        if (icon->tex == NULL)
            continue;
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)icon->tex);
        const float iy = (float)r * row_h + y + 12.0f - 6.0f;
        const float dst[4] = { icon_x, iy, 32.0f, 32.0f };
        const float src[4] = { (float)((idx % 8) * 32), (float)((idx / 8) * 32),
                               (float)((idx % 8) * 32 + 32), (float)((idx / 8) * 32 + 32) };
        render_quad_add(dst, src, icon->width, icon->height, 0xffffffffu);
        render_quad_flush(dev);
    }

    /* row name + count text. */
    for (int r = 0; r < visible; r++) {
        int e    = base + scroll + r;
        int item = s_list[e * 2];
        int cnt  = s_list[e * 2 + 1];
        const float ty = (float)r * row_h + y + 12.0f;
        char buf[96];
        if (item < 0) {
            /* the -1 "select none / remove" entry (retail EN: "Nothing"). */
            snprintf(buf, sizeof buf, "%s", "Nothing");
        } else {
            int rec = tables_item_find_slot_by_id(&g_item, item >> 6);
            const char *nm = (rec >= 0) ? g_item.records[rec].singular : "?";
            if (cnt > 1)
                snprintf(buf, sizeof buf, "%s x%d", nm, cnt);
            else
                snprintf(buf, sizeof buf, "%s", nm);
        }
        font_draw_text(dev, text_x, ty, buf, 0xffffffffu, 0.8f);
    }

    /* bottom description panel (FUN_00469b3a, all.c:66837) — bg + the
     * highlighted item's desc/price/possessed (C4b-4a). */
    display_menu_description_render(dev);

    /* the SHARED hand cursor (FUN_00435747), drawn LAST exactly as the engine's
     * cc04 render wrapper FUN_0048fdaf does (FUN_0046b00a → FUN_00435747).
     * Self-gates on g_cursor_visible: the open snapped it on, the cc04 close
     * hides it (C4b-4b — reuses the title/options/skip-prompt cursor). */
    title_save_dialog_cursor_render(dev);

    /* PORT-DEBT(C4b-4c): per-row type-coloured row text (FUN_004361b2) + the
     * selected-row brightness pulse, and the price-status line, all depend on
     * the daily-market price-trend (region pricing tables) — not yet ported.
     * The data_win "tooltip base" tail quad (all.c:66843, fixed (440,440)) is
     * also deferred pending a visual check of what it contributes. */
    (void)y;
}

#endif /* _WIN32 */

