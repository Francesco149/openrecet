/* OpenRecet Trace Studio v3 — proxy d3d8.dll (capture).
 *
 * Drop-in d3d8.dll used by BOTH the port and retail (app-dir loads before
 * System32). Wraps the factory + device and records the exact D3D8 command
 * stream + every referenced resource (textures/VB/IB, dedup by pointer) into a
 * flat container (orv3_format.h), sufficient to RE-RENDER each frame. Only the
 * device + factory are wrapped; resources are returned UNWRAPPED and snapshotted
 * lazily by read-only Lock (port allocates them D3DPOOL_MANAGED -> lockable).
 *
 * Capture spans frames [0, OPENRECET_V3_CAPFRAME] and the proxy reads back the
 * reference backbuffer at the target frame, so the replayer reconstructs the
 * full inherited device state and compares against the exact same frame.
 */
#define CINTERFACE
#define COBJMACROS
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "../format/orv3_format.h"

typedef struct WrapD3D { const IDirect3D8Vtbl       *lpVtbl; IDirect3D8       *real; LONG refs; } WrapD3D;
typedef struct WrapDev { const IDirect3DDevice8Vtbl *lpVtbl; IDirect3DDevice8 *real; LONG refs; } WrapDev;

/* factory custom */
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_QueryInterface(IDirect3D8*, REFIID, void**);
static ULONG   STDMETHODCALLTYPE my_IDirect3D8_AddRef(IDirect3D8*);
static ULONG   STDMETHODCALLTYPE my_IDirect3D8_Release(IDirect3D8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_CreateDevice(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
/* device custom (lifetime + every captured call) */
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_QueryInterface(IDirect3DDevice8*, REFIID, void**);
static ULONG   STDMETHODCALLTYPE my_IDirect3DDevice8_AddRef(IDirect3DDevice8*);
static ULONG   STDMETHODCALLTYPE my_IDirect3DDevice8_Release(IDirect3DDevice8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Present(IDirect3DDevice8*, const RECT*, const RECT*, HWND, const RGNDATA*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_BeginScene(IDirect3DDevice8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_EndScene(IDirect3DDevice8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Clear(IDirect3DDevice8*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetRenderState(IDirect3DDevice8*, D3DRENDERSTATETYPE, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTextureStageState(IDirect3DDevice8*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTransform(IDirect3DDevice8*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetMaterial(IDirect3DDevice8*, const D3DMATERIAL8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTexture(IDirect3DDevice8*, DWORD, IDirect3DBaseTexture8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetStreamSource(IDirect3DDevice8*, UINT, IDirect3DVertexBuffer8*, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetIndices(IDirect3DDevice8*, IDirect3DIndexBuffer8*, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetVertexShader(IDirect3DDevice8*, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitive(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitive(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitiveUP(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitiveUP(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetLight(IDirect3DDevice8*, DWORD, const D3DLIGHT8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_LightEnable(IDirect3DDevice8*, DWORD, WINBOOL);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_GetBackBuffer(IDirect3DDevice8*, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface8**);

#include "proxy_generated.h"

/* ── logging ── */
static FILE     *g_log;
static HINSTANCE g_self;
/* Resolve output to Windows-LOCAL NTFS (never the \\wsl.localhost 9p mount —
 * writing the container there throttles the engine to a crawl). Default
 * %LOCALAPPDATA%\openrecet\v3 (same fast-disk pattern the port uses for se.pack);
 * WSL reads it back via /mnt/c/...  OPENRECET_V3_OUT overrides. */
static void proxy_out_path(char *out, size_t n, const char *leaf)
{
    const char *env = getenv("OPENRECET_V3_OUT");
    if (env && *env) { snprintf(out, n, "%s\\%s", env, leaf); return; }
    static char base[MAX_PATH]; static int inited;
    if (!inited) {
        const char *lad = getenv("LOCALAPPDATA");
        if (lad && *lad) {
            char a[MAX_PATH]; snprintf(a, sizeof a, "%s\\openrecet", lad); CreateDirectoryA(a, NULL);
            snprintf(base, sizeof base, "%s\\openrecet\\v3", lad); CreateDirectoryA(base, NULL);
        } else {
            char tmp[MAX_PATH]; GetTempPathA(sizeof tmp, tmp);
            snprintf(base, sizeof base, "%sopenrecet_v3", tmp); CreateDirectoryA(base, NULL);
        }
        inited = 1;
    }
    snprintf(out, n, "%s\\%s", base, leaf);
}
static void proxy_log(const char *fmt, ...)
{
    if (!g_log) { char p[MAX_PATH+32]; proxy_out_path(p, sizeof p, "v3proxy.log");
        g_log = fopen(p, "w"); if (!g_log) return; setvbuf(g_log, NULL, _IOLBF, 0); }
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
}

/* ── capture state ── */
static FILE     *g_cap;            /* container file */
static unsigned  g_capframe = 0xFFFFFFFFu; /* present-count target; default = never (use the GetBackBuffer trigger) */
static unsigned  g_frame;          /* present-counted frame index */
static int       g_capturing;
static unsigned  g_present_count;
static long      g_frame_start_pos; /* container offset after DEV_PARAMS; single-frame mode rewinds here each frame */
/* resource dedup: ptr -> id */
#define ORV3_MAXRES 4096
static void     *g_res_ptr[ORV3_MAXRES];
static int       g_res_id [ORV3_MAXRES];
static int       g_n_res;
static int       g_next_resid;

static unsigned prim_vcount(D3DPRIMITIVETYPE t, unsigned pc)
{
    switch (t) {
    case D3DPT_POINTLIST: return pc;
    case D3DPT_LINELIST: return pc*2u;
    case D3DPT_LINESTRIP: return pc?pc+1u:0u;
    case D3DPT_TRIANGLELIST: return pc*3u;
    case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return pc?pc+2u:0u;
    default: return 0u;
    }
}
static int res_lookup(void *p)
{
    for (int i = 0; i < g_n_res; i++) if (g_res_ptr[i] == p) return g_res_id[i];
    return -1;
}
static void res_remember(void *p, int id)
{
    if (g_n_res < ORV3_MAXRES) { g_res_ptr[g_n_res] = p; g_res_id[g_n_res] = id; g_n_res++; }
}

/* snapshot a 2D texture's managed sysmem copy (all mip levels) into the container */
static int snap_tex(IDirect3DBaseTexture8 *bt)
{
    if (!bt) return -1;
    int id = res_lookup(bt); if (id >= 0) return id;
    if (IDirect3DBaseTexture8_GetType(bt) != D3DRTYPE_TEXTURE) return -1;  /* P0: 2D only */
    IDirect3DTexture8 *tex = (IDirect3DTexture8*)bt;
    id = g_next_resid++;
    DWORD levels = IDirect3DTexture8_GetLevelCount(tex);
    orv3_wu(g_cap, ORV3_RES_TEX); orv3_wu(g_cap, (uint32_t)id); orv3_wu(g_cap, levels);
    for (DWORD l = 0; l < levels; l++) {
        D3DSURFACE_DESC d = {0}; IDirect3DTexture8_GetLevelDesc(tex, l, &d);
        D3DLOCKED_RECT lr = {0};
        int bpp = orv3_fmt_bpp((int)d.Format);
        if (FAILED(IDirect3DTexture8_LockRect(tex, l, &lr, NULL, D3DLOCK_READONLY)) || !bpp) {
            orv3_wu(g_cap, d.Width); orv3_wu(g_cap, d.Height); orv3_wu(g_cap, d.Format);
            orv3_wu(g_cap, 0); orv3_wu(g_cap, 0);                 /* rowbytes=0, datalen=0 */
            if (lr.pBits) IDirect3DTexture8_UnlockRect(tex, l);
            continue;
        }
        uint32_t rowbytes = d.Width * (uint32_t)bpp;
        orv3_wu(g_cap, d.Width); orv3_wu(g_cap, d.Height); orv3_wu(g_cap, d.Format);
        orv3_wu(g_cap, rowbytes); orv3_wu(g_cap, rowbytes * d.Height);
        for (DWORD r = 0; r < d.Height; r++)
            fwrite((const uint8_t*)lr.pBits + (size_t)r * lr.Pitch, 1, rowbytes, g_cap);
        IDirect3DTexture8_UnlockRect(tex, l);
    }
    res_remember(bt, id);
    return id;
}
static int snap_vb(IDirect3DVertexBuffer8 *vb)
{
    if (!vb) return -1;
    int id = res_lookup(vb); if (id >= 0) return id;
    D3DVERTEXBUFFER_DESC d = {0}; IDirect3DVertexBuffer8_GetDesc(vb, &d);
    BYTE *p = NULL;
    if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, D3DLOCK_READONLY)) || !p) {
        if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) || !p) return -1;
    }
    id = g_next_resid++;
    orv3_wu(g_cap, ORV3_RES_VB); orv3_wu(g_cap, (uint32_t)id);
    orv3_wu(g_cap, d.Size); orv3_wu(g_cap, d.FVF);
    orv3_wbytes(g_cap, p, d.Size);
    IDirect3DVertexBuffer8_Unlock(vb);
    res_remember(vb, id);
    return id;
}
static int snap_ib(IDirect3DIndexBuffer8 *ib)
{
    if (!ib) return -1;
    int id = res_lookup(ib); if (id >= 0) return id;
    D3DINDEXBUFFER_DESC d = {0}; IDirect3DIndexBuffer8_GetDesc(ib, &d);
    BYTE *p = NULL;
    if (FAILED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, D3DLOCK_READONLY)) || !p) {
        if (FAILED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, 0)) || !p) return -1;
    }
    id = g_next_resid++;
    orv3_wu(g_cap, ORV3_RES_IB); orv3_wu(g_cap, (uint32_t)id);
    orv3_wu(g_cap, d.Size); orv3_wu(g_cap, d.Format);
    orv3_wbytes(g_cap, p, d.Size);
    IDirect3DIndexBuffer8_Unlock(ib);
    res_remember(ib, id);
    return id;
}

/* read back the device backbuffer -> {u32 w,h, w*h*4 BGRA} (mirrors capture_backbuffer) */
static int readback_raw(IDirect3DDevice8 *dev, const char *path)
{
    IDirect3DSurface8 *surf = NULL;
    if (FAILED(IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &surf))) return 0;
    D3DSURFACE_DESC d = {0}; IDirect3DSurface8_GetDesc(surf, &d);
    D3DLOCKED_RECT lr = {0};
    if (FAILED(IDirect3DSurface8_LockRect(surf, &lr, NULL, D3DLOCK_READONLY))) {
        IDirect3DSurface8_Release(surf); return 0; }
    FILE *fp = fopen(path, "wb");
    if (fp) {
        uint32_t w = d.Width, h = d.Height;
        fwrite(&w, 4, 1, fp); fwrite(&h, 4, 1, fp);
        for (uint32_t r = 0; r < h; r++) fwrite((uint8_t*)lr.pBits + (size_t)r*lr.Pitch, 1, w*4, fp);
        fclose(fp);
    }
    IDirect3DSurface8_UnlockRect(surf); IDirect3DSurface8_Release(surf);
    return fp != NULL;
}

#define CAP (g_capturing && g_cap)

/* finalize the capture: snapshot the reference backbuffer + close the container.
 * Called either at a present-count target (Present) or when the app reads back
 * the backbuffer for its own screenshot (GetBackBuffer trigger — aligns capture
 * to the harness's --capture-frames in sim-frame space). */
static void finalize_capture(IDirect3DDevice8 *real_dev)
{
    if (!CAP) return;
    char ref[MAX_PATH+32]; proxy_out_path(ref, sizeof ref, "v3ref.raw");
    readback_raw(real_dev, ref);
    orv3_wu(g_cap, ORV3_EOF); fclose(g_cap); g_cap = NULL; g_capturing = 0;
    proxy_log("FINALIZE: frames 0..%u (%d resources) + reference\n", g_frame, g_next_resid);
}

/* ── real d3d8 resolution ── */
typedef IDirect3D8 *(WINAPI *PFN_Create8)(UINT);
static HMODULE g_realDll; static PFN_Create8 g_realCreate;
static void load_real_d3d8(void)
{
    if (g_realCreate) return;
    char sysdir[MAX_PATH]; UINT n = GetSystemDirectoryA(sysdir, sizeof sysdir);
    char path[MAX_PATH]; snprintf(path, sizeof path, "%.*s\\d3d8.dll", (int)n, sysdir);
    g_realDll = LoadLibraryA(path);
    if (!g_realDll) { proxy_log("FATAL real d3d8 (%s)\n", path); return; }
    g_realCreate = (PFN_Create8)(void*)GetProcAddress(g_realDll, "Direct3DCreate8");
}

__declspec(dllexport) IDirect3D8 * WINAPI Direct3DCreate8(UINT SDKVersion)
{
    load_real_d3d8(); if (!g_realCreate) return NULL;
    IDirect3D8 *real = g_realCreate(SDKVersion); if (!real) return NULL;
    const char *cf = getenv("OPENRECET_V3_CAPFRAME"); if (cf && *cf) g_capframe = (unsigned)atoi(cf);
    WrapD3D *w = (WrapD3D*)calloc(1, sizeof *w);
    w->lpVtbl = &g_IDirect3D8_vt; w->real = real; w->refs = 1;
    proxy_log("Direct3DCreate8 wrapped (capframe=%u)\n", g_capframe);
    return (IDirect3D8*)w;
}

/* ── factory custom ── */
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_QueryInterface(IDirect3D8 *This, REFIID riid, void **ppv)
{
    WrapD3D *w = (WrapD3D*)This;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D8)) {
        *ppv = This; InterlockedIncrement(&w->refs); return S_OK; }
    return w->real->lpVtbl->QueryInterface(w->real, riid, ppv);
}
static ULONG STDMETHODCALLTYPE my_IDirect3D8_AddRef(IDirect3D8 *This)
{ return (ULONG)InterlockedIncrement(&((WrapD3D*)This)->refs); }
static ULONG STDMETHODCALLTYPE my_IDirect3D8_Release(IDirect3D8 *This)
{ WrapD3D *w = (WrapD3D*)This; LONG r = InterlockedDecrement(&w->refs);
  if (r == 0) { w->real->lpVtbl->Release(w->real); free(w); } return (ULONG)r; }

static HRESULT STDMETHODCALLTYPE my_IDirect3D8_CreateDevice(
    IDirect3D8 *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **ppDev)
{
    WrapD3D *w = (WrapD3D*)This;
    IDirect3DDevice8 *real = NULL;
    HRESULT hr = w->real->lpVtbl->CreateDevice(w->real, Adapter, DeviceType,
                     hFocusWindow, BehaviorFlags, pp, &real);
    if (FAILED(hr) || !real) { if (ppDev) *ppDev = NULL; return hr; }
    WrapDev *wd = (WrapDev*)calloc(1, sizeof *wd);
    wd->lpVtbl = &g_IDirect3DDevice8_vt; wd->real = real; wd->refs = 1;

    /* open the capture container + write device params */
    char path[MAX_PATH+32]; proxy_out_path(path, sizeof path, "v3cap.bin");
    g_cap = fopen(path, "wb");
    if (g_cap && pp) {
        setvbuf(g_cap, NULL, _IOFBF, 1u<<20);
        orv3_wu(g_cap, ORV3_MAGIC); orv3_wu(g_cap, ORV3_VERSION);
        orv3_wu(g_cap, ORV3_DEV_PARAMS);
        orv3_wu(g_cap, pp->BackBufferWidth); orv3_wu(g_cap, pp->BackBufferHeight);
        orv3_wu(g_cap, pp->BackBufferFormat); orv3_wu(g_cap, pp->AutoDepthStencilFormat);
        orv3_wu(g_cap, (uint32_t)pp->Windowed); orv3_wu(g_cap, pp->BackBufferCount);
        orv3_wu(g_cap, pp->Flags); orv3_wu(g_cap, BehaviorFlags);
        orv3_wu(g_cap, pp->FullScreen_PresentationInterval);
        orv3_wu(g_cap, Adapter); orv3_wu(g_cap, (uint32_t)DeviceType);
        orv3_wu(g_cap, (uint32_t)pp->EnableAutoDepthStencil);
        g_frame_start_pos = ftell(g_cap);   /* single-frame mode rewinds here */
        g_capturing = 1;
        proxy_log("capture begin: %ux%u bbfmt=%u depth=%u flags=0x%x capframe=%u\n",
                  pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat,
                  pp->AutoDepthStencilFormat, pp->Flags, g_capframe);
    }
    *ppDev = (IDirect3DDevice8*)wd;
    return hr;
}

/* ── device custom ── */
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_QueryInterface(IDirect3DDevice8 *This, REFIID riid, void **ppv)
{ WrapDev *w = (WrapDev*)This;
  if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DDevice8)) {
      *ppv = This; InterlockedIncrement(&w->refs); return S_OK; }
  return w->real->lpVtbl->QueryInterface(w->real, riid, ppv); }
static ULONG STDMETHODCALLTYPE my_IDirect3DDevice8_AddRef(IDirect3DDevice8 *This)
{ return (ULONG)InterlockedIncrement(&((WrapDev*)This)->refs); }
static ULONG STDMETHODCALLTYPE my_IDirect3DDevice8_Release(IDirect3DDevice8 *This)
{ WrapDev *w = (WrapDev*)This; LONG r = InterlockedDecrement(&w->refs);
  if (r == 0) { w->real->lpVtbl->Release(w->real); free(w); } return (ULONG)r; }

static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Present(
    IDirect3DDevice8 *This, const RECT *s, const RECT *d, HWND wnd, const RGNDATA *dr)
{
    WrapDev *w = (WrapDev*)This;
    if (CAP) {
        if (g_frame == g_capframe) { orv3_wu(g_cap, ORV3_Present); orv3_wu(g_cap, g_frame); finalize_capture(w->real); }
        else {
            /* single-frame mode: this frame wasn't the target (the GetBackBuffer
             * trigger picks the target); rewind + re-arm for the next frame so the
             * container only ever holds ONE frame (no load-stretch volume). */
            fseek(g_cap, g_frame_start_pos, SEEK_SET);
            g_n_res = 0; g_next_resid = 0;
        }
    }
    g_present_count++;
    HRESULT hr = w->real->lpVtbl->Present(w->real, s, d, wnd, dr);
    g_frame++;
    return hr;
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_BeginScene(IDirect3DDevice8 *This)
{ if (CAP) orv3_wu(g_cap, ORV3_BeginScene); return ((WrapDev*)This)->real->lpVtbl->BeginScene(((WrapDev*)This)->real); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_EndScene(IDirect3DDevice8 *This)
{ if (CAP) orv3_wu(g_cap, ORV3_EndScene); return ((WrapDev*)This)->real->lpVtbl->EndScene(((WrapDev*)This)->real); }

static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Clear(
    IDirect3DDevice8 *This, DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (CAP) {
        orv3_wu(g_cap, ORV3_Clear); orv3_wu(g_cap, count);
        if (count && rects) fwrite(rects, sizeof(D3DRECT), count, g_cap);
        orv3_wu(g_cap, flags); orv3_wu(g_cap, color);
        uint32_t zb; memcpy(&zb, &z, 4); orv3_wu(g_cap, zb); orv3_wu(g_cap, stencil);
    }
    return ((WrapDev*)This)->real->lpVtbl->Clear(((WrapDev*)This)->real, count, rects, flags, color, z, stencil);
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetRenderState(IDirect3DDevice8 *This, D3DRENDERSTATETYPE s, DWORD v)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetRenderState); orv3_wu(g_cap, s); orv3_wu(g_cap, v); }
  return ((WrapDev*)This)->real->lpVtbl->SetRenderState(((WrapDev*)This)->real, s, v); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTextureStageState(IDirect3DDevice8 *This, DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetTextureStageState); orv3_wu(g_cap, st); orv3_wu(g_cap, t); orv3_wu(g_cap, v); }
  return ((WrapDev*)This)->real->lpVtbl->SetTextureStageState(((WrapDev*)This)->real, st, t, v); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTransform(IDirect3DDevice8 *This, D3DTRANSFORMSTATETYPE s, const D3DMATRIX *m)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetTransform); orv3_wu(g_cap, s); fwrite(m, sizeof(float), 16, g_cap); }
  return ((WrapDev*)This)->real->lpVtbl->SetTransform(((WrapDev*)This)->real, s, m); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetMaterial(IDirect3DDevice8 *This, const D3DMATERIAL8 *m)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetMaterial); fwrite(m, sizeof(float), 17, g_cap); }
  return ((WrapDev*)This)->real->lpVtbl->SetMaterial(((WrapDev*)This)->real, m); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTexture(IDirect3DDevice8 *This, DWORD stage, IDirect3DBaseTexture8 *tex)
{ if (CAP) { int id = snap_tex(tex); orv3_wu(g_cap, ORV3_SetTexture); orv3_wu(g_cap, stage); orv3_wu(g_cap, (uint32_t)id); }
  return ((WrapDev*)This)->real->lpVtbl->SetTexture(((WrapDev*)This)->real, stage, tex); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetStreamSource(IDirect3DDevice8 *This, UINT stream, IDirect3DVertexBuffer8 *vb, UINT stride)
{ if (CAP) { int id = snap_vb(vb); orv3_wu(g_cap, ORV3_SetStreamSource); orv3_wu(g_cap, stream); orv3_wu(g_cap, (uint32_t)id); orv3_wu(g_cap, stride); }
  return ((WrapDev*)This)->real->lpVtbl->SetStreamSource(((WrapDev*)This)->real, stream, vb, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetIndices(IDirect3DDevice8 *This, IDirect3DIndexBuffer8 *ib, UINT base)
{ if (CAP) { int id = snap_ib(ib); orv3_wu(g_cap, ORV3_SetIndices); orv3_wu(g_cap, (uint32_t)id); orv3_wu(g_cap, base); }
  return ((WrapDev*)This)->real->lpVtbl->SetIndices(((WrapDev*)This)->real, ib, base); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetVertexShader(IDirect3DDevice8 *This, DWORD h)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetVertexShader); orv3_wu(g_cap, h); }
  return ((WrapDev*)This)->real->lpVtbl->SetVertexShader(((WrapDev*)This)->real, h); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitive(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT sv, UINT pc)
{ if (CAP) { orv3_wu(g_cap, ORV3_DrawPrimitive); orv3_wu(g_cap, pt); orv3_wu(g_cap, sv); orv3_wu(g_cap, pc); }
  return ((WrapDev*)This)->real->lpVtbl->DrawPrimitive(((WrapDev*)This)->real, pt, sv, pc); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitive(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT mi, UINT nv, UINT si, UINT pc)
{ if (CAP) { orv3_wu(g_cap, ORV3_DrawIndexedPrimitive); orv3_wu(g_cap, pt); orv3_wu(g_cap, mi); orv3_wu(g_cap, nv); orv3_wu(g_cap, si); orv3_wu(g_cap, pc); }
  return ((WrapDev*)This)->real->lpVtbl->DrawIndexedPrimitive(((WrapDev*)This)->real, pt, mi, nv, si, pc); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitiveUP(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT pc, const void *data, UINT stride)
{ if (CAP) { orv3_wu(g_cap, ORV3_DrawPrimitiveUP); orv3_wu(g_cap, pt); orv3_wu(g_cap, pc); orv3_wu(g_cap, stride);
      uint32_t n = prim_vcount(pt, pc) * stride; orv3_wbytes(g_cap, data, n); }
  return ((WrapDev*)This)->real->lpVtbl->DrawPrimitiveUP(((WrapDev*)This)->real, pt, pc, data, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitiveUP(
    IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT mvi, UINT nvi, UINT pc,
    const void *idx, D3DFORMAT ifmt, const void *vdata, UINT stride)
{ if (CAP) { orv3_wu(g_cap, ORV3_DrawIndexedPrimitiveUP); orv3_wu(g_cap, pt); orv3_wu(g_cap, mvi); orv3_wu(g_cap, nvi); orv3_wu(g_cap, pc); orv3_wu(g_cap, ifmt);
      uint32_t isz = (ifmt == D3DFMT_INDEX16) ? 2u : 4u;
      orv3_wbytes(g_cap, idx, prim_vcount(pt, pc) * isz);
      orv3_wu(g_cap, stride);
      orv3_wbytes(g_cap, vdata, (mvi + nvi) * stride); }
  return ((WrapDev*)This)->real->lpVtbl->DrawIndexedPrimitiveUP(((WrapDev*)This)->real, pt, mvi, nvi, pc, idx, ifmt, vdata, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetLight(IDirect3DDevice8 *This, DWORD index, const D3DLIGHT8 *L)
{ if (CAP) { orv3_wu(g_cap, ORV3_SetLight); orv3_wu(g_cap, index); orv3_wbytes(g_cap, L, sizeof(D3DLIGHT8)); }
  return ((WrapDev*)This)->real->lpVtbl->SetLight(((WrapDev*)This)->real, index, L); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_LightEnable(IDirect3DDevice8 *This, DWORD index, WINBOOL en)
{ if (CAP) { orv3_wu(g_cap, ORV3_LightEnable); orv3_wu(g_cap, index); orv3_wu(g_cap, (uint32_t)en); }
  return ((WrapDev*)This)->real->lpVtbl->LightEnable(((WrapDev*)This)->real, index, en); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_GetBackBuffer(IDirect3DDevice8 *This, UINT bb, D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **pp)
{ WrapDev *w = (WrapDev*)This;
  HRESULT hr = w->real->lpVtbl->GetBackBuffer(w->real, bb, type, pp);
  if (CAP && g_present_count >= 1) { proxy_log("GetBackBuffer trigger @ frame %u\n", g_frame); finalize_capture(w->real); }
  return hr; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{ (void)reserved; if (reason == DLL_PROCESS_ATTACH) { g_self = h; proxy_log("DllMain attach\n"); } return TRUE; }
