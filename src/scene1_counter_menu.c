/*
 * scene1_counter_menu.c — see scene1_counter_menu.h.
 *
 * Retail: FUN_004850fe (list build), FUN_0048670f case 0 / case 10 + the slide
 * ramp (all.c:96212-96225), FUN_00409925's cc08 0/10 render block.  Verified
 * against rhost (buy-40pct-accept: counter Z f793 → cc08 0, Z f836 → cc08 10,
 * b754 1..15, cc08 4 at f851; b74c 1..12 from f794).
 */
#include "scene1_counter_menu.h"

#include <stddef.h>

#include "audio.h"        /* audio_play_se_by_id (FUN_00499519, no RNG) */
#include "save_bank.h"    /* SAVE_BANK_FIELD_MERCHANT_LEVEL / _CARD_DAY */

#define CM_F423_BYTE_OFF  0x2bc8b   /* DAT_0450f423 — gates list-B entry 6 */

static int s_list_a[COUNTER_MENU_MAX];   /* DAT_074b2d98 */
static int s_count_a;                    /* DAT_005cd0bc */
static int s_list_b[COUNTER_MENU_MAX];   /* DAT_074b28b0 */
static int s_count_b;                    /* DAT_005cd0c0 */
static int s_cursor;                     /* DAT_0438cc0c */
static int s_cursor2;                    /* DAT_0438cc10 */
static int s_slide;                      /* DAT_0438b74c */
static int s_slide2;                     /* DAT_0438b750 */
static int s_timer;                      /* DAT_0438b754 */

/* off_0052974c — the list-A entry labels. */
static const char *const k_labels[7] = {
    "Open Store", "Change Flooring", "Change Walls", "Change Carpet",
    "Change Tables", "Move Tables", "Cancel",
};

void counter_menu_reset(void)
{
    for (int i = 0; i < COUNTER_MENU_MAX; i++)
        s_list_a[i] = s_list_b[i] = 0;
    s_count_a = s_count_b = 0;
    s_cursor = s_cursor2 = 0;
    s_slide = s_slide2 = s_timer = 0;
}

void counter_menu_build(const uint8_t *bank)
{
    const int32_t *bd = (const int32_t *)bank;
    int level = bd ? bd[SAVE_BANK_FIELD_MERCHANT_LEVEL] : 0;   /* dword 45312 */
    int day   = bd ? bd[SAVE_BANK_FIELD_CARD_DAY] : 0;         /* dword 45307 */

    /* list A: Open Store, the merchant-level-gated decor entries, Cancel. */
    int n = 1;
    s_list_a[0] = COUNTER_MENU_OPEN_STORE;
    if (level >= 5)  { s_list_a[1] = 2; n = 2; }
    if (level >= 7)  s_list_a[n++] = 1;
    if (level >= 8)  s_list_a[n++] = 3;
    if (level >= 9)  s_list_a[n++] = 5;
    if (level >= 13) s_list_a[n++] = 4;
    s_list_a[n] = COUNTER_MENU_CANCEL;      /* engine DAT_074b2d94[n+1] = 6 */
    s_count_a = n + 1;

    /* list B (the cc08 15 Tear menu). */
    int m = 3;
    s_list_b[0] = 0;
    s_list_b[1] = 5;
    s_list_b[2] = 7;
    if (day >= 9) { s_list_b[3] = 8; m = 4; }
    if (level >= 6) s_list_b[m++] = 9;
    if (bank && bank[CM_F423_BYTE_OFF] != 0) s_list_b[m++] = 6;
    s_list_b[m] = 4;
    s_count_b = m + 1;
}

void counter_menu_open(void)
{
    s_timer = 0;      /* DAT_0438b754 */
    s_cursor = 0;     /* DAT_0438cc0c */
    s_cursor2 = 0;    /* DAT_0438cc10 */
}

void counter_menu_slide_tick(int cc08)
{
    if (cc08 == 0 || cc08 == 10 || cc08 == 15 || cc08 == 16 || cc08 == 30) {
        if (s_slide < 12) s_slide++;
    } else if (s_slide > 0) {
        s_slide--;
    }
    if (cc08 == 30) {
        if (s_slide2 < 12) s_slide2++;
    } else if (s_slide2 > 0) {
        s_slide2--;
    }
}

int counter_menu_input(uint16_t pressed, uint16_t held)
{
    if (pressed & 0x20) { audio_play_se_by_id(0x13d); return COUNTER_MENU_IN_CANCEL; }
    if (pressed & 0x10) { audio_play_se_by_id(0x143); return COUNTER_MENU_IN_CONFIRM; }
    int n = s_count_a;
    if (n <= 0)
        return COUNTER_MENU_IN_NONE;
    if (held & 0x4)
        s_cursor = (s_cursor + n - 1) % n;
    else if (held & 0x8)
        s_cursor = (s_cursor + n + 1) % n;
    else
        return COUNTER_MENU_IN_NONE;
    audio_play_se_by_id(0x146);
    return COUNTER_MENU_IN_NONE;
}

int counter_menu_close_tick(void)
{
    if (++s_timer != 15)
        return -1;
    return s_list_a[s_cursor];
}

int counter_menu_count(void)  { return s_count_a; }
int counter_menu_entry(int i) { return (i >= 0 && i < s_count_a) ? s_list_a[i] : -1; }
int counter_menu_cursor(void) { return s_cursor; }
int counter_menu_slide(void)  { return s_slide; }
int counter_menu_timer(void)  { return s_timer; }
const char *counter_menu_label(int entry)
{
    return (entry >= 0 && entry < 7) ? k_labels[entry] : "";
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>
#include <math.h>

#include "font_draw.h"     /* font_draw_text_centered (FUN_0047d14c) */
#include "render_quad.h"   /* FUN_00404efc / FUN_00405354 */
#include "sysassets.h"     /* item_win.tga = DAT_073d8748 */

/* FUN_00409925 asm 0x40a2b7-0x40a54f (all.c:6860-6960). */
void counter_menu_render(struct IDirect3DDevice8 *dev_in, int cc08)
{
    if (!dev_in || (cc08 != 0 && cc08 != 10))
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    const sprite_t *win = &g_sysassets.item_win_tga;
    if (!win->tex)
        return;
    const int n = s_count_a;
    const float y0 = (float)(48 * (7 - n));

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);

    /* entry buttons: slide in from the left (30 px per b74c step), the cursor
     * entry 40 px further right; while closing (b754) the others slide back
     * out and the chosen one pulses. */
    for (int i = 0; i < n; i++) {
        float x = (float)(30 * s_slide) - ((float)(16 * i) + 256.0f);
        float lim = (s_cursor == i) ? 0.0f : -40.0f;
        if (x > lim) x = lim;
        uint32_t color = 0xff7f7f7fu;
        if (s_cursor == i) {
            float s = (float)s_timer;
            s = s * 3.1415927f / 15.0f;
            s = (float)sin(s);
            int g = 127 - (int)(s * -128.0f);
            color = 0xff000000u | ((uint32_t)(g & 0xff) * 0x010101u);
        } else {
            x -= (float)(20 * s_timer);
        }
        const float dst[4] = { x + 10.0f, y0 + (float)(48 * i) + 128.0f, 256.0f, 64.0f };
        const float src[4] = { 448.0f, 944.0f, 704.0f, 1008.0f };
        render_quad_add(dst, src, win->width, win->height, color);
    }
    render_quad_flush(dev);

    /* labels (off_0052974c), scale 0.8, centred on the button. */
    for (int i = 0; i < n; i++) {
        float x = (float)(30 * s_slide) - ((float)(16 * i) + 256.0f);
        float lim = (s_cursor == i) ? 0.0f : -40.0f;
        if (x > lim) x = lim;
        uint32_t color = 0xff464646u;
        if (s_cursor == i) {
            float s = (float)s_timer;
            s = s * 3.1415927f / 15.0f;
            s = (float)sin(s);
            uint32_t u = (uint32_t)(int)(s * 128.0f);
            color = u | ((u + 127u) << 16) | ((u + 127u) << 8) | 0xff000000u;
        } else {
            x -= (float)(20 * s_timer);
        }
        font_draw_text_centered(dev_in, x + 154.0f, y0 + (float)(48 * i) + 152.0f,
                                counter_menu_label(s_list_a[i]), color, 0.8f);
    }

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

#endif /* _WIN32 */
