/*
 * test_scene1_maplight.c — scene1_maplight_compute() (port of FUN_00458f67).
 *
 * The Win32 device-side (SetLight/LightEnable/LIGHTING) is not exercised
 * here — these tests cover the pure value computation that drives it:
 *
 *   1. mode 3 (town / time-of-day): shoptime + clock_phase select/interpolate
 *      the preset row — shoptime<2 → daytime row 0, shoptime==2 lerps row0→row1
 *      (day→dusk) as clock_phase 1→2, ==3 lerps row1→row2, >=4 → night row 2.
 *      lit == 1.  HOUSE is mode 3.
 *   2. mode 2 (static dungeon): lightdir / lightcolor / lightamb pass
 *      straight through to direction / diffuse / ambient.
 *   3. mode 0 (sun): the dim sun direction is built (y = -0.5, diffuse
 *      stays 0), lit == 1 (the engine disable flag is unported → 0).
 *   4. mode 1 (animated): the diffuse/ambient pulse interpolates between
 *      the maplight_d/_a lo/hi pairs by cos(counter*speed); with the
 *      stubbed counter == 0 the pulse is at its hi end (c == 1).
 *   5. chr_ambient = clamp01(ambient + chrlightoffset).
 *   6. NULL rec → returns 0 (lighting off), type still D3DLIGHT_DIRECTIONAL.
 *   7. scene1_current_stage_record() bridges g_stage.records[HOUSE].
 */
#define _DEFAULT_SOURCE 1
#include <math.h>

#include "t.h"
#include "scene1_maplight.h"
#include "scene_map_meshes.h"   /* SCENE_MAP_STAGE_HOUSE */

/* scene1_maplight.c mode-1 path calls scene1_render_draw_counter(); that
 * symbol lives in the Win32-only scene1_render.c, which the host test
 * suite does not link.  Provide a deterministic stub so the pulse is
 * reproducible (counter 0 → cos(0)=1 → c=1 → pulse at hi end). */
uint32_t scene1_render_draw_counter(void) { return 0; }

#define T_ASSERT_NEAR(a, b) do { \
    double _a = (double)(a); \
    double _b = (double)(b); \
    if (fabs(_a - _b) > 1e-5) \
        T_FAIL("expected %s ~= %s (got %g, want %g)", #a, #b, _a, _b); \
} while (0)

int test_maplight_null_rec_off(void)
{
    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(NULL, 0, 0.0f, &v);
    T_ASSERT_EQ_I(lit, 0);
    T_ASSERT_EQ_I(v.type, 3);          /* D3DLIGHT_DIRECTIONAL */
    T_ASSERT_NEAR(v.diffuse[0], 0.0);
    T_ASSERT_NEAR(v.direction[1], 0.0);
    return 0;
}

int test_maplight_mode3_daytime(void)
{
    /* HOUSE = maplight:3.  shoptime 0 (< 2) → daytime row 0, no interp. */
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 0, 0.0f, &v);
    T_ASSERT_EQ_I(lit, 1);
    T_ASSERT_EQ_I(v.type, 3);
    /* MAPLIGHT3_PRESET row 0 (daytime). */
    T_ASSERT_NEAR(v.direction[0], 0.2);
    T_ASSERT_NEAR(v.direction[1], 0.4);
    T_ASSERT_NEAR(v.direction[2], 0.2);
    T_ASSERT_NEAR(v.diffuse[0], 0.8);
    T_ASSERT_NEAR(v.diffuse[1], 0.8);
    T_ASSERT_NEAR(v.diffuse[2], 0.9);
    T_ASSERT_NEAR(v.ambient[0], 0.6);
    T_ASSERT_NEAR(v.ambient[1], 0.6);
    T_ASSERT_NEAR(v.ambient[2], 0.6);
    return 0;
}

/* shoptime==2, clock_phase==1.0 → frac = 2-1 = 1 → result = row1 -
 * (row1-row0)*1 = row0 (daytime).  The boundary where the dusk ramp begins. */
int test_maplight_mode3_dusk_start(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 2, 1.0f, &v);
    T_ASSERT_EQ_I(lit, 1);
    /* Still row 0 (daytime) at the start of the shoptime-2 window. */
    T_ASSERT_NEAR(v.diffuse[0], 0.8);
    T_ASSERT_NEAR(v.diffuse[1], 0.8);
    T_ASSERT_NEAR(v.diffuse[2], 0.9);
    T_ASSERT_NEAR(v.ambient[0], 0.6);
    T_ASSERT_NEAR(v.ambient[1], 0.6);
    T_ASSERT_NEAR(v.ambient[2], 0.6);
    T_ASSERT_NEAR(v.direction[0], 0.2);
    return 0;
}

/* shoptime==2, clock_phase==1.5 → frac = 0.5 → per-channel midpoint of
 * row0 (day) and row1 (dusk). */
int test_maplight_mode3_dusk_midpoint(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 2, 1.5f, &v);
    T_ASSERT_EQ_I(lit, 1);
    /* dir  = ((1.0+0.2)/2, (0.5+0.4)/2, (-0.5+0.2)/2) */
    T_ASSERT_NEAR(v.direction[0], 0.6);
    T_ASSERT_NEAR(v.direction[1], 0.45);
    T_ASSERT_NEAR(v.direction[2], -0.15);
    /* diff = ((0.8+0.8)/2, (0.7+0.8)/2, (0.2+0.9)/2) */
    T_ASSERT_NEAR(v.diffuse[0], 0.8);
    T_ASSERT_NEAR(v.diffuse[1], 0.75);
    T_ASSERT_NEAR(v.diffuse[2], 0.55);
    /* amb  = ((0.9+0.6)/2, (0.6+0.6)/2, (0.3+0.6)/2) */
    T_ASSERT_NEAR(v.ambient[0], 0.75);
    T_ASSERT_NEAR(v.ambient[1], 0.6);
    T_ASSERT_NEAR(v.ambient[2], 0.45);
    return 0;
}

/* shoptime==2, clock_phase==2.0 → frac = 0 → result = row1 (dusk).  The
 * settled dusk the day-end trace parks on (ground truth: frame 2473+). */
int test_maplight_mode3_dusk_settled(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 2, 2.0f, &v);
    T_ASSERT_EQ_I(lit, 1);
    /* MAPLIGHT3_PRESET row 1 (warm/dusk). */
    T_ASSERT_NEAR(v.direction[0], 1.0);
    T_ASSERT_NEAR(v.direction[1], 0.5);
    T_ASSERT_NEAR(v.direction[2], -0.5);
    T_ASSERT_NEAR(v.diffuse[0], 0.8);
    T_ASSERT_NEAR(v.diffuse[1], 0.7);
    T_ASSERT_NEAR(v.diffuse[2], 0.2);
    T_ASSERT_NEAR(v.ambient[0], 0.9);
    T_ASSERT_NEAR(v.ambient[1], 0.6);
    T_ASSERT_NEAR(v.ambient[2], 0.3);
    return 0;
}

/* shoptime==3 lerps row1 (dusk) → row2 (night): phase 2.0 → row1, 3.0 → row2. */
int test_maplight_mode3_evening_to_night(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    /* frac = 3-2 = 1 → row1 (dusk). */
    (void)scene1_maplight_compute(&rec, 3, 2.0f, &v);
    T_ASSERT_NEAR(v.ambient[0], 0.9);   /* row1 */
    T_ASSERT_NEAR(v.ambient[2], 0.3);
    /* frac = 3-3 = 0 → row2 (night). */
    (void)scene1_maplight_compute(&rec, 3, 3.0f, &v);
    T_ASSERT_NEAR(v.ambient[0], 0.6);   /* row2 */
    T_ASSERT_NEAR(v.ambient[2], 1.2);
    T_ASSERT_NEAR(v.diffuse[0], 0.3);
    return 0;
}

/* shoptime>=4 → row 2 (night), no interp. */
int test_maplight_mode3_night(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 3;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 4, 3.5f, &v);
    T_ASSERT_EQ_I(lit, 1);
    /* MAPLIGHT3_PRESET row 2 (dim/night). */
    T_ASSERT_NEAR(v.direction[0], 1.0);
    T_ASSERT_NEAR(v.direction[1], 0.0);
    T_ASSERT_NEAR(v.direction[2], 0.0);
    T_ASSERT_NEAR(v.diffuse[0], 0.3);
    T_ASSERT_NEAR(v.diffuse[1], 0.3);
    T_ASSERT_NEAR(v.diffuse[2], 0.3);
    T_ASSERT_NEAR(v.ambient[0], 0.6);
    T_ASSERT_NEAR(v.ambient[1], 0.6);
    T_ASSERT_NEAR(v.ambient[2], 1.2);
    return 0;
}

int test_maplight_mode2_static_passthrough(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 2;
    rec.lightdir[0]   = 1.0f;  rec.lightdir[1]   = -1.0f; rec.lightdir[2]   = -1.0f;
    rec.lightcolor[0] = 0.7f;  rec.lightcolor[1] = 0.6f;  rec.lightcolor[2] = 0.5f;
    rec.lightamb[0]   = 0.1f;  rec.lightamb[1]   = 0.2f;  rec.lightamb[2]   = 0.3f;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 0, 0.0f, &v);
    T_ASSERT_EQ_I(lit, 1);
    T_ASSERT_NEAR(v.direction[0], 1.0);
    T_ASSERT_NEAR(v.direction[1], -1.0);
    T_ASSERT_NEAR(v.direction[2], -1.0);
    T_ASSERT_NEAR(v.diffuse[0], 0.7);
    T_ASSERT_NEAR(v.diffuse[1], 0.6);
    T_ASSERT_NEAR(v.diffuse[2], 0.5);
    T_ASSERT_NEAR(v.ambient[0], 0.1);
    T_ASSERT_NEAR(v.ambient[1], 0.2);
    T_ASSERT_NEAR(v.ambient[2], 0.3);
    return 0;
}

int test_maplight_mode0_sun(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 0;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 0, 0.0f, &v);
    /* Engine disable flag unported (→ 0): mode 0 builds the dim sun. */
    T_ASSERT_EQ_I(lit, 1);
    /* sun angle unported → 0: dir = (cos0*0.5, -0.5, sin0*0.5). */
    T_ASSERT_NEAR(v.direction[0], 0.5);
    T_ASSERT_NEAR(v.direction[1], -0.5);
    T_ASSERT_NEAR(v.direction[2], 0.0);
    T_ASSERT_NEAR(v.diffuse[0], 0.0);   /* diffuse stays 0 in mode 0 */
    return 0;
}

int test_maplight_mode1_animated_pulse(void)
{
    /* counter stub == 0 → t = 0 → cos(0)=1 → c = (1+1)/2 = 1 → hi end. */
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 1;
    rec.maplightspeed = 0.05f;
    /* maplight_d[ch] = {lo, hi}; maplight_a[ch] = {lo, hi}. */
    rec.maplight_d[0][0] = 0.1f; rec.maplight_d[0][1] = 0.9f;
    rec.maplight_d[1][0] = 0.2f; rec.maplight_d[1][1] = 0.8f;
    rec.maplight_d[2][0] = 0.3f; rec.maplight_d[2][1] = 0.7f;
    rec.maplight_a[0][0] = 0.0f; rec.maplight_a[0][1] = 0.5f;
    rec.maplight_a[1][0] = 0.0f; rec.maplight_a[1][1] = 0.4f;
    rec.maplight_a[2][0] = 0.0f; rec.maplight_a[2][1] = 0.3f;

    scene1_maplight_values_t v;
    int lit = scene1_maplight_compute(&rec, 0, 0.0f, &v);
    T_ASSERT_EQ_I(lit, 1);
    /* c == 1 → diffuse/ambient land on the hi values. */
    T_ASSERT_NEAR(v.diffuse[0], 0.9);
    T_ASSERT_NEAR(v.diffuse[1], 0.8);
    T_ASSERT_NEAR(v.diffuse[2], 0.7);
    T_ASSERT_NEAR(v.ambient[0], 0.5);
    T_ASSERT_NEAR(v.ambient[1], 0.4);
    T_ASSERT_NEAR(v.ambient[2], 0.3);
    /* sun-style direction (y = -0.5). */
    T_ASSERT_NEAR(v.direction[1], -0.5);
    return 0;
}

int test_maplight_chr_ambient_clamp(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 2;
    rec.lightamb[0] = 0.2f;   /* + offset 0.5 = 0.7 */
    rec.lightamb[1] = 0.8f;   /* + offset 0.5 = 1.3 → clamp 1.0 */
    rec.lightamb[2] = 0.0f;   /* + offset 0.5 = 0.5 */
    rec.chrlightoffset = 0.5f;

    scene1_maplight_values_t v;
    (void)scene1_maplight_compute(&rec, 0, 0.0f, &v);
    T_ASSERT_NEAR(v.chr_ambient[0], 0.7);
    T_ASSERT_NEAR(v.chr_ambient[1], 1.0);   /* clamped */
    T_ASSERT_NEAR(v.chr_ambient[2], 0.5);
    return 0;
}

int test_maplight_chr_ambient_clamp_low(void)
{
    stage_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.maplight = 2;
    rec.lightamb[0] = 0.0f;
    rec.chrlightoffset = -0.5f;   /* 0 - 0.5 = -0.5 → clamp 0.0 */

    scene1_maplight_values_t v;
    (void)scene1_maplight_compute(&rec, 0, 0.0f, &v);
    T_ASSERT_NEAR(v.chr_ambient[0], 0.0);   /* clamped low */
    return 0;
}

int test_maplight_current_stage_record_house(void)
{
    /* Populate g_stage as the boot parser would and confirm the bridge
     * returns records[HOUSE] (not NULL, not some other slot). */
    g_stage.count = SCENE_MAP_STAGE_HOUSE + 1;
    g_stage.records[SCENE_MAP_STAGE_HOUSE].maplight = 3;

    const stage_record_t *rec = scene1_current_stage_record();
    T_ASSERT(rec == &g_stage.records[SCENE_MAP_STAGE_HOUSE]);
    T_ASSERT_EQ_I(rec->maplight, 3);

    /* Empty table → NULL (lighting off). */
    g_stage.count = 0;
    T_ASSERT(scene1_current_stage_record() == NULL);
    return 0;
}
