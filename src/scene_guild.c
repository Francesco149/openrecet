/*
 * scene_guild.c — see scene_guild.h.
 *
 * Engine sources:
 *   FUN_004922c0 @ 0x4922c0  — per-location event tick.  Ported here: the
 *     entry-tick counter (DAT_09642c38) + the first-visit cutscene branch
 *     (all.c:94764-94775) + the RESTING menu-state counter update
 *     (all.c:94811-94833: the guildmaster-bubble ramp DAT_09642c40, the
 *     bob/text-budget DAT_09642c48, the bubble-text-variant timer
 *     DAT_09642c4c).  The full interactive state machine (cursor nav, the
 *     buy/sell/talk submenus, store expansion — all.c:94885+) is
 *     PORT-DEBT(guild-menu-nav), deferred to the buy-flow trace; the
 *     fade gate (FUN_00434d6a), daily-event probe (FUN_0045de68) and the
 *     group-6 follow-on cutscenes remain PORT-DEBT too.
 *   FUN_0049174e @ 0x49174e  — scene-init: resets the menu state, builds
 *     the option table (FUN_004918b0) and snaps the hand cursor.
 *   FUN_004918b0 @ 0x4918b0  — builds the option-type table _DAT_09640624
 *     + count DAT_005cfab4 (guild fresh-visit → {Buy,Sell,Talk,Leave}).
 *   FUN_00494a73 @ 0x494a73  — the 2D bg blit (guild variant 0) + menu UI.
 *   FUN_0049404b @ 0x49404b  — the menu panel + option list + guildmaster
 *     speech bubble (the FUN_00494a73 tail).
 *   FUN_00473769 @ 0x473769  — the texture-group-7 load (guild variant 0).
 *
 * The menu is gated on `!scene1_intro_dialogue_busy()`: retail's call-trace
 * shows FUN_004922c0 + FUN_0049404b run only BEFORE the first-visit cutscene
 * arms and AFTER it completes — never during (the dialogue takes over the
 * mode-6 update/render).  So the counters freeze through the cutscene and the
 * menu pops in once it ends, exactly as retail.
 *
 * The first-visit cutscene is iv1_3.ivt, armed through the shared dialogue
 * runtime (scene1_intro_dialogue_start_single(1,3) = port of FUN_0044ba2c(1,3,1)
 * → FUN_00452d07).  See docs/findings/merchant-guild-RE.md.
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

/* ─── working-arena field offsets (base DAT_044e3798, per-slot) ──────────────
 * The port pins the engine's per-location stage index (DAT_0438b1e0) to the
 * active save slot (save_work_active_slot) — see docs/findings/merchant-guild-RE.md
 * and the worldmap/tutorial siblings that share this base+scheme. */
#define GUILD_FIRSTVISIT_OFF  0x2bc5c   /* DAT_0450f3f4 — guild first-visit seen flag */
#define GUILD_TALKSEEN_OFF    0x2bc98   /* 6 talk-dialogue-seen bytes (the "New" badge) */
/* Slot-0-pinned dword indices into the working bank (DAT_0450fb98 / DAT_04510578
 * fall inside slot 0's 0x2dfc8-byte bank — PORT-DEBT(loc-routing), slot 0 only). */
#define GUILD_STORE_LEVEL_DWORD  0xb100   /* DAT_0450fb98 — store/merchant level */
#define GUILD_PERIOD_DWORD       0xb378   /* DAT_04510578 — time-of-day period   */
#define GUILD_RESTRICTED_OFF     0x2bc49  /* DAT_0450f3e1 — early-game fixed-stock
                                           * flag; while set, gold is force-pinned
                                           * to 10,000,000 (tutorial infinite money,
                                           * engine FUN_004922c0 all.c:94756-94758). */
#define GUILD_TUTORIAL_GOLD      10000000u /* DAT_00989680 (the pinned value)        */

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
        period      = (int)bank[GUILD_PERIOD_DWORD];        /* DAT_04510578 */
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

/* ─── scene-init menu reset (FUN_0049174e relevant subset) ──────────────────
 * Called by the worker-load cb on scene entry.  Resets the menu to its open
 * resting state + snaps the hand cursor onto the top option. */
void scene_guild_enter_reset(void)
{
    s_menu.entry_tick  = 0;          /* DAT_09642c38 = 0 */
    s_menu.mode        = 1;          /* DAT_09642c00 = 1 (menu open) */
    s_menu.cursor      = 0;          /* DAT_09642c04 = 0 */
    s_menu.scroll      = 0;          /* DAT_09642c08 = 0 */
    s_menu.bubble      = 0;          /* DAT_09642c40 = 0 */
    s_menu.bob         = 0;          /* DAT_09642c48 = 0 */
    s_menu.text_timer  = 0;          /* DAT_09642c4c = 0 */
    s_menu.sub_anim    = 0;          /* DAT_09642c1c = 0 */
    s_menu.scroll_anim = 0;          /* DAT_09642c20 = 0 */
    s_menu.c24         = 0;          /* DAT_09642c24 = 0 */
    s_menu.item_cursor = 0;          /* DAT_09642c10 = 0 */
    s_menu.item_scroll = 0;          /* DAT_09642c0c = 0 */
    s_menu.c14         = 0;          /* DAT_09642c14 = 0 */
    s_menu.c18         = 0;          /* DAT_09642c18 = 0 */
    s_menu.transition  = 0;          /* DAT_09642c3c = 0 */
    s_menu.ov_slide    = 0;          /* DAT_09642c50 = 0 — overlay closed */
    s_menu.yn_cursor   = 0;
    s_menu.flash_ctr   = 0;
    s_menu.price_anim  = 0;
    s_menu.up_bob      = 0;
    s_menu.dn_bob      = 0;
    s_menu.qty         = 0;
    s_menu.max_qty     = 0;
    s_menu.unit_price  = 0;
    s_menu.item_rec    = 0;

    scene_guild_build_table();       /* FUN_004918b0 */

    /* the shared item window starts closed: zero the slide counter so
     * display_menu_render is a no-op until a Buy/Sell opens it (a stale house
     * display-stand slide must not bleed into the resting guild menu). */
    stage_load_pulse_reset();

    /* FUN_0043561a + FUN_00435693(0x43a40000, cursor*0x22 + 84.0): raise +
     * snap the shared hand cursor onto the top option (328, 84). */
    title_save_dialog_cursor_set_visible(1);
    title_save_dialog_cursor_snap(328.0f, (float)(s_menu.cursor * 0x22) + 84.0f);
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

/* FUN_004922c0 return-3 (A-edge, all.c:95258-95320): set the highlighted item's
 * preview unit price (DAT_09642c60) + confirm/error SE.  Buy = base·0.7,
 * Sell = base·0.3 (PORT-DEBT(price-trend FUN_004361b2): market factor = 1.0). */
static void guild_buy_price_preview(int sel)
{
    int id = display_menu_selected();
    if (id == -1)
        return;
    int rec = tables_item_find_slot_by_id(&g_item, id >> 6);
    if (rec < 0)
        return;
    const float mult = (sel == 0) ? 0.7f : 0.3f;
    if (sel == 0) {                                /* Buy */
        const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        if (display_menu_owned_count(bank) < 15000) {
            s_menu.unit_price = (int)((float)g_item.records[rec].price * mult);
            if (display_menu_stock_cap() != 0 && s_menu.unit_price <= guild_gold()) {
                audio_play_se_by_id(0x143);
                return;
            }
        }
        audio_play_se_by_id(0x16a);
    } else {                                       /* Sell */
        s_menu.unit_price = (int)((float)g_item.records[rec].price * mult);
        s_menu.max_qty    = display_menu_stock_cap();
        audio_play_se_by_id(0x143);
    }
}

/* FUN_004922c0 return-1 (countdown done, all.c:95322-95389): cap the quantity
 * and open the mode-8 qty overlay.  Buy path only (Sell qty-cap is the simpler
 * stock cap, set in the preview). */
static void guild_buy_open_qty_overlay(int sel)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    int owned = display_menu_owned_count(bank);
    if (owned > 14999 && sel == 0)                 /* PORT-DEBT(item-limit msg) */
        return;
    /* PORT-DEBT(first-buy tutorial FUN_0044ba2c(1,0xf,0)): owns>9 & flag unset →
     * a tutorial dialogue; not reached by this early-visit trace. */
    int id = display_menu_selected();
    if (id == -1)
        return;
    int rec = tables_item_find_slot_by_id(&g_item, id >> 6);
    if (rec < 0)
        return;
    if (sel == 0) {                                /* Buy: clamp the qty cap */
        int gold = guild_gold();
        int max  = (s_menu.unit_price > 0) ? gold / s_menu.unit_price : 99;
        if (max > 99) max = 99;
        if (owned + max > 14999) max = 15000 - owned;
        int stock = display_menu_stock_cap();
        if (stock < max) max = stock;
        if (max > 99) max = 99;
        /* PORT-DEBT(first-sale 0xb-level cap, all.c:95354): skipped — the trace
         * qty (≤2) is well under it. */
        s_menu.max_qty = max;
        if (display_menu_stock_cap() == 0)         /* out of stock */
            return;
        if (gold < s_menu.unit_price)              /* can't afford one */
            return;
    }
    s_menu.qty        = 1;                          /* DAT_09642c5c */
    s_menu.item_rec   = rec;                        /* DAT_09642c58 */
    s_menu.mode       = 8;                          /* DAT_09642c00 = 8 */
    s_menu.ov_slide   = 1;                          /* DAT_09642c50 = 1 */
    s_menu.yn_cursor  = 0;                          /* DAT_09640600 = 0 (Yes) */
    s_menu.flash_ctr  = 0;                          /* DAT_096405fc */
    s_menu.price_anim = 0;                          /* _DAT_09642c54 */
    s_menu.up_bob     = 0;                          /* DAT_09642c64 */
    s_menu.dn_bob     = 0;                          /* DAT_09642c68 */
    title_save_dialog_cursor_snap(340.0f, 252.0f); /* FUN_00435693 — Yes position */
}

/* mode-8 confirm tail (all.c:95543-95566): add `qty` of the item to inventory,
 * deduct the cost, decrement the row stock + daily limit, play the purchase SE. */
static void guild_buy_commit(void)
{
    int sel = s_menu.entries[s_menu.cursor];
    if (sel != 0)                                   /* Sell path is PORT-DEBT */
        return;
    int id = display_menu_selected();
    if (id == -1)
        return;
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    int total = s_menu.qty * s_menu.unit_price;
    if (total > guild_gold())                       /* can't afford the batch */
        return;
    for (int i = 0; i < s_menu.qty; i++) {
        display_menu_inventory_return(bank, id);    /* FUN_00468d22 */
        display_menu_buy_post_add(bank);            /* FUN_00469a00 */
    }
    bank[SAVE_BANK_FIELD_GOLD] = (uint32_t)(guild_gold() - total);  /* gold -= cost */
    audio_play_se_by_id(0x14d);                     /* purchase SE */
    /* PORT-DEBT(first-buy save flags 0450f3f5/3f9): tutorial bookkeeping. */
}

/* ─── pure-C event tick (FUN_00490e24 → FUN_004922c0) ──────────────────────── */

void scene_guild_sim(void)
{
    /* FUN_00490e24: the gate FUN_0044c7b8() is a `return 0` stub, so the event
     * tick FUN_004922c0 always runs — EXCEPT the engine's mode-6 dispatch does
     * not call it while the first-visit dialogue is active (retail call-trace:
     * FUN_004922c0 fires only on the 2 pre-cutscene ticks + post-cutscene,
     * never during).  Model that by freezing the whole tick while busy, so the
     * menu counters resume from their pre-cutscene values when it ends. */
    if (scene1_intro_dialogue_busy())
        return;

    /* FUN_004922c0 top (all.c:94752): the entry-tick counter. */
    s_menu.entry_tick++;

    /* Tutorial infinite money (all.c:94756-94758): while the early-game
     * restricted-stock flag is set, gold is force-pinned to 10,000,000 every
     * frame.  So the buy flow's qty cap comes from the per-item stock ("N
     * Left"), NOT affordability, and the displayed gold never drops.  (The HUD
     * mirror was synced from this bank dword at scene entry, so leaving it is
     * correct — it stays at the pinned value, matching retail.) */
    {
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        if (bank != NULL &&
            ((const uint8_t *)bank)[GUILD_RESTRICTED_OFF] != 0)
            bank[SAVE_BANK_FIELD_GOLD] = GUILD_TUTORIAL_GOLD;
    }

    /* First-visit branch (all.c:94764-94775), gated on the 2nd entry tick. */
    if (s_menu.entry_tick == 2) {
        /* Guard (&DAT_045114fc)[loc] != 2 (not a dungeon) is structurally
         * always-true for mode 6 — not read literally (PORT-DEBT(loc-routing),
         * the 0xb7f2 stride is unreliable under slot pinning). */
        uint32_t *bank = save_work_dwords_at(save_work_active_slot());
        if (bank != NULL) {
            uint8_t *bb = (uint8_t *)bank;
            /* (&DAT_0450f3f4)[loc] == 0 — first-visit unseen: mark seen (once,
             * persists) + arm the iv1_3 cutscene, then return (busy next tick). */
            if (bb[GUILD_FIRSTVISIT_OFF] == 0) {
                bb[GUILD_FIRSTVISIT_OFF] = 1;
                scene1_intro_dialogue_start_single(1, 3);  /* FUN_0044ba2c(1,3,1) */
                return;
            }
        }
        /* PORT-DEBT(guild-events): the daily-event probe (FUN_0045de68) and the
         * group-6 follow-on cutscenes (all.c:94776-94808) are not ported. */
    }

    /* ── RESTING menu-state counter update (FUN_004922c0 94811-94833) ──────────
     * The full input/navigation state machine (all.c:94834+) is
     * PORT-DEBT(guild-menu-nav) — this trace drives no menu input, so only the
     * idle counters matter. */
    int sel_type = s_menu.entries[s_menu.cursor];

    /* Guildmaster bubble ramp: pop in while the menu is settled (or the
     * Expansion option is selected); pop out otherwise. */
    if (sel_type == 6 ||
        (s_menu.mode != 0 && s_menu.scroll_anim == 0 &&
         s_menu.entry_tick > 0xe && s_menu.c24 == 0)) {
        if (s_menu.bubble < 0xf)
            s_menu.bubble++;
    } else if (s_menu.bubble > 0) {
        s_menu.bubble--;
    }

    /* Bob / text-reveal budget runs only while the bubble is fully open. */
    if (s_menu.bubble == 0xf)
        s_menu.bob++;
    else
        s_menu.bob = 0;

    /* Bubble-text-variant timer: ≥0x78 switches "Before you stock up…" →
     * "Time to stock up a bit, eh?".  (Engine also force-sets 0x78 on menu
     * input via _DAT_073dddd4 & 0xc0000 — no input modeled at rest.) */
    s_menu.text_timer++;
    /* Holding a direction snaps the bubble text to variant B (94830:
     * _DAT_073dddd4 & 0xc0000 = held up|down). */
    if ((g_sim_buttons[0].held & 0x0cu) != 0)
        s_menu.text_timer = 0x78;

    s_menu.transition = 0;           /* DAT_09642c3c = 0 */

    /* ── main-menu cursor navigation (FUN_004922c0 95074-95084) ───────────────
     * Step 1 of the buy-flow nav: Up/Down move the option cursor (wrap + nav SE
     * + the 6-frame cursor slide).  Deliberately NOT the A-press dispatch: a Buy/
     * Sell A-press starts the submenu slide-in that slides the main panel OUT,
     * and without the item-window render sliding IN behind it the menu blanks
     * (a parity dip vs retail's list) — so the slide-in + mode-0 item list + the
     * qty overlay land together as step 2 (with FUN_00468338's guild-stock
     * population + render).  See merchant-guild-RE.md "BUY FLOW".
     *
     * Input = engine _DAT_073dddd4 = pressed(low16) | held(high16): actions A/B
     * are pressed & 0x10/0x20 (edge), directions Up/Down are held & 0x04/0x08
     * (auto-repeat — the engine's high-word 0x40000/0x80000 bits). */
    const uint16_t pressed = g_sim_buttons[0].pressed;
    const uint16_t held    = g_sim_buttons[0].held;
    int cursor_moved = 0;

    if (s_menu.mode == 1 && s_menu.entry_tick > 0xe) {
        if (s_menu.c18 == 1) {
            /* ── slide-OUT (item list backed out → main menu, 95060-95067) ──────
             * c20/c24 ramp back down from ~0x19; below 0x10 the panel is home and
             * the slide-out flag clears. */
            s_menu.scroll_anim = s_menu.c24 - 1;         /* c20 = c24 - 1 */
            s_menu.c24 = s_menu.scroll_anim;             /* c24 = c20 */
            if (s_menu.scroll_anim < 0x10) {
                s_menu.c24 = 0;
                s_menu.c18 = 0;
                s_menu.scroll_anim = 0;
            }
        } else if (s_menu.c24 < 1) {
            /* ── resting main menu (DAT_09642c24 == 0) ── cursor nav + A-dispatch.
             * Gated on no sub-anim + no B (95070-95073). */
            if (s_menu.sub_anim < 1 && (pressed & 0x20u) == 0) {
                if ((pressed & 0x10u) == 0) {
                    /* A not pressed → Up/Down option-cursor nav (95074-95084). */
                    int dir = (held & 0x04u) ? -1 : (held & 0x08u) ? 1 : 0;
                    if (dir != 0 && s_menu.count > 0) {
                        s_menu.cursor = (s_menu.count + dir + s_menu.cursor) % s_menu.count;
                        audio_play_se_by_id(0x146);      /* FUN_00499519(0x146) nav SE */
                        cursor_moved = 1;
                    }
                } else {
                    /* A pressed → dispatch the selected option (95086-95105). */
                    int sel = s_menu.entries[s_menu.cursor];
                    if (sel == 0 || sel == 1) {
                        /* Buy / Sell → start the item-window submenu slide-in. */
                        s_menu.c24 = 1;                  /* DAT_09642c24 = 1 */
                        s_menu.scroll_anim = 1;          /* DAT_09642c20 = 1 */
                        s_menu.item_cursor = 0;          /* DAT_09642c10 = 0 */
                        s_menu.item_scroll = 0;          /* DAT_09642c0c = 0 */
                        audio_play_se_by_id(0x143);      /* SE 0x143 (select) */
                    } else if (sel == 2) {
                        /* Talk → DAT_09642c1c = 1 (the submenu open anim).  The
                         * Talk submenu itself is PORT-DEBT(guild-menu-nav). */
                        s_menu.sub_anim = 1;
                        s_menu.item_cursor = 0;
                        s_menu.item_scroll = 0;
                        audio_play_se_by_id(0x143);
                    }
                    /* Fusion(3) / Leave(4) / Expansion(6) dispatch — still
                     * PORT-DEBT(guild-menu-nav); not exercised by the buy trace. */
                }
            }
            /* B-press "leave" (95108+) — PORT-DEBT(guild-menu-nav). */
        } else {
            /* ── submenu slide-in ramp (DAT_09642c24 >= 1, 95132-95167) ──────────
             * c20/c24 count up to 0x19; at 0xf arm + populate the item window; at
             * 0x19 hand off to the mode-0 item list. */
            s_menu.scroll_anim = s_menu.c24 + 1;         /* c20 = c24 + 1 */
            s_menu.c24 = s_menu.scroll_anim;             /* c24 = c20 */
            int sel = s_menu.entries[s_menu.cursor];
            if (s_menu.scroll_anim == 0xf && sel != 6 && sel != 3) {
                /* FUN_004682c5 (slide-activate) + FUN_00468338(7=buy/5=sell,1)
                 * (population) + FUN_004682d8 (price multiplier).
                 * display_menu_open arms the stage_load_pulse slide itself. */
                if (sel == 0) {                          /* Buy */
                    display_menu_open(7, 1);
                    display_menu_set_price_mult(0.7f);   /* 0x3f333333 */
                } else if (sel == 1) {                   /* Sell */
                    display_menu_open(5, 1);
                    display_menu_set_price_mult(0.3f);   /* 0x3e99999a */
                }
            }
            if (sel != 6 && s_menu.c24 == 0x19)
                s_menu.mode = 0;                         /* → item list (DAT_09642c00=0) */
        }
    } else if (s_menu.mode == 0) {
        /* ── item list (FUN_004922c0 mode 0, all.c:95242-95389) ────────────────
         * Buy/Sell drive the SHARED item window; the main cursor still points at
         * the Buy/Sell option (sel) so we know the price multiplier and direction.
         * display_menu_update returns 3 on the A-edge (and arms its own 6-frame
         * confirm countdown), then 1 when it elapses, 2 on B, 0 while navigating. */
        int sel = s_menu.entries[s_menu.cursor];
        display_menu_set_price_mult(sel == 0 ? 0.7f : 0.3f);    /* FUN_004682d8 */
        int r = display_menu_update(1);                          /* FUN_00469414(1) */
        if (r == 2) {
            /* B → back to the main menu (95251-95256): arm the panel slide-out
             * and let the item-window slide retract. */
            s_menu.mode = 1;
            s_menu.c18  = 1;
            stage_load_pulse_set_active(0);                      /* FUN_004682d0 */
            audio_play_se_by_id(0x13d);
        } else if (r == 3) {
            guild_buy_price_preview(sel);                        /* A-edge: preview price */
        } else if (r == 1) {
            guild_buy_open_qty_overlay(sel);                     /* open the qty overlay */
        }
    } else if (s_menu.mode == 8) {
        /* ── qty-confirm overlay (FUN_004922c0 mode 8, all.c:95536-95589) ──────── */
        int r = guild_qty_overlay_input();                       /* FUN_00491bc0(0) */
        if (r == 1 || r == 2) {
            if (r == 1)
                guild_buy_commit();                              /* purchase */
            display_menu_cursor_to_row();                        /* FUN_0046939a */
            s_menu.mode = 0;                                     /* → item list */
            title_save_dialog_cursor_set_visible(1);            /* FUN_0043561a */
        }
        /* r == -1: still animating — stay in mode 8. */
    }

    /* cursor visible (FUN_0043561a at LAB_0049282c — every mode-1 frame, incl.
     * the slide-in) + slide-to-row on an option move (LAB_00493563:
     * FUN_00435710(328, (cursor−scroll)·0x22 + 84)).  display_menu_open snaps the
     * cursor onto the item row at slide frame 0xf. */
    if (s_menu.mode == 1)
        title_save_dialog_cursor_set_visible(1);
    if (cursor_moved) {
        float cy = (float)((s_menu.cursor - s_menu.scroll) * 0x22) + 84.0f;
        title_save_dialog_cursor_slide(328.0f, cy);
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
     * row text (all.c:95943 SetTextureStageState(0,COLOROP,8)). */
    if (s_menu.mode != 0 && s_menu.count > 0) {
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

        const float label_x = panel_x + 120.0f;            /* 376 at rest */
        for (int idx = s_menu.count - 1; idx >= 0; idx--) {
            int type = s_menu.entries[s_menu.scroll + idx];
            float y = (float)(idx * 0x22) + 64.0f;
            float scale;
            if (idx == s_menu.cursor) {
                scale = 1.0769231f;                        /* selected, larger */
            } else {
                scale = 0.8615385f;                        /* others, smaller */
                y += 2.0f;
            }
            /* Color = (alpha<<24)|0x7f7f7f (grey-127 RGB modulating the dark
             * font under ADDSIGNED; the selected-row brightness pulse is
             * PORT-DEBT — settled to full at rest). */
            if (type >= 0 && type < (int)(sizeof k_menu_labels / sizeof *k_menu_labels))
                font_draw_text(d, label_x, y + 12.0f, k_menu_labels[type],
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
                    font_draw_text(d, label_x - 12.0f, y + 8.0f, "New", ncol, 0.5f);
                }
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

    /* quantity "%2d" — right-aligned in the title's space gap (292 - nameW/2). */
    const float half_name = font_measure_text(d, name, 0.8f) * 0.5f;
    {
        char q[8];
        snprintf(q, sizeof q, "%2d", s_menu.qty);
        font_draw_text_right(d, 292.0f - half_name, 196.0f, q, tcol, 1.0f);
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

/* worker_load case-6 body — port of the scene-init FUN_0049174e's relevant
 * effects (reset the menu state + build the option table + snap the cursor) +
 * the FUN_00473769 texture load.  Runs on the load worker; serialized before
 * the first mode-6 sim tick by the worker_load_busy() gate in sim_step_a. */
static void scene_guild_load_cb(void)
{
    scene_guild_enter_reset();   /* FUN_0049174e: menu reset + table + cursor */

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

    /* slot0: full-screen bg — dst (0,0,640,480), src top-left 640x480 of the
     * 1024x512 bmp (all.c:96191-96201). */
    if (g_scene_guild[SCENE_GUILD_TEX_BG].tex) {
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
    if (s_variant == 0 && g_scene_guild[SCENE_GUILD_TEX_KEEPER].tex) {
        const float dst[4] = { -64.0f, 32.0f, 448.0f, 448.0f };
        const float src[4] = { 0.0f, 0.0f, 512.0f, 512.0f };
        render_quad_bind(d, &g_scene_guild[SCENE_GUILD_TEX_KEEPER]);
        render_quad_add_mirrored(dst, src,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].width,
                        g_scene_guild[SCENE_GUILD_TEX_KEEPER].height,
                        0xffffffffu);
        render_quad_flush(d);
    }

    /* The menu UI draws only when the first-visit cutscene is NOT active:
     * retail skips FUN_00494a73's menu tail (FUN_0049404b/cursor) entirely
     * while the dialogue runs (the menu is hidden behind it).  bg + guildmaster
     * above DO draw through the cutscene (they coincide with retail's
     * cutscene-path bg + guildmaster standee — confirmed 1:1). */
    if (!scene1_intro_dialogue_busy()) {
        scene_guild_menu_render(d);                /* FUN_0049404b — menu panel    */
        display_menu_render(d);                    /* FUN_0046b00a — item window
                                                    * (Buy/Sell list; no-op while
                                                    * the slide counter is 0).     */
        /* FUN_0043537e (secondary banner) is a no-op at rest. */
        scene_guild_qty_overlay_render(d);         /* FUN_00491de0 — buy/sell confirm */
        /* FUN_00435117 (dialog frame): no-op gate. */
        title_save_dialog_cursor_render(d);        /* FUN_00435747 — hand cursor */
    }
}

#endif /* _WIN32 */
