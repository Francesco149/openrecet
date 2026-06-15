/*
 * encyclopedia.h — the pause-menu ENCYCLOPEDIA submenu (entry type 6).
 *
 * The item catalog (図鑑): a horizontal carousel of item CATEGORIES, each a
 * 3-column grid of item cells (icon + name) with a 3-row visible window +
 * vertical scroll, a per-screen completion-rate panel, a bottom description
 * panel for the cursor's item, and (A-press) an item-detail overlay.
 *
 * Engine functions ported here:
 *   FUN_0049f012 (0x49f012, 851B) → encyclopedia_setup        — build the catalog
 *   FUN_0049efb8 (0x49efb8,  90B) → encyclopedia_cursor_recompute
 *   FUN_0049f365 (0x49f365,1363B) → encyclopedia_update       — nav state machine
 *   FUN_0049f8b8 (0x49f8b8,2033B) → encyclopedia_render       — the catalog draw
 *   FUN_0046a336 (0x46a336,2722B) → encyclopedia_detail_render — item-detail overlay
 *
 * Dispatched from the pause menu (scene_pause.c):
 *   - pause_menu_setup       → encyclopedia_setup(0)           (engine L81616)
 *   - nav-commit type 6      → open the submenu + cursor snap  (FUN_00480614)
 *   - pause_menu_update      → encyclopedia_update() @ sub_anim==10  (FUN_0047fa76 L82045)
 *   - pause_menu_render      → encyclopedia_render() @ sub_anim>0    (FUN_004820ba L83937)
 *
 * Data model (engine BSS, exposed for host tests):
 *   slot table  g_enc_slot[100][60]  (short; -1 = no slot, -2 = undiscovered,
 *                                      >=0 = the catalog item_id)   DAT_09643698
 *   index table g_enc_index[100][60] (int; per-category sorted display order)  DAT_09646650
 *   cat keys    g_enc_cat_key[100]   (short; per-category header item_id)      DAT_09646578
 */
#ifndef OPENRECET_ENCYCLOPEDIA_H
#define OPENRECET_ENCYCLOPEDIA_H

#include <stdint.h>

#ifdef _WIN32
struct IDirect3DDevice8;
#endif

/* 100 categories × 60 cells (0x14 rows × 3 cols) per category. */
#define ENC_CAT_MAX   100
#define ENC_CAT_CELLS 0x3c   /* 60 = 20 rows × 3 cols */

/* ── Catalog data model (engine BSS; exposed for host tests) ── */
extern int16_t g_enc_slot[ENC_CAT_MAX * ENC_CAT_CELLS];    /* DAT_09643698 */
extern int32_t g_enc_index[ENC_CAT_MAX * ENC_CAT_CELLS];   /* DAT_09646650 */
extern int16_t g_enc_cat_key[ENC_CAT_MAX];                 /* DAT_09646578 */

/* ── Control state ── */
extern int32_t g_enc_detail;       /* DAT_0964c434 — 0 grid / 1 detail overlay open */
extern int32_t g_enc_col;          /* DAT_0964c430 — cursor column 0..2 */
extern int32_t g_enc_row;          /* DAT_0964c410 — cursor row */
extern int32_t g_enc_anim;         /* DAT_0964c424 — category-switch slide (±10 commits) */
extern int32_t g_enc_cat;          /* DAT_0964c428 — current category index */
extern int32_t g_enc_cat_count;    /* DAT_0964c42c — populated category count */
extern int32_t g_enc_comp_num;     /* DAT_0964c420 — completion numerator (discovered) */
extern int32_t g_enc_comp_den;     /* DAT_09643688 — completion denominator (catalog total) */
extern int32_t g_enc_scroll;       /* DAT_09646640 — top visible row */
extern int32_t g_enc_rows_cur;     /* DAT_09643690 — current category row count */
extern int32_t g_enc_rows_alt;     /* DAT_09643694 — next-category row count (L/R entry clamp) */
extern int32_t g_enc_rows_prev;    /* DAT_0964368c — previous-category row count */

/*
 * FUN_0049f012 — build the catalog from the active save's discovery store +
 * the item DB.  `all_banks`=0 → the active working bank only (the pause path);
 * !=0 → aggregate every bank.  Returns 1 iff 100% discovered (num==den).
 */
int  encyclopedia_setup(int all_banks);

/* FUN_0049efb8 — slide the shared hand cursor to the current (col,row-scroll). */
void encyclopedia_cursor_recompute(void);

/*
 * FUN_0049f365 — the per-frame update / navigation.  Returns 1 when the
 * submenu should CLOSE (B pressed); 0 otherwise.  Reads the pause input
 * fields (pressed g_sim_buttons[0].pressed / held .held).
 */
int  encyclopedia_update(void);

#ifdef _WIN32
/* FUN_0049f8b8 — render the catalog at submenu slide offset (px,py). */
void encyclopedia_render(struct IDirect3DDevice8 *dev, float px, float py);

/* FUN_0046a336 — the A-press item-detail overlay (gated on g_enc_detail). */
void encyclopedia_detail_render(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_ENCYCLOPEDIA_H */
