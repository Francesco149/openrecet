/* OpenRecet Trace Studio v3 — proxy d3d8.dll (capture).
 *
 * Drop-in d3d8.dll used by BOTH the port and retail (app-dir loads before
 * System32). Wraps the factory + device and records the exact D3D8 command
 * stream + the referenced resources (textures/VB/IB) into a flat container
 * (orv3_format.h), sufficient to RE-RENDER the frame. Only the device + factory
 * are wrapped; resources are returned UNWRAPPED and snapshotted by read-only Lock
 * (port allocates them D3DPOOL_MANAGED -> lockable).
 *
 * SINGLE-FRAME, DEFERRED-SNAPSHOT capture (P1 two-section): each frame's calls
 * accumulate in an in-memory buffer that is dropped every Present; only the
 * TARGET frame survives. Resource snapshots are DEFERRED to finalize, so the
 * multi-thousand-frame prologue/load costs ZERO snapshot work — only the target
 * frame's bound resources are ever read back (current contents, current pointers
 * ⇒ no stale data, no pointer-reuse-across-transition bug). The target frame is
 * picked by the app's own backbuffer readback (GetBackBuffer trigger, aligned to
 * the harness's --capture-frames) or a present-count target. At finalize the
 * proxy snapshots that frame's resources, patches their ids into the buffered
 * calls, writes [resources][calls], and reads back the reference backbuffer, so
 * the replayer re-renders + compares the exact same frame.
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

/* ── config (a key=value file NEXT TO the dll) ──
 * WSL env vars don't cross to the Windows exe (WSLENV), and a Frida-spawned
 * retail inherits frida-server's environment, not the WSL driver's — so getenv
 * can't configure the proxy for retail. Instead it reads `v3proxy.cfg` from the
 * dll's OWN directory (the driver writes it into the same app dir the proxy is
 * staged in, before spawn). Recognized keys:
 *   capframe=N   present-count frame to finalize (default: GetBackBuffer trigger)
 *   out=PATH     Windows dir for the container/log/reference (default LOCALAPPDATA)
 * getenv stays honored as a fallback (harmless for the port, which uses neither). */
static char     g_cfg_out[MAX_PATH];
static unsigned g_cfg_capframe = 0xFFFFFFFFu;
static int      g_cfg_loaded;
static void load_cfg(void)
{
    if (g_cfg_loaded) return;
    g_cfg_loaded = 1;
    char dll[MAX_PATH]; DWORD n = GetModuleFileNameA(g_self, dll, sizeof dll);
    if (!n || n >= sizeof dll) return;
    char *slash = strrchr(dll, '\\');
    if (!slash) return;
    slash[1] = 0;  /* keep the dll's dir + trailing '\' */
    char cfg[MAX_PATH + 16]; snprintf(cfg, sizeof cfg, "%sv3proxy.cfg", dll);
    FILE *f = fopen(cfg, "r"); if (!f) return;
    char line[MAX_PATH + 32];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char *key = line, *val = eq + 1;
        size_t vl = strlen(val);
        while (vl && (val[vl-1]=='\n' || val[vl-1]=='\r' || val[vl-1]==' ' || val[vl-1]=='\t')) val[--vl] = 0;
        if      (!strcmp(key, "capframe")) g_cfg_capframe = (unsigned)strtoul(val, NULL, 0);
        else if (!strcmp(key, "out") && *val) { snprintf(g_cfg_out, sizeof g_cfg_out, "%s", val); CreateDirectoryA(g_cfg_out, NULL); }
    }
    fclose(f);
}

/* Resolve output to Windows-LOCAL NTFS (never the \\wsl.localhost 9p mount —
 * writing the container there throttles the engine to a crawl). Default
 * %LOCALAPPDATA%\openrecet\v3 (same fast-disk pattern the port uses for se.pack);
 * WSL reads it back via /mnt/c/...  OPENRECET_V3_OUT overrides. */
static void proxy_out_path(char *out, size_t n, const char *leaf)
{
    load_cfg();
    if (g_cfg_out[0]) { snprintf(out, n, "%s\\%s", g_cfg_out, leaf); return; }
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
        g_log = fopen(p, "w"); if (!g_log) return; setvbuf(g_log, NULL, _IONBF, 0); }
    /* unbuffered: msvcrt treats _IOLBF as full-buffering, so a hard device.kill
     * (how the harness reaps a spawned retail/port) would lose every buffered
     * line and leave a 0-byte log even though the proxy ran. Unbuffered keeps the
     * log a faithful record of a capture that was killed mid-run. */
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
}

/* ── capture state ── */
static FILE     *g_cap;            /* container: [header][DEV_PARAMS] during the run; [resources][calls] appended at finalize */
static unsigned  g_capframe = 0xFFFFFFFFu; /* present-count target; default = never (use the GetBackBuffer trigger) */
static unsigned  g_frame;          /* present-counted frame index */
static int       g_capturing;
static unsigned  g_present_count;
/* resource dedup: ptr -> id (reset per finalize — one frame's id space) */
#define ORV3_MAXRES 32768
static void     *g_res_ptr[ORV3_MAXRES];
static int       g_res_id [ORV3_MAXRES];
static int       g_n_res;
static int       g_next_resid;

/* In-memory CALL buffer for the current frame. Calls are NOT written to the
 * container as they happen — they accumulate here and are dropped every Present
 * (single-frame capture: keep only the trigger frame). Resource snapshots are
 * DEFERRED to finalize: only the TARGET frame's bound resources are ever read
 * back, so the multi-thousand-frame prologue/load costs ZERO snapshot work (the
 * throttle + 963 MB balloon are gone). At finalize we snapshot that frame's
 * bound resources (current contents, current pointers — no stale data, no
 * pointer-reuse-across-transition bug), patch their ids into the buffered calls,
 * then write [resources][calls] — resources first, so the streaming replayer
 * still sees every id defined before it is used. */
static uint8_t  *g_cb; static size_t g_cb_len, g_cb_cap;
static void cb_ensure(size_t n)
{
    if (g_cb_len + n <= g_cb_cap) return;
    size_t nc = g_cb_cap ? g_cb_cap : (1u << 20);
    while (nc < g_cb_len + n) nc <<= 1;
    g_cb = (uint8_t*)realloc(g_cb, nc); g_cb_cap = nc;
}
static void cb_u(uint32_t v) { cb_ensure(4); memcpy(g_cb + g_cb_len, &v, 4); g_cb_len += 4; }
static void cb_data(const void *p, size_t n) { cb_ensure(n); if (n) memcpy(g_cb + g_cb_len, p, n); g_cb_len += n; }
static void cb_bytes(const void *p, uint32_t n) { cb_u(n); cb_data(p, n); }  /* length-prefixed (mirrors orv3_wbytes) */
static void cb_patch_u(size_t off, uint32_t v) { memcpy(g_cb + off, &v, 4); }

/* deferred resource binds: remember (kind,ptr,id-field-offset); snapshot + patch at finalize */
enum { PEND_TEX = 1, PEND_VB = 2, PEND_IB = 3 };
typedef struct { int kind; void *ptr; size_t off; } Pending;
static Pending  *g_pending; static int g_pending_n, g_pending_cap;
static void pend_push(int kind, void *ptr, size_t off)
{
    if (g_pending_n >= g_pending_cap) {
        g_pending_cap = g_pending_cap ? g_pending_cap * 2 : 8192;
        g_pending = (Pending*)realloc(g_pending, (size_t)g_pending_cap * sizeof *g_pending);
    }
    g_pending[g_pending_n].kind = kind; g_pending[g_pending_n].ptr = ptr; g_pending[g_pending_n].off = off; g_pending_n++;
}
/* emit a deferred resource-ref id field into g_cb (placeholder now; snapshot at finalize) */
static void cb_resref(int kind, void *ptr)
{
    size_t off = g_cb_len; cb_u(0xffffffffu);     /* placeholder; patched to the snapshot id (or -1) */
    if (ptr) pend_push(kind, ptr, off);
}
static void cb_reset(void) { g_cb_len = 0; g_pending_n = 0; }

/* ── device-state shadow (inherited-state preamble) ──
 * A single frame is SLICED out of a long run, so any scalar state the game set in
 * an EARLIER frame and did not re-set inside the kept frame is INHERITED and must
 * be replayed first — else a 3D scene comes out wrong (overbright from a missing
 * lighting/ambient state; opaque-black quads from a missing alpha-blend state).
 * We shadow every scalar Set the game makes (render states, texture-stage states,
 * transforms, material, FVF) and emit the shadow as a preamble at each frame
 * boundary, so the kept frame's buffer begins at the exact inherited state. Only
 * states the game ACTUALLY set are emitted (no GetRenderState-all, no invalid-enum
 * risk — cheaper and more bullet-proof). Resource BINDINGS are NOT shadowed: every
 * draw re-binds its own texture/stream/indices, so frame-start bindings never
 * matter, and shadowing them would resurrect the load-time snapshot cost. */
#define ORV3_NRS    256
#define ORV3_NTSS   32
#define ORV3_NXFORM 260   /* D3DTS_WORLD(256)..WORLD3(259) is the max index */
static DWORD   g_sh_rs[ORV3_NRS];          static uint8_t g_sh_rs_set[ORV3_NRS];
static DWORD   g_sh_tss[8][ORV3_NTSS];     static uint8_t g_sh_tss_set[8][ORV3_NTSS];
static float   g_sh_xform[ORV3_NXFORM][16];static uint8_t g_sh_xform_set[ORV3_NXFORM];
static uint8_t g_sh_mat[68];               static uint8_t g_sh_mat_set;
static DWORD   g_sh_fvf;                    static uint8_t g_sh_fvf_set;
static void shadow_rs(DWORD s, DWORD v)             { if (s < ORV3_NRS) { g_sh_rs[s] = v; g_sh_rs_set[s] = 1; } }
static void shadow_tss(DWORD st, DWORD t, DWORD v)  { if (st < 8 && t < ORV3_NTSS) { g_sh_tss[st][t] = v; g_sh_tss_set[st][t] = 1; } }
static void shadow_xform(DWORD s, const void *m)    { if (s < ORV3_NXFORM) { memcpy(g_sh_xform[s], m, 64); g_sh_xform_set[s] = 1; } }
static void shadow_mat(const void *m)               { memcpy(g_sh_mat, m, 68); g_sh_mat_set = 1; }
static void shadow_fvf(DWORD h)                     { g_sh_fvf = h; g_sh_fvf_set = 1; }
/* emit the shadow as a scalar-state preamble into the (just-reset) call buffer */
static void emit_shadow_preamble(void)
{
    for (int s = 0; s < ORV3_NRS; s++)
        if (g_sh_rs_set[s]) { cb_u(ORV3_SetRenderState); cb_u((uint32_t)s); cb_u(g_sh_rs[s]); }
    for (int st = 0; st < 8; st++) for (int t = 0; t < ORV3_NTSS; t++)
        if (g_sh_tss_set[st][t]) { cb_u(ORV3_SetTextureStageState); cb_u((uint32_t)st); cb_u((uint32_t)t); cb_u(g_sh_tss[st][t]); }
    for (int x = 0; x < ORV3_NXFORM; x++)
        if (g_sh_xform_set[x]) { cb_u(ORV3_SetTransform); cb_u((uint32_t)x); cb_data(g_sh_xform[x], 64); }
    if (g_sh_mat_set) { cb_u(ORV3_SetMaterial); cb_data(g_sh_mat, 68); }
    if (g_sh_fvf_set) { cb_u(ORV3_SetVertexShader); cb_u(g_sh_fvf); }
}

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

/* read back the device backbuffer -> {u32 w,h, w*h*4 BGRA} via the shared
 * CopyRects-through-sysmem helper (works for retail's non-lockable backbuffer) */
static int readback_raw(IDirect3DDevice8 *dev, const char *path)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = orv3_readback_bgra(dev, &w, &h);
    if (!px) { proxy_log("readback FAILED (GetBackBuffer/CopyRects/Lock)\n"); return 0; }
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fwrite(&w, 4, 1, fp); fwrite(&h, 4, 1, fp);
        fwrite(px, 1, (size_t)w * h * 4u, fp);
        fclose(fp);
    }
    free(px);
    return fp != NULL;
}

#define CAP (g_capturing && g_cap)

/* finalize the capture: snapshot the TARGET frame's bound resources (read back
 * NOW — correct contents, correct pointers, no load-time work), patch their ids
 * into the buffered calls, write [resources][calls][Present], read back the
 * reference backbuffer, close. Called at a present-count target (Present) or when
 * the app reads back the backbuffer for its own screenshot (GetBackBuffer
 * trigger — aligns capture to the harness's --capture-frames in sim-frame space). */
static void finalize_capture(IDirect3DDevice8 *real_dev)
{
    if (!CAP) return;
    g_n_res = 0; g_next_resid = 0;                 /* fresh id space for this single frame */
    for (int i = 0; i < g_pending_n; i++) {
        int id = -1;
        switch (g_pending[i].kind) {
        case PEND_TEX: id = snap_tex((IDirect3DBaseTexture8*)g_pending[i].ptr); break;
        case PEND_VB:  id = snap_vb ((IDirect3DVertexBuffer8*)g_pending[i].ptr); break;
        case PEND_IB:  id = snap_ib ((IDirect3DIndexBuffer8*)g_pending[i].ptr); break;
        }
        cb_patch_u(g_pending[i].off, (uint32_t)id);
    }
    fwrite(g_cb, 1, g_cb_len, g_cap);              /* calls (resources already written by snap_* above) */
    orv3_wu(g_cap, ORV3_Present); orv3_wu(g_cap, g_frame);
    char ref[MAX_PATH+32]; proxy_out_path(ref, sizeof ref, "v3ref.raw");
    readback_raw(real_dev, ref);
    orv3_wu(g_cap, ORV3_EOF); fclose(g_cap); g_cap = NULL; g_capturing = 0;
    proxy_log("FINALIZE: frame %u (%u call-bytes, %d resources) + reference\n",
              g_frame, (unsigned)g_cb_len, g_next_resid);
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
    load_cfg();
    if (g_cfg_capframe != 0xFFFFFFFFu) g_capframe = g_cfg_capframe;     /* cfg (retail path) */
    const char *cf = getenv("OPENRECET_V3_CAPFRAME"); if (cf && *cf) g_capframe = (unsigned)atoi(cf);  /* env override */
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
        /* g_cap now holds only [header][DEV_PARAMS]; nothing more is written to it
         * until finalize (calls live in the in-memory g_cb buffer, resources are
         * deferred). No per-frame file I/O ⇒ the load-stretch costs nothing. */
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
        if (g_frame == g_capframe) finalize_capture(w->real);  /* present-count target */
        else {
            /* not the target (GetBackBuffer trigger picks it): drop this frame's
             * calls + re-arm (no file I/O), then seed the next frame's buffer with
             * the inherited device-state preamble (shadow = end of this frame =
             * start of the next), so whichever frame becomes the target replays
             * the state it inherited rather than D3D defaults. */
            cb_reset();
            emit_shadow_preamble();
        }
    }
    g_present_count++;
    HRESULT hr = w->real->lpVtbl->Present(w->real, s, d, wnd, dr);
    g_frame++;
    return hr;
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_BeginScene(IDirect3DDevice8 *This)
{ if (CAP) cb_u(ORV3_BeginScene); return ((WrapDev*)This)->real->lpVtbl->BeginScene(((WrapDev*)This)->real); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_EndScene(IDirect3DDevice8 *This)
{ if (CAP) cb_u(ORV3_EndScene); return ((WrapDev*)This)->real->lpVtbl->EndScene(((WrapDev*)This)->real); }

static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Clear(
    IDirect3DDevice8 *This, DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (CAP) {
        cb_u(ORV3_Clear); cb_u(count);
        if (count && rects) cb_data(rects, (size_t)count * sizeof(D3DRECT));
        cb_u(flags); cb_u(color);
        uint32_t zb; memcpy(&zb, &z, 4); cb_u(zb); cb_u(stencil);
    }
    return ((WrapDev*)This)->real->lpVtbl->Clear(((WrapDev*)This)->real, count, rects, flags, color, z, stencil);
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetRenderState(IDirect3DDevice8 *This, D3DRENDERSTATETYPE s, DWORD v)
{ if (CAP) { cb_u(ORV3_SetRenderState); cb_u(s); cb_u(v); shadow_rs(s, v); }
  return ((WrapDev*)This)->real->lpVtbl->SetRenderState(((WrapDev*)This)->real, s, v); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTextureStageState(IDirect3DDevice8 *This, DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v)
{ if (CAP) { cb_u(ORV3_SetTextureStageState); cb_u(st); cb_u(t); cb_u(v); shadow_tss(st, t, v); }
  return ((WrapDev*)This)->real->lpVtbl->SetTextureStageState(((WrapDev*)This)->real, st, t, v); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTransform(IDirect3DDevice8 *This, D3DTRANSFORMSTATETYPE s, const D3DMATRIX *m)
{ if (CAP) { cb_u(ORV3_SetTransform); cb_u(s); cb_data(m, 16 * sizeof(float)); shadow_xform(s, m); }
  return ((WrapDev*)This)->real->lpVtbl->SetTransform(((WrapDev*)This)->real, s, m); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetMaterial(IDirect3DDevice8 *This, const D3DMATERIAL8 *m)
{ if (CAP) { cb_u(ORV3_SetMaterial); cb_data(m, 17 * sizeof(float)); shadow_mat(m); }
  return ((WrapDev*)This)->real->lpVtbl->SetMaterial(((WrapDev*)This)->real, m); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetTexture(IDirect3DDevice8 *This, DWORD stage, IDirect3DBaseTexture8 *tex)
{ if (CAP) { cb_u(ORV3_SetTexture); cb_u(stage); cb_resref(PEND_TEX, tex); }
  return ((WrapDev*)This)->real->lpVtbl->SetTexture(((WrapDev*)This)->real, stage, tex); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetStreamSource(IDirect3DDevice8 *This, UINT stream, IDirect3DVertexBuffer8 *vb, UINT stride)
{ if (CAP) { cb_u(ORV3_SetStreamSource); cb_u(stream); cb_resref(PEND_VB, vb); cb_u(stride); }
  return ((WrapDev*)This)->real->lpVtbl->SetStreamSource(((WrapDev*)This)->real, stream, vb, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetIndices(IDirect3DDevice8 *This, IDirect3DIndexBuffer8 *ib, UINT base)
{ if (CAP) { cb_u(ORV3_SetIndices); cb_resref(PEND_IB, ib); cb_u(base); }
  return ((WrapDev*)This)->real->lpVtbl->SetIndices(((WrapDev*)This)->real, ib, base); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetVertexShader(IDirect3DDevice8 *This, DWORD h)
{ if (CAP) { cb_u(ORV3_SetVertexShader); cb_u(h); shadow_fvf(h); }
  return ((WrapDev*)This)->real->lpVtbl->SetVertexShader(((WrapDev*)This)->real, h); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitive(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT sv, UINT pc)
{ if (CAP) { cb_u(ORV3_DrawPrimitive); cb_u(pt); cb_u(sv); cb_u(pc); }
  return ((WrapDev*)This)->real->lpVtbl->DrawPrimitive(((WrapDev*)This)->real, pt, sv, pc); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitive(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT mi, UINT nv, UINT si, UINT pc)
{ if (CAP) { cb_u(ORV3_DrawIndexedPrimitive); cb_u(pt); cb_u(mi); cb_u(nv); cb_u(si); cb_u(pc); }
  return ((WrapDev*)This)->real->lpVtbl->DrawIndexedPrimitive(((WrapDev*)This)->real, pt, mi, nv, si, pc); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawPrimitiveUP(IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT pc, const void *data, UINT stride)
{ if (CAP) { cb_u(ORV3_DrawPrimitiveUP); cb_u(pt); cb_u(pc); cb_u(stride);
      cb_bytes(data, prim_vcount(pt, pc) * stride); }
  return ((WrapDev*)This)->real->lpVtbl->DrawPrimitiveUP(((WrapDev*)This)->real, pt, pc, data, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_DrawIndexedPrimitiveUP(
    IDirect3DDevice8 *This, D3DPRIMITIVETYPE pt, UINT mvi, UINT nvi, UINT pc,
    const void *idx, D3DFORMAT ifmt, const void *vdata, UINT stride)
{ if (CAP) { cb_u(ORV3_DrawIndexedPrimitiveUP); cb_u(pt); cb_u(mvi); cb_u(nvi); cb_u(pc); cb_u(ifmt);
      uint32_t isz = (ifmt == D3DFMT_INDEX16) ? 2u : 4u;
      cb_bytes(idx, prim_vcount(pt, pc) * isz);
      cb_u(stride);
      cb_bytes(vdata, (mvi + nvi) * stride); }
  return ((WrapDev*)This)->real->lpVtbl->DrawIndexedPrimitiveUP(((WrapDev*)This)->real, pt, mvi, nvi, pc, idx, ifmt, vdata, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetLight(IDirect3DDevice8 *This, DWORD index, const D3DLIGHT8 *L)
{ if (CAP) { cb_u(ORV3_SetLight); cb_u(index); cb_bytes(L, sizeof(D3DLIGHT8)); }
  return ((WrapDev*)This)->real->lpVtbl->SetLight(((WrapDev*)This)->real, index, L); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_LightEnable(IDirect3DDevice8 *This, DWORD index, WINBOOL en)
{ if (CAP) { cb_u(ORV3_LightEnable); cb_u(index); cb_u((uint32_t)en); }
  return ((WrapDev*)This)->real->lpVtbl->LightEnable(((WrapDev*)This)->real, index, en); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_GetBackBuffer(IDirect3DDevice8 *This, UINT bb, D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **pp)
{ WrapDev *w = (WrapDev*)This;
  HRESULT hr = w->real->lpVtbl->GetBackBuffer(w->real, bb, type, pp);
  /* The GetBackBuffer trigger is the FALLBACK target (the port's path: capture
   * the frame the app reads back for its own screenshot). When a capframe is set
   * (the retail/cfg path) it is authoritative — don't let retail's own internal
   * GetBackBuffer calls finalize early. */
  if (CAP && g_capframe == 0xFFFFFFFFu && g_present_count >= 1) {
      proxy_log("GetBackBuffer trigger @ frame %u\n", g_frame); finalize_capture(w->real); }
  return hr; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{ (void)reserved; if (reason == DLL_PROCESS_ATTACH) { g_self = h; proxy_log("DllMain attach\n"); } return TRUE; }
