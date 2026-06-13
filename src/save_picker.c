/*
 * save_picker.c — see save_picker.h.
 *
 * Engine: FUN_0049b537 (perm init) + FUN_0049b556 (card-list render).
 * The render is transcribed from objdump @0x49b556..0x49c050 (Ghidra dropped
 * the FP .rdata consts, the SetTexture args, the sprintf format strings, and
 * the TIME seconds vararg); float constants resolved from .rdata via
 * tools/analyze/pe.py. See docs/plans/pause-menu.md M4.
 */

#include "save_picker.h"

#include "save_bank.h"   /* save_bank_dwords_at — per-card slot fields */

/* ── shared picker globals (engine .bss) ── */
int32_t g_save_picker_perm[SAVE_PICKER_SLOTS] = { 0 };
int32_t g_save_picker_count      = 0;
int32_t g_save_picker_frame      = 0;
int32_t g_save_picker_restricted = 0;
int32_t g_save_picker_hpage_anim = 0;
uint8_t g_save_picker_avail[SAVE_PICKER_SLOTS] = { 0 };

/* ── FUN_0049b537 — identity slot-perm + count = 100 ── */
void save_picker_perm_init(void)
{
    for (int i = 0; i < SAVE_PICKER_SLOTS; i++)
        g_save_picker_perm[i] = i;
    g_save_picker_count = SAVE_PICKER_SLOTS;
}

void save_picker_reset(void)
{
    for (int i = 0; i < SAVE_PICKER_SLOTS; i++)
        g_save_picker_perm[i] = 0;
    g_save_picker_count      = 0;
    g_save_picker_frame      = 0;
    g_save_picker_restricted = 0;
    g_save_picker_hpage_anim = 0;
    for (int i = 0; i < SAVE_PICKER_SLOTS; i++)
        g_save_picker_avail[i] = 0;
}

#ifdef _WIN32

#include <d3d8.h>
#include <math.h>          /* sinf — the cursor breathe / save-phase pulse */
#include <stdio.h>         /* snprintf */

#include "sysassets.h"            /* g_sysassets.item_win_tga = DAT_073d8748 */
#include "scene_pause.h"          /* g_scene_pause_pause = DAT_073d86a8 (plaque) */
#include "render_quad.h"          /* bind / add / flush / draw_rotated_rect */
#include "font_draw.h"            /* font_draw_text / _right = FUN_0047ca05 / 0047d2db */
#include "scene1_top_hud.h"       /* scene1_top_hud_draw_number = FUN_00406a60 */
#include "scene1_merchant_hud.h"  /* scene1_merchant_hud_draw_level = FUN_00481ec3 */

/* Save-bank dword field indices the picker card reads (see save_bank.h). */
#define F_OCCUPIED     SAVE_BANK_FIELD_OCCUPIED      /* 2     — playtime frames@60 */
#define F_GOLD         SAVE_BANK_FIELD_GOLD          /* 3 */
#define F_SCORE        SAVE_BANK_FIELD_SCORE         /* 0xb0f7 */
#define F_LOOP         SAVE_BANK_FIELD_LOOP          /* 0xb0f9 */
#define F_CARD_DAY     SAVE_BANK_FIELD_CARD_DAY      /* 0xb0fb */
#define F_PORTRAIT_ROT SAVE_BANK_FIELD_PORTRAIT_ROT  /* 0xb0fc */
#define F_CHAR_LEVEL   SAVE_BANK_FIELD_CHAR_LEVEL    /* 0xb100 */
#define F_GAME_MODE    SAVE_BANK_FIELD_GAME_MODE     /* 0xb759 */
#define F_SURV_SUBMODE 0xb78d                        /* byte +0x2de34 (mode-2 split) */

/* Engine's diffuse build: (hi<<8|lo)<<8|lo)<<8|lo = 0xAArrggbb, AA=hi,
 * rgb=lo (a grey). hi/lo are bounded to [0,255] in every reachable path. */
static uint32_t picker_argb(int hi, int lo)
{
    return (((uint32_t)hi << 8 | (uint32_t)lo) << 8 | (uint32_t)lo) << 8
           | (uint32_t)lo;
}

/* FUN_0049b556 — the card-list render. */
void save_picker_render(struct IDirect3DDevice8 *dev_in,
                        float x, int cursor, int scroll,
                        int vscroll, int hscroll, int phase)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    const sprite_t *iw = &g_sysassets.item_win_tga;       /* DAT_073d8748 */
    const uint32_t iw_w = iw->width, iw_h = iw->height;
    char buf[256];

    g_save_picker_frame++;                                /* _DAT_09643574++ (49b566) */
    const int count = g_save_picker_count;                /* local_48 (49b55f) */
    const int restricted = g_save_picker_restricted;
    const int wings = (g_save_picker_hpage_anim >= 10);   /* 9 < DAT_09643520 */

    render_quad_bind(dev, iw);                             /* SetTexture(0, item_win) 49b581 */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                          D3DTOP_ADDSIGNED); /* (0,1,8) 49b594 */

    /* ── Pass 1: the card background boxes (3 pages × 5 rows). ── */
    for (int page = 0; page != 0x780; page += 0x280) {    /* 0 / 640 / 1280 */
        for (int row = 0; row != 5; row++) {
            if (row > count) continue;                    /* always false */
            int hi = 0xc8, lo = 0x5f;                     /* esi / local_c */
            int slot = row - 1 + scroll;
            if (slot > count - 1 || slot < 0)
                continue;
            if (!(page == 0x280 || wings))
                continue;
            const int pslot = g_save_picker_perm[slot];
            const uint32_t *bank = save_bank_dwords_at(pslot);
            const int mode = bank ? (int)bank[F_GAME_MODE] : 0;
            if (slot == cursor) {                         /* selected (49b606) */
                hi = 0xff;
                lo = (int)(sinf((float)g_save_picker_frame * 0.1f) * 32.0f
                           + 159.0f);                     /* breathe (49b60f) */
                if (restricted != 0
                    && (mode != 3 || g_save_picker_avail[pslot] == 0)) /* dim (49b64f) */
                    lo -= 0x40;
                if (phase > 0) {                          /* save-anim pulse (49b673) */
                    lo += (int)(sinf((float)phase * 3.1415927f / 30.0f) * 128.0f);
                    if (lo > 0xff) lo = 0xff;
                }
            } else if (restricted != 0
                       && (mode != 3 || g_save_picker_avail[pslot] == 0)) { /* (49b6bd) */
                lo = 0x20;
            }
            int skip = 0;
            if (row == 1) {                               /* row-slide fade (49b6df) */
                if (hscroll > 0) hi = (5 - hscroll) * 0x33;
            } else if (row == 0) {
                if (hscroll >= 0) skip = 1;
                else hi = hscroll * -0x33;
            }
            if (skip) continue;
            const float dst_y = ((float)(row * 0x8c) - 92.0f)
                                - (float)(hscroll * 0x1c);
            const float dst_x = ((x - (float)(vscroll << 7)) - 640.0f)
                                + (float)page;
            const float src[4] = { 0.0f, 320.0f, 640.0f, 480.0f };
            const float dst[4] = { dst_x, dst_y, 640.0f, 160.0f };
            render_quad_add(dst, src, iw_w, iw_h, picker_argb(hi, lo));
        }
    }
    render_quad_flush(dev);                                /* 49b7ed */

    /* ── Pass 2: per-card content (3 pages × 5 rows). ── */
    for (int page = 0; page < 3; page++) {                 /* 0 / 1 / 2 */
        for (int row = 0; row != 5; row++) {
            if (row > count) continue;
            int slot = row - 1 + scroll;
            if (slot > count - 1 || slot < 0)
                continue;
            if (!(page == 1 || wings))
                continue;

            /* Wing-page slot adjustment (dead at rest; page is always 1). */
            int adj = slot;
            if (page == 0) {                               /* left wing (49b840) */
                adj = slot - 4;
                if (scroll - 4 < 0) adj = slot - 3;
                if (scroll - 3 < 0) adj += 1;
                if (scroll - 2 < 0) adj += 1;
            } else if (page == 2) {                        /* right wing (49b85e) */
                if (slot + 3 > count - 1) continue;        /* goto LAB_0049bf3a */
                adj = slot + 3;
                if (count - 1 < scroll - 2) adj = slot + 2;
                if (count - 1 < scroll - 1) adj -= 1;
                if (count - 1 < scroll)     adj -= 1;
            }
            const int pslot = g_save_picker_perm[adj];
            const uint32_t *bank = save_bank_dwords_at(pslot);
            const int mode = bank ? (int)bank[F_GAME_MODE] : 0;

            const float CX = ((x + 80.0f - (float)(vscroll << 7)) - 640.0f)
                             + (float)(page * 0x280);      /* card x (49b89b) */
            const float CY = ((float)(row * 0x8c) - 76.0f)
                             - (float)(hscroll * 0x1c);    /* card y (49b8f0) */

            int hi = 0xc8, lo = 0x5f;                      /* edi / local_1c */
            if (adj == cursor) {                           /* selected (49b902) */
                hi = 0xff;
                lo = 0x7f - (int)(sinf((float)phase * 3.1415927f / 30.0f)
                                  * -128.0f);              /* (49b91c) */
            }
            if (restricted != 0
                && (mode != 3 || g_save_picker_avail[pslot] == 0)) /* (49b959) */
                lo = 0x40;
            int skip = 0;
            if (row == 1) {                                /* (49b97d) */
                if (hscroll > 0) hi = (5 - hscroll) * 0x33;
            } else if (row == 0) {
                if (hscroll >= 0) skip = 1;
                else hi = hscroll * -0x33;
            }
            if (skip) continue;
            const uint32_t color = picker_argb(hi, lo);

            /* file number "%03d" (perm[slot]+1), left of the card. */
            snprintf(buf, sizeof buf, "%03d", pslot + 1);  /* 49b9b7 */
            font_draw_text(dev, CX - 64.0f, CY + 48.0f, buf, color, 1.0f);
            const float meta_y = CY + 48.0f;               /* [ebp-0x18] reuse */

            if (!bank || bank[F_OCCUPIED] == 0) {          /* EMPTY (49ba06) */
                snprintf(buf, sizeof buf, "NO-DATA");       /* 49ba14 */
                font_draw_text(dev, CX + 160.0f, meta_y, buf, color, 1.0f);
                continue;
            }

            /* ── OCCUPIED card (49ba44) ── */
            render_quad_bind(dev, iw);                     /* SetTexture(0, item_win) */
            {   /* inner box: src(480,0)-(768,128) → dst(CX,CY,288,128) */
                const float src[4] = { 480.0f, 0.0f, 768.0f, 128.0f };
                const float dst[4] = { CX, CY, 288.0f, 128.0f };
                render_quad_add(dst, src, iw_w, iw_h, color);
            }
            render_quad_flush(dev);                         /* 49baaf */

            if (mode == 3) {                                /* survival badge (49bab4) */
                const float src[4] = { 288.0f, 480.0f, 383.0f, 575.0f };
                const float dst[4] = { CX + 464.0f, CY + 16.0f, 96.0f, 96.0f };
                render_quad_add(dst, src, iw_w, iw_h, color);
                render_quad_flush(dev);
            }

            /* rotating clock-hand portrait (49bb22): normalized UVs, item_win. */
            {
                const float rot = (float)(int32_t)bank[F_PORTRAIT_ROT];
                const float angle = 1.5707964f - (rot * 3.1415927f) / 3.0f;
                const float pdst[4] = { -16.0f, -54.0f, 16.0f, 10.0f };
                const float puv[4]  = { 0.454102f, 0.125977f,
                                        0.483398f, 0.186523f };
                render_quad_draw_rotated_rect(dev, CX + 52.0f, CY + 72.0f,
                                              angle, pdst, puv, color);
            }

            /* big day number (49bbba): x by digit count, value = day+1. */
            {
                int day = (int32_t)bank[F_CARD_DAY] + 1;
                if (day > 9999) day = 9999;
                float dx;
                if (day >= 1000)     dx = CX + 130.0f;
                else if (day >= 100) dx = CX + 120.0f;
                else if (day >= 10)  dx = CX + 116.0f;
                else                 dx = CX + 112.0f;
                scene1_top_hud_draw_number(dev, dx, CY + 76.0f, day, 0, color, 0);
            }
            /* gold number (49bc49): icon=1 ("pix"), comma=1. */
            scene1_top_hud_draw_number(dev, CX + 272.0f, CY + 28.0f,
                                       (int32_t)bank[F_GOLD], 1, color, 1);

            /* "Merchant Level" plaque from pause.tga (49bc73). */
            render_quad_bind(dev, &g_scene_pause_pause);   /* SetTexture(0, pause) */
            {
                const float src[4] = { 720.0f, 368.0f, 864.0f, 416.0f };
                const float dst[4] = { CX + 160.0f, CY + 60.0f, 144.0f, 48.0f };
                render_quad_add(dst, src, g_scene_pause_pause.width,
                                g_scene_pause_pause.height, color);
            }
            render_quad_flush(dev);                         /* 49bcf0 */

            /* level digits (49bcf5): drawn as value+1, binds item_win itself. */
            scene1_merchant_hud_draw_level(dev, CX + 312.0f, CY + 64.0f,
                                           (int32_t)bank[F_CHAR_LEVEL], color);

            /* SCORE / LOOP / TIME rows, scale 0.8 (49bd1b..). */
            snprintf(buf, sizeof buf, "SCORE");
            font_draw_text(dev, CX + 352.0f, CY + 16.0f, buf, color, 0.8f);
            snprintf(buf, sizeof buf, "%8d", (int32_t)bank[F_SCORE]);
            font_draw_text_right(dev, CX + 466.0f, CY + 16.0f, buf, color, 0.8f);

            snprintf(buf, sizeof buf, "LOOP ");
            font_draw_text(dev, CX + 352.0f, meta_y, buf, color, 0.8f);
            snprintf(buf, sizeof buf, "%3d", (int32_t)bank[F_LOOP] + 1);
            font_draw_text_right(dev, CX + 466.0f, meta_y, buf, color, 0.8f);

            {   /* TIME %3d:%02d:%02d — frames@60 → h:mm:ss (49be31). */
                int pt = (int32_t)bank[F_OCCUPIED];
                int sec = (pt / 0x3c) % 0x3c;
                int min = (pt / 0xe10) % 0x3c;
                int hr  = pt / 0x34bc0;
                if (hr > 999) { hr = 999; min = 59; sec = 59; }
                snprintf(buf, sizeof buf, "TIME %3d:%02d:%02d", hr, min, sec);
                font_draw_text(dev, CX + 352.0f, CY + 80.0f, buf, color, 0.8f);
            }

            /* game-mode label, scale 0.65 (49beb5). */
            if (mode != 0) {
                const char *ms = "";
                if (mode == 3)      ms = "Endless";
                else if (mode == 1) ms = "New Game+";
                else if (mode == 2) ms = (bank[F_SURV_SUBMODE] != 0)
                                         ? "Survival Hell" : "Normal Survival";
                snprintf(buf, sizeof buf, "%s", ms);
                font_draw_text(dev, CX + 384.0f, CY + 104.0f, buf, color, 0.65f);
            }
        }
    }

    /* ── Pass 2 tail (page == 3): scroll arrows under COLOROP=MODULATE. ── */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                          D3DTOP_MODULATE);  /* (0,1,4) 49bf63 */
    render_quad_bind(dev, iw);                                /* 49bf78 */
    if (scroll > 0) {                                         /* up arrow (49bf7e) */
        const float src[4] = { 448.0f, 896.0f, 512.0f, 944.0f };
        const float dst[4] = { x, 64.0f, 64.0f, 48.0f };
        render_quad_add(dst, src, iw_w, iw_h, 0xffffffffu);
    }
    if (scroll < count - 3) {                                /* down arrow (49bfe2) */
        const float src[4] = { 512.0f, 896.0f, 576.0f, 944.0f };
        const float dst[4] = { x, 408.0f, 64.0f, 48.0f };
        render_quad_add(dst, src, iw_w, iw_h, 0xffffffffu);
    }
    render_quad_flush(dev);                                   /* 49c049 */
}

#endif /* _WIN32 */
