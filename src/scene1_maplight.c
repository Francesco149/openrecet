/*
 * scene1_maplight.c — port of FUN_00458f67 (the per-stage FFP map light
 * builder).  See scene1_maplight.h for the chip writeup + the maplight
 * mode table.
 *
 * Line refs below are to docs/decompiled/all.c (FUN_00458f67 spans
 * L53687-L53931).
 */

#include "scene1_maplight.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "scene_map_meshes.h"   /* SCENE_MAP_STAGE_HOUSE */

/* scene1_render_draw_counter (DAT_06a49b24, +=1/frame) is declared in
 * scene1_render.h, but that header's body is entirely _WIN32-guarded, so
 * it is invisible to the host test build that exercises the mode-1 pulse.
 * The accessor itself is pure (uint32_t, no Win32 surface), so forward-
 * declare it here for both targets. */
uint32_t scene1_render_draw_counter(void);   /* mode 1 animation phase */

/* ─── current stage record (DAT_068dd2f0 analog) ──────────────────────── */

const stage_record_t *scene1_current_stage_record(void)
{
    /* Active stage is fixed at HOUSE today.  g_stage is populated by
     * tables_parse_stage (tables.c) at boot; if the load failed the
     * count is zero and we report "no record" (lighting off). */
    if (g_stage.count <= SCENE_MAP_STAGE_HOUSE) return NULL;
    return &g_stage.records[SCENE_MAP_STAGE_HOUSE];
}

/* ─── mode-3 time-of-day preset table ─────────────────────────────────── */

/* Engine local_98[0..0x1a]: three 9-float rows, each
 * [dir.x dir.y dir.z  diff.r diff.g diff.b  amb.r amb.g amb.b].
 * Row 0 = daytime, row 1 = warm/dusk, row 2 = dim/night.  The engine
 * picks/interpolates by the day-night clock (DAT_0438b1e0 index into
 * DAT_0450fb88 + the DAT_0438b7d4 fraction). */
static const float MAPLIGHT3_PRESET[3][9] = {
    { 0.2f, 0.4f,  0.2f,  0.8f, 0.8f, 0.9f,  0.6f, 0.6f, 0.6f },
    { 1.0f, 0.5f, -0.5f,  0.8f, 0.7f, 0.2f,  0.9f, 0.6f, 0.3f },
    { 1.0f, 0.0f,  0.0f,  0.3f, 0.3f, 0.3f,  0.6f, 0.6f, 1.2f },
};

/* ─── unported engine globals (mode-0/1 inputs) ───────────────────────── */

/* _DAT_073de39c — sun angle (mode 0). Unported → 0. */
static float ml_sun_angle(void)         { return 0.0f; }
/* _DAT_0438bec8 — mode-0 "disable light" flag (== 1.0 → lighting off).
 * Unported → 0 (mode 0 builds the dim sun light rather than disabling). */
static float ml_mode0_disable_flag(void){ return 0.0f; }

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* ─── pure value computation ──────────────────────────────────────────── */

int scene1_maplight_compute(const stage_record_t *rec,
                            int shoptime, float clock_phase,
                            scene1_maplight_values_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->type = 3;  /* D3DLIGHT_DIRECTIONAL */
    if (!rec) return 0;

    int mode = rec->maplight;          /* +0x1ae0 */
    int lit  = 1;

    if (mode == 3) {
        /* L53746-L53793: town time-of-day preset select + interpolate.
         * `shoptime` = (int)DAT_0450fb88[slot]; `clock_phase` = DAT_0438b7d4
         * (the animated phase eased toward shoptime in sim_step_a).  The
         * engine's integer branch (objdump 0x4590d2: cmp ecx,1 jg / cmp
         * ecx,2 je / cmp ecx,3 je — the decomp's 2.8026e-45/4.2039e-45
         * denormals are Ghidra rendering the int constants 2/3 as float
         * bit-patterns).  Interp lerps row[shoptime-2]↔row[shoptime-1] as
         * clock_phase sweeps [shoptime-1, shoptime]. */
        if (shoptime < 2) {
            /* L53747-L53757: shoptime 0/1 → row 0 (daytime), no interp. */
            const float *p = MAPLIGHT3_PRESET[0];
            out->direction[0] = p[0]; out->direction[1] = p[1]; out->direction[2] = p[2];
            out->diffuse[0]   = p[3]; out->diffuse[1]   = p[4]; out->diffuse[2]   = p[5];
            out->ambient[0]   = p[6]; out->ambient[1]   = p[7]; out->ambient[2]   = p[8];
        } else if (shoptime == 2 || shoptime == 3) {
            /* L53759-L53781: interpolate per channel.  hi = row[shoptime-1],
             * lo = row[shoptime-2], frac = (float)shoptime - clock_phase;
             * result = hi - (hi - lo) * frac  (→ lo at frac 1 / phase=st-1,
             * hi at frac 0 / phase=st). */
            const float *hi = MAPLIGHT3_PRESET[shoptime - 1];
            const float *lo = MAPLIGHT3_PRESET[shoptime - 2];
            float frac = (float)shoptime - clock_phase;
            float o[9];
            for (int k = 0; k < 9; k++) o[k] = hi[k] - (hi[k] - lo[k]) * frac;
            out->direction[0] = o[0]; out->direction[1] = o[1]; out->direction[2] = o[2];
            out->diffuse[0]   = o[3]; out->diffuse[1]   = o[4]; out->diffuse[2]   = o[5];
            out->ambient[0]   = o[6]; out->ambient[1]   = o[7]; out->ambient[2]   = o[8];
        } else {
            /* L53782-L53792: shoptime >= 4 → row 2 (night), no interp. */
            const float *p = MAPLIGHT3_PRESET[2];
            out->direction[0] = p[0]; out->direction[1] = p[1]; out->direction[2] = p[2];
            out->diffuse[0]   = p[3]; out->diffuse[1]   = p[4]; out->diffuse[2]   = p[5];
            out->ambient[0]   = p[6]; out->ambient[1]   = p[7]; out->ambient[2]   = p[8];
        }
    } else if (mode == 2) {
        /* L53805-L53813: static lightdir / lightcolor / lightamb. */
        out->direction[0] = rec->lightdir[0];   /* +0x1ab4 */
        out->direction[1] = rec->lightdir[1];
        out->direction[2] = rec->lightdir[2];
        out->diffuse[0]   = rec->lightcolor[0]; /* +0x1ac0 */
        out->diffuse[1]   = rec->lightcolor[1];
        out->diffuse[2]   = rec->lightcolor[2];
        out->ambient[0]   = rec->lightamb[0];   /* +0x1acc */
        out->ambient[1]   = rec->lightamb[1];
        out->ambient[2]   = rec->lightamb[2];
    } else if (mode == 0) {
        /* L53815-L53845: mode 0.  With the disable flag set the engine
         * turns lighting off entirely; otherwise it builds a dim sun
         * light from the (unported) sun angle.  HOUSE never hits this. */
        if (ml_mode0_disable_flag() == 1.0f) {
            lit = 0;
        } else {
            float a = -ml_sun_angle();
            /* FUN_00503a44 = cos, FUN_00503994 = sin (engine trig pair). */
            out->direction[0] = (float)cos((double)a) * 0.5f;
            out->direction[1] = -0.5f;
            out->direction[2] = (float)sin((double)a) * 0.5f;
            /* diffuse stays 0; ambient = mode0_disable_flag * 0.75 (0 here). */
        }
    } else {
        /* mode 1 — L53846-L53892: animated diffuse/ambient pulse.  The
         * engine pulses between maplight_d[*][0] and maplight_d[*][1]
         * (and maplight_a pairs) via cos of (draw_counter * maplightspeed).
         * Direction uses the sun angle like mode 0. */
        float a = -ml_sun_angle();
        out->direction[0] = (float)cos((double)a) * 0.5f;
        out->direction[1] = -0.5f;
        out->direction[2] = (float)sin((double)a) * 0.5f;
        float t = (float)scene1_render_draw_counter() * rec->maplightspeed; /* +0x1ae8 */
        float c = ((float)cos((double)t) + 1.0f) * 0.5f;  /* 0..1 pulse */
        /* maplight_d[ch] = {lo, hi}; maplight_a[ch] = {lo, hi}. */
        out->diffuse[0] = (rec->maplight_d[0][1] - rec->maplight_d[0][0]) * c + rec->maplight_d[0][0];
        out->diffuse[1] = (rec->maplight_d[1][1] - rec->maplight_d[1][0]) * c + rec->maplight_d[1][0];
        out->diffuse[2] = (rec->maplight_d[2][1] - rec->maplight_d[2][0]) * c + rec->maplight_d[2][0];
        out->ambient[0] = (rec->maplight_a[0][1] - rec->maplight_a[0][0]) * c + rec->maplight_a[0][0];
        out->ambient[1] = (rec->maplight_a[1][1] - rec->maplight_a[1][0]) * c + rec->maplight_a[1][0];
        out->ambient[2] = (rec->maplight_a[2][1] - rec->maplight_a[2][0]) * c + rec->maplight_a[2][0];
    }

    /* L53900-L53929: chr-light ambient = light ambient + chrlightoffset,
     * clamped [0,1].  Consumed by the chr walker (FUN_004176ff, unported);
     * computed here so it inherits the right numbers when that ports. */
    float off = rec->chrlightoffset;  /* +0x1adc */
    out->chr_ambient[0] = clamp01(out->ambient[0] + off);
    out->chr_ambient[1] = clamp01(out->ambient[1] + off);
    out->chr_ambient[2] = clamp01(out->ambient[2] + off);

    return lit;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "save_work.h"        /* save_work_dwords_at / save_work_active_slot */
#include "save_bank.h"        /* SAVE_BANK_FIELD_CLOCK_TARGET (DAT_0450fb88)  */
#include "scene1_top_hud.h"   /* scene1_top_hud_clock_phase (DAT_0438b7d4)    */

/* Cached light built by scene1_build_maplight, re-applied by
 * scene1_maplight_rebind (engine keeps it at DAT_06a49a40). */
static D3DLIGHT8 g_maplight;
static int       g_maplight_lit;

static void fill_light(D3DLIGHT8 *L, const scene1_maplight_values_t *v)
{
    memset(L, 0, sizeof(*L));
    L->Type        = (D3DLIGHTTYPE)v->type;
    L->Diffuse.r   = v->diffuse[0];
    L->Diffuse.g   = v->diffuse[1];
    L->Diffuse.b   = v->diffuse[2];
    L->Ambient.r   = v->ambient[0];
    L->Ambient.g   = v->ambient[1];
    L->Ambient.b   = v->ambient[2];
    L->Direction.x = v->direction[0];
    L->Direction.y = v->direction[1];
    L->Direction.z = v->direction[2];
    /* Specular left zero (engine zeroes the whole struct first). */
}

void scene1_build_maplight(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    scene1_maplight_values_t v;
    const stage_record_t *rec = scene1_current_stage_record();

    /* Live mode-3 inputs (engine reads them as globals inside FUN_00458f67):
     *   shoptime    = (int)DAT_0450fb88[slot]  (working bank CLOCK_TARGET)
     *   clock_phase = DAT_0438b7d4             (top-HUD animated phase) */
    int   shoptime    = 0;
    float clock_phase = 0.0f;
    {
        const uint32_t *wb = save_work_dwords_at(save_work_active_slot());
        if (wb) shoptime = (int)wb[SAVE_BANK_FIELD_CLOCK_TARGET];
        clock_phase = scene1_top_hud_clock_phase();
    }
    g_maplight_lit = scene1_maplight_compute(rec, shoptime, clock_phase, &v);
    fill_light(&g_maplight, &v);

    if (g_maplight_lit) {
        /* L53894-L53899: SetLight(0,&light) + LightEnable(0,TRUE) +
         * SetRenderState(LIGHTING, TRUE). */
        IDirect3DDevice8_SetLight(dev, 0, &g_maplight);
        IDirect3DDevice8_LightEnable(dev, 0, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);
    } else {
        /* L53817-L53820: LightEnable(0,FALSE) + LIGHTING=FALSE. */
        IDirect3DDevice8_LightEnable(dev, 0, FALSE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    }
}

void scene1_maplight_rebind(struct IDirect3DDevice8 *dev_in, int enable)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    if (enable && g_maplight_lit) {
        IDirect3DDevice8_SetLight(dev, 0, &g_maplight);
        IDirect3DDevice8_LightEnable(dev, 0, TRUE);
    } else {
        IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    }
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, (enable && g_maplight_lit));
}

#endif /* _WIN32 */
