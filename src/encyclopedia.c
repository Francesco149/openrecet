/*
 * encyclopedia.c — the pause-menu ENCYCLOPEDIA submenu (entry type 6).
 *
 * 1:1 port of the engine's catalog (図鑑) functions; see encyclopedia.h for
 * the engine↔port map and the data model.  Transcribed from
 * docs/decompiled/all.c (FUN_0049f012/efb8/f365/f8b8/46a336) cross-checked
 * against objdump (FP constants + the packed-slot pointer arithmetic).
 *
 * Data model recap (the non-obvious part): the per-category slot table is
 * PACKED with the DISCOVERED items only — the engine's inner build loop
 * advances the slot write pointer ONLY on a discovered item, so undiscovered
 * catalog entries are transient (overwritten next iteration).  A category with
 * catalog items but ZERO discoveries keeps a single -2 placeholder at slot 0
 * (one empty "?" frame).  The index table is sorted (rank then id) and maps a
 * display cell → its packed slot.  Completion% = discovered / total-catalog.
 */
#include "encyclopedia.h"

#include <string.h>

#include "tables_item.h"   /* g_item / tables_item_find_slot_by_id (FUN_004681f6) */
#include "save_work.h"     /* save_work_active_slot / save_work_bank_at (DAT_0438b1e0) */
#include "save_bank.h"     /* SAVE_BANK_COUNT */
#include "audio.h"         /* audio_play_se_by_id (FUN_00499519) */
#include "sim.h"           /* g_sim_buttons[0].pressed/held (DAT_073dddd4/d6) */
#include "scene.h"         /* g_scene_state (DAT_0438b1c0) */

/* the shared hand cursor (FUN_0043561a/612/693/710) — pure-C setters. */
void title_save_dialog_cursor_set_visible(int on);
void title_save_dialog_cursor_snap(float x, float y);
void title_save_dialog_cursor_slide(float x, float y);

/* ── catalog data model (engine BSS) ── */
int16_t g_enc_slot[ENC_CAT_MAX * ENC_CAT_CELLS];    /* DAT_09643698 */
int32_t g_enc_index[ENC_CAT_MAX * ENC_CAT_CELLS];   /* DAT_09646650 */
int16_t g_enc_cat_key[ENC_CAT_MAX];                 /* DAT_09646578 */

int32_t g_enc_detail, g_enc_col, g_enc_row, g_enc_anim, g_enc_cat, g_enc_cat_count;
int32_t g_enc_comp_num, g_enc_comp_den, g_enc_scroll;
int32_t g_enc_rows_cur, g_enc_rows_alt, g_enc_rows_prev;

/* item-detail overlay state (DAT_0734b998 / DAT_0734b96c / DAT_005c6ee4).
 * The engine shares these with the shop tooltip (modeled as statics in
 * scene1_display_menu.c); the encyclopedia keeps its own copy — behaviorally
 * equivalent for the encyclopedia detail overlay (Phase 2 render). */
int32_t g_enc_tooltip_item = -1;   /* DAT_0734b998 — hovered item (id<<6) */
int32_t g_enc_tooltip_mode = 0;    /* DAT_0734b96c — overlay mode (0=closed) */
int32_t g_enc_tooltip_owned = 0;   /* DAT_005c6ee4 — owned count */

/* The discovery store: per working bank, 100 records × 0x12 dwords at bank
 * byte 0x279d8 (dword 0x9e76).  record[ci]: dword[0]=category key,
 * dword[1]=catalog count, byte[8+subindex]=discovered flag. */
#define ENC_DISC_BYTE  0x279d8
#define ENC_DISC_STRIDE 0x48

static uint8_t *enc_disc_rec(int bank_slot, int ci)
{
    uint8_t *b = save_work_bank_at(bank_slot);
    return b ? (b + ENC_DISC_BYTE + (size_t)ci * ENC_DISC_STRIDE) : 0;
}

/* FUN_0045526a — parallel bubble sort (keys ascending, co-permute idx). */
static void enc_sort(const int32_t *keys, int32_t *idx, int n)
{
    /* The engine sorts a LOCAL key buffer alongside the index slice; we keep
     * keys read-only by copying it (n <= 60). */
    int32_t k[ENC_CAT_CELLS];
    if (n > ENC_CAT_CELLS) n = ENC_CAT_CELLS;
    for (int i = 0; i < n; i++) k[i] = keys[i];
    for (int pass = 0; pass < n - 1; pass++) {
        for (int j = n - 1; j > pass; j--) {       /* bubble the min toward front */
            if (k[j] < k[j - 1]) {
                int32_t tk = k[j]; k[j] = k[j - 1]; k[j - 1] = tk;
                int32_t ti = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = ti;
            }
        }
    }
}

/* FUN_0049efb8 — slide the hand cursor to the current cell. */
void encyclopedia_cursor_recompute(void)
{
    title_save_dialog_cursor_slide(
        (float)g_enc_col * 149.33334f + 72.0f,
        (float)((g_enc_row - g_enc_scroll) * 0x58) + 112.0f);
}

/* FUN_0049f012 — build the catalog from the active bank's discovery store +
 * the item DB.  all_banks=0 → active bank only (the pause path). */
int encyclopedia_setup(int all_banks)
{
    g_enc_detail = 0;
    g_enc_col = 0;
    g_enc_anim = 0;
    g_enc_cat = 0;
    g_enc_row = 0;
    g_enc_scroll = 0;

    /* slot table: 3000 dwords of 0xffffffff = 6000 shorts of -1. */
    memset(g_enc_slot, 0xff, sizeof g_enc_slot);

    /* index table: 0..59 repeating per category. */
    for (int c = 0; c < ENC_CAT_MAX; c++)
        for (int k = 0; k < ENC_CAT_CELLS; k++)
            g_enc_index[c * ENC_CAT_CELLS + k] = k;

    const int n = g_item.count;   /* DAT_005c80ac */

    /* cat-key array: store each distinct category's first item_id (overwritten
     * per populated category in the main loop). */
    {
        int prev = -1, ki = -1;
        for (int i = 0; i < n; i++) {
            int catv = g_item.records[i].category;
            if (prev != catv) {
                ki++;
                if (ki >= 0 && ki < ENC_CAT_MAX)
                    g_enc_cat_key[ki] = (int16_t)g_item.records[i].item_id;
                prev = catv;
            }
        }
    }

    g_enc_cat_count = 0;
    g_enc_comp_den  = 0;
    g_enc_comp_num  = 0;

    /* build the discovery store key+count (zero key/count, preserve flags). */
    const int active = save_work_active_slot();   /* DAT_0438b1e0 */
    const int bank_lo = all_banks ? 0 : active;
    const int bank_hi = all_banks ? SAVE_BANK_COUNT : (active + 1);
    for (int b = bank_lo; b < bank_hi; b++) {
        /* zero key+count on all 100 records (flags untouched). */
        for (int ci = 0; ci < ENC_CAT_MAX; ci++) {
            uint8_t *r = enc_disc_rec(b, ci);
            if (!r) continue;
            ((int32_t *)r)[0] = 0;   /* key   */
            ((int32_t *)r)[1] = 0;   /* count */
        }
        /* re-scan the item DB → per distinct category set key + count++. */
        int prev = -1, ci = -1;
        for (int i = 0; i < n; i++) {
            int catv = g_item.records[i].category;
            if (prev != catv) { ci++; prev = catv; }
            if (ci >= 0 && ci < ENC_CAT_MAX) {
                uint8_t *r = enc_disc_rec(b, ci);
                if (r) { ((int32_t *)r)[0] = catv; ((int32_t *)r)[1] += 1; }
            }
        }
    }

    /* main population loop — one iteration per category index ci (0..99); the
     * slot/index/cat-key tables all advance by their category stride. */
    int32_t sortkeys[ENC_CAT_CELLS];
    for (int ci = 0; ci < ENC_CAT_MAX; ci++) {
        uint8_t *drec = enc_disc_rec(active, ci);
        const int32_t key   = drec ? ((int32_t *)drec)[0] : 0;
        const int32_t count = drec ? ((int32_t *)drec)[1] : 0;

        int16_t *slotbase  = &g_enc_slot[ci * ENC_CAT_CELLS];
        int32_t *idxbase   = &g_enc_index[ci * ENC_CAT_CELLS];

        if (count > 0) {
            int hdr = tables_item_find_slot_by_id(&g_item, key * 100);
            if (hdr >= 0)
                g_enc_cat_key[ci] = (int16_t)g_item.records[hdr].item_id;

            int placed = 0;                 /* local_1c */
            if (count != 1) {
                int16_t *wp = slotbase;     /* psVar10 — advances only on a hit */
                for (int it = 0; it != count - 1; it++) {
                    g_enc_comp_den++;
                    *wp = -2;               /* default this slot to undiscovered */

                    /* first sub-scan: subindex of the `it`-th record in `key`. */
                    int subindex = it;      /* puVar5 default = local_8 */
                    {
                        int seen = 0;
                        for (int j = 0; j < n; j++) {
                            if (g_item.records[j].category == key) {
                                if (seen == it) { subindex = g_item.records[j].subindex; break; }
                                seen++;
                            }
                        }
                    }

                    /* second sub-scan: the discovery flag. active-bank path only
                     * checks the active bank (all-banks ORs across all). */
                    int discovered = 0;
                    if (all_banks) {
                        for (int b = 0; b < SAVE_BANK_COUNT; b++) {
                            uint8_t *r = enc_disc_rec(b, ci);
                            if (r && r[8 + subindex]) { discovered = 1; break; }
                        }
                    } else {
                        discovered = drec && drec[8 + subindex];
                    }

                    if (discovered) {
                        int rec = tables_item_find_slot_by_id(&g_item, key * 100 + subindex + 1);
                        int16_t iid = (rec >= 0) ? (int16_t)g_item.records[rec].item_id : -1;
                        int32_t rank = (rec >= 0) ? g_item.records[rec].rank : 0;
                        *wp = iid;
                        sortkeys[placed] = (((int32_t)iid / 100) * 100 + 10 + rank) * 0xc80 + iid;
                        if (iid != -2) g_enc_comp_num++;
                        placed++;
                        wp++;               /* advance: the slot is taken */
                    } else if (placed > 0) {
                        *wp = -1;           /* empty trailer once items exist */
                    }
                }
            }
            enc_sort(sortkeys, idxbase, placed);
            g_enc_cat_count++;
        }
    }

    encyclopedia_cursor_recompute();
    return (g_enc_comp_num == g_enc_comp_den) ? 1 : 0;
}

/* ── FUN_0049f365 — the per-frame update / navigation ──────────────────────
 * Returns 1 to CLOSE the submenu (B pressed); 0 otherwise. */
int encyclopedia_update(void)
{
    const uint16_t pressed = g_sim_buttons[0].pressed;  /* DAT_073dddd4 */
    const uint16_t held    = g_sim_buttons[0].held;     /* DAT_073dddd6 */

    const int catn = g_enc_cat_count;   /* iVar5 */
    int cat        = g_enc_cat;         /* iVar6 */
    int recompute  = 0;                 /* bVar3 */

    if (pressed & 0x20u) {              /* B → close */
        audio_play_se_by_id(0x13d);
        return 1;
    }

    /* per-frame row-count precompute over prev/cur/next category. */
    int local_rows[3];                 /* clamped ≤3 cell count */
    int row_count[3];                  /* ceil(populated/3) rows */
    const int base = catn - 1 + cat;   /* iVar1 — prev category */
    for (int k = 0; k < 3; k++) {
        int catk = catn ? (base + k) % catn : 0;
        int clamped = 0, raw = 0;
        for (int s = 0; s < ENC_CAT_CELLS; s++) {
            if (g_enc_slot[catk * ENC_CAT_CELLS + s] != -1) {
                if (++clamped > 3) clamped = 3;
                raw++;
            }
        }
        local_rows[k] = clamped;
        row_count[k]  = (raw - 1) / 3 + 1;
    }
    /* the precompute writes (&DAT_0964368c)[0..2] = prev/cur/next row counts. */
    g_enc_rows_prev = row_count[0];    /* DAT_0964368c */
    g_enc_rows_cur  = row_count[1];    /* DAT_09643690 */
    g_enc_rows_alt  = row_count[2];    /* DAT_09643694 */

    /* category-switch animation in flight: tick only, no input read. */
    if (g_enc_anim != 0) {
        if (g_enc_anim > 0) g_enc_anim++;
        if (g_enc_anim < 0) g_enc_anim--;
        if (g_enc_anim == 10)  { g_enc_anim = 0; g_enc_cat = catn ? (base) % catn : 0; }
        if (g_enc_anim == -10) { g_enc_anim = 0; g_enc_cat = catn ? (cat + 1) % catn : 0; }
        return 0;
    }

    /* detail overlay open: A closes it. */
    if (g_enc_detail == 1) {
        if (pressed & 0x40u) {
            g_enc_tooltip_mode = 0;             /* FUN_004681d3 */
            audio_play_se_by_id(0x2c6);
            g_enc_detail = 0;
            title_save_dialog_cursor_set_visible(1);   /* FUN_0043561a */
        }
        return 0;
    }

    /* detail overlay open (A on a discovered cell). */
    if (pressed & 0x40u) {
        int disp  = g_enc_col + (g_enc_row + cat * 0x14) * 3;
        int sidx  = g_enc_index[disp + cat * ENC_CAT_CELLS];
        int16_t iid = g_enc_slot[sidx + cat * ENC_CAT_CELLS];
        if (iid >= 0) {
            g_enc_tooltip_item = iid << 6;      /* FUN_00468286 */
            g_enc_tooltip_mode = (g_scene_state == 9) ? 3 : 4;   /* FUN_004681db(2|3)→mode+1 */
            audio_play_se_by_id(0x2c6);
            g_enc_detail = 1;
            title_save_dialog_cursor_set_visible(0);   /* FUN_00435612 */
        }
        return 0;
    }

    /* ── row scroll (current category has >1 row) ── */
    if (g_enc_rows_cur > 1) {
        if (held & 4u) {                         /* UP */
            audio_play_se_by_id(0x146);          /* plays once on up-held (the && side effect) */
            cat = g_enc_cat;
            if (g_enc_row > 0) {
                audio_play_se_by_id(0x146);      /* …and again on an actual move */
                recompute = 1;
                g_enc_row = (g_enc_rows_cur - 1 + g_enc_row) % g_enc_rows_cur;
                cat = g_enc_cat;
                if (g_enc_row - g_enc_scroll < 0) g_enc_scroll--;
            }
        }
        int skip_fixup = 0;
        if ((held & 8u) == 0 || g_enc_rows_cur - 1 <= g_enc_row) {
            if (!recompute) skip_fixup = 1;      /* no move → goto column nav */
        } else {                                 /* DOWN */
            audio_play_se_by_id(0x146);
            recompute = 1;
            g_enc_row = (g_enc_rows_cur + 1 + g_enc_row) % g_enc_rows_cur;
            cat = g_enc_cat;
            if (g_enc_row - g_enc_scroll > 2) g_enc_scroll++;
        }
        if (!skip_fixup) {
            /* on the new row, pull the column left off any empty (-1) cells. */
            for (int i = 0; i < 3; i++) {
                int p = g_enc_col + (g_enc_row + cat * 0x14) * 3;
                if (g_enc_slot[p] == -1) g_enc_col--;
            }
        }
    }

    /* ── column / category-edge nav ── */
    if (held & 0x10u) {                          /* L-shoulder → prev category */
        audio_play_se_by_id(0x146);
        g_enc_anim--;
        g_enc_col = 0;
        if (g_enc_row > g_enc_rows_alt - 1) g_enc_row = g_enc_rows_alt - 1;
        if (g_enc_row > 2) g_enc_row = 2;
        g_enc_scroll = 0;
        encyclopedia_cursor_recompute();
    } else if (held & 0x80u) {                   /* R-shoulder → next category */
        audio_play_se_by_id(0x146);
        g_enc_anim++;
        g_enc_col = local_rows[0] - 1;
        int rb = (catn ? ((catn - 1 + cat) % catn) : 0) * 0x14;
        if (g_enc_slot[g_enc_col + (rb + g_enc_row) * 3] == -1) g_enc_row--;
        if (g_enc_slot[g_enc_col + (rb + g_enc_row) * 3] == -1) g_enc_row--;
        if (g_enc_row > g_enc_rows_prev - 1) g_enc_row = g_enc_rows_prev - 1;
        if (g_enc_row > 2) g_enc_row = 2;
        g_enc_scroll = 0;
        encyclopedia_cursor_recompute();
    } else if (held & 2u) {                      /* LEFT */
        audio_play_se_by_id(0x146);
        if (g_enc_col == 0) {                     /* at left edge → prev category */
            g_enc_anim++;
            g_enc_col = local_rows[0] - 1;
            int rb = (catn ? ((catn - 1 + cat) % catn) : 0) * 0x14;
            if (g_enc_slot[g_enc_col + (rb + g_enc_row) * 3] == -1) g_enc_row--;
            if (g_enc_slot[g_enc_col + (rb + g_enc_row) * 3] == -1) g_enc_row--;
            if (g_enc_row > g_enc_rows_prev - 1) g_enc_row = g_enc_rows_prev - 1;
            if (g_enc_row > 2) g_enc_row = 2;
            g_enc_scroll = 0;
            encyclopedia_cursor_recompute();
        } else {
            g_enc_col = (g_enc_col + 2) % 3;     /* col-- */
        }
        recompute = 1;
    } else if (held & 1u) {                      /* RIGHT */
        audio_play_se_by_id(0x146);
        if (g_enc_col == local_rows[1] - 1) {    /* at last populated col → next category */
            g_enc_anim--;
            g_enc_col = 0;
            if (g_enc_row > g_enc_rows_alt - 1) g_enc_row = g_enc_rows_alt - 1;
            if (g_enc_row > 2) g_enc_row = 2;
            g_enc_scroll = 0;
            encyclopedia_cursor_recompute();
        } else {
            g_enc_col = (g_enc_col + 4) % 3;     /* col++ */
            if (g_enc_slot[g_enc_col + (g_enc_row + g_enc_cat * 0x14) * 3] == -1) g_enc_row--;
        }
        recompute = 1;
    }

    /* tail: the engine writes two debug-text rows (FUN_00451874 5,0x17/0x18) that
     * are INVISIBLE in retail (DAT_06a49938 BSS-zero — debug overlay never drawn;
     * already stubbed in font.c) — omitted.  Then recompute on a within-cat move. */
    if (recompute) encyclopedia_cursor_recompute();
    return 0;
}

#ifdef _WIN32

#include "render_quad.h"   /* render_quad_bind/add/flush/state_setup */
#include "font_draw.h"     /* font_draw_text (FUN_0047ca05) / _centered (FUN_0047d14c) */
#include "sysassets.h"     /* g_sysassets.item_win_tga / data_win_tga / item_icons[] */
#include "scene_pause.h"   /* g_scene_pause_pause (DAT_073d86a8 pause.tga board) */
#include <stdio.h>

/* FUN_00469abb — comma-grouped number (the price). */
static void enc_format_number(char *out, size_t n, int v)
{
    if (v < 1000000) {
        if (v < 1000) snprintf(out, n, "%d", v);
        else          snprintf(out, n, "%d,%03d", v / 1000, v % 1000);
    } else {
        snprintf(out, n, "%d,%03d,%03d", v / 1000000, (v / 1000) % 1000, v % 1000);
    }
}

static void enc_quad(struct IDirect3DDevice8 *d, sprite_t *spr,
                     float dx, float dy, float dw, float dh,
                     float sl, float st, float sr, float sb)
{
    const float dst[4] = { dx, dy, dw, dh };
    const float src[4] = { sl, st, sr, sb };
    render_quad_bind(d, spr);
    render_quad_add(dst, src, spr->width, spr->height, 0xffffffffu);
    render_quad_flush(d);
}

/* FUN_0048edee — the up scroll arrow (item_win src(448,896,512,944)). */
static void enc_arrow_up(struct IDirect3DDevice8 *d, float x, float y)
{
    enc_quad(d, &g_sysassets.item_win_tga, x - 32.0f, y - 24.0f, 64.0f, 48.0f,
             448.0f, 896.0f, 512.0f, 944.0f);
}
/* FUN_0048ee77 — the down scroll arrow (item_win src(512,896,576,944)). */
static void enc_arrow_down(struct IDirect3DDevice8 *d, float x, float y)
{
    enc_quad(d, &g_sysassets.item_win_tga, x - 32.0f, y - 24.0f, 64.0f, 48.0f,
             512.0f, 896.0f, 576.0f, 944.0f);
}

/* FUN_0049f8b8 — render the catalog at submenu slide offset (px,py). */
void encyclopedia_render(struct IDirect3DDevice8 *d, float px, float py)
{
    const int catn = g_enc_cat_count;
    if (catn <= 0) return;          /* nothing built (empty save) */

    /* count discovered (non -1/-2) cells in the current category. */
    int local_40 = 0;
    for (int s = 0; s < ENC_CAT_CELLS; s++) {
        int16_t v = g_enc_slot[g_enc_cat * ENC_CAT_CELLS + s];
        if (v != -1 && v != -2) local_40++;
    }

    /* carousel: prev / current / next category panels. */
    for (int p = 0; p < 3; p++) {
        int catidx = (g_enc_cat + catn - 1 + p) % catn;
        float panel_x = (float)(p * 0x280 + (g_enc_anim << 6)) - 640.0f;
        if (!(((g_enc_anim >= 0) || p != 0) && ((g_enc_anim < 1) || p != 2)))
            continue;

        if (p == 1) {
            /* board strip (item_win), absolute dst(200,4,240,77). */
            enc_quad(d, &g_sysassets.item_win_tga, 200.0f, 4.0f, 240.0f, 77.0f,
                     448.0f, 736.0f, 688.0f, 813.0f);
            /* completion panel (pause.tga) dst(px+504, py+88, 128, 256). */
            enc_quad(d, &g_scene_pause_pause, px + 504.0f, py + 88.0f, 128.0f, 256.0f,
                     880.0f, 0.0f, 1008.0f, 256.0f);
            /* completion labels + percent (centered at px+568). */
            int hdr = tables_item_find_slot_by_id(&g_item, g_enc_cat_key[catidx]);
            const float cx = px + 568.0f;
            font_draw_text_centered(d, cx, 220.0f, "Completion", 0xffffffffu, 0.8f);
            font_draw_text_centered(d, cx, 240.0f, "Rate",       0xffffffffu, 0.8f);
            float pct = (float)g_enc_comp_num / (float)g_enc_comp_den * 100.0f;
            if (g_enc_comp_num > 0 && pct < 1.0f) pct = 1.0f;
            char pbuf[32];
            snprintf(pbuf, sizeof pbuf, "%3d\x81\x93", (int)pct);   /* "%3d％" */
            font_draw_text_centered(d, cx, 264.0f, pbuf, 0xffffffffu, 0.8f);
            /* category name (absolute center 320,34); "？？？" if none discovered. */
            const char *cname = "\x81\x48\x81\x48\x81\x48";          /* "？？？" */
            if (local_40 != 0 && hdr >= 0) {
                int c = g_item.records[hdr].category;
                if (c >= 0 && c < ITEM_CATEGORY_COUNT)
                    cname = g_item.categories[c].singular;
            }
            font_draw_text_centered(d, 320.0f, 34.0f, cname, 0xffffffffu, 0.8f);
        }

        /* 3×3 visible grid. */
        for (int cell = 0; cell < 9; cell++) {
            int sidx = catidx * ENC_CAT_CELLS + cell + g_enc_scroll * 3;
            if (g_enc_slot[sidx] == -1) continue;
            float gx = (float)(cell % 3) * 149.33334f + panel_x + px + 88.0f;
            float gy = (float)(cell / 3) * 0x58 + py + 80.0f;
            /* slot frame (pause.tga src(880,256,944,320)) 64×64. */
            enc_quad(d, &g_scene_pause_pause, gx, gy, 64.0f, 64.0f,
                     880.0f, 256.0f, 944.0f, 320.0f);
            if (g_enc_slot[sidx] == -2) continue;
            int rec = tables_item_find_slot_by_id(
                &g_item, g_enc_slot[g_enc_index[sidx] + catidx * ENC_CAT_CELLS]);
            if (rec < 0) continue;
            /* item name centered at (gx+32, gy+48), scale 0.692. */
            font_draw_text_centered(d, gx + 32.0f, gy + 48.0f,
                                    g_item.records[rec].singular, 0xffffffffu, 0.692307f);
            /* item icon: atlas item_icons[category], cell=subindex. */
            int c    = g_item.records[rec].category;
            int icel = g_item.records[rec].subindex;
            if (c >= 0 && c < SYSASSETS_ITEM_CATEGORIES &&
                g_sysassets.item_icons[c].tex) {
                float sl = (float)((icel % 8) * 32), st = (float)((icel / 8) * 32);
                enc_quad(d, &g_sysassets.item_icons[c],
                         gx + 16.0f, gy + 16.0f, 32.0f, 32.0f,
                         sl, st, sl + 32.0f, st + 32.0f);
            }
        }
    }

    /* bottom description board (item_win src(0,320,640,480)) dst(px,py+332,640,160). */
    enc_quad(d, &g_sysassets.item_win_tga, px, py + 332.0f, 640.0f, 160.0f,
             0.0f, 320.0f, 640.0f, 480.0f);

    /* description text for the cursor's item (not mid-slide, cell holds an item). */
    if (g_enc_anim == 0) {
        int disp = g_enc_col + (g_enc_row + g_enc_cat * 0x14) * 3;
        int16_t iid = g_enc_slot[g_enc_index[disp + g_enc_cat * ENC_CAT_CELLS] + g_enc_cat * ENC_CAT_CELLS];
        if (iid >= 0) {
            int rec = tables_item_find_slot_by_id(&g_item, iid);
            if (rec >= 0) {
                const float tx = px + 80.0f;
                font_draw_text(d, tx, py + 368.0f, g_item.records[rec].desc_line1, 0xffffffffu, 0.8f);
                font_draw_text(d, tx, py + 396.0f, g_item.records[rec].desc_line2, 0xffffffffu, 0.8f);
                char num[32], pr[64];
                enc_format_number(num, sizeof num, g_item.records[rec].price);
                snprintf(pr, sizeof pr, "Price- %s", num);
                font_draw_text(d, tx, py + 424.0f, pr, 0xffffffffu, 0.8f);
            }
        }
    }

    /* scroll arrows. */
    if (g_enc_scroll > 0)
        enc_arrow_up(d, px + 32.0f, py + 54.0f);
    if (g_enc_scroll < g_enc_rows_cur - 3)
        enc_arrow_down(d, px + 32.0f, py + 320.0f);

    /* category L/R selector (data_win): the engine binds ONCE, adds the two
     * stacked 200×32 quads, then flushes ONCE (one batched draw, 4 prims). */
    {
        sprite_t *dw = &g_sysassets.data_win_tga;
        render_quad_bind(d, dw);
        const float d1[4] = { px + 472.0f, 16.0f, 200.0f, 32.0f };
        const float s1[4] = { 288.0f, 416.0f, 488.0f, 448.0f };
        render_quad_add(d1, s1, dw->width, dw->height, 0xffffffffu);
        const float d2[4] = { px + 472.0f, 48.0f, 200.0f, 32.0f };
        const float s2[4] = { 288.0f, 448.0f, 488.0f, 480.0f };
        render_quad_add(d2, s2, dw->width, dw->height, 0xffffffffu);
        render_quad_flush(d);
    }

    /* item-detail overlay (A-press) — Phase 2. */
    if (g_enc_detail == 1)
        encyclopedia_detail_render(d);
}

/* FUN_0046a336 — the item-detail overlay.  PORT-DEBT(encyclopedia-detail):
 * the big item-detail popup is the next milestone; stubbed so the grid lands
 * first.  The simple open+view trace never presses the detail button. */
void encyclopedia_detail_render(struct IDirect3DDevice8 *d)
{
    (void)d;
}

#endif /* _WIN32 */
