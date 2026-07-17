/* OpenRecet Trace Studio v3 — proxy d3d8.dll (capture).
 *
 * Drop-in d3d8.dll used by BOTH the port and retail (app-dir loads before
 * System32). Wraps the factory + device and records the exact D3D8 command
 * stream + the referenced resources (textures/VB/IB) into a flat container
 * (orv3_format.h), sufficient to RE-RENDER the frame. Only the device + factory
 * are wrapped; resources are returned UNWRAPPED and snapshotted by read-only Lock
 * (port allocates them D3DPOOL_MANAGED -> lockable).
 *
 * DEFERRED-SNAPSHOT capture (P1 two-section): each frame's calls accumulate in an
 * in-memory buffer that is dropped every Present; only KEPT frames survive.
 * Resource snapshots are DEFERRED to write_frame, so the multi-thousand-frame
 * prologue/load costs ZERO snapshot work — only a kept frame's bound resources are
 * ever read back (current contents, current pointers ⇒ no stale data, no
 * pointer-reuse-across-transition bug), content-hash dedup'd across the whole
 * window. Two keep-triggers, one container format:
 *   • GetBackBuffer MULTI (the PORT): the app reads back each caprange frame for
 *     its own screenshot; the proxy piggybacks on that readback to keep the frame.
 *   • present-WINDOW (RETAIL / cfg / runtime-armed): retail does NOT read back per
 *     frame, so the window is addressed by present-count — keep every present in
 *     [capframe, capframe+capcount). capcount==1 is the P1/R2 single frame.
 * write_frame snapshots a kept frame's resources, patches their ids into the
 * buffered calls, writes [new RES][preamble][calls][Present], and reads back the
 * reference backbuffer, so the replayer re-renders + compares the exact frame.
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
/* GX-04: VB/IB wrappers. A VB/IB's bytes are written ONLY via Lock/Unlock (D3D8
 * CreateVertexBuffer has no init-data param) or ProcessVertices (census-gated
 * 0-observed), so intercepting Lock/Unlock here captures EVERY content version. The
 * `shadow` mirrors the buffer's current bytes (updated at each writable Unlock from the
 * app's still-mapped locked pointer — no re-Lock of `real`); a bind FREEZES the current
 * shadow (freeze-at-bind), so a same-frame re-mutation yields two distinct captured
 * versions. `gen` bumps per writable Unlock (diagnostic). lock_* remember the pending
 * Lock's range so Unlock copies exactly what was written (partial locks accumulate). */
typedef struct WrapVB {
    const IDirect3DVertexBuffer8Vtbl *lpVtbl; IDirect3DVertexBuffer8 *real; LONG refs;
    uint32_t size, fvf, gen; uint8_t *shadow; int shadow_valid;
    void *lock_ptr; uint32_t lock_off, lock_size; DWORD lock_flags;
} WrapVB;
typedef struct WrapIB {
    const IDirect3DIndexBuffer8Vtbl *lpVtbl; IDirect3DIndexBuffer8 *real; LONG refs;
    uint32_t size, fmt, gen; uint8_t *shadow; int shadow_valid;
    void *lock_ptr; uint32_t lock_off, lock_size; DWORD lock_flags;
} WrapIB;

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
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateTexture(IDirect3DDevice8*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8**);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetRenderTarget(IDirect3DDevice8*, IDirect3DSurface8*, IDirect3DSurface8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CopyRects(IDirect3DDevice8*, IDirect3DSurface8*, const RECT*, UINT, IDirect3DSurface8*, const POINT*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateVertexBuffer(IDirect3DDevice8*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer8**);
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateIndexBuffer(IDirect3DDevice8*, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer8**);
/* GX-04 VB/IB wrapper custom (lifetime + Lock/Unlock content versioning) */
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_QueryInterface(IDirect3DVertexBuffer8*, REFIID, void**);
static ULONG   STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_AddRef(IDirect3DVertexBuffer8*);
static ULONG   STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Release(IDirect3DVertexBuffer8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Lock(IDirect3DVertexBuffer8*, UINT, UINT, BYTE**, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Unlock(IDirect3DVertexBuffer8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_QueryInterface(IDirect3DIndexBuffer8*, REFIID, void**);
static ULONG   STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_AddRef(IDirect3DIndexBuffer8*);
static ULONG   STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Release(IDirect3DIndexBuffer8*);
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Lock(IDirect3DIndexBuffer8*, UINT, UINT, BYTE**, DWORD);
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Unlock(IDirect3DIndexBuffer8*);

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
 *   capframe=N   present-count WINDOW start (default: GetBackBuffer trigger)
 *   capcount=M   present-count WINDOW length (default 1 = single frame)
 *   armwait=1    IDLE until OrV3ArmWindowAt fires — suppress the GetBackBuffer
 *                MULTI keep-trigger entirely. For anchor-relative retail capture:
 *                through the long pre-anchor load g_capframe is unset, so the
 *                MULTI trigger would otherwise keep ANY stray readback (retail's
 *                own, or a leftover agent capture) as a bogus load frame. With
 *                armwait the proxy keeps NOTHING until the runtime arm sets the
 *                present-window, then the WINDOW branch alone decides keeps.
 *   refhash=1    HASH references instead of raw ones: per kept frame, append a
 *                line to v3refs.txt (kept#, present, dims, fnv1a-64 of the BGRA
 *                pixels) and write NO v3ref_NNN.raw. A thousands-of-frames window
 *                at 3 MB/raw would be GBs of duplicate pixels; the hash carries
 *                the same bit-exactness check (replay --verify-hashes recomputes
 *                it over the re-rendered frame).
 *   refraw_every=N  with refhash: still write a full v3ref_NNN.raw every N kept
 *                frames (spot-check / divergence forensics anchor). 0 = none.
 *   out=PATH     Windows dir for the container/log/reference (default LOCALAPPDATA)
 * getenv stays honored as a fallback (harmless for the port, which uses neither). */
static char     g_cfg_out[MAX_PATH];
static unsigned g_cfg_capframe = 0xFFFFFFFFu;
static unsigned g_cfg_capcount = 1u;
static int      g_cfg_armwait;             /* idle until OrV3ArmWindowAt (no MULTI keep) */
static int      g_cfg_refhash;             /* hash refs into v3refs.txt (no per-frame raw) */
static unsigned g_cfg_refraw_every;        /* with refhash: full raw every N kept frames (0=never) */
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
        else if (!strcmp(key, "capcount")) { g_cfg_capcount = (unsigned)strtoul(val, NULL, 0); if (!g_cfg_capcount) g_cfg_capcount = 1u; }
        else if (!strcmp(key, "armwait"))  g_cfg_armwait = (int)strtoul(val, NULL, 0);
        else if (!strcmp(key, "refhash"))  g_cfg_refhash = (int)strtoul(val, NULL, 0);
        else if (!strcmp(key, "refraw_every")) g_cfg_refraw_every = (unsigned)strtoul(val, NULL, 0);
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
static unsigned  g_capframe = 0xFFFFFFFFu; /* WINDOW start (present-count); 0xFFFFFFFF = unset → GetBackBuffer MULTI (port) */
static unsigned  g_capcount = 1u;          /* WINDOW length in presents (retail present-window keep mode); 1 = single frame */
static unsigned  g_frame;          /* present-counted frame index */
static int       g_capturing;
static unsigned  g_present_count;
/* resource dedup: CONTENT-HASH -> id, persisted across the WHOLE session (NOT
 * reset per frame). A resource's bytes are hashed (fnv1a-64 over type+body); an
 * already-seen hash reuses its id and writes NOTHING, so a texture/mesh bound in
 * every frame of a window is stored ONCE (the full-extent dedup win — storage
 * stays ≈ one frame regardless of window length). Content-hash (not pointer) is
 * correct for BOTH a re-locked dynamic buffer (new bytes -> new hash -> new id)
 * AND a freed pointer reused across a scene transition (pointer-dedup would
 * alias them; content-hash cannot). g_next_resid is the session-wide allocator. */
#define ORV3_MAXRES 32768
static uint64_t  g_res_hash[ORV3_MAXRES];
static int       g_res_id  [ORV3_MAXRES];
static int       g_n_res;
static int       g_next_resid;
static unsigned  g_kept;            /* kept-frame counter (0-based; names per-frame refs) */
static int       g_frame_kept;      /* this present was already written by the GetBackBuffer trigger */

#define ORV3_FNV_SEED 0xcbf29ce484222325ull
static uint64_t fnv1a(const void *p, size_t n, uint64_t h)
{ const uint8_t *b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; } return h; }

/* reusable blob buffer: a snapshot's RES body is built here (so each resource is
 * locked only ONCE per snapshot), then hashed + dedup'd + written. */
static uint8_t  *g_bl; static size_t g_bl_len, g_bl_cap;
static void bl_reset(void) { g_bl_len = 0; }
static void bl_ensure(size_t n) { if (g_bl_len + n <= g_bl_cap) return; size_t nc = g_bl_cap ? g_bl_cap : (1u<<16); while (nc < g_bl_len + n) nc <<= 1; g_bl = (uint8_t*)realloc(g_bl, nc); g_bl_cap = nc; }
static void bl_u(uint32_t v) { bl_ensure(4); memcpy(g_bl + g_bl_len, &v, 4); g_bl_len += 4; }
static void bl_data(const void *p, size_t n) { bl_ensure(n); if (n) memcpy(g_bl + g_bl_len, p, n); g_bl_len += n; }
static void bl_bytes(const void *p, uint32_t n) { bl_u(n); bl_data(p, n); }  /* length-prefixed (mirrors orv3_wbytes) */

/* dedup a RES body (the bytes AFTER [type][id]) by content hash: return an
 * existing id if these exact bytes were already stored this session, else assign
 * a new id, write [type][id][body] to the container, and remember the hash.
 * 64-bit fnv over type+body — collision odds for a few thousand resources are
 * ~1e-13, far below the project's tolerance, so no byte-compare-on-match. */
static int dedup_or_write(uint32_t type, const uint8_t *body, size_t bodylen)
{
    uint64_t h = fnv1a(body, bodylen, fnv1a(&type, 4, ORV3_FNV_SEED));
    for (int i = 0; i < g_n_res; i++) if (g_res_hash[i] == h) return g_res_id[i];
    int id = g_next_resid++;
    orv3_wu(g_cap, type); orv3_wu(g_cap, (uint32_t)id);
    fwrite(body, 1, bodylen, g_cap);
    if (g_n_res < ORV3_MAXRES) { g_res_hash[g_n_res] = h; g_res_id[g_n_res] = id; g_n_res++; }
    return id;
}

/* ── render-target texture registry (v3 RT capture) ──
 * RTs are identified by IDENTITY (the pointer CreateTexture handed back), NOT
 * content: a DEFAULT-pool RT texture can't be locked (snap_tex would store
 * datalen=0), and content-hash dedup would alias two same-size RTs and never
 * carry their (proxy-can't-read) pixels — an RT's content is produced by the
 * replayed SetRenderTarget/draw/CopyRects stream, not stored. my_CreateTexture
 * records every usage&RENDERTARGET texture here; snap_tex routes a BOUND RT here
 * (it's also SetTexture'd as a composite source); classify_surface resolves an RT
 * surface's parent here. The resid is assigned LAZILY at first reference
 * (snap_rt_tex), the RES_RT_TEX record written once into the container. */
typedef struct { void *ptr; uint32_t w, h, fmt, levels, usage; int id; } RtTex;
#define ORV3_MAXRT 64
static RtTex g_rt[ORV3_MAXRT]; static int g_n_rt;
static RtTex *rt_find(void *ptr)
{ for (int i = 0; i < g_n_rt; i++) if (g_rt[i].ptr == ptr) return &g_rt[i]; return NULL; }
static void rt_register(void *ptr, uint32_t w, uint32_t h, uint32_t fmt, uint32_t levels, uint32_t usage)
{
    if (!ptr || rt_find(ptr) || g_n_rt >= ORV3_MAXRT) return;
    g_rt[g_n_rt] = (RtTex){ ptr, w, h, fmt, levels ? levels : 1u, usage, -1 };
    g_n_rt++;
}
/* assign+write the RES_RT_TEX (once) for a registered RT texture; return its id
 * (-1 if not an RT). Body: [w][h][fmt][levels][usage] after [type][id]. */
static int snap_rt_tex(void *ptr)
{
    RtTex *r = rt_find(ptr);
    if (!r) return -1;
    if (r->id < 0) {
        r->id = g_next_resid++;
        orv3_wu(g_cap, ORV3_RES_RT_TEX); orv3_wu(g_cap, (uint32_t)r->id);
        orv3_wu(g_cap, r->w); orv3_wu(g_cap, r->h); orv3_wu(g_cap, r->fmt);
        orv3_wu(g_cap, r->levels); orv3_wu(g_cap, r->usage);
    }
    return r->id;
}

/* In-memory CALL buffer for the CURRENT frame only. Calls accumulate here as
 * they happen; at a frame boundary they are either WRITTEN (a kept window frame,
 * via write_frame) or DROPPED (cb_reset on a load/non-capture frame). Resource
 * snapshots are DEFERRED to write_frame: only a KEPT frame's bound resources are
 * ever read back, so the multi-thousand-frame prologue/load costs ZERO snapshot
 * work (no throttle, no 963 MB balloon). write_frame snapshots that frame's bound
 * resources (current contents/pointers — no stale data, no pointer-reuse bug),
 * content-hash dedup'd across the whole window, patches their ids into these
 * calls, and writes [new RES][preamble][calls][Present] — resources first, so
 * the streaming replayer always sees an id defined before it is used. */
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

/* per-frame RESOURCE-CONTENT arena (GX-04 freeze-at-bind): a VB/IB bind memcpies the
 * wrapper's current-generation shadow HERE (cheap, no hash/dedup/container-write), so a
 * later same-frame mutation can't overwrite the version this draw used. Reset per frame
 * like g_cb (dropped frames cost 0 — nothing reaches g_cap). write_frame hashes+dedups
 * the frozen bytes into RES_VB/RES_IB. */
static uint8_t  *g_rc; static size_t g_rc_len, g_rc_cap;
static void rc_ensure(size_t n) { if (g_rc_len + n <= g_rc_cap) return; size_t nc = g_rc_cap ? g_rc_cap : (1u<<16); while (nc < g_rc_len + n) nc <<= 1; g_rc = (uint8_t*)realloc(g_rc, nc); g_rc_cap = nc; }
static size_t rc_append(const void *p, uint32_t n) { rc_ensure(n); size_t off = g_rc_len; if (n) memcpy(g_rc + off, p, n); g_rc_len += n; return off; }

/* deferred resource binds: remember (kind,ptr,id-field-offset); snapshot + patch at
 * write_frame. A frozen VB/IB entry (frz=1) also carries the arena range [rc_off,rc_len)
 * of its bind-time content + meta (fvf/fmt); write_frame snaps THOSE bytes instead of
 * re-locking `ptr` (which is kept only for the multibind diagnostic). */
enum { PEND_TEX = 1, PEND_VB = 2, PEND_IB = 3, PEND_SURF = 4 };
typedef struct { int kind; void *ptr; size_t off; int frz; size_t rc_off; uint32_t rc_len, meta; } Pending;
static Pending  *g_pending; static int g_pending_n, g_pending_cap;
static Pending *pend_new(int kind, void *ptr, size_t off)
{
    if (g_pending_n >= g_pending_cap) {
        g_pending_cap = g_pending_cap ? g_pending_cap * 2 : 8192;
        g_pending = (Pending*)realloc(g_pending, (size_t)g_pending_cap * sizeof *g_pending);
    }
    Pending *e = &g_pending[g_pending_n++];
    e->kind = kind; e->ptr = ptr; e->off = off; e->frz = 0; e->rc_off = 0; e->rc_len = 0; e->meta = 0;
    return e;
}
static void pend_push(int kind, void *ptr, size_t off) { pend_new(kind, ptr, off); }
/* emit a deferred resource-ref id field into g_cb (placeholder now; snapshot at write_frame) */
static void cb_resref(int kind, void *ptr)
{
    size_t off = g_cb_len; cb_u(0xffffffffu);     /* placeholder; patched to the snapshot id (or -1) */
    if (ptr) pend_push(kind, ptr, off);
}
/* emit a resource-ref that FREEZES `shadow[0..size]` into the content arena NOW; if the
 * buffer was never Locked through the wrapper (shadow invalid — shouldn't happen since
 * content only comes via Lock) fall back to a frame-end real-Lock snapshot of `real`. */
static void cb_resref_frozen(int kind, void *wrapper, const uint8_t *shadow, int shadow_valid,
                             uint32_t size, uint32_t meta, void *real)
{
    size_t off = g_cb_len; cb_u(0xffffffffu);
    if (!wrapper) return;
    if (shadow_valid) {
        Pending *e = pend_new(kind, wrapper, off);
        e->frz = 1; e->rc_off = rc_append(shadow, size); e->rc_len = size; e->meta = meta;
    } else {
        pend_push(kind, real, off);   /* fallback: snapshot real at frame-end */
    }
}
static void cb_reset(void) { g_cb_len = 0; g_pending_n = 0; g_rc_len = 0; }

/* ── GX-03 measurement: same-frame VB/IB re-mutation risk surface ──
 * Resource snapshots are DEFERRED to write_frame (kept frames only), so a buffer
 * bound at draw A, then re-mutated (Lock/Unlock) before draw B in the SAME frame,
 * gets ONE frame-end snapshot for both draws — draw A would replay with B's content.
 * Recettear locks its VBs read-write in place (decompile FUN@71555 `Lock(0,0,&p,0)`,
 * flags=0) and hands out transient buffers it Releases after use, so the hazard is
 * real IN PRINCIPLE. This measures whether it OCCURS on the captured scene, at ZERO
 * cost on the thousands of non-kept load frames (write_frame runs only on kept
 * frames): per kept frame we count binds, MULTIBIND pointers (a ptr bound >1× in one
 * frame = a reused buffer whose one frame-end snapshot may not equal its per-draw
 * content), and SNAPFAIL (id==-1: a bound buffer that could not be locked at
 * frame-end, e.g. already released). 0 multibind + 0 snapfail over the whole window
 * ⇒ every VB/IB is used at most once per frame and snapshotted OK ⇒ the frame-end
 * snapshot IS the per-draw content (GX-03 completeness holds for this scene). */
static long g_rb_vb_binds, g_rb_vb_multibind, g_rb_vb_snapfail, g_rb_vb_fallback;
static long g_rb_ib_binds, g_rb_ib_multibind, g_rb_ib_snapfail, g_rb_ib_fallback;
static int  g_rb_frames;                                  /* kept frames measured */
static int  g_rb_vb_multibind_max, g_rb_ib_multibind_max; /* worst single frame */
/* GX-04: post-wrap, VB/IB content is FROZEN at bind (per-draw versions), so `fallback`
 * (a bind snapshotted the old frame-end way — buffer never Locked through the wrapper)
 * should stay 0 and `snapfail` counts only that residual path. `multibind` still counts
 * pointer reuse (informational: the risk surface the freeze now handles correctly). */
/* count DISTINCT pointers of `kind` in g_pending that appear more than once. O(n²)
 * over one frame's pending list (a few hundred entries) — trivial, kept frames only. */
static int pending_multibind(int kind)
{
    int multi = 0;
    for (int i = 0; i < g_pending_n; i++) {
        if (g_pending[i].kind != kind || !g_pending[i].ptr) continue;
        int first = 1;
        for (int j = 0; j < i; j++)
            if (g_pending[j].kind == kind && g_pending[j].ptr == g_pending[i].ptr) { first = 0; break; }
        if (!first) continue;
        for (int j = i + 1; j < g_pending_n; j++)
            if (g_pending[j].kind == kind && g_pending[j].ptr == g_pending[i].ptr) { multi++; break; }
    }
    return multi;
}

/* ── surface identity (v3 RT capture) ──
 * SetRenderTarget/CopyRects cite IDirect3DSurface8 POINTERS. The replayer can't
 * re-use the app's pointers, so each surface is recorded as a SURFREF [kind][resid]
 * the replayer can reconstruct: the backbuffer (GetBackBuffer), the auto depth
 * (GetDepthStencilSurface), or an RT texture's level-0 surface (GetSurfaceLevel of
 * tex[resid]). We classify a pointer by: equality with the cached real
 * backbuffer/depth surfaces (the app's "saved RT" from GetRenderTarget is the same
 * object) → GetContainer(IID_IDirect3DTexture8) for a texture-backed RT → else an
 * argument-position default. The app obtained the RT surfaces via GetSurfaceLevel
 * at init (FUN_0047ae65) — the proxy never saw that call, so GetContainer is how we
 * recover the parent texture. Cached refs are held for the proxy's lifetime (the
 * device owns the underlying surfaces; the extra ref is harmless). */
static IDirect3DSurface8 *g_bb_surf, *g_depth_surf;
static void ensure_special_surfaces(IDirect3DDevice8 *real)
{
    if (!g_bb_surf)    IDirect3DDevice8_GetBackBuffer(real, 0, D3DBACKBUFFER_TYPE_MONO, &g_bb_surf);
    if (!g_depth_surf) IDirect3DDevice8_GetDepthStencilSurface(real, &g_depth_surf);
}
static uint32_t classify_surface(IDirect3DDevice8 *real, IDirect3DSurface8 *surf, int is_depth, void **out_tex)
{
    *out_tex = NULL;
    if (!surf) return ORV3_SURF_NULL;
    ensure_special_surfaces(real);
    if (surf == g_bb_surf)    return ORV3_SURF_BACKBUFFER;
    if (surf == g_depth_surf) return ORV3_SURF_DEPTH;
    IDirect3DTexture8 *tex = NULL;
    if (SUCCEEDED(IDirect3DSurface8_GetContainer(surf, &IID_IDirect3DTexture8, (void**)&tex)) && tex) {
        IDirect3DTexture8_Release(tex);   /* app holds its own ref; we keep only the ptr VALUE */
        *out_tex = tex;
        return ORV3_SURF_TEX;
    }
    return is_depth ? ORV3_SURF_DEPTH : ORV3_SURF_BACKBUFFER;
}
/* emit a SURFREF [kind][resid] into the call buffer. SURF_TEX's resid is the RT
 * texture's resource id — DEFERRED (snapshotted at write_frame like cb_resref, so
 * the RES_RT_TEX is written before the calls that cite it); other kinds carry 0. */
static void cb_surfref(IDirect3DDevice8 *real, IDirect3DSurface8 *surf, int is_depth)
{
    void *tex = NULL;
    uint32_t kind = classify_surface(real, surf, is_depth, &tex);
    cb_u(kind);
    if (kind == ORV3_SURF_TEX && tex) { size_t off = g_cb_len; cb_u(0xffffffffu); pend_push(PEND_SURF, tex, off); }
    else cb_u(0);
}

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
/* write the shadow as a scalar-state preamble straight to the container, at the
 * START of a kept frame's section (NOT into the call buffer): only kept frames
 * pay for it, so the multi-thousand-frame load never churns it. Each kept frame
 * begins at its exact inherited state ⇒ replayable standalone. */
static void write_shadow_preamble(void)
{
    for (int s = 0; s < ORV3_NRS; s++)
        if (g_sh_rs_set[s]) { orv3_wu(g_cap, ORV3_SetRenderState); orv3_wu(g_cap, (uint32_t)s); orv3_wu(g_cap, g_sh_rs[s]); }
    for (int st = 0; st < 8; st++) for (int t = 0; t < ORV3_NTSS; t++)
        if (g_sh_tss_set[st][t]) { orv3_wu(g_cap, ORV3_SetTextureStageState); orv3_wu(g_cap, (uint32_t)st); orv3_wu(g_cap, (uint32_t)t); orv3_wu(g_cap, g_sh_tss[st][t]); }
    for (int x = 0; x < ORV3_NXFORM; x++)
        if (g_sh_xform_set[x]) { orv3_wu(g_cap, ORV3_SetTransform); orv3_wu(g_cap, (uint32_t)x); fwrite(g_sh_xform[x], sizeof(float), 16, g_cap); }
    if (g_sh_mat_set) { orv3_wu(g_cap, ORV3_SetMaterial); fwrite(g_sh_mat, 1, 68, g_cap); }
    if (g_sh_fvf_set) { orv3_wu(g_cap, ORV3_SetVertexShader); orv3_wu(g_cap, g_sh_fvf); }
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
/* snapshot a 2D texture's managed sysmem copy (all mip levels), content-hash
 * dedup'd. Builds the RES_TEX body in the blob buffer (locks each level once),
 * then dedup_or_write returns the existing id if these exact bytes were already
 * stored. Body layout (mirrors the replayer's read): [levels] then per level
 * [w][h][fmt][rowbytes][datalen][raw rows]. */
static int snap_tex(IDirect3DBaseTexture8 *bt)
{
    if (!bt) return -1;
    if (rt_find((void*)bt)) return snap_rt_tex((void*)bt);   /* RT: identity, not content */
    if (IDirect3DBaseTexture8_GetType(bt) != D3DRTYPE_TEXTURE) return -1;  /* P0: 2D only */
    IDirect3DTexture8 *tex = (IDirect3DTexture8*)bt;
    DWORD levels = IDirect3DTexture8_GetLevelCount(tex);
    bl_reset(); bl_u(levels);
    for (DWORD l = 0; l < levels; l++) {
        D3DSURFACE_DESC d = {0}; IDirect3DTexture8_GetLevelDesc(tex, l, &d);
        D3DLOCKED_RECT lr = {0};
        int bpp = orv3_fmt_bpp((int)d.Format);
        if (FAILED(IDirect3DTexture8_LockRect(tex, l, &lr, NULL, D3DLOCK_READONLY)) || !bpp) {
            bl_u(d.Width); bl_u(d.Height); bl_u(d.Format);
            bl_u(0); bl_u(0);                 /* rowbytes=0, datalen=0 */
            if (lr.pBits) IDirect3DTexture8_UnlockRect(tex, l);
            continue;
        }
        uint32_t rowbytes = d.Width * (uint32_t)bpp;
        bl_u(d.Width); bl_u(d.Height); bl_u(d.Format);
        bl_u(rowbytes); bl_u(rowbytes * d.Height);
        for (DWORD r = 0; r < d.Height; r++)
            bl_data((const uint8_t*)lr.pBits + (size_t)r * lr.Pitch, rowbytes);
        IDirect3DTexture8_UnlockRect(tex, l);
    }
    return dedup_or_write(ORV3_RES_TEX, g_bl, g_bl_len);
}
static int snap_vb(IDirect3DVertexBuffer8 *vb)
{
    if (!vb) return -1;
    D3DVERTEXBUFFER_DESC d = {0}; IDirect3DVertexBuffer8_GetDesc(vb, &d);
    BYTE *p = NULL;
    if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, D3DLOCK_READONLY)) || !p) {
        if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) || !p) return -1;
    }
    bl_reset(); bl_u(d.Size); bl_u(d.FVF); bl_bytes(p, d.Size);  /* [size][fvf][datalen][data] */
    IDirect3DVertexBuffer8_Unlock(vb);
    return dedup_or_write(ORV3_RES_VB, g_bl, g_bl_len);
}
static int snap_ib(IDirect3DIndexBuffer8 *ib)
{
    if (!ib) return -1;
    D3DINDEXBUFFER_DESC d = {0}; IDirect3DIndexBuffer8_GetDesc(ib, &d);
    BYTE *p = NULL;
    if (FAILED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, D3DLOCK_READONLY)) || !p) {
        if (FAILED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, 0)) || !p) return -1;
    }
    bl_reset(); bl_u(d.Size); bl_u(d.Format); bl_bytes(p, d.Size);  /* [size][fmt][datalen][data] */
    IDirect3DIndexBuffer8_Unlock(ib);
    return dedup_or_write(ORV3_RES_IB, g_bl, g_bl_len);
}
/* GX-04: snapshot a VB/IB from FROZEN bind-time bytes (not a live Lock). Body layout is
 * byte-identical to snap_vb/snap_ib, so a static buffer's frozen snapshot dedups to the
 * exact same id as its old frame-end snapshot ⇒ replay is unchanged; only a same-frame
 * re-mutation now yields a second, distinct id. */
static int snap_vb_bytes(const uint8_t *data, uint32_t size, uint32_t fvf)
{ bl_reset(); bl_u(size); bl_u(fvf); bl_bytes(data, size); return dedup_or_write(ORV3_RES_VB, g_bl, g_bl_len); }
static int snap_ib_bytes(const uint8_t *data, uint32_t size, uint32_t fmt)
{ bl_reset(); bl_u(size); bl_u(fmt); bl_bytes(data, size); return dedup_or_write(ORV3_RES_IB, g_bl, g_bl_len); }

/* per-kept-frame REFERENCE: read back the backbuffer once via the shared
 * CopyRects-through-sysmem helper (works for retail's non-lockable backbuffer),
 * then either write the raw {u32 w,h, w*h*4 BGRA} file (default) or — refhash —
 * append a fnv1a-64 line to v3refs.txt (+ a full raw every refraw_every frames).
 * v3refs.txt is unbuffered like the log: a hard device.kill keeps every
 * completed frame's line. */
static FILE *g_refs;
static void write_reference(IDirect3DDevice8 *dev, unsigned kept, unsigned present)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = orv3_readback_bgra(dev, &w, &h);
    if (!px) { proxy_log("readback FAILED (GetBackBuffer/CopyRects/Lock)\n"); return; }
    int want_raw = !g_cfg_refhash
                || (g_cfg_refraw_every && (kept % g_cfg_refraw_every) == 0);
    if (g_cfg_refhash) {
        if (!g_refs) {
            char p[MAX_PATH + 32]; proxy_out_path(p, sizeof p, "v3refs.txt");
            g_refs = fopen(p, "w"); if (g_refs) setvbuf(g_refs, NULL, _IONBF, 0);
        }
        if (g_refs) {
            uint64_t hsh = fnv1a(px, (size_t)w * h * 4u, ORV3_FNV_SEED);
            fprintf(g_refs, "REF %u present=%u w=%u h=%u fnv64=%016llx\n",
                    kept, present, w, h, (unsigned long long)hsh);
        }
    }
    if (want_raw) {
        char leaf[32]; snprintf(leaf, sizeof leaf, "v3ref_%03u.raw", kept);
        char path[MAX_PATH + 32]; proxy_out_path(path, sizeof path, leaf);
        FILE *fp = fopen(path, "wb");
        if (fp) {
            fwrite(&w, 4, 1, fp); fwrite(&h, 4, 1, fp);
            fwrite(px, 1, (size_t)w * h * 4u, fp);
            fclose(fp);
        }
    }
    free(px);
}

#define CAP (g_capturing && g_cap)

/* GX-00 dynamic census sidecar — rewrite v3cap.census.json with the current
 * per-forwarded-method call counts (g_fwd_calls, from proxy_generated.h; cumulative
 * over the whole process, NOT capture-gated). Written at each KEPT frame — like the
 * container frames themselves, so a hard device.kill still leaves the last completed
 * frame's census on disk — and the counts up to & including the last kept frame
 * bound the compared window. A render_affecting_unsupported method with a non-zero
 * count means the replay of this scene is INCOMPLETE (GX-01 record-or-fail). Tiny
 * (~84 lines), so the per-kept-frame rewrite is free vs the frame's own I/O. */
static void write_census_sidecar(void)
{
    char path[MAX_PATH+32]; proxy_out_path(path, sizeof path, "v3cap.census.json");
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs("{\n \"schema_version\": 1,\n \"forwarded_calls\": {\n", f);
    for (int i = 0; i < FWD__COUNT; i++)
        fprintf(f, "  \"%s\": %ld%s\n", g_fwd_names[i], (long)g_fwd_calls[i],
                i + 1 < FWD__COUNT ? "," : "");
    fputs(" },\n \"resource_binds\": {\n", f);   /* GX-03 same-frame re-mutation measure */
    fprintf(f,
        "  \"kept_frames\": %d,\n"
        "  \"vb_binds\": %ld, \"vb_multibind\": %ld, \"vb_multibind_max\": %d, \"vb_snapfail\": %ld, \"vb_fallback\": %ld,\n"
        "  \"ib_binds\": %ld, \"ib_multibind\": %ld, \"ib_multibind_max\": %d, \"ib_snapfail\": %ld, \"ib_fallback\": %ld\n",
        g_rb_frames,
        g_rb_vb_binds, g_rb_vb_multibind, g_rb_vb_multibind_max, g_rb_vb_snapfail, g_rb_vb_fallback,
        g_rb_ib_binds, g_rb_ib_multibind, g_rb_ib_multibind_max, g_rb_ib_snapfail, g_rb_ib_fallback);
    fputs(" }\n}\n", f);
    fclose(f);
}

/* write ONE kept frame's section to the container (does NOT close — multi-frame
 * keeps appending). Snapshot this frame's bound resources NOW (read back current
 * contents/pointers, no load-time work; content-hash dedup'd so a resource bound
 * every frame is stored once), patch their ids into the buffered calls, then
 * write [new RES][scalar preamble][this frame's calls][Present]. Read a per-frame
 * reference (v3ref_NNN.raw, NNN = kept-frame index) for the replayer to compare,
 * and fflush so a hard device.kill leaves every COMPLETED frame intact on disk
 * (the replayer tolerates a missing trailing EOF). Called per GetBackBuffer
 * readback (the harness's caprange frames) or at a present-count target. */
static void write_frame(IDirect3DDevice8 *real_dev)
{
    if (!CAP) return;
    for (int i = 0; i < g_pending_n; i++) {
        int id = -1;
        switch (g_pending[i].kind) {
        case PEND_TEX:  id = snap_tex((IDirect3DBaseTexture8*)g_pending[i].ptr); break;
        case PEND_VB:   id = g_pending[i].frz
                             ? snap_vb_bytes(g_rc + g_pending[i].rc_off, g_pending[i].rc_len, g_pending[i].meta)
                             : snap_vb((IDirect3DVertexBuffer8*)g_pending[i].ptr);
                        g_rb_vb_binds++;
                        if (!g_pending[i].frz) { g_rb_vb_fallback++; if (g_pending[i].ptr && id < 0) g_rb_vb_snapfail++; }
                        break;
        case PEND_IB:   id = g_pending[i].frz
                             ? snap_ib_bytes(g_rc + g_pending[i].rc_off, g_pending[i].rc_len, g_pending[i].meta)
                             : snap_ib((IDirect3DIndexBuffer8*)g_pending[i].ptr);
                        g_rb_ib_binds++;
                        if (!g_pending[i].frz) { g_rb_ib_fallback++; if (g_pending[i].ptr && id < 0) g_rb_ib_snapfail++; }
                        break;
        case PEND_SURF: id = snap_rt_tex(g_pending[i].ptr); break;   /* RT surface's parent texture */
        }
        cb_patch_u(g_pending[i].off, (uint32_t)id);
    }
    { int vm = pending_multibind(PEND_VB), im = pending_multibind(PEND_IB);   /* GX-03 measure */
      g_rb_vb_multibind += vm; g_rb_ib_multibind += im; g_rb_frames++;
      if (vm > g_rb_vb_multibind_max) g_rb_vb_multibind_max = vm;
      if (im > g_rb_ib_multibind_max) g_rb_ib_multibind_max = im; }
    write_shadow_preamble();                       /* inherited scalar state (RES already written above) */
    fwrite(g_cb, 1, g_cb_len, g_cap);              /* this frame's own calls */
    orv3_wu(g_cap, ORV3_Present); orv3_wu(g_cap, g_frame);
    write_reference(real_dev, g_kept, g_frame);
    fflush(g_cap);
    write_census_sidecar();                        /* GX-00: refresh forwarded-call census */
    proxy_log("KEEP present-frame %u (kept#%u, %u call-bytes, %d res total)\n",
              g_frame, g_kept, (unsigned)g_cb_len, g_next_resid);
    g_kept++;
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
    if (g_cfg_capframe != 0xFFFFFFFFu) { g_capframe = g_cfg_capframe; g_capcount = g_cfg_capcount; }  /* cfg present-window (retail/port) */
    const char *cf = getenv("OPENRECET_V3_CAPFRAME"); if (cf && *cf) g_capframe = (unsigned)atoi(cf);  /* env override */
    const char *cc = getenv("OPENRECET_V3_CAPCOUNT"); if (cc && *cc) { int n = atoi(cc); if (n > 0) g_capcount = (unsigned)n; }
    WrapD3D *w = (WrapD3D*)calloc(1, sizeof *w);
    w->lpVtbl = &g_IDirect3D8_vt; w->real = real; w->refs = 1;
    proxy_log("Direct3DCreate8 wrapped (capframe=%u)\n", g_capframe);
    return (IDirect3D8*)w;
}

/* ── runtime arm (retail anchor-relative capture) ──
 * A cfg-fixed present-count only reaches the deterministic-early TITLE; the
 * post-load gameplay window's present-count is nondeterministic (turbo load-
 * stretch), so the harness computes it LIVE — anchor present-count + offset — and
 * calls this to set the window before g_frame reaches `start`. Until armed the
 * proxy idles (the unset-capframe branch drops every present), so calling this any
 * time before `start` is fine: g_capframe/g_capcount are plain targets the WINDOW
 * branch reads. WINAPI(stdcall) + the Makefile's -Wl,--kill-at ⇒ an undecorated
 * export name for Frida's NativeFunction(addr,'void',['uint','uint'],'stdcall'). */
__declspec(dllexport) void WINAPI OrV3ArmWindowAt(unsigned start, unsigned count)
{
    g_capframe = start;
    if (count) g_capcount = count;
    proxy_log("ARM window [%u,%u) via export (now at present %u)\n",
              g_capframe, g_capframe + g_capcount, g_frame);
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
        if (g_capframe != 0xFFFFFFFFu) {
            /* WINDOW mode (retail / cfg / runtime-armed present-count window): keep
             * every present in [g_capframe, g_capframe+g_capcount), drop the rest,
             * finalize after the last. The retail counterpart of the port's
             * GetBackBuffer MULTI trigger — retail does NOT read back per frame, so
             * the window is addressed by present-count. capcount==1 reproduces the
             * P1/R2 single-frame capture. Resetting on EVERY present (after a kept
             * write_frame, or to drop a non-window frame) keeps g_cb to the CURRENT
             * frame only — the old single-frame path let it accumulate from frame 0
             * (harmless only because each frame re-Clears, but wasteful + wrong-count). */
            if (g_frame >= g_capframe && g_frame < g_capframe + g_capcount) {
                write_frame(w->real);                            /* keep: snapshot+preamble+calls+ref */
                if (g_frame + 1u == g_capframe + g_capcount) {   /* last frame in the window */
                    orv3_wu(g_cap, ORV3_EOF); fclose(g_cap); g_cap = NULL; g_capturing = 0;
                    proxy_log("FINALIZE window [%u,%u) (%u kept) + references\n",
                              g_capframe, g_capframe + g_capcount, g_kept);
                }
            }
            cb_reset();   /* next frame fresh (kept: after write_frame; dropped: no carry-over) */
        } else if (!g_frame_kept) {
            /* MULTI-FRAME (port GetBackBuffer trigger): this present was NOT a
             * kept (caprange) frame — drop its calls + pending. The shadow keeps
             * accumulating in the Set hooks, so the next kept frame's preamble is
             * still complete. ZERO snapshot/file work on load/non-capture frames. */
            cb_reset();
        }
        g_frame_kept = 0;
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
/* Is `p` one of OUR wrappers? (identity by vtable — a raw VB/IB the app might have
 * round-tripped through GetStreamSource/GetIndices, which forward `real`, is NOT.) */
static WrapVB *as_wrap_vb(IDirect3DVertexBuffer8 *p)
{ return (p && (const void*)((WrapVB*)p)->lpVtbl == (const void*)&g_IDirect3DVertexBuffer8_vt) ? (WrapVB*)p : NULL; }
static WrapIB *as_wrap_ib(IDirect3DIndexBuffer8 *p)
{ return (p && (const void*)((WrapIB*)p)->lpVtbl == (const void*)&g_IDirect3DIndexBuffer8_vt) ? (WrapIB*)p : NULL; }

static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetStreamSource(IDirect3DDevice8 *This, UINT stream, IDirect3DVertexBuffer8 *vb, UINT stride)
{ WrapVB *w = as_wrap_vb(vb);
  IDirect3DVertexBuffer8 *realvb = w ? w->real : vb;   /* unwrap (or pass a raw VB through) */
  if (CAP) { cb_u(ORV3_SetStreamSource); cb_u(stream);
             if (w) cb_resref_frozen(PEND_VB, w, w->shadow, w->shadow_valid, w->size, w->fvf, w->real);
             else   cb_resref(PEND_VB, vb);            /* raw/unwrapped: frame-end snapshot */
             cb_u(stride); }
  return ((WrapDev*)This)->real->lpVtbl->SetStreamSource(((WrapDev*)This)->real, stream, realvb, stride); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetIndices(IDirect3DDevice8 *This, IDirect3DIndexBuffer8 *ib, UINT base)
{ WrapIB *w = as_wrap_ib(ib);
  IDirect3DIndexBuffer8 *realib = w ? w->real : ib;
  if (CAP) { cb_u(ORV3_SetIndices);
             if (w) cb_resref_frozen(PEND_IB, w, w->shadow, w->shadow_valid, w->size, w->fmt, w->real);
             else   cb_resref(PEND_IB, ib);
             cb_u(base); }
  return ((WrapDev*)This)->real->lpVtbl->SetIndices(((WrapDev*)This)->real, realib, base); }
/* ── GX-04: VB/IB wrapping (per-draw content versioning) ──
 * CreateVertexBuffer/CreateIndexBuffer are INTERCEPTED: make the real buffer, wrap it so
 * every Lock/Unlock (the SOLE content writer — D3D8 has no create-time init data) is
 * seen. The wrapper's shadow tracks the current bytes; a bind freezes it. */
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateVertexBuffer(
    IDirect3DDevice8 *This, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **ppVB)
{
    IDirect3DVertexBuffer8 *real = NULL;
    HRESULT hr = ((WrapDev*)This)->real->lpVtbl->CreateVertexBuffer(((WrapDev*)This)->real, len, usage, fvf, pool, &real);
    if (FAILED(hr) || !real) { if (ppVB) *ppVB = NULL; return hr; }
    WrapVB *w = (WrapVB*)calloc(1, sizeof *w);
    w->lpVtbl = &g_IDirect3DVertexBuffer8_vt; w->real = real; w->refs = 1;
    w->size = len; w->fvf = fvf; w->shadow = (uint8_t*)calloc(1, len ? len : 1);
    if (ppVB) *ppVB = (IDirect3DVertexBuffer8*)w;
    return hr;
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateIndexBuffer(
    IDirect3DDevice8 *This, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **ppIB)
{
    IDirect3DIndexBuffer8 *real = NULL;
    HRESULT hr = ((WrapDev*)This)->real->lpVtbl->CreateIndexBuffer(((WrapDev*)This)->real, len, usage, fmt, pool, &real);
    if (FAILED(hr) || !real) { if (ppIB) *ppIB = NULL; return hr; }
    WrapIB *w = (WrapIB*)calloc(1, sizeof *w);
    w->lpVtbl = &g_IDirect3DIndexBuffer8_vt; w->real = real; w->refs = 1;
    w->size = len; w->fmt = (uint32_t)fmt; w->shadow = (uint8_t*)calloc(1, len ? len : 1);
    if (ppIB) *ppIB = (IDirect3DIndexBuffer8*)w;
    return hr;
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_QueryInterface(IDirect3DVertexBuffer8 *This, REFIID riid, void **ppv)
{ WrapVB *w = (WrapVB*)This;
  if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DVertexBuffer8)) { *ppv = This; InterlockedIncrement(&w->refs); return S_OK; }
  return w->real->lpVtbl->QueryInterface(w->real, riid, ppv); }
static ULONG STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_AddRef(IDirect3DVertexBuffer8 *This)
{ return (ULONG)InterlockedIncrement(&((WrapVB*)This)->refs); }
static ULONG STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Release(IDirect3DVertexBuffer8 *This)
{ WrapVB *w = (WrapVB*)This; LONG r = InterlockedDecrement(&w->refs);
  if (r == 0) { w->real->lpVtbl->Release(w->real); free(w->shadow); free(w); } return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Lock(IDirect3DVertexBuffer8 *This, UINT off, UINT size, BYTE **ppb, DWORD flags)
{ WrapVB *w = (WrapVB*)This;
  HRESULT hr = w->real->lpVtbl->Lock(w->real, off, size, ppb, flags);
  w->lock_ptr = (SUCCEEDED(hr) && ppb) ? *ppb : NULL;
  w->lock_off = off; w->lock_size = size ? size : (w->size > off ? w->size - off : 0); w->lock_flags = flags;
  return hr; }
static HRESULT STDMETHODCALLTYPE my_IDirect3DVertexBuffer8_Unlock(IDirect3DVertexBuffer8 *This)
{ WrapVB *w = (WrapVB*)This;
  if (w->lock_ptr && !(w->lock_flags & D3DLOCK_READONLY) && w->shadow) {
      uint32_t end = w->lock_off + w->lock_size; if (end > w->size) end = w->size;
      if (end > w->lock_off) memcpy(w->shadow + w->lock_off, w->lock_ptr, end - w->lock_off);
      w->shadow_valid = 1; w->gen++; }
  w->lock_ptr = NULL;
  return w->real->lpVtbl->Unlock(w->real); }
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_QueryInterface(IDirect3DIndexBuffer8 *This, REFIID riid, void **ppv)
{ WrapIB *w = (WrapIB*)This;
  if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DIndexBuffer8)) { *ppv = This; InterlockedIncrement(&w->refs); return S_OK; }
  return w->real->lpVtbl->QueryInterface(w->real, riid, ppv); }
static ULONG STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_AddRef(IDirect3DIndexBuffer8 *This)
{ return (ULONG)InterlockedIncrement(&((WrapIB*)This)->refs); }
static ULONG STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Release(IDirect3DIndexBuffer8 *This)
{ WrapIB *w = (WrapIB*)This; LONG r = InterlockedDecrement(&w->refs);
  if (r == 0) { w->real->lpVtbl->Release(w->real); free(w->shadow); free(w); } return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Lock(IDirect3DIndexBuffer8 *This, UINT off, UINT size, BYTE **ppb, DWORD flags)
{ WrapIB *w = (WrapIB*)This;
  HRESULT hr = w->real->lpVtbl->Lock(w->real, off, size, ppb, flags);
  w->lock_ptr = (SUCCEEDED(hr) && ppb) ? *ppb : NULL;
  w->lock_off = off; w->lock_size = size ? size : (w->size > off ? w->size - off : 0); w->lock_flags = flags;
  return hr; }
static HRESULT STDMETHODCALLTYPE my_IDirect3DIndexBuffer8_Unlock(IDirect3DIndexBuffer8 *This)
{ WrapIB *w = (WrapIB*)This;
  if (w->lock_ptr && !(w->lock_flags & D3DLOCK_READONLY) && w->shadow) {
      uint32_t end = w->lock_off + w->lock_size; if (end > w->size) end = w->size;
      if (end > w->lock_off) memcpy(w->shadow + w->lock_off, w->lock_ptr, end - w->lock_off);
      w->shadow_valid = 1; w->gen++; }
  w->lock_ptr = NULL;
  return w->real->lpVtbl->Unlock(w->real); }

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
  /* MULTI-FRAME trigger (the port's path): the app reads back the backbuffer for
   * its OWN screenshot once per harness caprange frame (capture_backbuffer()),
   * and the only other GetBackBuffer is at device-init (pre-Present, gated out by
   * present_count>=1). So every readback here = one window frame to KEEP: write
   * it and re-arm (the readback is the frame's last GPU op before Present, so the
   * shadow + calls are complete). When a capframe is set (the retail/cfg path) it
   * is authoritative — don't let retail's own internal GetBackBuffer finalize.
   * armwait (anchor-relative retail capture): SUPPRESS this trigger entirely so a
   * stray readback during the long pre-arm load can't keep a bogus load frame —
   * the proxy idles until OrV3ArmWindowAt sets the present-window, then the WINDOW
   * branch in Present is the sole keep authority. */
  if (CAP && g_capframe == 0xFFFFFFFFu && g_present_count >= 1 && !g_cfg_armwait) {
      write_frame(w->real);
      cb_reset();            /* start the next frame's call buffer fresh */
      g_frame_kept = 1;      /* tell Present this frame is already written */
  }
  return hr; }

/* ── render-target capture (v3) ── CreateTexture (record RT-usage textures so a
 * bound/targeted RT resolves to a stable id), SetRenderTarget + CopyRects (the
 * off-screen composite the pause backdrop is built with). All forward verbatim;
 * recording is gated on CAP. The RT REGISTRY is populated unconditionally (the RTs
 * are created at init via FUN_0047ae65, before the capture window). */
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CreateTexture(
    IDirect3DDevice8 *This, UINT w, UINT h, UINT levels, DWORD usage,
    D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **ppTex)
{
    WrapDev *wd = (WrapDev*)This;
    HRESULT hr = wd->real->lpVtbl->CreateTexture(wd->real, w, h, levels, usage, fmt, pool, ppTex);
    if (SUCCEEDED(hr) && ppTex && *ppTex && (usage & D3DUSAGE_RENDERTARGET)) {
        rt_register((void*)*ppTex, w, h, (uint32_t)fmt, levels, (uint32_t)usage);
        proxy_log("RT texture %p created %ux%u fmt=%u levels=%u usage=0x%x\n",
                  (void*)*ppTex, w, h, (unsigned)fmt, levels, (unsigned)usage);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_SetRenderTarget(
    IDirect3DDevice8 *This, IDirect3DSurface8 *color, IDirect3DSurface8 *depth)
{
    WrapDev *wd = (WrapDev*)This;
    if (CAP) { cb_u(ORV3_SetRenderTarget); cb_surfref(wd->real, color, 0); cb_surfref(wd->real, depth, 1); }
    return wd->real->lpVtbl->SetRenderTarget(wd->real, color, depth);
}
static HRESULT STDMETHODCALLTYPE my_IDirect3DDevice8_CopyRects(
    IDirect3DDevice8 *This, IDirect3DSurface8 *src, const RECT *rects, UINT count,
    IDirect3DSurface8 *dst, const POINT *points)
{
    WrapDev *wd = (WrapDev*)This;
    if (CAP) {
        cb_u(ORV3_CopyRects); cb_surfref(wd->real, src, 0); cb_surfref(wd->real, dst, 0);
        cb_u(count);
        /* count>0 ⇒ rects+points are non-NULL (D3D8: NULL arrays ⇒ count 0 = whole
         * surface). Defensive zero-fill keeps the record well-sized regardless. */
        for (UINT i = 0; i < count; i++) { RECT z = {0};  cb_data(rects  ? &rects[i]  : &z, sizeof(RECT));  }
        for (UINT i = 0; i < count; i++) { POINT z = {0}; cb_data(points ? &points[i] : &z, sizeof(POINT)); }
    }
    return wd->real->lpVtbl->CopyRects(wd->real, src, rects, count, dst, points);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{ (void)reserved; if (reason == DLL_PROCESS_ATTACH) { g_self = h; proxy_log("DllMain attach\n"); } return TRUE; }
