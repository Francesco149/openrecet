/* OpenRecet Trace Studio v3 — resident replay core (see replay_core.h).
 *
 * Same call+resource semantics as the proven replay.c (bit-exact), restructured so
 * open() creates the device + ALL resources + indexes frame byte-ranges ONCE, and
 * render(idx) issues just that frame's call section (resources already live) and
 * reads back. `step()` walks one record over an in-memory cursor, creating the
 * resource iff `do_res` and issuing the call iff `do_calls` — pass 1 (open):
 * do_res=1; render: do_calls=1. The whole container is in memory so VB/IB/UP/texel
 * data is referenced zero-copy.
 */
#define CINTERFACE
#define COBJMACROS
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../format/orv3_format.h"
#include "replay_core.h"

#define MAXRES 32768   /* matches the proxy's ORV3_MAXRES */

typedef struct { uint32_t start, end, present, draws, calls; } FrameRec;

struct OrV3Replay {
    uint8_t *data; size_t len;
    IDirect3D8 *d3d; IDirect3DDevice8 *dev; HWND hwnd; HINSTANCE hinst;
    uint32_t W, H;
    IDirect3DTexture8      *tex[MAXRES];
    uint8_t                 is_rt[MAXRES];   /* tex[id] created with RENDERTARGET usage */
    IDirect3DVertexBuffer8 *vb [MAXRES];
    IDirect3DIndexBuffer8  *ib [MAXRES];
    FrameRec *frames; int nframes;
    int has_rt;     /* any frame binds a render target ⇒ cross-frame RT content ⇒
                     * orv3_replay_render_history is required to reconstruct it */
    IDirect3DSurface8 *autods;  /* the auto depth-stencil, grabbed at create time;
                                 * the DEPTH surfref resolves to THIS, not the
                                 * live GetDepthStencilSurface (NULL after a blur
                                 * binds an RT with no depth → would un-depth the
                                 * restore + every later frame). */
    uint8_t *buf;   /* last readback (orv3_readback_bgra malloc'd) */
};

typedef struct { const uint8_t *p, *end; } Cur;
static uint32_t cu(Cur *c) { uint32_t v; if (c->p + 4 > c->end) return 0xffffffffu; memcpy(&v, c->p, 4); c->p += 4; return v; }

static int op_is_draw(uint32_t op)
{
    return op == ORV3_DrawPrimitive || op == ORV3_DrawIndexedPrimitive
        || op == ORV3_DrawPrimitiveUP || op == ORV3_DrawIndexedPrimitiveUP;
}
/* a "call" = any issued d3d call record (state/draw/clear/light/scene/RT), i.e. not
 * a RES definition and not the Present marker — what per-frame "calls" counts. */
static int op_is_call(uint32_t op)
{
    return (op >= ORV3_SetRenderState && op <= ORV3_EndScene)
        || op == ORV3_SetRenderTarget || op == ORV3_CopyRects;
}

/* Reconstruct the IDirect3DSurface8* a SURFREF [kind][resid] cites. The app's
 * surface handles aren't replayable, so we rebuild from kind: the backbuffer
 * (GetBackBuffer), the auto depth (GetDepthStencilSurface), or an RT texture's
 * level-0 surface (GetSurfaceLevel of tex[resid] — created with RENDERTARGET usage
 * by the RES_RT_TEX path). Returns an AddRef'd surface (caller Releases) or NULL. */
static IDirect3DSurface8 *resolve_surface(OrV3Replay *R, uint32_t kind, int32_t resid)
{
    IDirect3DSurface8 *s = NULL;
    switch (kind) {
    case ORV3_SURF_BACKBUFFER:
        IDirect3DDevice8_GetBackBuffer(R->dev, 0, D3DBACKBUFFER_TYPE_MONO, &s); break;
    case ORV3_SURF_DEPTH:
        /* the STORED auto depth-stencil, not the live one (which is NULL while an
         * RT with no depth is bound — the blur). AddRef so the caller can Release. */
        if (R->autods) { s = R->autods; IDirect3DSurface8_AddRef(s); }
        else IDirect3DDevice8_GetDepthStencilSurface(R->dev, &s);
        break;
    case ORV3_SURF_TEX:
        if (resid >= 0 && resid < MAXRES && R->tex[resid])
            IDirect3DTexture8_GetSurfaceLevel(R->tex[resid], 0, &s);
        break;
    default: break;   /* ORV3_SURF_NULL */
    }
    return s;
}

/* Walk ONE record at the cursor; advance past it; create the resource iff do_res;
 * issue the call iff do_calls. Draw calls are additionally gated on the [lo,hi)
 * window: `*ndrawn` counts draws ENCOUNTERED, and a draw is only ISSUED while
 * `lo <= *ndrawn < hi` (hi < 0 ⇒ unbounded above). State/clear/scene calls are
 * ALWAYS issued (so the device state is correct at any draw); the cursor always
 * advances past the full payload regardless, so a skipped draw still parses
 * correctly. [0,K) = prefix (build-up); [J,J+1) = a single draw in isolation over
 * the clear. Returns the op (ORV3_Present / ORV3_EOF terminate a walk); the caller
 * classifies it (op_is_draw/op_is_call) to count. */
static uint32_t step(Cur *c, OrV3Replay *R, int do_res, int do_calls,
                     int lo, int hi, int *ndrawn)
{
    uint32_t op = cu(c);
    if (op == 0xffffffffu || op == ORV3_EOF) return ORV3_EOF;
    IDirect3DDevice8 *dev = R->dev;
    /* a draw is issued only if calls are on, this is a draw op, and its index is in window */
    int draw_ok = do_calls && *ndrawn >= lo && (hi < 0 || *ndrawn < hi);
    if (op_is_draw(op)) { (*ndrawn)++; if (!draw_ok) do_calls = 0; }
    switch (op) {
    case ORV3_RES_TEX: {
        uint32_t id = cu(c), levels = cu(c);
        IDirect3DTexture8 *tex = NULL;
        for (uint32_t lvl = 0; lvl < levels; lvl++) {
            uint32_t lw = cu(c), lh = cu(c), lf = cu(c), lrb = cu(c), ld = cu(c);
            const uint8_t *ldata = c->p; c->p += ld;
            if (do_res && lvl == 0)
                IDirect3DDevice8_CreateTexture(dev, lw, lh, levels, 0, (D3DFORMAT)lf,
                                               D3DPOOL_MANAGED, &tex);
            if (do_res && tex && ld && lrb) {
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(IDirect3DTexture8_LockRect(tex, lvl, &lr, NULL, 0))) {
                    for (uint32_t r = 0; r < lh; r++)
                        memcpy((uint8_t *)lr.pBits + (size_t)r * lr.Pitch,
                               ldata + (size_t)r * lrb, lrb);
                    IDirect3DTexture8_UnlockRect(tex, lvl);
                }
            }
        }
        if (do_res && id < MAXRES) R->tex[id] = tex;
        break; }
    case ORV3_RES_VB: {
        uint32_t id = cu(c), size = cu(c), fvf = cu(c), dl = cu(c);
        const uint8_t *data = c->p; c->p += dl;
        if (do_res) {
            IDirect3DVertexBuffer8 *vb = NULL;
            IDirect3DDevice8_CreateVertexBuffer(dev, size, 0, fvf, D3DPOOL_MANAGED, &vb);
            BYTE *p = NULL;
            if (vb && SUCCEEDED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) && p) {
                memcpy(p, data, dl); IDirect3DVertexBuffer8_Unlock(vb);
            }
            if (id < MAXRES) R->vb[id] = vb;
        }
        break; }
    case ORV3_RES_IB: {
        uint32_t id = cu(c), size = cu(c), fmt = cu(c), dl = cu(c);
        const uint8_t *data = c->p; c->p += dl;
        if (do_res) {
            IDirect3DIndexBuffer8 *ib = NULL;
            IDirect3DDevice8_CreateIndexBuffer(dev, size, 0, (D3DFORMAT)fmt, D3DPOOL_MANAGED, &ib);
            BYTE *p = NULL;
            if (ib && SUCCEEDED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, 0)) && p) {
                memcpy(p, data, dl); IDirect3DIndexBuffer8_Unlock(ib);
            }
            if (id < MAXRES) R->ib[id] = ib;
        }
        break; }
    case ORV3_RES_RT_TEX: {   /* a render-target texture (no pixel data — filled by the replayed stream) */
        uint32_t id = cu(c), w = cu(c), h = cu(c), fmt = cu(c), levels = cu(c), usage = cu(c);
        if (do_res) {
            IDirect3DTexture8 *tex = NULL;
            /* RENDERTARGET usage ⇒ MUST be D3DPOOL_DEFAULT (can't be MANAGED). Its
             * level-0 surface is bound by SetRenderTarget / sampled by SetTexture. */
            IDirect3DDevice8_CreateTexture(dev, w, h, levels, usage, (D3DFORMAT)fmt,
                                           D3DPOOL_DEFAULT, &tex);
            if (id < MAXRES) R->tex[id] = tex;
        }
        break; }
    case ORV3_SetRenderState: { uint32_t s = cu(c), v = cu(c);
        if (do_calls) { IDirect3DDevice8_SetRenderState(dev, (D3DRENDERSTATETYPE)s, v); } break; }
    case ORV3_SetTextureStageState: { uint32_t st = cu(c), t = cu(c), v = cu(c);
        if (do_calls) { IDirect3DDevice8_SetTextureStageState(dev, st, (D3DTEXTURESTAGESTATETYPE)t, v); } break; }
    case ORV3_SetTransform: { uint32_t s = cu(c); const float *mx = (const float *)c->p; c->p += 64;
        if (do_calls) { IDirect3DDevice8_SetTransform(dev, (D3DTRANSFORMSTATETYPE)s, (const D3DMATRIX *)mx); } break; }
    case ORV3_SetMaterial: { const float *mt = (const float *)c->p; c->p += 68;
        if (do_calls) { IDirect3DDevice8_SetMaterial(dev, (const D3DMATERIAL8 *)mt); } break; }
    case ORV3_SetTexture: { uint32_t stage = cu(c); int32_t id = (int32_t)cu(c);
        if (do_calls) { IDirect3DDevice8_SetTexture(dev, stage, (id >= 0 && id < MAXRES) ? (IDirect3DBaseTexture8 *)R->tex[id] : NULL); } break; }
    case ORV3_SetStreamSource: { uint32_t stream = cu(c); int32_t id = (int32_t)cu(c); uint32_t stride = cu(c);
        if (do_calls) { IDirect3DDevice8_SetStreamSource(dev, stream, (id >= 0 && id < MAXRES) ? R->vb[id] : NULL, stride); } break; }
    case ORV3_SetIndices: { int32_t id = (int32_t)cu(c); uint32_t base = cu(c);
        if (do_calls) { IDirect3DDevice8_SetIndices(dev, (id >= 0 && id < MAXRES) ? R->ib[id] : NULL, base); } break; }
    case ORV3_SetVertexShader: { uint32_t h = cu(c);
        if (do_calls) { IDirect3DDevice8_SetVertexShader(dev, h); } break; }
    case ORV3_DrawPrimitive: { uint32_t pt = cu(c), sv = cu(c), pc = cu(c);
        if (do_calls) { IDirect3DDevice8_DrawPrimitive(dev, (D3DPRIMITIVETYPE)pt, sv, pc); } break; }
    case ORV3_DrawIndexedPrimitive: { uint32_t pt = cu(c), mi = cu(c), nv = cu(c), si = cu(c), pc = cu(c);
        if (do_calls) { IDirect3DDevice8_DrawIndexedPrimitive(dev, (D3DPRIMITIVETYPE)pt, mi, nv, si, pc); } break; }
    case ORV3_DrawPrimitiveUP: { uint32_t pt = cu(c), pc = cu(c), stride = cu(c), dl = cu(c);
        const void *v = c->p; c->p += dl;
        if (do_calls) { IDirect3DDevice8_DrawPrimitiveUP(dev, (D3DPRIMITIVETYPE)pt, pc, v, stride); } break; }
    case ORV3_DrawIndexedPrimitiveUP: { uint32_t pt = cu(c), mvi = cu(c), nvi = cu(c), pc = cu(c), ifmt = cu(c), il = cu(c);
        const void *idx = c->p; c->p += il; uint32_t stride = cu(c), vl = cu(c); const void *v = c->p; c->p += vl;
        if (do_calls) { IDirect3DDevice8_DrawIndexedPrimitiveUP(dev, (D3DPRIMITIVETYPE)pt, mvi, nvi, pc, idx, (D3DFORMAT)ifmt, v, stride); } break; }
    case ORV3_Clear: { uint32_t count = cu(c); const D3DRECT *rects = (const D3DRECT *)c->p; c->p += (size_t)count * 16;
        uint32_t flags = cu(c), color = cu(c), zb = cu(c), stencil = cu(c); float z; memcpy(&z, &zb, 4);
        if (do_calls) { IDirect3DDevice8_Clear(dev, count, count ? rects : NULL, flags, color, z, stencil); } break; }
    case ORV3_SetLight: { uint32_t index = cu(c), dl = cu(c); const void *L = c->p; c->p += dl;
        if (do_calls && dl) { IDirect3DDevice8_SetLight(dev, index, (const D3DLIGHT8 *)L); } break; }
    case ORV3_LightEnable: { uint32_t index = cu(c), en = cu(c);
        if (do_calls) { IDirect3DDevice8_LightEnable(dev, index, en); } break; }
    case ORV3_SetRenderTarget: {
        uint32_t ck = cu(c); int32_t cr = (int32_t)cu(c);
        uint32_t dk = cu(c); int32_t dr = (int32_t)cu(c);
        if (do_calls) {
            IDirect3DSurface8 *cs = resolve_surface(R, ck, cr);
            IDirect3DSurface8 *ds = resolve_surface(R, dk, dr);
            IDirect3DDevice8_SetRenderTarget(dev, cs, ds);
            if (cs) IDirect3DSurface8_Release(cs);
            if (ds) IDirect3DSurface8_Release(ds);
        }
        break; }
    case ORV3_CopyRects: {
        uint32_t sk = cu(c); int32_t sr = (int32_t)cu(c);
        uint32_t dk = cu(c); int32_t dr = (int32_t)cu(c);
        uint32_t count = cu(c);
        const RECT *rects = (const RECT *)c->p; c->p += (size_t)count * sizeof(RECT);
        const POINT *points = (const POINT *)c->p; c->p += (size_t)count * sizeof(POINT);
        if (do_calls) {
            IDirect3DSurface8 *ss = resolve_surface(R, sk, sr);
            IDirect3DSurface8 *ds = resolve_surface(R, dk, dr);
            if (ss && ds)
                IDirect3DDevice8_CopyRects(dev, ss, count ? rects : NULL, count, ds, count ? points : NULL);
            if (ss) IDirect3DSurface8_Release(ss);
            if (ds) IDirect3DSurface8_Release(ds);
        }
        break; }
    case ORV3_BeginScene: if (do_calls) { IDirect3DDevice8_BeginScene(dev); } break;
    case ORV3_EndScene:   if (do_calls) { IDirect3DDevice8_EndScene(dev);   } break;
    case ORV3_Present: cu(c); return ORV3_Present;
    default: return 0xfffffffeu;   /* unknown op */
    }
    return op;
}

OrV3Replay *orv3_replay_open(const char *cap_path, char *err, int errlen)
{
    #define FAIL(msg) do { if (err) snprintf(err, errlen, "%s", msg); orv3_replay_close(R); return NULL; } while (0)
    OrV3Replay *R = (OrV3Replay *)calloc(1, sizeof(*R));
    if (!R) return NULL;

    FILE *f = fopen(cap_path, "rb");
    if (!f) FAIL("cannot open container");
    fseek(f, 0, SEEK_END); R->len = ftell(f); fseek(f, 0, SEEK_SET);
    R->data = (uint8_t *)malloc(R->len);
    if (!R->data || fread(R->data, 1, R->len, f) != R->len) { fclose(f); FAIL("read container"); }
    fclose(f);

    Cur c = { R->data, R->data + R->len };
    if (cu(&c) != ORV3_MAGIC) FAIL("bad magic");
    cu(&c); /* version */
    if (cu(&c) != ORV3_DEV_PARAMS) FAIL("expected DEV_PARAMS");
    uint32_t W = cu(&c), H = cu(&c), bbfmt = cu(&c), depthfmt = cu(&c), windowed = cu(&c),
             bbcount = cu(&c), presentflags = cu(&c), behavior = cu(&c), interval = cu(&c),
             adapter = cu(&c), devtype = cu(&c), autods = cu(&c);
    (void)interval;
    R->W = W; R->H = H;

    R->hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = R->hinst;
    wc.lpszClassName = "orv3replaycore"; RegisterClassA(&wc);
    R->hwnd = CreateWindowA("orv3replaycore", "orv3", WS_OVERLAPPEDWINDOW, 0, 0, W, H,
                            NULL, NULL, R->hinst, NULL);
    R->d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!R->d3d) FAIL("Direct3DCreate8");
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = W; pp.BackBufferHeight = H; pp.BackBufferFormat = bbfmt;
    pp.BackBufferCount = bbcount ? bbcount : 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = windowed ? TRUE : FALSE; pp.Flags = presentflags;
    pp.EnableAutoDepthStencil = autods ? TRUE : FALSE; pp.AutoDepthStencilFormat = depthfmt;
    pp.hDeviceWindow = R->hwnd;
    if (FAILED(IDirect3D8_CreateDevice(R->d3d, adapter, (D3DDEVTYPE)devtype, R->hwnd,
                                       behavior, &pp, &R->dev)))
        FAIL("CreateDevice");
    /* Grab the auto depth-stencil NOW, while it's bound — the DEPTH surfref must
     * resolve to it even after a blur unbinds the live one (see resolve_surface). */
    IDirect3DDevice8_GetDepthStencilSurface(R->dev, &R->autods);

    /* pass 1: create every resource + index each kept frame's [start,end) + counts */
    int cap = 16; R->frames = (FrameRec *)malloc(cap * sizeof(FrameRec)); R->nframes = 0;
    uint32_t sect_start = (uint32_t)(c.p - R->data), ndraws = 0, ncalls = 0;
    int dummy = 0;
    for (;;) {
        uint32_t op = step(&c, R, /*do_res*/1, /*do_calls*/0, /*lo*/0, /*hi*/-1, &dummy);
        if (op == ORV3_EOF) break;
        if (op == 0xfffffffeu) FAIL("unknown op in container");
        if (op_is_draw(op)) ndraws++;
        if (op_is_call(op)) ncalls++;
        if (op == ORV3_SetRenderTarget) R->has_rt = 1;
        if (op == ORV3_Present) {
            if (R->nframes == cap) { cap *= 2; R->frames = (FrameRec *)realloc(R->frames, cap * sizeof(FrameRec)); }
            uint32_t endoff = (uint32_t)(c.p - R->data);
            R->frames[R->nframes].start = sect_start;
            R->frames[R->nframes].end = endoff;
            R->frames[R->nframes].present = 0;   /* payload already consumed; not needed here */
            R->frames[R->nframes].draws = ndraws;
            R->frames[R->nframes].calls = ncalls;
            R->nframes++;
            sect_start = endoff; ndraws = ncalls = 0;
        }
    }
    if (R->nframes == 0) FAIL("no kept frames");
    return R;
    #undef FAIL
}

int orv3_replay_count (const OrV3Replay *r) { return r ? r->nframes : 0; }
int orv3_replay_has_rt(const OrV3Replay *r) { return r ? r->has_rt : 0; }
int orv3_replay_width (const OrV3Replay *r) { return r ? (int)r->W : 0; }
int orv3_replay_height(const OrV3Replay *r) { return r ? (int)r->H : 0; }
int orv3_replay_draws(const OrV3Replay *r, int i) { return (r && i >= 0 && i < r->nframes) ? (int)r->frames[i].draws : -1; }
int orv3_replay_calls(const OrV3Replay *r, int i) { return (r && i >= 0 && i < r->nframes) ? (int)r->frames[i].calls : -1; }

const uint8_t *orv3_replay_render_range(OrV3Replay *r, int idx, int lo, int hi)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    if (lo < 0) lo = 0;
    Cur c = { r->data + r->frames[idx].start, r->data + r->frames[idx].end };
    int ndrawn = 0;
    for (;;) {
        uint32_t op = step(&c, r, /*do_res*/0, /*do_calls*/1, lo, hi, &ndrawn);
        if (op == ORV3_EOF || op == ORV3_Present) break;
        if (op == 0xfffffffeu) return NULL;
    }
    if (r->buf) { free(r->buf); r->buf = NULL; }
    uint32_t gw = 0, gh = 0;
    r->buf = orv3_readback_bgra(r->dev, &gw, &gh);
    if (!r->buf || gw != r->W || gh != r->H) return NULL;
    return r->buf;
}

const uint8_t *orv3_replay_render_upto(OrV3Replay *r, int idx, int max_draws)
{
    return orv3_replay_render_range(r, idx, 0, max_draws);
}

const uint8_t *orv3_replay_render(OrV3Replay *r, int idx)
{
    return orv3_replay_render_range(r, idx, 0, -1);
}

const uint8_t *orv3_replay_render_history(OrV3Replay *r, int idx)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    /* Replay frames [0..idx] in sequence on the resident device WITHOUT readback
     * or RT-reset between them, so cross-frame render-target content accumulates
     * (an RT filled at the open-ramp frame is still populated when a later rest
     * frame samples it — the pause backdrop [0]). For an RT-free container this
     * equals the single-frame render: each frame's Clear + preamble + per-draw
     * rebinds overwrite the prior frames, which leave no trace on the backbuffer.
     * O(idx) per call — for random-access scrub the viewer can cache; an in-ORDER
     * sweep (verify) gets the same accumulation in O(1) per frame for free. */
    for (int fi = 0; fi <= idx; fi++) {
        Cur c = { r->data + r->frames[fi].start, r->data + r->frames[fi].end };
        int ndrawn = 0;
        for (;;) {
            uint32_t op = step(&c, r, /*do_res*/0, /*do_calls*/1, /*lo*/0, /*hi*/-1, &ndrawn);
            if (op == ORV3_EOF || op == ORV3_Present) break;
            if (op == 0xfffffffeu) return NULL;
        }
    }
    if (r->buf) { free(r->buf); r->buf = NULL; }
    uint32_t gw = 0, gh = 0;
    r->buf = orv3_readback_bgra(r->dev, &gw, &gh);
    if (!r->buf || gw != r->W || gh != r->H) return NULL;
    return r->buf;
}

void orv3_replay_close(OrV3Replay *r)
{
    if (!r) return;
    for (int i = 0; i < MAXRES; i++) {
        if (r->tex[i]) IDirect3DTexture8_Release(r->tex[i]);
        if (r->vb[i])  IDirect3DVertexBuffer8_Release(r->vb[i]);
        if (r->ib[i])  IDirect3DIndexBuffer8_Release(r->ib[i]);
    }
    if (r->autods) IDirect3DSurface8_Release(r->autods);
    if (r->dev) IDirect3DDevice8_Release(r->dev);
    if (r->d3d) IDirect3D8_Release(r->d3d);
    if (r->hwnd) DestroyWindow(r->hwnd);
    free(r->frames); free(r->buf); free(r->data); free(r);
}
