/*
 * light_debug.c — hikari light-plane visualization + free-fly camera.
 * See light_debug.h for the mode description and wiring map.
 *
 * NOT engine code — a study/recording tool layered on the port.  It only
 * ever runs when toggled (F5), so the TAS/trace paths are unaffected:
 * input_poll zeroes the game buttons while active, which also keeps a
 * running recording from picking up camera keys as gameplay.
 */
#include "light_debug.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <d3d8.h>

#include <math.h>
#include <stdio.h>

/* ─── state ─────────────────────────────────────────────────────────── */

static int   g_on = 0;
static int   g_mode = 0;              /* 0 tint / 1 flat / 2 border */
#define LD_MODE_COUNT 3

/* free camera pose */
static float g_eye[3];
static float g_yaw, g_pitch;          /* radians; yaw about +Y */

/* per-frame plane counter (reset at hikari_begin) */
static int   g_plane = 0;
static int   g_cur_plane = 0;         /* plane the current draw uses */

/* Fill / border colour pairs, one per hikari plane (draw order in the
 * pass-3 walk: g09, g10, g14, g15, nohit).  Saturated, mutually distant
 * hues; borders are the pair's bright complement so mode 2 reads even
 * where fills alone don't. */
static const D3DCOLOR LD_FILL[5] = {
    0xffb01818,   /* g09   — red        */
    0xff18a018,   /* g10   — green      */
    0xff2050e0,   /* g14   — blue       */
    0xffb020b0,   /* g15   — magenta    */
    0xffc07010,   /* nohit — orange     */
};
static const D3DCOLOR LD_BORDER[5] = {
    0xffffe040,   /* vs red     — yellow */
    0xffffffff,   /* vs green   — white  */
    0xff40e0ff,   /* vs blue    — cyan   */
    0xff80ff80,   /* vs magenta — mint   */
    0xffffffff,   /* vs orange  — white  */
};

/* ─── toggle / mode ─────────────────────────────────────────────────── */

int light_debug_active(void) { return g_on; }

void light_debug_set_mode(int mode)
{
    if (mode < 0) mode = 0;
    if (mode >= LD_MODE_COUNT) mode = LD_MODE_COUNT - 1;
    g_mode = mode;
}

static int g_autostart = 0;
void light_debug_set_autostart(void) { g_autostart = 1; }
void light_debug_maybe_autostart(const float current_view[16])
{
    if (!g_autostart || g_on) return;
    g_autostart = 0;
    light_debug_toggle(current_view);
}

void light_debug_cycle_mode(void)
{
    if (!g_on) return;
    g_mode = (g_mode + 1) % LD_MODE_COUNT;
    fprintf(stderr, "light-debug: mode %d (%s)\n", g_mode,
            g_mode == 0 ? "tint" : g_mode == 1 ? "flat" : "tint+border");
}

/* Recover eye + yaw/pitch from a row-vector D3D view matrix: the upper
 * 3x3's COLUMNS are the camera axes; row 3 is -eye·axis per column. */
void light_debug_toggle(const float current_view[16])
{
    g_on = !g_on;
    fprintf(stderr, "light-debug: %s\n", g_on ? "ON (WASD/QE move, arrows look, "
            "SHIFT fast, CTRL slow; F6 cycles viz mode)" : "off");
    if (!g_on || !current_view) return;

    const float *V = current_view;
    float xaxis[3] = { V[0], V[4], V[8]  };
    float yaxis[3] = { V[1], V[5], V[9]  };
    float zaxis[3] = { V[2], V[6], V[10] };   /* camera backward (RH) */
    for (int i = 0; i < 3; i++)
        g_eye[i] = -(V[12] * xaxis[i] + V[13] * yaxis[i] + V[14] * zaxis[i]);
    /* forward = -zaxis; F = (cosP·sinY, sinP, cosP·cosY) */
    float F[3] = { -zaxis[0], -zaxis[1], -zaxis[2] };
    g_pitch = (float)asin(F[1] < -1.f ? -1.f : F[1] > 1.f ? 1.f : F[1]);
    g_yaw   = (float)atan2(F[0], F[2]);
}

/* ─── free camera ───────────────────────────────────────────────────── */

static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void light_debug_camera_tick(float out_view[16])
{
    /* speeds are per render frame (60fps normally; turbo just flies faster) */
    float move = 0.12f, look = 0.030f;
    if (key(VK_SHIFT))   { move *= 4.0f; look *= 2.0f; }
    if (key(VK_CONTROL)) { move *= 0.25f; look *= 0.5f; }

    if (key(VK_LEFT))  g_yaw   -= look;
    if (key(VK_RIGHT)) g_yaw   += look;
    if (key(VK_UP))    g_pitch += look;
    if (key(VK_DOWN))  g_pitch -= look;
    const float plim = 1.55f;
    if (g_pitch >  plim) g_pitch =  plim;
    if (g_pitch < -plim) g_pitch = -plim;

    float cp = (float)cos(g_pitch), sp = (float)sin(g_pitch);
    float sy = (float)sin(g_yaw),   cy = (float)cos(g_yaw);
    float F[3] = { cp * sy, sp, cp * cy };              /* forward   */
    float R[3] = { cy, 0.0f, -sy };                     /* right (flat) */

    if (key('W')) for (int i = 0; i < 3; i++) g_eye[i] += F[i] * move;
    if (key('S')) for (int i = 0; i < 3; i++) g_eye[i] -= F[i] * move;
    if (key('D')) for (int i = 0; i < 3; i++) g_eye[i] += R[i] * move;
    if (key('A')) for (int i = 0; i < 3; i++) g_eye[i] -= R[i] * move;
    if (key('E')) g_eye[1] += move;
    if (key('Q')) g_eye[1] -= move;

    /* rebuild the row-vector RH view matrix (same shape the port's
     * camera builder emits: axes in columns, -eye·axis translation) */
    float zaxis[3] = { -F[0], -F[1], -F[2] };
    /* xaxis = normalize(cross(up, zaxis)), up = +Y */
    float xaxis[3] = { zaxis[2], 0.0f, -zaxis[0] };
    float xl = (float)sqrt(xaxis[0]*xaxis[0] + xaxis[2]*xaxis[2]);
    if (xl > 1e-6f) { xaxis[0] /= xl; xaxis[2] /= xl; }
    else { xaxis[0] = 1.0f; xaxis[2] = 0.0f; }          /* looking straight up/down */
    /* yaxis = cross(zaxis, xaxis) */
    float yaxis[3] = {
        zaxis[1]*xaxis[2] - zaxis[2]*xaxis[1],
        zaxis[2]*xaxis[0] - zaxis[0]*xaxis[2],
        zaxis[0]*xaxis[1] - zaxis[1]*xaxis[0],
    };

    out_view[0] = xaxis[0]; out_view[1] = yaxis[0]; out_view[2]  = zaxis[0]; out_view[3]  = 0;
    out_view[4] = xaxis[1]; out_view[5] = yaxis[1]; out_view[6]  = zaxis[1]; out_view[7]  = 0;
    out_view[8] = xaxis[2]; out_view[9] = yaxis[2]; out_view[10] = zaxis[2]; out_view[11] = 0;
    out_view[12] = -(g_eye[0]*xaxis[0] + g_eye[1]*xaxis[1] + g_eye[2]*xaxis[2]);
    out_view[13] = -(g_eye[0]*yaxis[0] + g_eye[1]*yaxis[1] + g_eye[2]*yaxis[2]);
    out_view[14] = -(g_eye[0]*zaxis[0] + g_eye[1]*zaxis[1] + g_eye[2]*zaxis[2]);
    out_view[15] = 1.0f;
}

/* ─── hikari plane draw overrides ───────────────────────────────────── */

void light_debug_hikari_begin(void) { g_plane = 0; }

void light_debug_plane_predraw(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    g_cur_plane = g_plane % 5;
    g_plane++;

    /* opaque, both-sided, unique hue; keep the z-test so the room still
     * occludes correctly, keep z-write off like the real pass */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, LD_FILL[g_cur_plane]);
    if (g_mode == 1) {
        /* flat: solid silhouette */
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    } else {
        /* tint: fill hue + lit vertex colour (two-tone) */
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADD);
    }
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
}

int light_debug_wire_enabled(void) { return g_mode == 2; }

void light_debug_wire_begin(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, LD_BORDER[g_cur_plane]);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
}

void light_debug_wire_end(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_SOLID);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, LD_FILL[g_cur_plane]);
    IDirect3DDevice8_SetTextureStageState(dev, 0,
        D3DTSS_COLOROP, g_mode == 1 ? D3DTOP_SELECTARG2 : D3DTOP_ADD);
}

void light_debug_hikari_end(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    /* restore what the overrides touched to the alpha-pass steady state
     * (the walker's caller re-asserts combiner + blend right after, and
     * its cleanup tail resets TEXTUREFACTOR/cull — this just makes sure
     * nothing leaks if that ever changes) */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_SOLID);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
}

#endif /* _WIN32 */
