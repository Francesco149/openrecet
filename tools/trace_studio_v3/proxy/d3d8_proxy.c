/* OpenRecet Trace Studio v3 — proxy d3d8.dll.
 *
 * Drop-in d3d8.dll wrapper used by BOTH the port and the retail exe (app-dir
 * loads before System32). Wraps Direct3DCreate8 -> IDirect3D8 -> CreateDevice
 * -> IDirect3DDevice8 and records the full call + resource stream for replay.
 *
 * P0a (this stage): forwarding skeleton ONLY — wrap factory + device, forward
 * every method, log load/CreateDevice/Present so we can prove the port runs
 * normally with the proxy in place. Capture (P0b) + the binary container come
 * next; the hooks below are stubbed so the structure is already in place.
 *
 * Design note: only the DEVICE and FACTORY are wrapped. Resources (textures,
 * VBs, IBs) are returned to the app UNWRAPPED (real pointers) — their contents
 * are snapshotted lazily at use-time by read-only Lock (the port allocates them
 * D3DPOOL_MANAGED -> lockable). So the only wrapped `this` is the device, which
 * is only ever used as the receiver of device calls. Keeps the proxy small.
 */
#define CINTERFACE
#define COBJMACROS
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ── wrapper objects (first member MUST be lpVtbl to match COM layout) ── */
typedef struct WrapD3D {
    const IDirect3D8Vtbl *lpVtbl;
    IDirect3D8           *real;
    LONG                  refs;
} WrapD3D;

typedef struct WrapDev {
    const IDirect3DDevice8Vtbl *lpVtbl;
    IDirect3DDevice8           *real;
    LONG                        refs;
} WrapDev;

/* ── custom (intercepted) methods — forward-declared so the generated
 *    vtable initializers can reference them; defined below. ── */
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_QueryInterface(IDirect3D8 *This, REFIID riid, void **ppv);
static ULONG   STDMETHODCALLTYPE my_IDirect3D8_AddRef(IDirect3D8 *This);
static ULONG   STDMETHODCALLTYPE my_IDirect3D8_Release(IDirect3D8 *This);
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_CreateDevice(IDirect3D8 *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **ppDev);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_QueryInterface(IDirect3DDevice8 *This, REFIID riid, void **ppv);
static ULONG   STDMETHODCALLTYPE my_IDirect3DDevice8_AddRef(IDirect3DDevice8 *This);
static ULONG   STDMETHODCALLTYPE my_IDirect3DDevice8_Release(IDirect3DDevice8 *This);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Present(IDirect3DDevice8 *This, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty);

#include "proxy_generated.h"

/* ── logging / capture stubs (P0b fills these) ── */
static FILE     *g_log;
static unsigned  g_present_count;
static HINSTANCE g_self;            /* this DLL's module handle (for path resolve) */

/* Resolve an output path next to THIS dll (build/) unless OPENRECET_V3_OUT
 * overrides — sidesteps WSL<->Windows path translation for the test. */
static void proxy_out_path(char *out, size_t n, const char *leaf)
{
    const char *dir = getenv("OPENRECET_V3_OUT");
    if (dir && *dir) { snprintf(out, n, "%s\\%s", dir, leaf); return; }
    char self[MAX_PATH] = ".";
    DWORD k = GetModuleFileNameA(g_self, self, sizeof self);
    while (k && self[k] != '\\' && self[k] != '/') k--;   /* strip filename */
    self[k] = 0;
    snprintf(out, n, "%s\\%s", self, leaf);
}

static void proxy_log(const char *fmt, ...)
{
    if (!g_log) {
        char path[MAX_PATH + 32];
        proxy_out_path(path, sizeof path, "v3proxy.log");
        g_log = fopen(path, "w");
        if (!g_log) return;
        setvbuf(g_log, NULL, _IOLBF, 0);
    }
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
}

/* ── real d3d8.dll resolution (load by full system path; never ourselves) ── */
typedef IDirect3D8 *(WINAPI *PFN_Create8)(UINT);
static HMODULE     g_realDll;
static PFN_Create8 g_realCreate;

static void load_real_d3d8(void)
{
    if (g_realCreate) return;
    char sysdir[MAX_PATH];
    UINT n = GetSystemDirectoryA(sysdir, sizeof sysdir);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%.*s\\d3d8.dll", (int)n, sysdir);
    g_realDll = LoadLibraryA(path);
    if (!g_realDll) { proxy_log("FATAL: real d3d8 load failed (%s)\n", path); return; }
    g_realCreate = (PFN_Create8)(void*)GetProcAddress(g_realDll, "Direct3DCreate8");
    proxy_log("real d3d8: %s create=%p\n", path, (void*)g_realCreate);
}

/* ── exported entry point (undecorated name via the .def) ── */
__declspec(dllexport) IDirect3D8 * WINAPI Direct3DCreate8(UINT SDKVersion)
{
    load_real_d3d8();
    if (!g_realCreate) return NULL;
    IDirect3D8 *real = g_realCreate(SDKVersion);
    if (!real) { proxy_log("real Direct3DCreate8 -> NULL\n"); return NULL; }
    WrapD3D *w = (WrapD3D*)calloc(1, sizeof *w);
    w->lpVtbl = &g_IDirect3D8_vt;
    w->real   = real;
    w->refs   = 1;
    proxy_log("Direct3DCreate8(0x%x) wrapped factory=%p real=%p\n",
              SDKVersion, (void*)w, (void*)real);
    return (IDirect3D8*)w;
}

/* ── factory custom methods ── */
static HRESULT STDMETHODCALLTYPE my_IDirect3D8_QueryInterface(IDirect3D8 *This, REFIID riid, void **ppv)
{
    WrapD3D *w = (WrapD3D*)This;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D8)) {
        *ppv = This; InterlockedIncrement(&w->refs); return S_OK;
    }
    return w->real->lpVtbl->QueryInterface(w->real, riid, ppv);
}
static ULONG STDMETHODCALLTYPE my_IDirect3D8_AddRef(IDirect3D8 *This)
{
    return (ULONG)InterlockedIncrement(&((WrapD3D*)This)->refs);
}
static ULONG STDMETHODCALLTYPE my_IDirect3D8_Release(IDirect3D8 *This)
{
    WrapD3D *w = (WrapD3D*)This;
    LONG r = InterlockedDecrement(&w->refs);
    if (r == 0) { w->real->lpVtbl->Release(w->real); free(w); }
    return (ULONG)r;
}
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
    wd->lpVtbl = &g_IDirect3DDevice8_vt;
    wd->real   = real;
    wd->refs   = 1;
    proxy_log("CreateDevice ok: %ux%u fmt=%d windowed=%d flags=0x%x wrap=%p real=%p\n",
              pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
              pp ? (int)pp->BackBufferFormat : -1, pp ? pp->Windowed : -1,
              BehaviorFlags, (void*)wd, (void*)real);
    *ppDev = (IDirect3DDevice8*)wd;
    return hr;
}

/* ── device custom methods (P0a: lifetime only; P0b adds capture) ── */
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_QueryInterface(IDirect3DDevice8 *This, REFIID riid, void **ppv)
{
    WrapDev *w = (WrapDev*)This;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DDevice8)) {
        *ppv = This; InterlockedIncrement(&w->refs); return S_OK;
    }
    return w->real->lpVtbl->QueryInterface(w->real, riid, ppv);
}
static ULONG STDMETHODCALLTYPE my_IDirect3DDevice8_AddRef(IDirect3DDevice8 *This)
{
    return (ULONG)InterlockedIncrement(&((WrapDev*)This)->refs);
}
static ULONG STDMETHODCALLTYPE my_IDirect3DDevice8_Release(IDirect3DDevice8 *This)
{
    WrapDev *w = (WrapDev*)This;
    LONG r = InterlockedDecrement(&w->refs);
    if (r == 0) {
        proxy_log("device Release -> 0 (presents=%u)\n", g_present_count);
        w->real->lpVtbl->Release(w->real); free(w);
    }
    return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_Present(
    IDirect3DDevice8 *This, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    WrapDev *w = (WrapDev*)This;
    g_present_count++;
    if (g_present_count <= 3 || g_present_count % 200 == 0)
        proxy_log("Present #%u\n", g_present_count);
    return w->real->lpVtbl->Present(w->real, src, dst, wnd, dirty);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { g_self = h; proxy_log("DllMain attach\n"); }
    return TRUE;
}
