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
static int   g_overlay = 0;           /* hikari-plane recolour overlay (F6) */
static int   g_mode = 0;              /* 0 tint / 1 flat / 2 border */
#define LD_MODE_COUNT 3

/* free camera pose */
static float g_eye[3];
static float g_yaw, g_pitch;          /* radians; yaw about +Y (+yaw = turn left) */

/* scripted flyoff (F7): a one-shot cinematic dolly, the recettear-study Godot
 * --flyout shape but richer — hold → pull back+up into the void (looking UP at
 * the peak to reveal the flat painted town behind the back wall) → swing to the
 * side → dip low → fly THROUGH the window and pan left/right over the theatre-
 * flat outside → come home.
 *
 * The path is keyed in the game's locked-pose LOCAL frame (captured on F7):
 * basis fwd0 / up=(0,1,0) / right0, and the default focus pivot0 =
 * eye0 + fwd0·PIVOT (room centre).  Each keyframe gives the EYE offset and the
 * LOOKAT offset in that frame (units = world units along right/up/fwd):
 *   eye    = eye0   + ef·fwd0 + eu·up + er·right0
 *   lookat = pivot0 + lf·fwd0 + lu·up + lr·right0
 * (−ef = pull back off the angle; +ef = dolly toward/through the far wall;
 *  +lu = look up; ±lr = look left/right; +lf = look further out the window). */
static int   g_fly_on = 0;            /* a flyoff is playing */
static float g_fly_t = 0.0f;          /* seconds into the path */
static float g_fly_eye0[3], g_fly_fwd0[3], g_fly_right0[3], g_fly_pivot0[3];
#define LD_FLY_PIVOT  10.0f           /* focal distance eye→room-centre (world units) */
#define LD_FLY_DT     (1.0f/60.0f)    /* path advance per render tick (~60fps) */

/* LD_WIN = horizontal offset (world units, −=left) that re-centres the DESCENT
 * on the back window.  The locked ¾ forward aims right-of-window (toward the
 * door), so without this the dip + flythrough pop out over the door and the
 * left window instead of THROUGH the back window.  Tune this one number if the
 * dive still isn't dead-centre on the window. */
#define LD_WIN (-8.0f)

/* keyframes: {t_sec, ef, eu, er, lf, lu, lr}.  Total ~16s = ~20% faster than
 * the old 20s path.  lerp'd with smoothstep so each leg eases in/out.
 * The eye offset (er) and the look offset (lr) both carry LD_WIN through the
 * descent so the eye is IN FRONT OF the window and aimed at it — then the
 * flythrough passes through it and the L/R pan swings around IT (not the door). */
static const struct { float t, ef, eu, er, lf, lu, lr; } LD_FLY[] = {
    { 0.0f,   0.0f,  0.0f,       0.0f,        0.0f,  0.0f,  0.0f        },  /* locked ¾ pose */
    { 1.0f,   0.0f,  0.0f,       0.0f,        0.0f,  0.0f,  0.0f        },  /* hold — sell the normal frame */
    { 4.5f, -16.0f, 15.0f,       0.0f,        0.0f,  4.0f,  0.0f        },  /* pull back+up; LOOK UP → the town flat */
    { 7.8f, -10.0f, 11.0f, LD_WIN-4.0f,       0.0f,  1.0f,  LD_WIN*0.5f },  /* swing to the WINDOW side, still high */
    {10.2f,   2.0f, -4.0f,       LD_WIN,      0.0f, -0.5f,  LD_WIN      },  /* dip low, centred ON the back window */
    {12.2f,  16.0f, -3.0f,       LD_WIN,      9.0f,  0.0f,  LD_WIN-6.0f },  /* fly THROUGH the window, look LEFT (out) */
    {13.6f,  17.0f, -3.0f,       LD_WIN,      9.0f,  0.0f,  LD_WIN+6.0f },  /* look RIGHT over the town flat */
    {16.0f,   0.0f,  0.0f,       0.0f,        0.0f,  0.0f,  0.0f        },  /* come home to the locked pose */
};
#define LD_FLY_N ((int)(sizeof(LD_FLY)/sizeof(LD_FLY[0])))

/* eased motion state — velocities lerp toward the key/mouse targets so
 * dolly moves start and stop softly (video-friendly) */
static float g_vel[3];                /* world units / frame */
static float g_avel_yaw, g_avel_pitch;/* radians / frame (keyboard look) */
static float g_mvel_yaw, g_mvel_pitch;/* radians / frame (mouse look) */

/* mouse capture (owned while the mode is on) */
static void *g_hwnd = NULL;           /* HWND from main.c */
static int   g_mouse_captured = 0;

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
int light_debug_overlay_active(void) { return g_on && g_overlay; }

void light_debug_set_mode(int mode)
{
    if (mode < 0) mode = 0;
    if (mode >= LD_MODE_COUNT) mode = LD_MODE_COUNT - 1;
    g_mode = mode;
    g_overlay = 1;   /* an explicit mode request (--light-debug-mode) means show it */
}

static int g_autostart = 0;
void light_debug_set_autostart(void) { g_autostart = 1; }
void light_debug_maybe_autostart(const float current_view[16])
{
    if (!g_autostart || g_on) return;
    g_autostart = 0;
    light_debug_toggle(current_view);
}

/* F6: step the hikari overlay through off → tint → flat → border → off.
 * (The camera keeps running with the scene rendered normally when off.) */
void light_debug_cycle_mode(void)
{
    if (!g_on) return;
    if (!g_overlay) { g_overlay = 1; g_mode = 0; }
    else if (g_mode + 1 >= LD_MODE_COUNT) { g_overlay = 0; }
    else { g_mode++; }
    if (!g_overlay)
        fprintf(stderr, "light-debug: overlay off (scene normal)\n");
    else
        fprintf(stderr, "light-debug: overlay %s\n",
                g_mode == 0 ? "tint" : g_mode == 1 ? "flat" : "tint+border");
}

void light_debug_set_hwnd(void *hwnd) { g_hwnd = hwnd; }

/* Centre the cursor in the game window's client area; returns the centre
 * in screen coords via out.  Falls back to the foreground window when no
 * hwnd was handed over (headless runs never get here — no captured mouse
 * deltas without a real cursor moving). */
static void ld_cursor_centre(POINT *out)
{
    HWND h = (HWND)g_hwnd;
    if (!h) h = GetForegroundWindow();
    RECT rc;
    if (h && GetClientRect(h, &rc)) {
        POINT c = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
        ClientToScreen(h, &c);
        *out = c;
    } else {
        out->x = GetSystemMetrics(SM_CXSCREEN) / 2;
        out->y = GetSystemMetrics(SM_CYSCREEN) / 2;
    }
}

/* Only steer the real cursor when the game window is focused — SetCursorPos
 * is GLOBAL, and a hidden/background run (--light-debug headless captures)
 * must never yank the user's desktop mouse around. */
static int ld_window_focused(void)
{
    HWND h = (HWND)g_hwnd;
    return h && GetForegroundWindow() == h;
}

static void ld_mouse_capture(int on)
{
    if (on == g_mouse_captured) return;
    g_mouse_captured = on;
    ShowCursor(on ? FALSE : TRUE);      /* balanced; per-thread, so own window only */
    if (on && ld_window_focused()) {
        POINT c;
        ld_cursor_centre(&c);
        SetCursorPos(c.x, c.y);
    }
}

/* Recover eye + yaw/pitch from a row-vector D3D view matrix: the upper
 * 3x3's COLUMNS are the camera axes; row 3 is -eye·axis per column. */
void light_debug_toggle(const float current_view[16])
{
    g_on = !g_on;
    fprintf(stderr, "light-debug: %s\n", g_on ? "ON (WASD/QE move, mouse/arrows look, "
            "SHIFT fast, CTRL slow; F6 cycles viz mode)" : "off");
    ld_mouse_capture(g_on);
    if (!g_on || !current_view) return;

    g_vel[0] = g_vel[1] = g_vel[2] = 0.0f;
    g_avel_yaw = g_avel_pitch = 0.0f;
    g_mvel_yaw = g_mvel_pitch = 0.0f;

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

/* F7: play the one-shot cinematic flyoff from the game's current locked
 * pose.  Engages the free camera if needed, snapshots the orbit pivot +
 * reference offset, and starts the path at t=0 (press again to replay). */
void light_debug_flyoff(const float current_view[16])
{
    if (!g_on) light_debug_toggle(current_view);   /* seamless from game cam */
    /* local frame of the current pose: fwd0, right0 (horizontal), pivot0 */
    float cp = (float)cos(g_pitch), sp = (float)sin(g_pitch);
    float sy = (float)sin(g_yaw),   cy = (float)cos(g_yaw);
    g_fly_fwd0[0] = cp * sy; g_fly_fwd0[1] = sp; g_fly_fwd0[2] = cp * cy;
    g_fly_right0[0] = cy; g_fly_right0[1] = 0.0f; g_fly_right0[2] = -sy;  /* horizontal right */
    for (int i = 0; i < 3; i++) {
        g_fly_eye0[i]   = g_eye[i];
        g_fly_pivot0[i] = g_eye[i] + g_fly_fwd0[i] * LD_FLY_PIVOT;
    }
    g_fly_t = 0.0f;
    g_fly_on = 1;
    fprintf(stderr, "light-debug: flyoff playing (%.1fs reveal — up-peek + window flythrough)\n",
            LD_FLY[LD_FLY_N-1].t);
}

/* ─── free camera ───────────────────────────────────────────────────── */

static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* smoothstep — eased 0..1 so the scripted dolly starts and stops softly */
static float ld_smooth(float t)
{
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

void light_debug_camera_tick(float out_view[16])
{
    /* target speeds per render frame (60fps normally; turbo flies faster) */
    float move = 0.12f, look = 0.030f;
    if (key(VK_SHIFT))   { move *= 4.0f; look *= 2.0f; }
    if (key(VK_CONTROL)) { move *= 0.25f; look *= 0.5f; }

    /* ── scripted flyoff: orbit the room pivot along the keyframed path;
     * touching any move key hands control back to manual mid-flight. ── */
    if (g_fly_on) {
        if (key('W')||key('S')||key('A')||key('D')||key('Q')||key('E')) {
            g_fly_on = 0;   /* manual override from wherever the dolly is */
        } else {
            g_fly_t += LD_FLY_DT;
            /* find the active keyframe leg and smoothstep across it */
            float ef, eu, er, lf, lu, lr;
            if (g_fly_t >= LD_FLY[LD_FLY_N-1].t) {
                const int L = LD_FLY_N-1;
                ef=LD_FLY[L].ef; eu=LD_FLY[L].eu; er=LD_FLY[L].er;
                lf=LD_FLY[L].lf; lu=LD_FLY[L].lu; lr=LD_FLY[L].lr;
                g_fly_on = 0;   /* path complete — hold home; manual resumes here */
            } else {
                int k = 0;
                while (k < LD_FLY_N-1 && g_fly_t > LD_FLY[k+1].t) k++;
                float u = (g_fly_t - LD_FLY[k].t) / (LD_FLY[k+1].t - LD_FLY[k].t);
                float s = ld_smooth(u);
                #define LDL(f) (LD_FLY[k].f + s * (LD_FLY[k+1].f - LD_FLY[k].f))
                ef=LDL(ef); eu=LDL(eu); er=LDL(er); lf=LDL(lf); lu=LDL(lu); lr=LDL(lr);
                #undef LDL
            }
            /* eye + lookat from the local-frame offsets */
            for (int i = 0; i < 3; i++) {
                float up = (i==1) ? 1.0f : 0.0f;
                g_eye[i] = g_fly_eye0[i] + ef*g_fly_fwd0[i] + eu*up + er*g_fly_right0[i];
            }
            float lookat[3];
            for (int i = 0; i < 3; i++) {
                float up = (i==1) ? 1.0f : 0.0f;
                lookat[i] = g_fly_pivot0[i] + lf*g_fly_fwd0[i] + lu*up + lr*g_fly_right0[i];
            }
            float Ff[3] = { lookat[0]-g_eye[0], lookat[1]-g_eye[1], lookat[2]-g_eye[2] };
            float fl = (float)sqrt(Ff[0]*Ff[0]+Ff[1]*Ff[1]+Ff[2]*Ff[2]);
            if (fl > 1e-6f) { Ff[0]/=fl; Ff[1]/=fl; Ff[2]/=fl; }
            g_pitch = (float)asin(Ff[1] < -1.f ? -1.f : Ff[1] > 1.f ? 1.f : Ff[1]);
            g_yaw   = (float)atan2(Ff[0], Ff[2]);
            float za[3] = { -Ff[0], -Ff[1], -Ff[2] };
            float xa[3] = { za[2], 0.0f, -za[0] };
            float xln = (float)sqrt(xa[0]*xa[0] + xa[2]*xa[2]);
            if (xln > 1e-6f) { xa[0] /= xln; xa[2] /= xln; } else { xa[0] = 1.0f; xa[2] = 0.0f; }
            float ya[3] = {
                za[1]*xa[2] - za[2]*xa[1], za[2]*xa[0] - za[0]*xa[2], za[0]*xa[1] - za[1]*xa[0] };
            out_view[0]=xa[0]; out_view[1]=ya[0]; out_view[2]=za[0]; out_view[3]=0;
            out_view[4]=xa[1]; out_view[5]=ya[1]; out_view[6]=za[1]; out_view[7]=0;
            out_view[8]=xa[2]; out_view[9]=ya[2]; out_view[10]=za[2]; out_view[11]=0;
            out_view[12]=-(g_eye[0]*xa[0]+g_eye[1]*xa[1]+g_eye[2]*xa[2]);
            out_view[13]=-(g_eye[0]*ya[0]+g_eye[1]*ya[1]+g_eye[2]*ya[2]);
            out_view[14]=-(g_eye[0]*za[0]+g_eye[1]*za[1]+g_eye[2]*za[2]);
            out_view[15]=1.0f;
            /* zero the manual velocities so the handoff at path-end doesn't lurch */
            g_vel[0]=g_vel[1]=g_vel[2]=0.0f;
            g_avel_yaw=g_avel_pitch=g_mvel_yaw=g_mvel_pitch=0.0f;
            return;
        }
    }

    /* ── look: mouse deltas (captured, recentred every tick) + arrows ──
     * Everything goes through an eased angular velocity so pans start and
     * stop softly on video.  Convention: +yaw turns LEFT (F swings from
     * +z toward +x, and +x is screen-left in this RH world). */
    float mdx = 0.0f, mdy = 0.0f;
    if (g_mouse_captured && ld_window_focused()) {
        POINT c, p;
        ld_cursor_centre(&c);
        if (GetCursorPos(&p)) {
            mdx = (float)(p.x - c.x);
            mdy = (float)(p.y - c.y);
            SetCursorPos(c.x, c.y);
        }
    }
    const float MSENS = 0.0032f;    /* rad per pixel */
    const float MEASE = 0.22f;      /* mouse smoothing (higher = snappier) */
    const float KEASE = 0.09f;      /* key accel (lower = softer ease) */
    g_mvel_yaw   += (-mdx * MSENS - g_mvel_yaw)   * MEASE;  /* mouse right → turn right */
    g_mvel_pitch += (-mdy * MSENS - g_mvel_pitch) * MEASE;  /* mouse up    → look up    */

    float ty = 0.0f, tp = 0.0f;     /* arrow-key angular targets */
    if (key(VK_LEFT))  ty += look;  /* left arrow → turn left */
    if (key(VK_RIGHT)) ty -= look;
    if (key(VK_UP))    tp += look;
    if (key(VK_DOWN))  tp -= look;
    g_avel_yaw   += (ty - g_avel_yaw)   * KEASE;
    g_avel_pitch += (tp - g_avel_pitch) * KEASE;

    g_yaw   += g_avel_yaw   + g_mvel_yaw;
    g_pitch += g_avel_pitch + g_mvel_pitch;
    const float plim = 1.55f;
    if (g_pitch >  plim) g_pitch =  plim;
    if (g_pitch < -plim) g_pitch = -plim;

    float cp = (float)cos(g_pitch), sp = (float)sin(g_pitch);
    float sy = (float)sin(g_yaw),   cy = (float)cos(g_yaw);
    float F[3] = { cp * sy, sp, cp * cy };              /* forward */
    float R[3] = { -cy, 0.0f, sy };                     /* screen-right (flat) */

    /* ── move: eased velocity toward the key target ── */
    float T[3] = { 0.0f, 0.0f, 0.0f };
    if (key('W')) for (int i = 0; i < 3; i++) T[i] += F[i] * move;
    if (key('S')) for (int i = 0; i < 3; i++) T[i] -= F[i] * move;
    if (key('D')) for (int i = 0; i < 3; i++) T[i] += R[i] * move;
    if (key('A')) for (int i = 0; i < 3; i++) T[i] -= R[i] * move;
    if (key('E')) T[1] += move;
    if (key('Q')) T[1] -= move;
    for (int i = 0; i < 3; i++) {
        g_vel[i] += (T[i] - g_vel[i]) * KEASE;
        g_eye[i] += g_vel[i];
    }

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
