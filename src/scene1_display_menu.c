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

#include "sim.h"                 /* g_sim_buttons[0].pressed (edge mask = DAT_073dddd4) */
#include "stage_load_pulse.h"    /* the slide ramp (FUN_004693e3 = DAT_0734b98c/9a0) */
#include "save_bank.h"           /* SAVE_BANK_ITEM_TABLE_DWORD / _FIELD_ITEM_COUNT */
#include "save_work.h"           /* save_work_dwords_at / save_work_active_slot */
#include "tables_item.h"         /* g_item DB (FUN_004681f6 record lookup) */
#include "scene1_chr_prepass.h"  /* chr_prepass_sort (FUN_0045526a co-sort) */

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
        font_draw_text(dev, text_x, ty, buf, 0xff7f7f7fu, 0.8f);
    }

    /* PORT-DEBT(C4b-3..4): the category-header text, the per-row type-coloured
     * text + selected-row brightness pulse, the description window
     * (DAT_0734b990), and the hand cursor (FUN_00469b3a) are the next chips. */
    (void)y;
}

#endif /* _WIN32 */

