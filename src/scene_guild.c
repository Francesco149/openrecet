/*
 * scene_guild.c — see scene_guild.h.
 *
 * Engine sources:
 *   FUN_004922c0 @ 0x4922c0  — the mode-6 tick (Merchant's Guild / market):
 *     the entry tick + its entry-tick-2 arms (the iv1_3 first visit, the town
 *     event FUN_0045de68 = event_probe, the iv6_2/3/4 story beats), the
 *     guildmaster bubble counters, the Leave fades (c2c: the first Leave → the
 *     iv1_16 gift; c28: out to the world map), the main menu (nav, Buy / Sell /
 *     Talk / Expansion, B = Leave), the shared item window (mode 0), the qty
 *     overlay (mode 8), the Talk submenu (mode 2), the expansion Yes/No (mode
 *     4).  rhost multiday-tutorials-suggest (6 guild purchases + Leave) replays
 *     RNG- and save-identical.  PORT-DEBT(guild-fusion): the Fusion option
 *     (modes 5/6 + the recipe list, FUN_004916a7 / 00469155 / 00469118 /
 *     004132c1); PORT-DEBT(stub, FUN_00468fd0): the Sell commit's bag removal;
 *     PORT-DEBT(stub, FUN_004864fd): the expansion's shop re-layout;
 *     PORT-DEBT(stub, FUN_00434ceb): the "Item limit reached" box.
 *   FUN_0049174e @ 0x49174e  — scene init (via FUN_00490e16 at the world-map
 *     exit): the menu reset, work 0xb382/0xb381 = 1, the option table.
 *   FUN_004918b0 @ 0x4918b0  — builds the option-type table _DAT_09640624
 *     + count DAT_005cfab4 (guild fresh-visit → {Buy,Sell,Talk,Leave}).
 *   FUN_00494a73 @ 0x494a73  — the 2D bg blit (guild variant 0) + menu UI.
 *   FUN_0049404b @ 0x49404b  — the menu panel + option list + guildmaster
 *     speech bubble (the FUN_00494a73 tail).
 *   FUN_00473769 @ 0x473769  — the texture-group-7 load (guild variant 0).
 *
 * The update dispatch FUN_004536cb skips FUN_00490e24 while a dialogue runs
 * (DAT_0438b1c8 != 0), so the tick freezes through cutscenes; the menu render
 * stays up behind an overlay dialogue (the iv1_9 reminder).
 * See docs/findings/merchant-guild-RE.md.
 */

#include "scene_guild.h"

#include <stdint.h>
#include <stddef.h>   /* NULL */

#include "save_work.h"               /* save_work_dwords_at / _active_slot */
#include "scene1_intro_dialogue.h"   /* scene1_intro_dialogue_start_single/_busy */
#include "title_save_dialog.h"       /* shared hand cursor (FUN_0043561a/00435693) */
#include "sim.h"                      /* g_sim_buttons[0] — _DAT_073dddd4/d6       */
#include "audio.h"                    /* audio_play_se_by_id (FUN_00499519, no RNG) */
#include "scene1_display_menu.h"      /* the shared item window (Buy slide-in/list) */
#include "stage_load_pulse.h"         /* the item-window slide ramp (FUN_004693e3) */
#include "tables_item.h"              /* g_item DB (price / name for the qty overlay) */
#include "save_bank.h"                /* SAVE_BANK_FIELD_GOLD / _ITEM_TABLE_DWORD     */
#include "choice_box.h"                /* the expansion Yes/No (FUN_00434def/ed2)      */
#include "event_probe.h"               /* FUN_0045de68                                */
#include "fade.h"                      /* FUN_004526f5 / 004528b3 / 0045281c          */
#include "news_daily.h"                /* news_price_trend (FUN_004361b2)             */
#include "scene.h"                     /* g_scene_state (DAT_0438b1c0)                */
#include "scene_worldmap.h"            /* scene_worldmap_set_sel_dest (FUN_0049de0e)  */
#include "scene1_records.h"            /* scene1_records_reset (FUN_0040f64b)         */
#include "worker_load.h"               /* worker_load_spawn (FUN_00452cde)            */
#include "x87.h"                       /* spill32 / x87_ftol                          */

#include <stdio.h>                     /* snprintf — the expansion prompt             */

/* ─── working-arena field offsets (base DAT_044e3798, per-slot) ──────────────
 * The port pins the engine's per-location stage index (DAT_0438b1e0) to the
 * active save slot (save_work_active_slot) — see docs/findings/merchant-guild-RE.md
 * and the worldmap/tutorial siblings that share this base+scheme. */
#define GUILD_FIRSTVISIT_OFF  0x2bc5c   /* DAT_0450f3f4 — guild first-visit seen flag */
#define GUILD_FIRSTLEAVE_OFF  0x2bc5d   /* DAT_0450f3f5 — first-leave/first-buy gate: while
                                         * clear, Leave fires the iv1_9 "bread" tutorial; a
                                         * purchase sets it (then Leave transitions out).  One
                                         * byte after first-visit (DAT_0450f3f4 + 1). */
#define GUILD_TALKSEEN_OFF    0x2bc98   /* 6 talk-dialogue-seen bytes (the "New" badge) */
/* Slot-0-pinned dword indices into the working bank (DAT_0450fb98 / DAT_04510578
 * fall inside slot 0's 0x2dfc8-byte bank — PORT-DEBT(loc-routing), slot 0 only). */
#define GUILD_STORE_LEVEL_DWORD  0xb100   /* DAT_0450fb98 — store/merchant level */
#define GUILD_TIER_DWORD         0xb378   /* DAT_04510578 — shop tier (work +0x2cde0) */
#define GUILD_RESTRICTED_OFF     0x2bc49  /* DAT_0450f3e1 — early-game fixed-stock
                                           * flag; while set, gold is force-pinned
                                           * to 10,000,000 (tutorial infinite money,
                                           * engine FUN_004922c0 all.c:94756-94758). */
#define GUILD_TUTORIAL_GOLD      10000000u /* DAT_00989680 (the pinned value)        */
#define GUILD_FIRSTLEAVE_DONE_OFF 0x2bc92 /* DAT_0450f42a — iv1_16 + the gift seen: Leave
                                           * goes straight out (c28) from then on */
#define GUILD_STORY_OFF          0x2bcd0  /* DAT_0450f468..6c — the iv6_2/3/4 guild beats */
#define GUILD_TUT_A_OFF          0x2bc61  /* DAT_0450f3f9 — world map: the market lit  */
#define GUILD_TUT_B_OFF          0x2bc70  /* DAT_0450f408 — world map: home lit        */
#define W_SCORE_TODAY            0xb0f8   /* today's score (+3 per purchase)           */
#define W_PERIOD_FLAG            0xb382   /* DAT_045105a0 — the trip home costs a period */

/* Market variant flag (engine DAT_0963c5f0). */
static int s_variant = 0;

/* ─── menu state (engine DAT_09642cXX block + the option table) ──────────────
 * Only the fields the RESTING render reads / the resting update writes.  The
 * navigation-only fields (scroll/submenu/confirm) stay 0 until the nav port. */
static struct {
    int entry_tick;   /* DAT_09642c38 — per-entry frame counter            */
    int mode;         /* DAT_09642c00 — 1 = main menu (0 closed, 2 talk…)  */
    int cursor;       /* DAT_09642c04 — selected option index              */
    int scroll;       /* DAT_09642c08 — option-list scroll origin          */
    int bubble;       /* DAT_09642c40 — guildmaster-bubble pop-in 0..0xf    */
    int bob;          /* DAT_09642c48 — bubble bob / text reveal budget    */
    int text_timer;   /* DAT_09642c4c — bubble-text-variant timer (≥0x78→B) */
    int sub_anim;     /* DAT_09642c1c — talk-submenu open anim             */
    int scroll_anim;  /* DAT_09642c20 — submenu slide-in counter           */
    int c24;          /* DAT_09642c24 — submenu slide-in sibling           */
    int item_cursor;  /* DAT_09642c10 — talk/item cursor (reset on dispatch)*/
    int item_scroll;  /* DAT_09642c0c — talk/item scroll (reset on dispatch)*/
    int c14;          /* DAT_09642c14 — confirm-transition flag            */
    int c18;          /* DAT_09642c18 — item-list→menu slide-OUT flag      */
    int transition;   /* DAT_09642c3c — 1 = mid daily-event transition     */
    int c28;          /* DAT_09642c28 — Leave: the fade out to the world map */
    int c2c;          /* DAT_09642c2c — the first Leave: fade → iv1_16      */
    int f_cursor;     /* DAT_09642c30 — fusion recipe cursor               */
    int f_scroll;     /* DAT_09642c34 — fusion recipe scroll               */
    int entries[8];   /* _DAT_09640624 — per-row option type codes         */
    int count;        /* DAT_005cfab4  — option count                      */

    /* ── buy/sell qty-confirm overlay (mode 8; FUN_00491bc0 / FUN_00491de0) ── */
    int ov_slide;     /* DAT_09642c50 — overlay open/close slide (0..4)     */
    int yn_cursor;    /* DAT_09640600 — Yes(0)/No(1) selector               */
    int flash_ctr;    /* DAT_096405fc — confirm/cancel flash counter (0..8) */
    int flash_kind;   /* DAT_096405f8 — 1 buy / 2 No-cancel / 3 B-cancel    */
    int price_anim;   /* _DAT_09642c54 — price-number wobble phase          */
    int up_bob;       /* DAT_09642c64 — up-arrow press bob (0..4)           */
    int dn_bob;       /* DAT_09642c68 — down-arrow press bob (0..4)         */
    int qty;          /* DAT_09642c5c — chosen quantity                     */
    int max_qty;      /* DAT_005cfae4 — quantity cap                        */
    int unit_price;   /* DAT_09642c60 — per-unit price                      */
    int item_rec;     /* DAT_09642c58 — item-DB record (name lookup)        */
} s_menu;

void scene_guild_set_variant(int v) { s_variant = v; }
int  scene_guild_variant(void)      { return s_variant; }

/* ─── option-type table builder (FUN_004918b0) ──────────────────────────────
 * Type codes index the label table (0 Buy, 1 Sell, 2 Talk, 3 Fusion, 4 Leave,
 * 5 Leave, 6 Expansion).  Fresh first-visit guild (store level 0, period 0)
 * → {0,1,2,4} = Buy/Sell/Talk/Leave. */
static void scene_guild_build_table(void)
{
    if (s_variant == 1) {            /* ichiba (dest 1) — not exercised */
        s_menu.entries[0] = 0;
        s_menu.entries[1] = 1;
        s_menu.entries[2] = 5;
        s_menu.count = 3;
        return;
    }

    int store_level = 0, period = 0;
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank != NULL) {
        store_level = (int)bank[GUILD_STORE_LEVEL_DWORD];   /* DAT_0450fb98 */
        period      = (int)bank[GUILD_TIER_DWORD];        /* DAT_04510578 */
    }

    s_menu.entries[0] = 0;           /* Buy  */
    s_menu.entries[1] = 1;           /* Sell */
    int i = 2;
    if (store_level > 3) {           /* Fusion appears at store level 4+ */
        s_menu.entries[2] = 3;
        i = 3;
    }
    int expand = (period == 0 && store_level > 10) ||
                 (period == 1 && store_level > 18) ||
                 (period == 2 && store_level > 24);
    if (expand) {                    /* store-Expansion option */
        s_menu.entries[i] = 6;
        i++;
    }
    s_menu.entries[i]     = 2;       /* Talk  */
    s_menu.entries[i + 1] = 4;       /* Leave */
    s_menu.count = i + 2;
}

/* ─── scene-init FUN_0049174e (via FUN_00490e16 at the world-map exit) ──────── */
void scene_guild_enter_reset(void)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    title_save_dialog_gate_reset();  /* FUN_00434ce3: DAT_0438b148 = 0 */
    s_menu.ov_slide    = 0;          /* DAT_09642c50 */
    if (bank != NULL) {
        bank[W_PERIOD_FLAG]                = 1;   /* the trip home costs a period */
        bank[SAVE_BANK_FIELD_RESUME_MODE]  = 1;
    }
    s_menu.yn_cursor   = 0;          /* DAT_09640600 */
    s_menu.flash_ctr   = 0;          /* DAT_096405fc */
    s_menu.price_anim  = 0;          /* DAT_09642c54 */
    s_menu.c28         = 0;
    s_menu.c2c         = 0;
    s_menu.mode        = 1;          /* DAT_09642c00 = 1 (menu open) */
    s_menu.cursor      = 0;          /* DAT_09642c04 */
    s_menu.scroll      = 0;          /* DAT_09642c08 */
    s_menu.item_cursor = 0;          /* DAT_09642c10 */
    s_menu.item_scroll = 0;          /* DAT_09642c0c */
    /* FUN_0043561a + FUN_00435693(328, cursor·34 + 84): raise + snap the shared
     * hand cursor onto the top option. */
    title_save_dialog_cursor_set_visible(1);
    title_save_dialog_cursor_snap(328.0f, (float)(s_menu.cursor * 0x22) + 84.0f);
    s_menu.entry_tick  = 0;          /* DAT_09642c38 */
    s_menu.transition  = 0;          /* DAT_09642c3c */
    s_menu.bubble      = 0;          /* DAT_09642c40 */
    s_menu.bob         = 0;          /* DAT_09642c48 */
    s_menu.text_timer  = 0;          /* DAT_09642c4c */
    s_menu.sub_anim    = 0;          /* DAT_09642c1c */
    /* FUN_0040d132 (PORT-DEBT(stub, FUN_0040d132)), FUN_0040f64b(1). */
    scene1_records_reset(1);
    s_menu.c14         = 0;
    s_menu.c18         = 0;
    s_menu.scroll_anim = 0;          /* DAT_09642c20 */
    s_menu.c24         = 0;
    scene_guild_build_table();       /* FUN_004918b0 */
    /* DAT_09642bf4 (the fusion recipe count from DAT_09642bf0 + merchant
     * level), DAT_005cfab8 = 1, DAT_09642c44 = 0, DAT_09642bfc = 0 —
     * PORT-DEBT(guild-fusion). */
    s_menu.qty = s_menu.max_qty = s_menu.unit_price = s_menu.item_rec = 0;
    s_menu.up_bob = s_menu.dn_bob = 0;

    /* the shared item window starts closed: zero the slide counter so
     * display_menu_render is a no-op until a Buy/Sell opens it (port-side: a
     * house display-stand slide must not bleed into the resting guild menu). */
    stage_load_pulse_reset();
}

/* FUN_004682c5 + FUN_00468338(7 buy | 5 sell, 1) + FUN_004682d8(0.7 | 0.3):
 * open the shared item window on the guild stock / the bag. */
static void guild_open_item_window(int type)
{
    if (type != 0) {
        display_menu_open(5, 1);
        display_menu_set_price_mult(0.3f);   /* 0x3e99999a */
    } else {
        display_menu_open(7, 1);
        display_menu_set_price_mult(0.7f);   /* 0x3f333333 */
    }
}

/* FUN_0049161d — the unpause hook (FUN_00453384, mode 6): the item window's
 * scroll / cursor back (it was rebuilt by FUN_00468338), the qty overlay's
 * Yes/No cursor, the price multiplier.
 * PORT-DEBT(guild-fusion): the Fusion row's re-highlight (FUN_004916a7). */
void scene_guild_unpause(void)
{
    if (stage_load_pulse_get_active())                       /* FUN_004682bf */
        display_menu_state_restore();                         /* FUN_00468131 */
    if (s_menu.ov_slide > 0)                                  /* DAT_09642c50 */
        title_save_dialog_cursor_snap((float)(96 * s_menu.yn_cursor) + 340.0f, 252.0f);
    display_menu_set_price_mult(s_menu.entries[s_menu.cursor] != 0 ? 0.3f : 0.7f);
}

/* ─── buy-flow helpers (item list mode 0 → qty overlay mode 8) ─────────────── */

/* working-bank gold (engine (&DAT_044e37a4)[loc*0xb7f2] = dword 3). */
static int guild_gold(void)
{
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    return bank ? (int)bank[SAVE_BANK_FIELD_GOLD] : 0;
}

/* FUN_00491bc0(0) — one frame of the qty-confirm overlay (mode 8).  Returns
 * -1 (animating / still open), 1 (buy confirmed), 2 (cancelled).  param_1 is 0
 * at the mode-8 call site, so the close-frame cursor snap (FUN_00435693) is the
 * caller's job (FUN_0043561a on return). */
static int guild_qty_overlay_input(void)
{
    const uint16_t pressed = g_sim_buttons[0].pressed;   /* DAT_073dddd4 lo */
    const uint16_t held    = g_sim_buttons[0].held;      /* DAT_073dddd6    */

    s_menu.price_anim++;                          /* _DAT_09642c54++ */
    if (s_menu.up_bob > 0) s_menu.up_bob--;       /* DAT_09642c64 */
    if (s_menu.dn_bob > 0) s_menu.dn_bob--;       /* DAT_09642c68 */
    if (s_menu.ov_slide < 1)
        return 0;

    /* a confirm/cancel is flashing then sliding the box out (94513-94537). */
    if (s_menu.flash_ctr != 0) {
        if (s_menu.flash_kind == 3) {             /* B-cancel: straight slide out */
            s_menu.ov_slide--;
            if (s_menu.ov_slide != 0) return -1;
            return 2;
        }
        s_menu.flash_ctr++;                        /* flash ~7 frames */
        if (s_menu.flash_ctr < 8) return -1;
        s_menu.ov_slide--;
        if (s_menu.ov_slide != 0) return -1;
        return s_menu.flash_kind;                  /* 1 buy / 2 No-cancel */
    }

    s_menu.ov_slide++;                             /* slide IN (1..4) */
    if (s_menu.ov_slide < 5)
        return -1;
    s_menu.ov_slide = 4;                           /* clamp fully open */

    if ((pressed & 0x20u) == 0) {                  /* B not pressed */
        if ((pressed & 0x10u) == 0) {              /* A not pressed → nav */
            /* Right toggles Yes→No, Left toggles No→Yes (pressed edge), with a
             * 6-frame cursor ease to (340 + yn*96, 252). */
            if ((pressed & 0x01u) && s_menu.yn_cursor == 0) {
                audio_play_se_by_id(0x146);
                s_menu.yn_cursor ^= 1;
                title_save_dialog_cursor_slide((float)(s_menu.yn_cursor * 0x60) + 340.0f,
                                               252.0f);
                return -1;
            }
            if ((pressed & 0x02u) && s_menu.yn_cursor == 1) {
                audio_play_se_by_id(0x146);
                s_menu.yn_cursor ^= 1;
                title_save_dialog_cursor_slide((float)(s_menu.yn_cursor * 0x60) + 340.0f,
                                               252.0f);
                return -1;
            }
            /* Up/Down (held, auto-repeat) adjust the quantity. */
            if ((held & 0x04u) == 0) {             /* up not held */
                if ((held & 0x08u) == 0)           /* down not held → idle */
                    return -1;
                if (s_menu.qty < 2) {              /* already 1 → error beep */
                    audio_play_se_by_id(0x16a);
                    return -1;
                }
                s_menu.qty--;
                s_menu.dn_bob = 4;
            } else {                               /* up held */
                if (s_menu.max_qty <= s_menu.qty) {/* at cap → error beep */
                    audio_play_se_by_id(0x16a);
                    return -1;
                }
                s_menu.qty++;
                s_menu.up_bob = 4;
            }
            audio_play_se_by_id(0x146);
            return -1;
        }
        /* A pressed. */
        if (s_menu.yn_cursor == 0) {               /* Yes → confirm buy */
            s_menu.flash_ctr  = 1;
            s_menu.flash_kind = 1;
            audio_play_se_by_id(0x143);
            return -1;
        }
        s_menu.flash_kind = 2;                      /* No → cancel */
    } else {
        s_menu.flash_kind = 3;                      /* B → cancel */
    }
    s_menu.flash_ctr = 1;
    audio_play_se_by_id(0x13d);
    return -1;
}

/* The unit price (asm 0x493252-0x4932bd / 0x4932e8-0x493353): the item's base
 * price × 0.7 (buy) or 0.3 (sell), each step spilled to a float, then × the
 * FUN_004361b2 trend factor (≥2 → 1.5, 1 → 1.2, ≤−2 → 0.7, −1 → 0.8, 0 → none)
 * and __ftol'd.  The guild is mode 6, so the trend's tutorial-sell gate is off. */
static int guild_unit_price(const uint32_t *bank, int rec, int handle, float mult)
{
    float p = spill32((long double)spill32((long double)g_item.records[rec].price) * mult);
    const int t = news_price_trend((const uint8_t *)bank, handle, 0);
    if (t >= 2)       p = spill32((long double)p * 1.5f);
    else if (t >= 1)  p = spill32((long double)p * 1.2f);
    else if (t <= -2) p = spill32((long double)p * 0.7f);
    else if (t <= -1) p = spill32((long double)p * 0.8f);
    return x87_ftol(p);
}

/* FUN_004922c0 item-list return 3 (A-edge, all_ida 105210-105282): set the
 * highlighted item's unit price (DAT_09642c60) + the confirm/error SE. */
static void guild_buy_price_preview(uint32_t *bank, int sel)
{
    int handle = display_menu_selected();
    if (handle == -1)
        return;
    int rec = tables_item_find_slot_by_id(&g_item, handle >> 6);
    if (rec < 0)
        return;
    if (sel == 0) {                                /* Buy */
        if (display_menu_owned_count(bank) >= 15000) {
            audio_play_se_by_id(0x16a);
            return;
        }
        s_menu.unit_price = guild_unit_price(bank, rec, handle, 0.7f);
        if (display_menu_stock_cap() == 0 || guild_gold() < s_menu.unit_price) {
            audio_play_se_by_id(0x16a);
            return;
        }
    } else {                                       /* Sell */
        s_menu.unit_price = guild_unit_price(bank, rec, display_menu_selected(), 0.3f);
        s_menu.max_qty    = display_menu_stock_cap();
    }
    audio_play_se_by_id(0x143);
}

/* FUN_004922c0 item-list return 1 (the confirm countdown elapsed, all_ida
 * 105159-105208): the item-limit / first-visit gates, the quantity cap, then
 * open the mode-8 qty overlay. */
static void guild_buy_open_qty_overlay(uint32_t *bank, int sel)
{
    const uint8_t *bb = (const uint8_t *)bank;
    int owned = display_menu_owned_count(bank);                 /* FUN_00491b16 */
    if (owned >= 15000 && sel == 0) {
        /* FUN_00434ceb("Item limit reached, cannot buy any more.") —
         * PORT-DEBT(stub, FUN_00434ceb): the message box is unported. */
        return;
    }
    if (bb[GUILD_FIRSTLEAVE_DONE_OFF] == 0 && (int32_t)bank[SAVE_BANK_FIELD_ITEM_COUNT] >= 10) {
        scene1_intro_dialogue_start_single(1, 15);              /* FUN_0044ba2c(1,15,0) */
        return;
    }
    int handle = display_menu_selected();
    if (handle == -1)
        return;
    int rec = tables_item_find_slot_by_id(&g_item, handle >> 6);
    if (sel == 0) {                                /* Buy: cap the quantity */
        /* retail divides unguarded (a 0 price would fault); the port caps at 99 */
        int max = s_menu.unit_price > 0 ? guild_gold() / s_menu.unit_price : 99;
        if (max > 99) max = 99;
        if (owned + max >= 15000) max = 15000 - owned;
        if (max > display_menu_stock_cap()) max = display_menu_stock_cap();
        if (max > 99) max = 99;
        int count = (int32_t)bank[SAVE_BANK_FIELD_ITEM_COUNT];
        if (bb[GUILD_FIRSTLEAVE_DONE_OFF] == 0 && max > 11 - count)
            max = 11 - count;                      /* the first visit: 11 items */
        s_menu.max_qty = max;
        if (display_menu_stock_cap() == 0 || guild_gold() < s_menu.unit_price)
            return;
    }
    s_menu.item_rec   = rec;                        /* DAT_09642c58 */
    s_menu.qty        = 1;                          /* DAT_09642c5c */
    s_menu.mode       = 8;                          /* DAT_09642c00 = 8 */
    s_menu.ov_slide   = 1;                          /* DAT_09642c50 = 1 */
    s_menu.yn_cursor  = 0;                          /* DAT_09640600 = 0 (Yes) */
    s_menu.flash_ctr  = 0;                          /* DAT_096405fc */
    s_menu.price_anim = 0;                          /* _DAT_09642c54 */
    s_menu.up_bob     = 0;                          /* DAT_09642c64 */
    s_menu.dn_bob     = 0;                          /* DAT_09642c68 */
    title_save_dialog_cursor_snap(340.0f, 252.0f); /* FUN_00435693 — Yes position */
}

/* mode-8 confirm (all_ida 105246-105292). */
static void guild_buy_commit(uint32_t *bank)
{
    uint8_t *bb = (uint8_t *)bank;
    if (s_menu.entries[s_menu.cursor] != 0) {
        /* Sell: qty × FUN_00468fd0 (take the row's item out of the bag), gold +=
         * price·qty.  PORT-DEBT(stub, FUN_00468fd0): the removal is unported,
         * so the guild Sell commit stays inert. */
        return;
    }
    int handle = display_menu_selected();
    if (handle == -1)
        return;
    int total = s_menu.unit_price * s_menu.qty;
    if (guild_gold() < total)
        return;
    for (int i = 0; i < s_menu.qty; i++) {
        display_menu_inventory_return(bank, handle);    /* FUN_00468d22 */
        display_menu_buy_post_add(bank);                /* FUN_00469a00 */
    }
    bank[W_SCORE_TODAY] += 3;
    bank[SAVE_BANK_FIELD_GOLD] = (uint32_t)(guild_gold() - total);
    display_menu_possessed_invalidate();                /* DAT_005c6ee4 = -1 */
    audio_play_se_by_id(0x14d);
    if ((int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] != 2 && (int32_t)bank[SAVE_BANK_FIELD_CARD_DAY] == 0) {
        bb[GUILD_FIRSTLEAVE_OFF] = 1;                   /* a purchase unlocks Leave */
        bb[GUILD_TUT_A_OFF]      = 0;
    }
}

/* item_cursor 0..5 → talk-dialogue script (FUN_004922c0 94937-94955).  Shared by
 * the sim (mode-2 dispatch) so it lives outside the Win32 render block. */
static const int k_talk_scripts[6] = { 0x0a, 0x0b, 0x0c, 0x0e, 0x18, 0x19 };

/* DAT_005cfad4: the store-expansion cost per shop tier (work +0x2cde0). */
static const int k_expand_cost[4] = { 10000, 30000, 100000, 500000 };

/* FUN_004922c0's cursor tail: LABEL_114 raises the shared hand cursor
 * (FUN_0043561a); LABEL_329/331/332 ease it (FUN_00435710) onto the main
 * option, the fusion row and the submenu row. */
static void guild_cursor_tail(int show, int main_row, int fusion_row, int sub_row)
{
    if (show)
        title_save_dialog_cursor_set_visible(1);
    if (main_row)
        title_save_dialog_cursor_slide(328.0f, (float)((s_menu.cursor - s_menu.scroll) * 0x22) + 84.0f);
    if (fusion_row)
        title_save_dialog_cursor_slide(92.0f, (float)(s_menu.f_cursor - s_menu.f_scroll) * 32.0f + 148.0f);
    if (sub_row)
        title_save_dialog_cursor_slide(328.0f,
                                       (float)((s_menu.item_cursor - s_menu.item_scroll) * 0x22) + 84.0f);
}

/* LABEL_53: out to the world map — cursor off, item window off, the mode-8
 * load + fade-in (FUN_0047386b's texture-group-7 release has no port twin). */
static void guild_exit_to_worldmap(void)
{
    title_save_dialog_cursor_set_visible(0);             /* FUN_00435612 */
    stage_load_pulse_set_active(0);                      /* FUN_004682d0 */
    worker_load_spawn();                                 /* FUN_00452cde */
    fade_phase_out_start(0, 0x11);                       /* FUN_0045281c(0,17) */
}

/* The entry-tick-2 arms (all_ida 104897-104952).  Returns 1 when the tick ends. */
static int guild_entry_events(uint32_t *bank)
{
    uint8_t *bb = (uint8_t *)bank;
    if ((int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] == 2)
        return 0;
    /* first visit: mark seen + the iv1_3 cutscene (FUN_0044ba2c(1,3,1)) +
     * FUN_00452809 (PORT-DEBT(stub, FUN_00452809), as at the tutorial sites). */
    if (bb[GUILD_FIRSTVISIT_OFF] == 0) {
        bb[GUILD_FIRSTVISIT_OFF] = 1;
        scene1_intro_dialogue_start_single(1, 3);
        return 1;
    }
    int grp, idx;
    if (event_probe(bank, s_variant == 0 ? 3 : 1, &grp, &idx, 1)) {
        scene1_intro_dialogue_start_single(grp, idx);   /* FUN_0044ba2c(grp,idx,1) */
        s_menu.transition = 1;
        return 1;
    }
    /* the story-mode guild beats: flag 0x2bcd0 set, before the evening, at the
     * guild — iv6_2, iv6_3, iv6_4 (each re-arms the entry tick), then straight
     * back out to the world map. */
    if (bb[GUILD_STORY_OFF] == 1 && (int32_t)bank[SAVE_BANK_FIELD_CLOCK_TARGET] < 2 && s_variant == 0) {
        for (int k = 1; k <= 3; k++) {
            if (bb[GUILD_STORY_OFF + k] == 0) {
                bb[GUILD_STORY_OFF + k] = 1;
                scene1_intro_dialogue_start_single(6, k + 1);
                s_menu.transition = 1;
                s_menu.entry_tick = 1;
                return 1;
            }
        }
        if (bb[GUILD_STORY_OFF + 4] == 0) {
            bb[GUILD_STORY_OFF + 4] = 1;
            g_scene_state = 8;
            guild_exit_to_worldmap();
            return 1;
        }
    }
    return 0;
}

/* ─── pure-C event tick (FUN_00490e24 → FUN_004922c0, all_ida 104808) ──────── */

void scene_guild_sim(void)
{
    /* The update dispatch FUN_004536cb reaches FUN_00490e24 only while no
     * dialogue runs (DAT_0438b1c8 == 0), so the whole tick — the entry-tick
     * counter included — freezes through a cutscene. */
    if (scene1_intro_dialogue_busy())
        return;
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    uint8_t *bb = (uint8_t *)bank;
    const uint16_t pressed = g_sim_buttons[0].pressed;   /* DAT_073dddd4 lo  */
    const uint16_t held    = g_sim_buttons[0].held;      /* the auto-repeat hi word */
    int v70 = 0;   /* ease the cursor onto the main option */
    int v69 = 0;   /* … onto the submenu row */
    int v71 = 0;   /* … onto the fusion row */

    s_menu.entry_tick++;                                 /* DAT_09642c38 */
    /* the tutorial's fixed stock: gold pinned to 10,000,000 every frame. */
    if (bb[GUILD_RESTRICTED_OFF] != 0)
        bank[SAVE_BANK_FIELD_GOLD] = GUILD_TUTORIAL_GOLD;
    if (title_save_dialog_gate_tick() == -1)            /* FUN_00434d6a: a message box */
        return;
    if (s_menu.entry_tick == 2 && guild_entry_events(bank))
        return;

    const int v73 = s_menu.entries[s_menu.cursor];

    /* the guildmaster's bubble: pops in while the menu is settled (or Expansion
     * is highlighted); the bob/reveal budget runs while it is fully open. */
    if (v73 == 6 || (s_menu.mode != 0 && s_menu.scroll_anim == 0 &&
                     s_menu.entry_tick >= 15 && s_menu.c24 == 0)) {
        if (s_menu.bubble < 15)
            s_menu.bubble++;
    } else if (s_menu.bubble > 0) {
        s_menu.bubble--;
    }
    if (s_menu.bubble == 15)
        s_menu.bob++;
    else
        s_menu.bob = 0;
    s_menu.text_timer++;
    if ((held & 0x0cu) != 0)                             /* DAT_073dddd4 & 0xc0000 */
        s_menu.text_timer = 120;
    s_menu.transition = 0;

    /* the first Leave after the first purchase: fade, then iv1_16 (Tear's
     * "back to Recettear" scene; FUN_0044ba2c(1,16,4) — PORT-DEBT(simplified,
     * FUN_0044ba2c): the port's spawn has no mode-4 end arm, FUN_004526f5(0,3)),
     * the gift item 0x3ea00 into the bag, and on to the c28 exit. */
    if (s_menu.c2c > 0) {
        if (++s_menu.c2c == 2)
            fade_phase1_start(0, 0x11);                  /* FUN_004526f5(0,17) */
        if (s_menu.c2c >= 3 && fade_is_done()) {
            scene1_intro_dialogue_start_single(1, 16);
            s_menu.c2c = 0;
            bb[GUILD_FIRSTLEAVE_DONE_OFF] = 1;
            s_menu.c28 = 3;
            int n = (int32_t)bank[SAVE_BANK_FIELD_ITEM_COUNT];
            bank[6 + n] = 0x3ea00u;
            bank[SAVE_BANK_FIELD_ITEM_COUNT] = (uint32_t)(n + 1);
        }
        return;
    }
    /* Leave: fade, then the world map with the cursor on this market. */
    if (s_menu.c28 > 0) {
        if (++s_menu.c28 == 2)
            fade_phase1_start(0, 0x11);
        if (s_menu.c28 > 2 && fade_is_done()) {
            g_scene_state = 8;
            scene_worldmap_set_sel_dest(s_variant != 0 ? 1 : 3);   /* FUN_0049de0e */
            guild_exit_to_worldmap();
        }
        return;
    }

    if (s_menu.mode == 2) {
        /* ── submenu (Talk at the guild; the Buy/Sell picker at the market) ── */
        if (s_menu.c14 != 0) {                           /* closing */
            if (s_menu.sub_anim <= 15) {
                s_menu.sub_anim = 0;
                s_menu.mode     = 1;
                s_menu.c14      = 0;
            } else {
                s_menu.sub_anim--;
            }
        } else if (s_menu.sub_anim < 25) {
            s_menu.sub_anim++;
        }
        if (s_menu.scroll_anim <= 0) {
            if (s_menu.sub_anim != 25) {
                guild_cursor_tail(1, v70, v71, v69);
                return;
            }
            const int n = (s_variant == 0 && v73 != 0) ? 7 : 3;
            if ((pressed & 0x20u) == 0 && ((pressed & 0x10u) == 0 || s_menu.item_cursor != n - 1)) {
                if ((pressed & 0x10u) == 0) {
                    if (held & 0x04u) {
                        if (s_menu.item_cursor > 0) {
                            audio_play_se_by_id(0x146);
                            v69 = 1;
                            if (--s_menu.item_cursor - s_menu.item_scroll < 0)
                                s_menu.item_scroll--;
                        }
                    } else if ((held & 0x08u) && s_menu.item_cursor < n - 1) {
                        audio_play_se_by_id(0x146);
                        v69 = 1;
                        if (++s_menu.item_cursor - s_menu.item_scroll > 5)
                            s_menu.item_scroll++;
                    }
                    guild_cursor_tail(1, v70, v71, v69);
                    return;
                }
                s_menu.scroll_anim = 1;                  /* A on a row */
                audio_play_se_by_id(0x143);
                guild_cursor_tail(1, v70, v71, v69);
                return;
            }
            /* B, or A on the last row */
            if (s_variant != 0) {
                if (bb[GUILD_FIRSTLEAVE_DONE_OFF] != 0)
                    s_menu.c28++;
                else
                    s_menu.c2c++;
            } else {
                s_menu.c14 = 1;
                v70 = 1;
            }
            audio_play_se_by_id(0x13d);
            guild_cursor_tail(1, v70, v71, v69);
            return;
        }
        if (s_menu.c18 == 1) {                           /* the row flash backing out */
            if (--s_menu.scroll_anim <= 15) {
                s_menu.scroll_anim = 0;
                s_menu.c18 = 0;
            }
            return;
        }
        if (++s_menu.scroll_anim == 16) {
            if (s_variant == 0 && v73 == 2) {
                /* a Talk topic: its dialogue + the "New" badge off. */
                const int c = s_menu.item_cursor;
                if (c >= 0 && c < 6)
                    scene1_intro_dialogue_start_single(1, k_talk_scripts[c]);
                s_menu.scroll_anim = 0;
                bb[GUILD_TALKSEEN_OFF + c] = 1;
                return;
            }
            guild_open_item_window(s_menu.entries[s_menu.cursor]);
        }
        if (s_menu.scroll_anim == 25) {
            if (s_variant != 0 || s_menu.entries[s_menu.cursor] != 1)
                s_menu.mode = 0;
            else
                s_menu.scroll_anim = 0;
        }
        return;
    }

    switch (s_menu.mode) {
    case 4: {   /* ── the expansion Yes/No ── */
        const int r = choice_box_poll(pressed, 0);       /* FUN_00434ed2(0) */
        if (r == 1) {
            const int tier = (int32_t)bank[GUILD_TIER_DWORD];
            const int cost = k_expand_cost[tier & 3];
            if ((int32_t)bank[SAVE_BANK_FIELD_GOLD] < cost)
                return;
            bank[SAVE_BANK_FIELD_GOLD] -= (uint32_t)cost;
            audio_play_se_by_id(0x14d);
            s_menu.mode = 1;
            if (tier < 3)
                bank[GUILD_TIER_DWORD] = (uint32_t)(tier + 1);
            /* FUN_004864fd: re-lay the shop for the new tier —
             * PORT-DEBT(stub, FUN_004864fd). */
            s_menu.count = 0;
            scene_guild_build_table();
        } else if (r != 2) {
            return;
        } else {
            s_menu.mode = 1;
        }
        guild_cursor_tail(0, 1, v71, v69);
        return;
    }
    case 1: {   /* ── the main menu ── */
        if (s_menu.entry_tick < 15) {
            guild_cursor_tail(1, v70, v71, v69);
            return;
        }
        if (s_menu.c18 == 1) {                           /* panel sliding back in */
            int v = s_menu.c24 - 1;
            s_menu.c24 = v;
            if (v <= 15) {
                v = 0;
                s_menu.c24 = 0;
                s_menu.c18 = 0;
            }
            s_menu.scroll_anim = v;
            guild_cursor_tail(1, v70, v71, v69);
            return;
        }
        if (s_menu.c24 > 0) {                            /* panel sliding out */
            const int v = ++s_menu.c24;
            s_menu.scroll_anim = v;
            if (v == 15 && v73 != 6) {
                if (v73 == 3) {
                    s_menu.f_cursor = 0;
                    s_menu.f_scroll = 0;
                    v71 = 1;
                } else {
                    guild_open_item_window(s_menu.entries[s_menu.cursor]);
                }
            }
            if (s_menu.entries[s_menu.cursor] == 6) {
                if (v == 15) {
                    char msg[96];
                    s_menu.mode = 4;
                    s_menu.scroll_anim = 0;
                    s_menu.c24 = 0;
                    snprintf(msg, sizeof msg, "Expanding the store will cost %dpix.<BR>Continue?",
                             k_expand_cost[(int32_t)bank[GUILD_TIER_DWORD] & 3]);
                    choice_box_open(msg, 0, 0);          /* FUN_00434def */
                }
            } else if (v == 25) {
                s_menu.mode = 0;
            }
            guild_cursor_tail(1, v70, v71, v69);
            return;
        }
        if (s_menu.sub_anim > 0) {                       /* the submenu opening */
            if (++s_menu.sub_anim == 15) {
                s_menu.mode = 2;
                v69 = 1;
            }
            guild_cursor_tail(1, v70, v71, v69);
            return;
        }
        if ((pressed & 0x20u) == 0) {
            if ((pressed & 0x10u) == 0) {                /* LABEL_158: Up/Down */
                int d = (held & 0x04u) ? -1 : (held & 0x08u) ? 1 : 0;
                if (d != 0) {
                    v70 = 1;
                    s_menu.cursor = (s_menu.count + s_menu.cursor + d) % s_menu.count;
                    audio_play_se_by_id(0x146);
                }
                guild_cursor_tail(1, v70, v71, v69);
                return;
            }
            if (v73 != 4 && v73 != 5) {                  /* A: open the option */
                if (v73 == 2) {
                    s_menu.sub_anim = 1;
                } else if (v73 == 3) {
                    /* Fusion — PORT-DEBT(guild-fusion): the recipe list
                     * (FUN_004916a7 / 00469155 / 00469118, modes 5/6) is
                     * unported; the option stays shut. */
                    guild_cursor_tail(1, v70, v71, v69);
                    return;
                } else {
                    if (v73 == 6 && (int32_t)bank[SAVE_BANK_FIELD_GOLD]
                                    < k_expand_cost[(int32_t)bank[GUILD_TIER_DWORD] & 3]) {
                        audio_play_se_by_id(0x16a);
                        guild_cursor_tail(1, v70, v71, v69);
                        return;
                    }
                    s_menu.c24 = 1;
                    s_menu.scroll_anim = 1;
                }
                s_menu.item_cursor = 0;
                s_menu.item_scroll = 0;
                audio_play_se_by_id(0x143);
                guild_cursor_tail(1, v70, v71, v69);
                return;
            }
        }
        /* LABEL_163 — Leave (B, or A on Leave): blocked by the iv1_9 "buy
         * something first" reminder until the first purchase; else the exit. */
        if (bb[GUILD_FIRSTLEAVE_OFF] != 0) {
            if (bb[GUILD_FIRSTLEAVE_DONE_OFF] != 0)
                s_menu.c28++;
            else
                s_menu.c2c++;
            if ((int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] != 2 && (int32_t)bank[SAVE_BANK_FIELD_CARD_DAY] == 0) {
                bb[GUILD_TUT_A_OFF] = 0;
                bb[GUILD_TUT_B_OFF] = 1;
            }
        } else {
            scene1_intro_dialogue_start_single(1, 9);    /* FUN_0044ba2c(1,9,0) */
        }
        audio_play_se_by_id(0x13d);
        guild_cursor_tail(1, v70, v71, v69);
        return;
    }
    case 8: {   /* ── the qty Yes/No overlay ── */
        const int r = guild_qty_overlay_input();         /* FUN_00491bc0(0) */
        if (r == 1)
            guild_buy_commit(bank);
        else if (r != 2)
            return;
        display_menu_cursor_to_row();                    /* FUN_0046939a */
        s_menu.mode = 0;
        title_save_dialog_cursor_set_visible(1);
        return;
    }
    case 5:
    case 6:
        /* the fusion confirm / result — PORT-DEBT(guild-fusion). */
        return;
    default:
        break;   /* mode 3 is never set (dead in retail) */
    }

    /* ── mode 0: the shared item window (Buy / Sell) ── */
    if (v73 == 3)
        return;   /* the fusion list — PORT-DEBT(guild-fusion) */
    display_menu_set_price_mult(v73 == 0 ? 0.7f : 0.3f); /* FUN_004682d8 */
    const int r = display_menu_update(1);                /* FUN_00469414(1) */
    if (r == 2) {                                        /* B: back to the menu */
        s_menu.mode = 1;
        s_menu.c18 = 1;
        stage_load_pulse_set_active(0);                  /* FUN_004682d0 */
        audio_play_se_by_id(0x13d);
        guild_cursor_tail(0, 1, v71, v69);
    } else if (r == 3) {
        guild_buy_price_preview(bank, v73);
    } else if (r == 1) {
        guild_buy_open_qty_overlay(bank, v73);
    }
}

/* ─── Win32 worker_load wiring + render ─────────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <math.h>          /* sinf — the "New" badge sparkle */
#include <stdio.h>         /* snprintf — the qty-overlay title / price strings */

#include "render_quad.h"   /* render_quad_state_setup/bind/add/add_mirrored/flush */
#include "sprite.h"        /* sprite_t, sprite_load */
#include "worker_load.h"   /* worker_load_set_cb */
#include "sysassets.h"     /* g_sysassets.item_win_tga (the menu panel frame) */
#include "font_draw.h"     /* font_draw_text / font_draw_text_box */
#include "scene1_dialogue_run.h"  /* ive_box_scale (FUN_0046c86f bubble pop math) */

sprite_t g_scene_guild[SCENE_GUILD_TEX_COUNT];

static IDirect3DDevice8 *g_scene_guild_dev = NULL;

/* Guild variant (DAT_0963c5f0 == 0) texture set — FUN_00473769 group-7 loads
 * (all.c:72012-72029).  Engine kind=7 dropped by sprite_load, as elsewhere. */
static const struct { const char *path; uint32_t w, h; }
g_scene_guild_assets[SCENE_GUILD_TEX_COUNT] = {
    [SCENE_GUILD_TEX_BG]      = { "bmp/ivent/bg_guild.bmp",     0x400, 0x200 },
    [SCENE_GUILD_TEX_KEEPER]  = { "bmp/ivent/13syounin_01.tga", 0x200, 0x200 },
    [SCENE_GUILD_TEX_BORD]    = { "bmp/result/bord01.tga",      0x200, 0x100 },
    [SCENE_GUILD_TEX_CHRNAME] = { "bmp/ivent/chrname.tga",      0x200, 0x200 },
    [SCENE_GUILD_TEX_LEVEWIN] = { "bmp/leve_win.tga",           0x200, 0x100 },
    [SCENE_GUILD_TEX_SHOPMODE] = { "bmp/shopmode.tga",          0x400, 0x200 },
};

/* ─── menu option / bubble strings (engine PTR_PTR_005cfaf0 + bubble tables) ── */
static const char *const k_menu_labels[] = {
    "Buy", "Sell", "Talk", "Fusion", "Leave", "Leave", "Expansion",
};
/* Bubble B (DAT_09642c4c >= 0x78), guild variant, indexed by the selected
 * option type code (engine PTR_s_Time_to_stock_up… @0x5cfb30). */
static const char *const k_bubble_b_guild[] = {
    "Time to stock up a bit,<BR>eh? Step on up!",          /* 0 Buy   */
    "Selling off excess stock?",                            /* 1 Sell  */
    "If there's anything you<BR>don't know, just ask.",     /* 2 Talk  */
    "Remember, you need<BR>ingredients if you want<BR>to fuse things!", /* 3 Fusion */
    "Well, come back any time!",                            /* 4 Leave */
    "Well, come back any time!",                            /* 5       */
    "",                                                     /* 6 Expansion → cost string */
};
/* Bubble A (DAT_09642c4c < 0x78), guild variant (engine
 * PTR_s_Before_you_stock_up… @0x5cfb28). */
static const char *const k_bubble_a_guild =
    "Before you stock up,<BR>make sure to read up on<BR>everything you can!";

/* Talk-submenu topics (engine PTR_s_What_is_the_guild_005cfb0c, 7 entries): the
 * 6 questions (cursor 0..5, each → a dialogue iv1_0a/0b/0c/0e/18/19 + a "New"
 * badge while unseen) + the "Never mind" close row (cursor 6). */
static const char *const k_talk_topics[] = {
    "What is the guild?",       /* 0 → iv1_10 (0x0a) */
    "What can I do here?",       /* 1 → iv1_11 (0x0b) */
    "About merchant levels",     /* 2 → iv1_12 (0x0c) */
    "About the town",            /* 3 → iv1_14 (0x0e) */
    "About unknown items",       /* 4 → iv1_24 (0x18) */
    "About fusion",              /* 5 → iv1_25 (0x19) */
    "Never mind",                /* 6 → close (no dialogue) */
};

static void scene_guild_bind_add(IDirect3DDevice8 *d, const sprite_t *s,
                                 const float dst[4], const float src[4],
                                 uint32_t color, int mirrored)
{
    if (!s->tex) return;
    render_quad_bind(d, s);
    if (mirrored)
        render_quad_add_mirrored(dst, src, s->width, s->height, color);
    else
        render_quad_add(dst, src, s->width, s->height, color);
    render_quad_flush(d);
}

/* ─── the menu UI (FUN_0049404b) ────────────────────────────────────────────
 * Panel frame + option list + guildmaster speech bubble.  The buy/sell-confirm
 * overlay (FUN_00491de0, gated DAT_09642c50>0), the Talk submenu (mode 2) and
 * the Fusion sub-screen (FUN_00493616) are PORT-DEBT(guild-menu-nav). */
static void scene_guild_menu_render(IDirect3DDevice8 *d)
{
    /* slot: the panel-frame width origin (DAT_0734b98c-style slide); at rest
     * DAT_09642c20 == 0 ⇒ x origin 256. */
    float panel_x = 256.0f;
    if (s_menu.scroll_anim > 0xf)
        panel_x = 256.0f + (float)((s_menu.scroll_anim * 5 - 0x4b) * 8);

    /* Panel frame: item_win.tga (DAT_073d8748), dst (panel_x,-8,400,320),
     * src (0,0,400,320).  MODULATE (inherited from render_quad_state_setup). */
    {
        const float dst[4] = { panel_x, -8.0f, 400.0f, 320.0f };
        const float src[4] = { 0.0f, 0.0f, 400.0f, 320.0f };
        scene_guild_bind_add(d, &g_sysassets.item_win_tga, dst, src, 0xffffffffu, 0);
    }

    /* Option list (DAT_09642c00 != 0).  Engine sets COLOROP=ADDSIGNED for the
     * row text (all.c:95943 SetTextureStageState(0,COLOROP,8)).  As the Talk
     * submenu opens (c1c = sub_anim grows 0→0x19) the main rows collapse: the
     * non-selected rows slide up + cull, the selected row slides up-left to head
     * the submenu (all.c:95944-96030). */
    if (s_menu.mode != 0 && s_menu.count > 0) {
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

        const float label_x = panel_x + 120.0f;            /* 376 at rest */
        const int   c1c      = s_menu.sub_anim;            /* Talk submenu open anim */
        for (int idx = s_menu.count - 1; idx >= 0; idx--) {
            int type = s_menu.entries[s_menu.scroll + idx];
            float rx = label_x;
            float ry = (float)(idx * 0x22) + 64.0f;
            float scale = 1.0769231f;                      /* selected, larger */
            int   draw  = 1;
            if (idx == s_menu.cursor) {
                /* selected row slides up-left once the submenu opens past 0x14
                 * (all.c:95971-95983); the brightness pulse is PORT-DEBT. */
                if (c1c > 0x14) {
                    int s = c1c - 0x14;
                    rx -= (float)(s * 0xc);
                    if (s_menu.cursor == 0)
                        ry -= (float)(s * 7);
                    else
                        ry -= (float)(((idx * 0x22 + 0x22) / 5) * s);
                }
            } else {
                /* non-selected rows slide up + cull as the submenu opens
                 * (all.c:96011-96022). */
                scale = 0.8615385f;                        /* others, smaller */
                if (c1c > 0 && c1c < 0x19 && s_menu.c14 == 0) {
                    ry -= (float)((c1c * idx * 0x22) / 0xf);
                    if (ry < 64.0f) ry = 64.0f;
                }
                ry += 2.0f;
                draw = (s_menu.c14 != 0) || (c1c < 6) || ((c1c * -0x20 + 0x19f) >= 0);
            }
            if (!draw)
                continue;

            /* Color = (alpha<<24)|0x7f7f7f (grey-127 RGB modulating the dark
             * font under ADDSIGNED; the selected-row brightness pulse is
             * PORT-DEBT — settled to full at rest). */
            if (type >= 0 && type < (int)(sizeof k_menu_labels / sizeof *k_menu_labels))
                font_draw_text(d, rx, ry + 12.0f, k_menu_labels[type],
                               0xff7f7f7fu, scale);

            /* "New" badge next to Talk (type 2) when any of the 6 talk
             * dialogues is unseen (all.c:95987-96008). */
            if (s_variant == 0 && type == 2) {
                uint32_t *bank = save_work_dwords_at(save_work_active_slot());
                int unseen = 0;
                if (bank != NULL) {
                    const uint8_t *bb = (const uint8_t *)bank;
                    for (int t = 0; t < 6; t++)
                        if (bb[GUILD_TALKSEEN_OFF + t] == 0) unseen = 1;
                }
                if (unseen) {
                    /* Sparkle (all.c:95997-96004): R = sin(c38·0.1)·64+191,
                     * G = ·32+159, B = 0x7f; small 0.5 scale (superscript). */
                    float sp = sinf((float)s_menu.entry_tick * 0.1f);
                    int r = (int)(sp * 64.0f + 191.0f);
                    int g = (int)(sp * 32.0f + 159.0f);
                    uint32_t ncol = 0xff000000u | ((uint32_t)(r & 0xff) << 16) |
                                    ((uint32_t)(g & 0xff) << 8) | 0x7fu;
                    font_draw_text(d, rx - 12.0f, ry + 8.0f, "New", ncol, 0.5f);
                }
            }
        }

        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    }

    /* ── Talk submenu rows (DAT_09642c00 == 2, all.c:96034-96090) ───────────────
     * 6 visible topic rows sliding in from the right (rx = panel_x+280 − slide,
     * fading in by alpha=(c1c−0xf)·0x34), the cursor row larger, a per-topic "New"
     * badge on unseen topics, and a single up/down scroll arrow (item_win.tga). */
    if (s_menu.mode == 2) {
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

        const int c1c = s_menu.sub_anim;
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        const uint8_t *bb = bank ? (const uint8_t *)bank : NULL;

        if (c1c > 0xf) {
            int alpha = (c1c - 0xf) * 0x34;                /* fade-in (96044) */
            if (alpha > 0xff) alpha = 0xff;
            const float slide = (float)((c1c - 0xf) * 0x10);
            for (int row = 0; row < 6; row++) {
                int topic = s_menu.item_scroll + row;      /* c0c + row */
                float rx = (panel_x + 280.0f) - slide;     /* 376 at full open */
                float ry = (float)(row * 0x22) + 64.0f;
                float scale = 1.0769231f;
                if (s_menu.item_cursor - s_menu.item_scroll != row) {
                    scale = 0.8615385f;                    /* non-cursor row */
                    ry += 2.0f;
                }
                /* row text: grey-127 (the cursor-row confirm-slide pulse is
                 * PORT-DEBT(talk-confirm-flash) — settled to 0x7f). */
                const char *label = (topic >= 0 && topic < 7) ? k_talk_topics[topic] : "";
                uint32_t col = ((uint32_t)alpha << 24) | 0x7f7f7fu;
                font_draw_text(d, rx, ry + 12.0f, label, col, scale);

                /* per-topic "New" badge (96068): unseen topic 0..5. */
                if (topic >= 0 && topic < 6 && bb && bb[GUILD_TALKSEEN_OFF + topic] == 0) {
                    float sp = sinf((float)s_menu.entry_tick * 0.1f);
                    int rr = (int)(sp * 64.0f + 191.0f);
                    int gg = (int)(sp * 32.0f + 159.0f);
                    uint32_t ncol = ((uint32_t)alpha << 24) |
                                    ((uint32_t)(rr & 0xff) << 16) |
                                    ((uint32_t)(gg & 0xff) << 8) | 0x7fu;
                    font_draw_text(d, rx - 12.0f, ry + 8.0f, "New", ncol, 0.5f);
                }
            }
        }

        /* scroll arrows (item_win.tga cells), all.c:96085-96090.  Up shown when
         * scrolled (c0c>0), down when at the top (c0c<1); 1 of 2 (7 topics / 6
         * rows ⇒ c0c ∈ {0,1}).  dst (panel_x+16, 40|248, 64, 48), grey-127. */
        {
            const sprite_t *win = &g_sysassets.item_win_tga;
            if (win->tex) {
                IDirect3DDevice8_SetTexture(d, 0, (IDirect3DBaseTexture8 *)win->tex);
                if (s_menu.item_scroll > 0) {              /* up arrow */
                    const float dst[4] = { panel_x + 16.0f, 40.0f, 64.0f, 48.0f };
                    const float src[4] = { 448.0f, 896.0f, 512.0f, 944.0f };
                    render_quad_add(dst, src, win->width, win->height, 0xff7f7f7fu);
                } else {                                   /* down arrow */
                    const float dst[4] = { panel_x + 16.0f, 248.0f, 64.0f, 48.0f };
                    const float src[4] = { 512.0f, 896.0f, 576.0f, 944.0f };
                    render_quad_add(dst, src, win->width, win->height, 0xff7f7f7fu);
                }
                render_quad_flush(d);
            }
        }

        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    }

    /* Guildmaster speech bubble (DAT_09642c40 > 0).  COLOROP=MODULATE
     * (all.c:96092). */
    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    if (s_menu.bubble > 0) {
        int tag_alpha = s_menu.bubble * 0x20 - 0xe1;       /* 255 at full open */

        /* Bubble body (shopmode.tga, H-mirrored).  ive_box_scale gives the
         * pop-in scale/alpha from DAT_09642c40 (closing = DAT_005cfab8==0 = 0). */
        float sx = 1.0f, sy = 1.0f; int alpha = 0xff;
        ive_box_scale(s_menu.bubble, &sx, &sy, &alpha, 0);
        {
            const float dst[4] = { 368.0f - sx * 208.0f, 376.0f - sy * 88.0f,
                                   sx * 416.0f, sy * 176.0f };
            const float src[4] = { 0.0f, 176.0f, 416.0f, 352.0f };
            scene_guild_bind_add(d, &g_scene_guild[SCENE_GUILD_TEX_SHOPMODE], dst, src,
                                 ((uint32_t)alpha << 24) | 0xffffffu, 1);
        }

        /* DAT_005cfab8 is init-1 / never cleared ⇒ the name tag + body text
         * always draw. */
        if (tag_alpha > 0) {
            /* "Guild Master" name tag: chrname.tga cell 0xb (col 1, row 4) =
             * src (128,128,256,160), dst (308,300,128,32). */
            const float dst[4] = { 308.0f, 300.0f, 128.0f, 32.0f };
            const float src[4] = { 128.0f, 128.0f, 256.0f, 160.0f };
            scene_guild_bind_add(d, &g_scene_guild[SCENE_GUILD_TEX_CHRNAME], dst, src,
                                 ((uint32_t)tag_alpha << 24) | 0xffffffu, 0);
        }

        /* Bubble body text — FUN_00465db4 @ (250,348), scale 1.0, typewriter
         * budget DAT_09642c48*2. */
        int sel_type = s_menu.entries[s_menu.cursor];
        const char *text;
        if (sel_type == 6) {
            /* Expansion cost string — not reachable on the fresh menu
             * (PORT-DEBT(guild-menu-nav): the cost-table lookup is unported). */
            text = "";
        } else if (s_menu.text_timer < 0x78) {
            text = k_bubble_a_guild;
        } else if (sel_type >= 0 &&
                   sel_type < (int)(sizeof k_bubble_b_guild / sizeof *k_bubble_b_guild)) {
            text = k_bubble_b_guild[sel_type];
        } else {
            text = "";
        }
        uint32_t text_color = ((uint32_t)(tag_alpha > 0 ? tag_alpha : 0) << 24) | 0xffffffu;
        font_draw_text_box(d, 250.0f, 348.0f, text, text_color, 1.0f, s_menu.bob * 2);
    }
}

/* ─── the buy/sell qty-confirm overlay (FUN_00491de0) ────────────────────────
 * Drawn on top of the item window when the overlay is open (DAT_09642c50 > 0).
 * Everything renders under COLOROP=ADDSIGNED with grey-127 modulation (the same
 * neutral-pass the menu option list uses), reset to MODULATE at the end.
 *   - confirm box   savewindow.tga, src(0,0,512,128), centred-grow by slide/4.
 *   - title         " Buying     %s. Are you sure?" centred (320,200) scale 0.8
 *                   (the qty number tucks into the title's space gap).
 *   - qty "%2d"     right-aligned at (292 - nameWidth/2, 196) scale 1.0.
 *   - price line    "Stock Price…%spix" (buy) at (112,240), total = qty·price.
 *   - Yes / No      (380,240) / (472,240) scale 1.0.
 *   - up/down arrows item_win.tga, stacked at x = 272 - nameWidth/2, only while
 *                   qty < max (up) / qty > 1 (down), with the press-bob offset. */
static void scene_guild_qty_overlay_render(IDirect3DDevice8 *d)
{
    if (s_menu.ov_slide <= 0)
        return;

    const float frac  = (float)s_menu.ov_slide / 4.0f;        /* open 0.25..1.0 */
    int alpha = (s_menu.ov_slide * 255) / 4;                  /* text fade-in    */
    if (alpha > 255) alpha = 255;
    const uint32_t tcol = ((uint32_t)alpha << 24) | 0x7f7f7fu; /* text grey       */

    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    /* confirm box (savewindow.tga) — grows from screen centre with the slide. */
    {
        const sprite_t *box = &g_sysassets.savewindow_tga;
        if (box->tex) {
            IDirect3DDevice8_SetTexture(d, 0, (IDirect3DBaseTexture8 *)box->tex);
            const float dst[4] = { 320.0f - frac * 256.0f, 224.0f - frac * 64.0f,
                                   frac * 512.0f, frac * 128.0f };
            const float src[4] = { 0.0f, 0.0f, 512.0f, 128.0f };
            render_quad_add(dst, src, box->width, box->height, 0xff7f7f7fu);
            render_quad_flush(d);
        }
    }

    /* item name (for the title + the qty-number placement). */
    const char *name = "";
    if (s_menu.item_rec >= 0 && s_menu.item_rec < g_item.count)
        name = g_item.records[s_menu.item_rec].singular;
    const int sel = s_menu.entries[s_menu.cursor];   /* 0 Buy / 1 Sell */

    /* title — " Buying     %s. Are you sure?" centred at (320,200). */
    {
        char title[160];
        snprintf(title, sizeof title,
                 sel == 0 ? " Buying     %s. Are you sure?"
                          : "Selling     %s. Are you sure?",
                 name);
        font_draw_text_centered(d, 320.0f, 200.0f, title, tcol, 0.8f);
    }

    /* quantity "%2d" — right-aligned in the title's space gap (292 - nameW/2),
     * in a PULSING green (engine asm 0x492008-0x49207f, the FPU amplitude Ghidra
     * dropped): wob = (int)(sin(c54·0.1)·-16); R=0x8f-wob, G=0xce-wob, B=0x8f-wob.
     * Under COLOROP=ADDSIGNED the glyph body stays white but its anti-aliased
     * edge throbs green ±16/channel (~±6% brightness, ~63-frame period).  c54
     * (price_anim) resets to 0 on overlay open + ticks each frame, so the pulse
     * phase aligns with retail (like the gold roll). */
    const float half_name = font_measure_text(d, name, 0.8f) * 0.5f;
    {
        char q[8];
        snprintf(q, sizeof q, "%2d", s_menu.qty);
        int wob = (int)(sinf((float)s_menu.price_anim * 0.1f) * -16.0f);
        const uint32_t qcol = ((uint32_t)alpha << 24)
                            | (uint32_t)((0x8f - wob) & 0xff) << 16
                            | (uint32_t)((0xce - wob) & 0xff) << 8
                            | (uint32_t)((0x8f - wob) & 0xff);
        font_draw_text_right(d, 292.0f - half_name, 196.0f, q, qcol, 1.0f);
    }

    /* price line — "Stock Price…%spix" (buy) with the comma-formatted total. */
    {
        int total = s_menu.qty * s_menu.unit_price;
        char num[32], line[80];
        if (total < 1000)
            snprintf(num, sizeof num, "%d", total);
        else if (total < 1000000)
            snprintf(num, sizeof num, "%d,%03d", total / 1000, total % 1000);
        else
            snprintf(num, sizeof num, "%d,%03d,%03d",
                     total / 1000000, (total / 1000) % 1000, total % 1000);
        /* full-width spaces (SJIS 0x81 0x40) between the label and the number. */
        snprintf(line, sizeof line,
                 sel == 0 ? "Stock Price\x81@\x81@\x81@\x81@\x81@%spix"
                          : "Purchase Price\x81@\x81@\x81@\x81@\x81@%spix",
                 num);
        font_draw_text(d, 112.0f, 240.0f, line, tcol, 0.8f);
    }

    /* Yes / No labels (the engine's per-confirm flash dim is a sub-1/127 sine
     * step — imperceptible — so both stay grey-127). */
    font_draw_text(d, 380.0f, 240.0f, "Yes", tcol, 1.0f);
    font_draw_text(d, 472.0f, 240.0f, "No",  tcol, 1.0f);

    /* up / down quantity arrows (item_win.tga) — only when adjustable. */
    {
        const sprite_t *win = &g_sysassets.item_win_tga;
        if (win->tex) {
            IDirect3DDevice8_SetTexture(d, 0, (IDirect3DBaseTexture8 *)win->tex);
            const float ax = 272.0f - half_name;
            if (s_menu.qty < s_menu.max_qty) {            /* up arrow */
                const float dst[4] = { ax, 178.0f - (float)s_menu.up_bob, 24.0f, 16.0f };
                const float src[4] = { 768.0f, 176.0f, 792.0f, 192.0f };
                render_quad_add(dst, src, win->width, win->height, 0xff7f7f7fu);
            }
            if (s_menu.qty > 1) {                          /* down arrow */
                const float dst[4] = { ax, 220.0f + (float)s_menu.dn_bob, 24.0f, 16.0f };
                const float src[4] = { 768.0f, 192.0f, 792.0f, 208.0f };
                render_quad_add(dst, src, win->width, win->height, 0xff7f7f7fu);
            }
            render_quad_flush(d);
        }
    }

    IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

/* worker_load case-6 body — the FUN_00473769 texture load.  Runs on the load worker; serialized before
 * the first mode-6 sim tick by the worker_load_busy() gate in sim_step_a. */
static void scene_guild_load_cb(void)
{
    /* FUN_0049174e ran at the world-map exit (FUN_00490e16); the loader only
     * brings in the textures. */

    /* PORT-DEBT(variant-1 ichiba): only the guild variant (DAT_0963c5f0 == 0,
     * world-map dest 3) loads its texture set; the variant-1 (ichiba) set
     * (01recette_04/02tear_01 + ichiba/ichiba2 bg) is not yet wired — dest 1 is
     * not exercised by the merchant's-guild arc. */
    if (g_scene_guild_dev == NULL || s_variant != 0)
        return;
    for (int i = 0; i < SCENE_GUILD_TEX_COUNT; i++) {
        sprite_load(g_scene_guild_dev, g_scene_guild_assets[i].path,
                    g_scene_guild_assets[i].w, g_scene_guild_assets[i].h,
                    &g_scene_guild[i]);
    }
}

void scene_guild_init(struct IDirect3DDevice8 *dev)
{
    g_scene_guild_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_cb(6, scene_guild_load_cb);
}

void scene_guild_render(struct IDirect3DDevice8 *dev)
{
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;

    /* FUN_00490e35 → FUN_0049b425 (2D state preset; FUN_00494a73 re-calls it at
     * its top — an idempotent no-op the engine duplicates, collapsed here). */
    render_quad_state_setup(d);

    /* FUN_00494a73 normal path (DAT_09642c3c == 0).  The mid-transition path
     * (single bg blit @ alpha 0xff000000) is PORT-DEBT — not exercised by the
     * first-visit cutscene. */

    /* Retail's FUN_004547ab skips the WHOLE mode-6 scene block (FUN_00490e35 →
     * FUN_00494a73, the guild bg + guildmaster + menu) when a full-screen-bg
     * conversation covers the screen — `DAT_0438b1c8 && FUN_0046c869()`
     * (== scene1_intro_dialogue_covers_screen(), the gate main.c already uses
     * for the INGAME HUD/scene).  A full conversation (first-visit cutscene /
     * Talk topic) paints its OWN full-screen bg_guild and draws the guildmaster
     * as a standee, so drawing the guild bg + keeper underneath is a pure
     * double-draw (the conversation's opaque bg re-paints the same texture over
     * them and the keeper is fully overdrawn) — pixel-invisible but a render-
     * program divergence retail does not have (the conversation also lays a
     * clear-to-black base first, tex 9fd8 — see merchant-guild-RE.md "render-
     * program drill").  The menu UI below is already gated on the dialogue
     * state; gate the bg + keeper the same way.  An OVERLAY dialogue with no bg
     * (the iv1_9 try-leave reminder, n_bg == 0) leaves covers_screen() false, so
     * the bg + keeper stay the menu backdrop. */
    const int conv_owns_bg = scene1_intro_dialogue_covers_screen();

    /* slot0: full-screen bg — dst (0,0,640,480), src top-left 640x480 of the
     * 1024x512 bmp (all.c:96191-96201). */
    if (!conv_owns_bg && g_scene_guild[SCENE_GUILD_TEX_BG].tex) {
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_bind(d, &g_scene_guild[SCENE_GUILD_TEX_BG]);
        render_quad_add(dst, src,
                        g_scene_guild[SCENE_GUILD_TEX_BG].width,
                        g_scene_guild[SCENE_GUILD_TEX_BG].height,
                        0xffffffffu);
        render_quad_flush(d);
    }

    /* slot1: the guildmaster (13syounin_01), drawn H-MIRRORED (FUN_00404e61)
     * into dst (-64,32,448,448) from the full 512x512 sprite (all.c:96217-96229,
     * guild variant — DAT_0963c5f0 == 0). */
    if (!conv_owns_bg && s_variant == 0 && g_scene_guild[SCENE_GUILD_TEX_KEEPER].tex) {
        const float dst[4] = { -64.0f, 32.0f, 448.0f, 448.0f };
        const float src[4] = { 0.0f, 0.0f, 512.0f, 512.0f };
        render_quad_bind(d, &g_scene_guild[SCENE_GUILD_TEX_KEEPER]);
        render_quad_add_mirrored(dst, src,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].width,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].height,
                        0xffffffffu);
        render_quad_flush(d);
    }

    /* The menu UI keeps drawing whenever the menu is actually UP, even while a
     * dialogue overlays it — retail renders the frozen menu behind the iv1_9
     * "you haven't purchased anything" reminder (the try-leave-without-buying
     * dialogue fires FROM the main menu, which stays visible behind the Tear
     * box; verified vs retail).  The first-visit cutscene is the exception: it
     * fires at entry_tick==2 before the menu is up, so retail shows no menu —
     * gate that out with `entry_tick > 0xe` (the same "menu settled" threshold
     * the nav uses).  So: draw when NOT busy (normal), OR when busy but the main
     * menu was up (mode 1 + settled).  mode-2 Talk-topic dialogues stay hidden
     * (unchanged — that path was confirmed 1:1).  bg + guildmaster above always
     * draw (they coincide with retail's cutscene-path bg + standee). */
    if (!scene1_intro_dialogue_busy() ||
        (s_menu.mode == 1 && s_menu.entry_tick > 0xe)) {
        scene_guild_menu_render(d);                /* FUN_0049404b — menu panel    */
        display_menu_render(d, 0.0f);              /* FUN_0046b00a — item window
                                                    * (Buy/Sell list; no-op while
                                                    * the slide counter is 0).     */
        /* FUN_0043537e (secondary banner) is a no-op at rest. */
        scene_guild_qty_overlay_render(d);         /* FUN_00491de0 — buy/sell confirm */
        title_save_dialog_cursor_render(d);        /* FUN_00435747 — hand cursor */
        title_save_dialog_render(d);               /* FUN_00435117 — message box */
    }
}

#endif /* _WIN32 */
