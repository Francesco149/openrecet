/*
 * settings.c — non-audio settings state. Two values + getters/setters.
 *
 * Mirrors engine globals DAT_056e5784 (slider3) and DAT_056e5782
 * (slider4). See header for the consumer call sites and engine
 * defaults rationale.
 */

#include "settings.h"

#include <stddef.h>

#include "audio_fade.h"
#include "save_bank.h"

/* Unbound (host tests): module-local. In the game both point into the
 * save-arena header (settings_bind_save_header). */
static int32_t g_local_slider3 = SETTINGS_SLIDER3_DEFAULT;
static int16_t g_local_slider4 = SETTINGS_SLIDER4_DEFAULT;
static int32_t *g_slider3 = &g_local_slider3;
static int16_t *g_slider4 = &g_local_slider4;

static int clamp(int value, int max)
{
    if (value < 0)   return 0;
    if (value > max) return max;
    return value;
}

void settings_reset(void)
{
    g_local_slider3 = SETTINGS_SLIDER3_DEFAULT;
    g_local_slider4 = SETTINGS_SLIDER4_DEFAULT;
    g_slider3 = &g_local_slider3;      /* unbind */
    g_slider4 = &g_local_slider4;
}

void settings_bind(int32_t *slider3, int16_t *slider4)
{
    g_slider3 = slider3 ? slider3 : &g_local_slider3;
    g_slider4 = slider4 ? slider4 : &g_local_slider4;
}

void settings_bind_save_header(void)
{
    uint8_t *h = save_bank_header_bytes();                  /* DAT_056e5770 */
    audio_fade_bind_sliders((int32_t *)(void *)(h + 0x08),  /* DAT_056e5778 Music */
                            (int32_t *)(void *)(h + 0x04),  /* DAT_056e5774 Sound */
                            (int32_t *)(void *)(h + 0x0c)); /* DAT_056e577c Voice */
    settings_bind((int32_t *)(void *)(h + 0x14),            /* DAT_056e5784 Message Speed */
                  (int16_t *)(void *)(h + 0x12));           /* DAT_056e5782 Unread Text Skip */
}

int settings_get_slider3(void)
{
    return (int)*g_slider3;
}

void settings_set_slider3(int value)
{
    *g_slider3 = clamp(value, SETTINGS_SLIDER3_MAX);
}

int settings_get_slider4(void)
{
    return (int)*g_slider4;
}

void settings_set_slider4(int value)
{
    *g_slider4 = (int16_t)clamp(value, SETTINGS_SLIDER4_MAX);
}
