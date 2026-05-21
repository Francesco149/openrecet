/*
 * settings.c — non-audio settings state. Two ints + getters/setters.
 *
 * Mirrors engine globals DAT_056e5784 (slider3) and DAT_056e5782
 * (slider4). See header for the consumer call sites and engine
 * defaults rationale.
 */

#include "settings.h"

static int g_slider3 = SETTINGS_SLIDER3_DEFAULT;
static int g_slider4 = SETTINGS_SLIDER4_DEFAULT;

static int clamp(int value, int max)
{
    if (value < 0)   return 0;
    if (value > max) return max;
    return value;
}

void settings_reset(void)
{
    g_slider3 = SETTINGS_SLIDER3_DEFAULT;
    g_slider4 = SETTINGS_SLIDER4_DEFAULT;
}

int settings_get_slider3(void)
{
    return g_slider3;
}

void settings_set_slider3(int value)
{
    g_slider3 = clamp(value, SETTINGS_SLIDER3_MAX);
}

int settings_get_slider4(void)
{
    return g_slider4;
}

void settings_set_slider4(int value)
{
    g_slider4 = clamp(value, SETTINGS_SLIDER4_MAX);
}
