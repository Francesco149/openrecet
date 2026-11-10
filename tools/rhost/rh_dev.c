/* rh_dev.c — --headless: hermetic devices for retail.
 *
 * Retail gets fake Direct3D 8, DirectMusic 8, DirectShow and DirectInput 8 objects instead of
 * the system's. Nothing is drawn or played; every object and every byte a device would
 * hold (textures, vertex buffers, render states, transforms) lives in the DEV region
 * 0x40000000.., laid out deterministically with its allocator metadata in-band. So
 *   - a run needs no GPU, no desktop session, no audio device: it starts straight from
 *     sshd on Windows (session 0) and under wine without an X server;
 *   - a tick costs only retail's own CPU work (its render code still runs: it writes
 *     state the sim reads — FUN_0046602e's hint slot, DAT_0438b760, the model anchors);
 *   - ALL state a tick can change is inside this process's deterministic regions, which
 *     is what makes in-process savestates possible (rh_snap.c).
 * The adapter answers (caps, format support, display mode) are the reference machine's
 * real ones, recorded by --d3d-probe (rh_dev_ref.h), so D3DX and the game decide as they
 * do there. The gate: a headless replay must give the per-tick RNG stream and the save.dat
 * bytes of the real-device golden (`rhost.py headless-check`).
 *
 * Method tables come from the mingw headers: tools/rhost/gen_dev.py emits a counting default
 * stub (S_OK) for every method not written here (`<Iface>_x_<Method>`), the vtables, and a
 * signature assert per hand-written method. Default stubs that retail actually calls are
 * listed at exit (`dev: defaulted ...`) — review each: a stub that should have written
 * an out parameter is a bug here. */
#define CINTERFACE
#define COBJMACROS
#include "rhost.h"
#include <d3d8.h>
#include <dmusici.h>
#include <strmif.h>
#include <control.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "rh_dev_ref.h"

extern void *const vt_IDirect3D8[], *const vt_IDirect3DDevice8[], *const vt_IDirect3DTexture8[], *const vt_IDirect3DSurface8[],
            *const vt_IDirect3DVertexBuffer8[], *const vt_IDirect3DIndexBuffer8[], *const vt_IDirectMusicPerformance8[],
            *const vt_IDirectMusicLoader8[], *const vt_IDirectMusicSegment8[], *const vt_IDirectMusicSegmentState8[],
            *const vt_IDirectMusicAudioPath[], *const vt_IGraphBuilder[], *const vt_IMediaControl[], *const vt_IVideoWindow[],
            *const vt_IMediaEventEx[], *const vt_IBasicAudio[], *const vt_IDirectInput8A[], *const vt_IDirectInputDevice8A[];

/* ================= DEV region: deterministic allocator, metadata in-band =================
 * Same shape as rh_heap.c's arena (size classes <= 64 KB with LIFO free lists, 4 KB-granular
 * first-fit spans above), but every word of allocator state is inside the region, so a
 * snapshot of [DEV_LO, top) is a snapshot of the allocator too. Blocks are zeroed on
 * allocation: nothing a device hands out depends on what was freed before. */
#define DEV_MAGIC 0x56454452u   /* "RDEV" */
#define DEV_NCLS 48
typedef struct { uint32_t magic, top, live, bytes, nalloc, nfree, bigfree, pad; uint32_t fl[DEV_NCLS]; } devhdr_t;
typedef struct { uint32_t cap, cls, magic, req; } dblk_t;
typedef struct { uint32_t next, size; } dspan_t;
#define DH ((devhdr_t *)(uintptr_t)RH_DEV_LO)
#define DBLK_LIVE 0xD0B10C4Bu
#define DCLS_BIG 0xFFFFu
static uint32_t g_dev_committed;

static void dev_commit(uint32_t top)
{
    if (top <= g_dev_committed) return;
    uint32_t want = (top + 0xfffffu) & ~0xfffffu;
    if (want > RH_DEV_HI) rh_fatal("rh_dev: DEV region exhausted (top %08x)", top);
    if (!VirtualAlloc((void *)(uintptr_t)g_dev_committed, want - g_dev_committed, MEM_COMMIT, PAGE_READWRITE))
        rh_fatal("rh_dev: commit %08x..%08x failed (err %lu)", g_dev_committed, want, GetLastError());
    rh_snap_committed(g_dev_committed, want);
    g_dev_committed = want;
}
static uint32_t dev_bump(uint32_t sz)
{
    uint32_t p = DH->top;
    dev_commit(p + sz);
    DH->top = p + sz;
    return p;
}
static uint32_t dev_class(uint32_t n, uint32_t *cap)
{
    if (n <= 256) { uint32_t c = n ? (n + 15u) & ~15u : 16u; *cap = c; return c / 16u - 1u; }
    int k = 31 - __builtin_clz(n - 1);
    uint32_t step = 1u << (k - 2), c = (n + step - 1) & ~(step - 1);
    *cap = c;
    return 16u + (uint32_t)(k - 8) * 4u + (c / step - 5u);
}
static void *dev_alloc(uint32_t n)
{
    dblk_t *b;
    if (n <= 0x10000u) {
        uint32_t cap, cls = dev_class(n, &cap), p = DH->fl[cls];
        if (p) { b = (dblk_t *)(uintptr_t)p; DH->fl[cls] = *(uint32_t *)(b + 1); }
        else { b = (dblk_t *)(uintptr_t)dev_bump(cap + sizeof *b); b->cap = cap; b->cls = cls; }
    } else {
        uint32_t chunk = (n + sizeof *b + 0xfffu) & ~0xfffu, *link = &DH->bigfree, p = 0;
        for (uint32_t s = *link; s; link = &((dspan_t *)(uintptr_t)s)->next, s = *link) {
            dspan_t *sp = (dspan_t *)(uintptr_t)s;
            if (sp->size < chunk) continue;
            if (sp->size == chunk) *link = sp->next;
            else { dspan_t *rest = (dspan_t *)(uintptr_t)(s + chunk); rest->next = sp->next; rest->size = sp->size - chunk; *link = s + chunk; }
            p = s; break;
        }
        if (!p) p = dev_bump(chunk);
        b = (dblk_t *)(uintptr_t)p; b->cap = chunk - sizeof *b; b->cls = DCLS_BIG;
    }
    b->magic = DBLK_LIVE; b->req = n;
    memset(b + 1, 0, b->cap);
    DH->live++; DH->bytes += b->cap; DH->nalloc++;
    return b + 1;
}
static void dev_free(void *ptr)
{
    if (!ptr) return;
    dblk_t *b = (dblk_t *)ptr - 1;
    if (b->magic != DBLK_LIVE) { rh_log("rh_dev: free of a dead block %p (f%u)\n", ptr, g_frame); return; }
    b->magic = 0; DH->live--; DH->bytes -= b->cap; DH->nfree++;
    if (b->cls != DCLS_BIG) { *(uint32_t *)(b + 1) = DH->fl[b->cls]; DH->fl[b->cls] = (uint32_t)(uintptr_t)b; return; }
    uint32_t lo = (uint32_t)(uintptr_t)b, sz = b->cap + sizeof *b, *link = &DH->bigfree, prev = 0;
    while (*link && *link < lo) { prev = *link; link = &((dspan_t *)(uintptr_t)*link)->next; }
    dspan_t *n = (dspan_t *)(uintptr_t)lo; n->next = *link; n->size = sz; *link = lo;
    if (n->next && lo + n->size == n->next) { dspan_t *r = (dspan_t *)(uintptr_t)n->next; n->size += r->size; n->next = r->next; }
    if (prev) { dspan_t *l = (dspan_t *)(uintptr_t)prev; if (prev + l->size == lo) { l->size += n->size; l->next = n->next; } }
}
int rh_dev_range(uint32_t *lo, uint32_t *hi)
{
    if (!O.headless || !g_dev_committed) return 0;
    *lo = RH_DEV_LO; *hi = (DH->top + 0xfffu) & ~0xfffu;
    return 1;
}
uint32_t rh_dev_committed(void) { return g_dev_committed; }

/* ================= objects ================= */
typedef struct { void *const *vt; uint32_t ref; } obj_t;
typedef struct tex tex_t;
typedef struct { obj_t o; tex_t *owner; uint32_t w, h, fmt, usage, pool, size; int pitch; uint8_t *bits; } surf_t;
struct tex { obj_t o; uint32_t w, h, levels, fmt, usage, pool; surf_t *lv[16]; };
typedef struct { obj_t o; uint32_t rtype, size, usage, fmt, fvf, pool; uint8_t *bits; } buf_t;
#define N_XF 512
#define N_LIGHT 32
typedef struct {
    obj_t o;
    D3DPRESENT_PARAMETERS pp; D3DDEVICE_CREATION_PARAMETERS cp;
    surf_t *bb, *ds, *rt, *zs;
    D3DVIEWPORT8 vp; D3DMATERIAL8 mat; D3DCLIPSTATUS8 clip;
    D3DLIGHT8 light[N_LIGHT]; uint8_t light_set[N_LIGHT], light_on[N_LIGHT];
    float plane[8][4];
    DWORD rs[256], tss[8][33];
    void *texb[8]; buf_t *stream[16]; uint32_t stride[16]; buf_t *indices; uint32_t base_vertex;
    DWORD vs, ps, next_shader, next_block, palette;
    D3DMATRIX xf[N_XF];
} dev_t;
static obj_t *g_d3d; static dev_t *g_dev;
static HWND g_focus, g_devwnd;   /* window handles differ per process: kept out of the DEV region (its hash is run-independent) */

static void *obj_new(void *const *vt, uint32_t size) { obj_t *o = dev_alloc(size); o->vt = vt; o->ref = 1; return o; }
#define SIMPLE_UNK(I) \
    static HRESULT WINAPI I##_x_QueryInterface(I *This, REFIID riid, void **ppv) { (void)riid; if (!ppv) return E_POINTER; ((obj_t *)This)->ref++; *ppv = This; return S_OK; } \
    static ULONG WINAPI I##_x_AddRef(I *This) { return ++((obj_t *)This)->ref; } \
    static ULONG WINAPI I##_x_Release(I *This) { obj_t *o = (obj_t *)This; uint32_t r = --o->ref; if (!r) dev_free(o); return r; }

/* ---- formats ---- */
#define FCC(a, b, c, d) ((uint32_t)MAKEFOURCC(a, b, c, d))
static int fmt_bytes(uint32_t f)
{
    switch (f) {
    case 21: case 22: case 31: case 34: case 62: case 63: case 64: case 65: case 67: case 71: case 75: case 77: case 79: case 102: return 4;
    case 20: return 3;
    case 23: case 24: case 25: case 26: case 29: case 30: case 40: case 51: case 60: case 61: case 70: case 73: case 80: case 101: return 2;
    case 27: case 28: case 41: case 50: case 52: return 1;
    default: return 0;
    }
}
static int fmt_block(uint32_t f) { return f == FCC('D', 'X', 'T', '1') ? 8 : (f == FCC('D', 'X', 'T', '2') || f == FCC('D', 'X', 'T', '3') || f == FCC('D', 'X', 'T', '4') || f == FCC('D', 'X', 'T', '5')) ? 16 : 0; }
static uint32_t surf_layout(uint32_t fmt, uint32_t w, uint32_t h, int *pitch)
{
    int blk = fmt_block(fmt);
    if (blk) { uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4; if (!bw) bw = 1; if (!bh) bh = 1; *pitch = (int)(bw * blk); return bw * blk * bh; }
    int bpp = fmt_bytes(fmt); if (!bpp) bpp = 4;
    *pitch = (int)((w * bpp + 3) & ~3u);
    return (uint32_t)*pitch * h;
}
static surf_t *surf_new(tex_t *owner, uint32_t w, uint32_t h, uint32_t fmt, uint32_t usage, uint32_t pool)
{
    surf_t *s = obj_new(vt_IDirect3DSurface8, sizeof *s);
    s->owner = owner; s->w = w; s->h = h; s->fmt = fmt; s->usage = usage; s->pool = pool;
    s->size = surf_layout(fmt, w, h, &s->pitch);
    s->bits = dev_alloc(s->size ? s->size : 16);
    return s;
}
static void surf_free(surf_t *s) { dev_free(s->bits); dev_free(s); }
static HRESULT lock_rect(surf_t *s, D3DLOCKED_RECT *lr, const RECT *rc)
{
    if (!lr) return D3DERR_INVALIDCALL;
    lr->Pitch = s->pitch; lr->pBits = s->bits;
    if (rc) {
        int blk = fmt_block(s->fmt);
        if (blk) lr->pBits = s->bits + (rc->top / 4) * s->pitch + (rc->left / 4) * blk;
        else { int bpp = fmt_bytes(s->fmt); lr->pBits = s->bits + rc->top * s->pitch + rc->left * (bpp ? bpp : 4); }
    }
    return D3D_OK;
}
static void surf_desc(const surf_t *s, D3DSURFACE_DESC *d)
{
    memset(d, 0, sizeof *d);
    d->Format = (D3DFORMAT)s->fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = s->usage; d->Pool = (D3DPOOL)s->pool;
    d->Size = s->size; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->Width = s->w; d->Height = s->h;
}

/* ---- IDirect3DSurface8 ---- */
static HRESULT WINAPI IDirect3DSurface8_x_QueryInterface(IDirect3DSurface8 *This, REFIID riid, void **ppv)
{ (void)riid; if (!ppv) return E_POINTER; IDirect3DSurface8_AddRef(This); *ppv = This; return S_OK; }
static ULONG WINAPI IDirect3DSurface8_x_AddRef(IDirect3DSurface8 *This)
{ surf_t *s = (surf_t *)This; return s->owner ? ++s->owner->o.ref : ++s->o.ref; }
static void tex_destroy(tex_t *t);
static ULONG WINAPI IDirect3DSurface8_x_Release(IDirect3DSurface8 *This)
{
    surf_t *s = (surf_t *)This;
    if (s->owner) { uint32_t r = --s->owner->o.ref; if (!r) tex_destroy(s->owner); return r; }
    uint32_t r = --s->o.ref;
    if (!r && s != g_dev->bb && s != g_dev->ds) {
        if (g_dev->rt == s) g_dev->rt = g_dev->bb;
        if (g_dev->zs == s) g_dev->zs = g_dev->ds;
        surf_free(s);
    }
    return r;
}
static HRESULT WINAPI IDirect3DSurface8_x_GetDevice(IDirect3DSurface8 *This, IDirect3DDevice8 **dev)
{ (void)This; g_dev->o.ref++; *dev = (IDirect3DDevice8 *)g_dev; return D3D_OK; }
static HRESULT WINAPI IDirect3DSurface8_x_GetContainer(IDirect3DSurface8 *This, REFIID riid, void **ppv)
{
    surf_t *s = (surf_t *)This; (void)riid;
    if (s->owner) { s->owner->o.ref++; *ppv = s->owner; } else { g_dev->o.ref++; *ppv = g_dev; }
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DSurface8_x_GetDesc(IDirect3DSurface8 *This, D3DSURFACE_DESC *d) { surf_desc((surf_t *)This, d); return D3D_OK; }
static HRESULT WINAPI IDirect3DSurface8_x_LockRect(IDirect3DSurface8 *This, D3DLOCKED_RECT *lr, const RECT *rc, DWORD fl)
{ (void)fl; return lock_rect((surf_t *)This, lr, rc); }

/* ---- IDirect3DTexture8 ---- */
static void tex_destroy(tex_t *t)
{
    for (int i = 0; i < 8; i++) if (g_dev->texb[i] == t) g_dev->texb[i] = NULL;
    for (uint32_t i = 0; i < t->levels; i++) {
        if (g_dev->rt == t->lv[i]) g_dev->rt = g_dev->bb;
        surf_free(t->lv[i]);
    }
    dev_free(t);
}
static HRESULT WINAPI IDirect3DTexture8_x_QueryInterface(IDirect3DTexture8 *This, REFIID riid, void **ppv)
{ (void)riid; if (!ppv) return E_POINTER; ((obj_t *)This)->ref++; *ppv = This; return S_OK; }
static ULONG WINAPI IDirect3DTexture8_x_AddRef(IDirect3DTexture8 *This) { return ++((obj_t *)This)->ref; }
static ULONG WINAPI IDirect3DTexture8_x_Release(IDirect3DTexture8 *This)
{ tex_t *t = (tex_t *)This; uint32_t r = --t->o.ref; if (!r) tex_destroy(t); return r; }
static HRESULT WINAPI IDirect3DTexture8_x_GetDevice(IDirect3DTexture8 *This, IDirect3DDevice8 **dev)
{ (void)This; g_dev->o.ref++; *dev = (IDirect3DDevice8 *)g_dev; return D3D_OK; }
static D3DRESOURCETYPE WINAPI IDirect3DTexture8_x_GetType(IDirect3DTexture8 *This) { (void)This; return D3DRTYPE_TEXTURE; }
static DWORD WINAPI IDirect3DTexture8_x_GetLevelCount(IDirect3DTexture8 *This) { return ((tex_t *)This)->levels; }
static HRESULT WINAPI IDirect3DTexture8_x_GetLevelDesc(IDirect3DTexture8 *This, UINT lv, D3DSURFACE_DESC *d)
{ tex_t *t = (tex_t *)This; if (lv >= t->levels) return D3DERR_INVALIDCALL; surf_desc(t->lv[lv], d); return D3D_OK; }
static HRESULT WINAPI IDirect3DTexture8_x_GetSurfaceLevel(IDirect3DTexture8 *This, UINT lv, IDirect3DSurface8 **out)
{ tex_t *t = (tex_t *)This; if (lv >= t->levels) return D3DERR_INVALIDCALL; t->o.ref++; *out = (IDirect3DSurface8 *)t->lv[lv]; return D3D_OK; }
static HRESULT WINAPI IDirect3DTexture8_x_LockRect(IDirect3DTexture8 *This, UINT lv, D3DLOCKED_RECT *lr, const RECT *rc, DWORD fl)
{ tex_t *t = (tex_t *)This; (void)fl; if (lv >= t->levels) return D3DERR_INVALIDCALL; return lock_rect(t->lv[lv], lr, rc); }

/* ---- vertex / index buffers ---- */
static void buf_destroy(buf_t *b)
{
    for (int i = 0; i < 16; i++) if (g_dev->stream[i] == b) g_dev->stream[i] = NULL;
    if (g_dev->indices == b) g_dev->indices = NULL;
    dev_free(b->bits); dev_free(b);
}
#define BUF_METHODS(I, RT) \
    static HRESULT WINAPI I##_x_QueryInterface(I *This, REFIID riid, void **ppv) { (void)riid; if (!ppv) return E_POINTER; ((obj_t *)This)->ref++; *ppv = This; return S_OK; } \
    static ULONG WINAPI I##_x_AddRef(I *This) { return ++((obj_t *)This)->ref; } \
    static ULONG WINAPI I##_x_Release(I *This) { buf_t *b = (buf_t *)This; uint32_t r = --b->o.ref; if (!r) buf_destroy(b); return r; } \
    static HRESULT WINAPI I##_x_GetDevice(I *This, IDirect3DDevice8 **dev) { (void)This; g_dev->o.ref++; *dev = (IDirect3DDevice8 *)g_dev; return D3D_OK; } \
    static D3DRESOURCETYPE WINAPI I##_x_GetType(I *This) { (void)This; return RT; } \
    static HRESULT WINAPI I##_x_Lock(I *This, UINT off, UINT size, BYTE **data, DWORD fl) \
    { buf_t *b = (buf_t *)This; (void)size; (void)fl; if (off > b->size) return D3DERR_INVALIDCALL; *data = b->bits + off; return D3D_OK; }
BUF_METHODS(IDirect3DVertexBuffer8, D3DRTYPE_VERTEXBUFFER)
BUF_METHODS(IDirect3DIndexBuffer8, D3DRTYPE_INDEXBUFFER)
static HRESULT WINAPI IDirect3DVertexBuffer8_x_GetDesc(IDirect3DVertexBuffer8 *This, D3DVERTEXBUFFER_DESC *d)
{
    buf_t *b = (buf_t *)This;
    d->Format = D3DFMT_VERTEXDATA; d->Type = D3DRTYPE_VERTEXBUFFER; d->Usage = b->usage; d->Pool = (D3DPOOL)b->pool; d->Size = b->size; d->FVF = b->fvf;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DIndexBuffer8_x_GetDesc(IDirect3DIndexBuffer8 *This, D3DINDEXBUFFER_DESC *d)
{
    buf_t *b = (buf_t *)This;
    d->Format = (D3DFORMAT)b->fmt; d->Type = D3DRTYPE_INDEXBUFFER; d->Usage = b->usage; d->Pool = (D3DPOOL)b->pool; d->Size = b->size;
    return D3D_OK;
}

/* ---- IDirect3DDevice8 ---- */
static HRESULT WINAPI IDirect3DDevice8_x_QueryInterface(IDirect3DDevice8 *This, REFIID riid, void **ppv)
{ (void)riid; if (!ppv) return E_POINTER; ((obj_t *)This)->ref++; *ppv = This; return S_OK; }
static ULONG WINAPI IDirect3DDevice8_x_AddRef(IDirect3DDevice8 *This) { return ++((obj_t *)This)->ref; }
static ULONG WINAPI IDirect3DDevice8_x_Release(IDirect3DDevice8 *This) { obj_t *o = (obj_t *)This; return o->ref ? --o->ref : 0; }
static UINT WINAPI IDirect3DDevice8_x_GetAvailableTextureMem(IDirect3DDevice8 *This) { (void)This; return REF_TEXMEM; }
static HRESULT WINAPI IDirect3DDevice8_x_GetDirect3D(IDirect3DDevice8 *This, IDirect3D8 **out)
{ (void)This; g_d3d->ref++; *out = (IDirect3D8 *)g_d3d; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetDeviceCaps(IDirect3DDevice8 *This, D3DCAPS8 *caps)
{ (void)This; memcpy(caps, REF_DEVCAPS, sizeof *caps); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetDisplayMode(IDirect3DDevice8 *This, D3DDISPLAYMODE *m) { (void)This; *m = REF_MODE; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetCreationParameters(IDirect3DDevice8 *This, D3DDEVICE_CREATION_PARAMETERS *p)
{ *p = ((dev_t *)This)->cp; p->hFocusWindow = g_focus; return D3D_OK; }
static void dev_buffers(dev_t *d)
{
    if (d->bb) { surf_free(d->bb); d->bb = NULL; }
    if (d->ds) { surf_free(d->ds); d->ds = NULL; }
    d->bb = surf_new(NULL, d->pp.BackBufferWidth, d->pp.BackBufferHeight, d->pp.BackBufferFormat, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT);
    if (d->pp.EnableAutoDepthStencil)
        d->ds = surf_new(NULL, d->pp.BackBufferWidth, d->pp.BackBufferHeight, d->pp.AutoDepthStencilFormat, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT);
    d->rt = d->bb; d->zs = d->ds;
    d->vp = (D3DVIEWPORT8){ 0, 0, d->pp.BackBufferWidth, d->pp.BackBufferHeight, 0.0f, 1.0f };
}
static void pp_fixup(D3DPRESENT_PARAMETERS *pp, HWND focus)
{
    HWND w = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
    if (pp->Windowed && (!pp->BackBufferWidth || !pp->BackBufferHeight)) {
        RECT rc = { 0, 0, 640, 480 }; if (w) GetClientRect(w, &rc);
        if (!pp->BackBufferWidth) pp->BackBufferWidth = (UINT)(rc.right ? rc.right : 1);
        if (!pp->BackBufferHeight) pp->BackBufferHeight = (UINT)(rc.bottom ? rc.bottom : 1);
    }
    if (pp->BackBufferFormat == D3DFMT_UNKNOWN) pp->BackBufferFormat = REF_MODE.Format;
    if (!pp->BackBufferCount) pp->BackBufferCount = 1;
}
static HRESULT WINAPI IDirect3DDevice8_x_Reset(IDirect3DDevice8 *This, D3DPRESENT_PARAMETERS *pp)
{
    dev_t *d = (dev_t *)This;
    pp_fixup(pp, g_focus);
    d->pp = *pp; g_devwnd = pp->hDeviceWindow; d->pp.hDeviceWindow = NULL;
    dev_buffers(d);
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_GetBackBuffer(IDirect3DDevice8 *This, UINT i, D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **out)
{ dev_t *d = (dev_t *)This; (void)type; if (i) { *out = NULL; return D3DERR_INVALIDCALL; } d->bb->o.ref++; *out = (IDirect3DSurface8 *)d->bb; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetRasterStatus(IDirect3DDevice8 *This, D3DRASTER_STATUS *rs)
{ (void)This; rs->InVBlank = FALSE; rs->ScanLine = 0; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateTexture(IDirect3DDevice8 *This, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **out)
{
    (void)This;
    if (!w || !h || !out) return D3DERR_INVALIDCALL;
    uint32_t full = 1; for (uint32_t m = w > h ? w : h; m > 1; m >>= 1) full++;
    if (!levels || levels > full) levels = full;
    if (levels > 16) levels = 16;
    tex_t *t = obj_new(vt_IDirect3DTexture8, sizeof *t);
    t->w = w; t->h = h; t->levels = levels; t->fmt = fmt; t->usage = usage; t->pool = pool;
    for (uint32_t i = 0; i < levels; i++) {
        uint32_t lw = w >> i, lh = h >> i;
        t->lv[i] = surf_new(t, lw ? lw : 1, lh ? lh : 1, fmt, usage, pool);
    }
    *out = (IDirect3DTexture8 *)t;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_CreateVolumeTexture(IDirect3DDevice8 *This, UINT w, UINT h, UINT dp, UINT lv, DWORD us, D3DFORMAT f, D3DPOOL p, IDirect3DVolumeTexture8 **out)
{ (void)This; (void)w; (void)h; (void)dp; (void)lv; (void)us; (void)f; (void)p; rh_log("dev: CreateVolumeTexture refused (f%u)\n", g_frame); *out = NULL; return D3DERR_NOTAVAILABLE; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateCubeTexture(IDirect3DDevice8 *This, UINT e, UINT lv, DWORD us, D3DFORMAT f, D3DPOOL p, IDirect3DCubeTexture8 **out)
{ (void)This; (void)e; (void)lv; (void)us; (void)f; (void)p; rh_log("dev: CreateCubeTexture refused (f%u)\n", g_frame); *out = NULL; return D3DERR_NOTAVAILABLE; }
static buf_t *buf_new(void *const *vt, uint32_t rtype, uint32_t len, uint32_t usage, uint32_t fmt, uint32_t fvf, uint32_t pool)
{
    buf_t *b = obj_new(vt, sizeof *b);
    b->rtype = rtype; b->size = len; b->usage = usage; b->fmt = fmt; b->fvf = fvf; b->pool = pool;
    b->bits = dev_alloc(len ? len : 16);
    return b;
}
static HRESULT WINAPI IDirect3DDevice8_x_CreateVertexBuffer(IDirect3DDevice8 *This, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **out)
{ (void)This; *out = (IDirect3DVertexBuffer8 *)buf_new(vt_IDirect3DVertexBuffer8, D3DRTYPE_VERTEXBUFFER, len, usage, D3DFMT_VERTEXDATA, fvf, pool); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateIndexBuffer(IDirect3DDevice8 *This, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **out)
{ (void)This; *out = (IDirect3DIndexBuffer8 *)buf_new(vt_IDirect3DIndexBuffer8, D3DRTYPE_INDEXBUFFER, len, usage, fmt, 0, pool); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateRenderTarget(IDirect3DDevice8 *This, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, WINBOOL lockable, IDirect3DSurface8 **out)
{ (void)This; (void)ms; (void)lockable; *out = (IDirect3DSurface8 *)surf_new(NULL, w, h, fmt, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateDepthStencilSurface(IDirect3DDevice8 *This, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8 **out)
{ (void)This; (void)ms; *out = (IDirect3DSurface8 *)surf_new(NULL, w, h, fmt, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateImageSurface(IDirect3DDevice8 *This, UINT w, UINT h, D3DFORMAT fmt, IDirect3DSurface8 **out)
{ (void)This; *out = (IDirect3DSurface8 *)surf_new(NULL, w, h, fmt, 0, D3DPOOL_SYSTEMMEM); return D3D_OK; }
/* pixels are never produced by drawing, but copies between surfaces are done faithfully
 * (D3DX loads through scratch surfaces): same format only, clipped to both surfaces */
static HRESULT WINAPI IDirect3DDevice8_x_CopyRects(IDirect3DDevice8 *This, IDirect3DSurface8 *src_, const RECT *rects, UINT n, IDirect3DSurface8 *dst_, const POINT *pts)
{
    (void)This;
    surf_t *s = (surf_t *)src_, *d = (surf_t *)dst_;
    if (!s || !d) return D3DERR_INVALIDCALL;
    if (s->fmt != d->fmt || fmt_block(s->fmt)) return D3D_OK;
    int bpp = fmt_bytes(s->fmt); if (!bpp) return D3D_OK;
    RECT all = { 0, 0, (LONG)s->w, (LONG)s->h };
    for (UINT i = 0; i < (rects ? n : 1); i++) {
        RECT r = rects ? rects[i] : all;
        LONG dx = pts ? pts[i].x : r.left, dy = pts ? pts[i].y : r.top;
        for (LONG y = r.top; y < r.bottom; y++) {
            LONG ty = dy + (y - r.top), w = r.right - r.left;
            if (y < 0 || y >= (LONG)s->h || ty < 0 || ty >= (LONG)d->h || r.left < 0 || dx < 0) continue;
            if (r.left + w > (LONG)s->w) w = (LONG)s->w - r.left;
            if (dx + w > (LONG)d->w) w = (LONG)d->w - dx;
            if (w > 0) memcpy(d->bits + ty * d->pitch + dx * bpp, s->bits + y * s->pitch + r.left * bpp, (size_t)w * bpp);
        }
    }
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_UpdateTexture(IDirect3DDevice8 *This, IDirect3DBaseTexture8 *src_, IDirect3DBaseTexture8 *dst_)
{
    (void)This;
    tex_t *s = (tex_t *)src_, *d = (tex_t *)dst_;
    if (!s || !d || s->o.vt != vt_IDirect3DTexture8 || d->o.vt != vt_IDirect3DTexture8) return D3DERR_INVALIDCALL;
    for (uint32_t i = 0; i < s->levels && i < d->levels; i++)
        if (s->lv[i]->size == d->lv[i]->size && s->lv[i]->fmt == d->lv[i]->fmt) memcpy(d->lv[i]->bits, s->lv[i]->bits, s->lv[i]->size);
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_SetRenderTarget(IDirect3DDevice8 *This, IDirect3DSurface8 *rt, IDirect3DSurface8 *zs)
{
    dev_t *d = (dev_t *)This;
    if (rt) { d->rt = (surf_t *)rt; d->vp = (D3DVIEWPORT8){ 0, 0, d->rt->w, d->rt->h, 0.0f, 1.0f }; }
    d->zs = (surf_t *)zs;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_GetRenderTarget(IDirect3DDevice8 *This, IDirect3DSurface8 **out)
{ dev_t *d = (dev_t *)This; IDirect3DSurface8_AddRef((IDirect3DSurface8 *)d->rt); *out = (IDirect3DSurface8 *)d->rt; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetDepthStencilSurface(IDirect3DDevice8 *This, IDirect3DSurface8 **out)
{
    dev_t *d = (dev_t *)This;
    if (!d->zs) { *out = NULL; return D3DERR_NOTFOUND; }
    IDirect3DSurface8_AddRef((IDirect3DSurface8 *)d->zs); *out = (IDirect3DSurface8 *)d->zs; return D3D_OK;
}
static const D3DMATRIX IDENT = { { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } } };
static HRESULT WINAPI IDirect3DDevice8_x_SetTransform(IDirect3DDevice8 *This, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{ if ((uint32_t)st < N_XF) ((dev_t *)This)->xf[st] = *m; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetTransform(IDirect3DDevice8 *This, D3DTRANSFORMSTATETYPE st, D3DMATRIX *m)
{ *m = (uint32_t)st < N_XF ? ((dev_t *)This)->xf[st] : IDENT; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_MultiplyTransform(IDirect3DDevice8 *This, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{
    if ((uint32_t)st >= N_XF) return D3D_OK;
    D3DMATRIX *a = &((dev_t *)This)->xf[st], r;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float v = 0; for (int k = 0; k < 4; k++) v += m->m[i][k] * a->m[k][j];
        r.m[i][j] = v;
    }
    *a = r; return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_SetViewport(IDirect3DDevice8 *This, const D3DVIEWPORT8 *vp) { ((dev_t *)This)->vp = *vp; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetViewport(IDirect3DDevice8 *This, D3DVIEWPORT8 *vp) { *vp = ((dev_t *)This)->vp; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetMaterial(IDirect3DDevice8 *This, const D3DMATERIAL8 *m) { ((dev_t *)This)->mat = *m; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetMaterial(IDirect3DDevice8 *This, D3DMATERIAL8 *m) { *m = ((dev_t *)This)->mat; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetLight(IDirect3DDevice8 *This, DWORD i, const D3DLIGHT8 *l)
{ dev_t *d = (dev_t *)This; if (i < N_LIGHT) { d->light[i] = *l; d->light_set[i] = 1; } return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetLight(IDirect3DDevice8 *This, DWORD i, D3DLIGHT8 *l)
{ dev_t *d = (dev_t *)This; if (i >= N_LIGHT || !d->light_set[i]) return D3DERR_INVALIDCALL; *l = d->light[i]; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_LightEnable(IDirect3DDevice8 *This, DWORD i, WINBOOL on)
{ dev_t *d = (dev_t *)This; if (i < N_LIGHT) { d->light_on[i] = on != 0; d->light_set[i] = 1; } return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetLightEnable(IDirect3DDevice8 *This, DWORD i, WINBOOL *on)
{ dev_t *d = (dev_t *)This; if (i >= N_LIGHT || !d->light_set[i]) return D3DERR_INVALIDCALL; *on = d->light_on[i]; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetClipPlane(IDirect3DDevice8 *This, DWORD i, const float *p)
{ if (i < 8) memcpy(((dev_t *)This)->plane[i], p, 16); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetClipPlane(IDirect3DDevice8 *This, DWORD i, float *p)
{ if (i >= 8) return D3DERR_INVALIDCALL; memcpy(p, ((dev_t *)This)->plane[i], 16); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetRenderState(IDirect3DDevice8 *This, D3DRENDERSTATETYPE s, DWORD v)
{ if ((uint32_t)s < 256) ((dev_t *)This)->rs[s] = v; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetRenderState(IDirect3DDevice8 *This, D3DRENDERSTATETYPE s, DWORD *v)
{ *v = (uint32_t)s < 256 ? ((dev_t *)This)->rs[s] : 0; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_EndStateBlock(IDirect3DDevice8 *This, DWORD *tok) { *tok = ++((dev_t *)This)->next_block; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateStateBlock(IDirect3DDevice8 *This, D3DSTATEBLOCKTYPE t, DWORD *tok)
{ (void)t; *tok = ++((dev_t *)This)->next_block; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetClipStatus(IDirect3DDevice8 *This, const D3DCLIPSTATUS8 *c) { ((dev_t *)This)->clip = *c; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetClipStatus(IDirect3DDevice8 *This, D3DCLIPSTATUS8 *c) { *c = ((dev_t *)This)->clip; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetTexture(IDirect3DDevice8 *This, DWORD st, IDirect3DBaseTexture8 **out)
{
    dev_t *d = (dev_t *)This;
    *out = st < 8 ? d->texb[st] : NULL;
    if (*out) ((obj_t *)*out)->ref++;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_SetTexture(IDirect3DDevice8 *This, DWORD st, IDirect3DBaseTexture8 *t)
{ if (st < 8) ((dev_t *)This)->texb[st] = t; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetTextureStageState(IDirect3DDevice8 *This, DWORD st, D3DTEXTURESTAGESTATETYPE ty, DWORD *v)
{ *v = (st < 8 && (uint32_t)ty < 33) ? ((dev_t *)This)->tss[st][ty] : 0; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetTextureStageState(IDirect3DDevice8 *This, DWORD st, D3DTEXTURESTAGESTATETYPE ty, DWORD v)
{ if (st < 8 && (uint32_t)ty < 33) ((dev_t *)This)->tss[st][ty] = v; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_ValidateDevice(IDirect3DDevice8 *This, DWORD *passes) { (void)This; if (passes) *passes = 1; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetInfo(IDirect3DDevice8 *This, DWORD id, void *p, DWORD n) { (void)This; (void)id; if (p) memset(p, 0, n); return S_FALSE; }
static HRESULT WINAPI IDirect3DDevice8_x_GetPaletteEntries(IDirect3DDevice8 *This, UINT n, PALETTEENTRY *e) { (void)This; (void)n; memset(e, 0, 256 * sizeof *e); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetCurrentTexturePalette(IDirect3DDevice8 *This, UINT n) { ((dev_t *)This)->palette = n; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetCurrentTexturePalette(IDirect3DDevice8 *This, UINT *n) { *n = ((dev_t *)This)->palette; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateVertexShader(IDirect3DDevice8 *This, const DWORD *decl, const DWORD *code, DWORD *h, DWORD usage)
{ dev_t *d = (dev_t *)This; (void)decl; (void)code; (void)usage; d->next_shader += 2; *h = 0x10001u + d->next_shader; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetVertexShader(IDirect3DDevice8 *This, DWORD h) { ((dev_t *)This)->vs = h; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetVertexShader(IDirect3DDevice8 *This, DWORD *h) { *h = ((dev_t *)This)->vs; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetVertexShaderConstant(IDirect3DDevice8 *This, DWORD r, void *p, DWORD n) { (void)This; (void)r; memset(p, 0, n * 16); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetVertexShaderDeclaration(IDirect3DDevice8 *This, DWORD h, void *p, DWORD *n) { (void)This; (void)h; (void)p; if (n) *n = 0; return D3DERR_INVALIDCALL; }
static HRESULT WINAPI IDirect3DDevice8_x_GetVertexShaderFunction(IDirect3DDevice8 *This, DWORD h, void *p, DWORD *n) { (void)This; (void)h; (void)p; if (n) *n = 0; return D3DERR_INVALIDCALL; }
static HRESULT WINAPI IDirect3DDevice8_x_SetStreamSource(IDirect3DDevice8 *This, UINT n, IDirect3DVertexBuffer8 *vb, UINT stride)
{ dev_t *d = (dev_t *)This; if (n < 16) { d->stream[n] = (buf_t *)vb; d->stride[n] = stride; } return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetStreamSource(IDirect3DDevice8 *This, UINT n, IDirect3DVertexBuffer8 **vb, UINT *stride)
{
    dev_t *d = (dev_t *)This;
    if (n >= 16) return D3DERR_INVALIDCALL;
    *vb = (IDirect3DVertexBuffer8 *)d->stream[n]; if (*vb) d->stream[n]->o.ref++;
    if (stride) *stride = d->stride[n];
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_SetIndices(IDirect3DDevice8 *This, IDirect3DIndexBuffer8 *ib, UINT base)
{ dev_t *d = (dev_t *)This; d->indices = (buf_t *)ib; d->base_vertex = base; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetIndices(IDirect3DDevice8 *This, IDirect3DIndexBuffer8 **ib, UINT *base)
{
    dev_t *d = (dev_t *)This;
    *ib = (IDirect3DIndexBuffer8 *)d->indices; if (*ib) d->indices->o.ref++;
    if (base) *base = d->base_vertex;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3DDevice8_x_CreatePixelShader(IDirect3DDevice8 *This, const DWORD *code, DWORD *h)
{ dev_t *d = (dev_t *)This; (void)code; d->next_shader += 2; *h = 0x20001u + d->next_shader; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_SetPixelShader(IDirect3DDevice8 *This, DWORD h) { ((dev_t *)This)->ps = h; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetPixelShader(IDirect3DDevice8 *This, DWORD *h) { *h = ((dev_t *)This)->ps; return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetPixelShaderConstant(IDirect3DDevice8 *This, DWORD r, void *p, DWORD n) { (void)This; (void)r; memset(p, 0, n * 16); return D3D_OK; }
static HRESULT WINAPI IDirect3DDevice8_x_GetPixelShaderFunction(IDirect3DDevice8 *This, DWORD h, void *p, DWORD *n) { (void)This; (void)h; (void)p; if (n) *n = 0; return D3DERR_INVALIDCALL; }
static HRESULT WINAPI IDirect3DDevice8_x_CreateAdditionalSwapChain(IDirect3DDevice8 *This, D3DPRESENT_PARAMETERS *pp, IDirect3DSwapChain8 **out)
{ (void)This; (void)pp; rh_log("dev: CreateAdditionalSwapChain refused (f%u)\n", g_frame); *out = NULL; return D3DERR_NOTAVAILABLE; }
static HRESULT WINAPI IDirect3DDevice8_x_GetFrontBuffer(IDirect3DDevice8 *This, IDirect3DSurface8 *dst)
{ (void)This; surf_t *s = (surf_t *)dst; if (s) memset(s->bits, 0, s->size); return D3D_OK; }
static void WINAPI IDirect3DDevice8_x_GetGammaRamp(IDirect3DDevice8 *This, D3DGAMMARAMP *r)
{ (void)This; for (int i = 0; i < 256; i++) r->red[i] = r->green[i] = r->blue[i] = (WORD)(i * 257); }

/* ---- IDirect3D8 ---- */
static HRESULT WINAPI IDirect3D8_x_QueryInterface(IDirect3D8 *This, REFIID riid, void **ppv) { (void)riid; if (!ppv) return E_POINTER; ((obj_t *)This)->ref++; *ppv = This; return S_OK; }
static ULONG WINAPI IDirect3D8_x_AddRef(IDirect3D8 *This) { return ++((obj_t *)This)->ref; }
static ULONG WINAPI IDirect3D8_x_Release(IDirect3D8 *This) { obj_t *o = (obj_t *)This; return o->ref ? --o->ref : 0; }
static UINT WINAPI IDirect3D8_x_GetAdapterCount(IDirect3D8 *This) { (void)This; return 1; }
static HRESULT WINAPI IDirect3D8_x_GetAdapterIdentifier(IDirect3D8 *This, UINT a, DWORD fl, D3DADAPTER_IDENTIFIER8 *id)
{
    (void)This; (void)fl; if (a) return D3DERR_INVALIDCALL;
    memset(id, 0, sizeof *id);
    strcpy(id->Driver, "rhost.dll"); strcpy(id->Description, "rhost headless device");
    return D3D_OK;
}
static UINT WINAPI IDirect3D8_x_GetAdapterModeCount(IDirect3D8 *This, UINT a) { (void)This; return a ? 0 : REF_NMODES; }
static HRESULT WINAPI IDirect3D8_x_EnumAdapterModes(IDirect3D8 *This, UINT a, UINT i, D3DDISPLAYMODE *m)
{
    (void)This; if (a || i >= REF_NMODES) return D3DERR_INVALIDCALL;
    m->Width = REF_MODES[i].w; m->Height = REF_MODES[i].h; m->RefreshRate = REF_MODES[i].hz; m->Format = (D3DFORMAT)REF_MODES[i].fmt;
    return D3D_OK;
}
static HRESULT WINAPI IDirect3D8_x_GetAdapterDisplayMode(IDirect3D8 *This, UINT a, D3DDISPLAYMODE *m)
{ (void)This; if (a) return D3DERR_INVALIDCALL; *m = REF_MODE; return D3D_OK; }
static long g_ref_miss;
static void ref_miss(const char *what, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ if (g_ref_miss++ < 40) rh_log("dev: %s(%u, %x, %u, %08x) is not in rh_dev_ref.h — answered NOTAVAILABLE (f%u)\n", what, a, b, c, d, g_frame); }
static HRESULT WINAPI IDirect3D8_x_CheckDeviceType(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, D3DFORMAT df, D3DFORMAT bf, WINBOOL win)
{
    (void)This; if (a) return D3DERR_INVALIDCALL;
    if (dt == D3DDEVTYPE_HAL && df == bf)
        for (size_t i = 0; i < sizeof REF_CDT / sizeof REF_CDT[0]; i++)
            if (REF_CDT[i].adfmt == (uint32_t)df && REF_CDT[i].windowed == (win != 0)) return (HRESULT)REF_CDT[i].hr;
    ref_miss("CheckDeviceType", dt, df, bf, win);
    return D3DERR_NOTAVAILABLE;
}
static HRESULT WINAPI IDirect3D8_x_CheckDeviceFormat(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, D3DFORMAT af, DWORD usage, D3DRESOURCETYPE rt, D3DFORMAT f)
{
    (void)This; if (a) return D3DERR_INVALIDCALL;
    if (dt == D3DDEVTYPE_HAL)
        for (size_t i = 0; i < sizeof REF_CDF / sizeof REF_CDF[0]; i++)
            if (REF_CDF[i].adfmt == (uint32_t)af && REF_CDF[i].usage == usage && REF_CDF[i].rtype == (uint32_t)rt && REF_CDF[i].fmt == (uint32_t)f)
                return (HRESULT)REF_CDF[i].hr;
    ref_miss("CheckDeviceFormat", af, usage, rt, f);
    return D3DERR_NOTAVAILABLE;
}
static HRESULT WINAPI IDirect3D8_x_CheckDeviceMultiSampleType(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, D3DFORMAT f, WINBOOL win, D3DMULTISAMPLE_TYPE ms)
{
    (void)This; if (a) return D3DERR_INVALIDCALL;
    if (dt == D3DDEVTYPE_HAL)
        for (size_t i = 0; i < sizeof REF_CDMS / sizeof REF_CDMS[0]; i++)
            if (REF_CDMS[i].adfmt == (uint32_t)f && REF_CDMS[i].windowed == (win != 0) && REF_CDMS[i].ms == (uint32_t)ms) return (HRESULT)REF_CDMS[i].hr;
    ref_miss("CheckDeviceMultiSampleType", dt, f, win, ms);
    return D3DERR_NOTAVAILABLE;
}
static HRESULT WINAPI IDirect3D8_x_CheckDepthStencilMatch(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df)
{
    (void)This; if (a) return D3DERR_INVALIDCALL;
    if (dt == D3DDEVTYPE_HAL)
        for (size_t i = 0; i < sizeof REF_CDSM / sizeof REF_CDSM[0]; i++)
            if (REF_CDSM[i].adfmt == (uint32_t)af && REF_CDSM[i].rtfmt == (uint32_t)rf && REF_CDSM[i].dsfmt == (uint32_t)df) return (HRESULT)REF_CDSM[i].hr;
    ref_miss("CheckDepthStencilMatch", af, rf, df, 0);
    return D3DERR_NOTAVAILABLE;
}
static HRESULT WINAPI IDirect3D8_x_GetDeviceCaps(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, D3DCAPS8 *caps)
{
    (void)This; if (a) return D3DERR_INVALIDCALL;
    memcpy(caps, dt == D3DDEVTYPE_REF ? REF_CAPS_REF : REF_CAPS_HAL, sizeof *caps);
    return D3D_OK;
}
static HMONITOR WINAPI IDirect3D8_x_GetAdapterMonitor(IDirect3D8 *This, UINT a) { (void)This; (void)a; return (HMONITOR)(uintptr_t)0x10001; }
static HRESULT WINAPI IDirect3D8_x_CreateDevice(IDirect3D8 *This, UINT a, D3DDEVTYPE dt, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    (void)This;
    if (a || !pp || !out) return D3DERR_INVALIDCALL;
    if (g_dev) { rh_log("dev: a second CreateDevice — returning the first device\n"); g_dev->o.ref++; *out = (IDirect3DDevice8 *)g_dev; return D3D_OK; }
    dev_t *d = obj_new(vt_IDirect3DDevice8, sizeof *d);
    g_dev = d;
    pp_fixup(pp, focus);
    d->pp = *pp; g_devwnd = pp->hDeviceWindow; d->pp.hDeviceWindow = NULL; g_focus = focus;
    d->cp.AdapterOrdinal = a; d->cp.DeviceType = dt; d->cp.BehaviorFlags = flags;
    for (int i = 0; i < N_XF; i++) d->xf[i] = IDENT;
    static const struct { uint16_t s; uint32_t v; } RS0[] = {   /* the D3D8 defaults a Get can see */
        { 7, 1 }, { 8, 3 }, { 9, 2 }, { 14, 1 }, { 16, 1 }, { 19, 2 }, { 20, 1 }, { 22, 3 }, { 23, 4 }, { 25, 8 }, { 29, 1 },
        { 36, 0x3f800000 }, { 137, 1 }, { 141, 1 }, { 145, 1 }, { 146, 2 }, { 147, 1 }, { 154, 0x3f800000 }, { 156, 0x3f800000 },
        { 158, 0x3f800000 }, { 162, 0xffffffff }, { 163, 0x3f800000 }, { 166, 0x42800000 }, { 168, 0xf }, { 171, 1 } };
    for (size_t i = 0; i < sizeof RS0 / sizeof RS0[0]; i++) d->rs[RS0[i].s] = RS0[i].v;
    for (int s = 0; s < 8; s++) {
        d->tss[s][D3DTSS_COLOROP] = s ? D3DTOP_DISABLE : D3DTOP_MODULATE; d->tss[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE; d->tss[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        d->tss[s][D3DTSS_ALPHAOP] = s ? D3DTOP_DISABLE : D3DTOP_SELECTARG1; d->tss[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; d->tss[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        d->tss[s][D3DTSS_TEXCOORDINDEX] = (DWORD)s; d->tss[s][D3DTSS_MAGFILTER] = D3DTEXF_POINT; d->tss[s][D3DTSS_MINFILTER] = D3DTEXF_POINT;
        d->tss[s][D3DTSS_MAXANISOTROPY] = 1;
    }
    dev_buffers(d);
    /* without D3DCREATE_FPU_PRESERVE the real CreateDevice leaves the calling thread at
     * single precision, CW 0x007F (engine quirk §138: retail's WinMain then sets PC53) */
    if (!(flags & D3DCREATE_FPU_PRESERVE)) { uint16_t cw = 0x007f; __asm__ volatile("fldcw %0" : : "m"(cw)); }
    rh_log("dev: CreateDevice type=%u flags=%lx %ux%u fmt=%u windowed=%d autoDS=%d/%u\n", (unsigned)dt, (unsigned long)flags, pp->BackBufferWidth,
           pp->BackBufferHeight, (unsigned)pp->BackBufferFormat, pp->Windowed, pp->EnableAutoDepthStencil, (unsigned)pp->AutoDepthStencilFormat);
    *out = (IDirect3DDevice8 *)d;
    return D3D_OK;
}
static IDirect3D8 *WINAPI my_Direct3DCreate8(UINT ver)
{
    (void)ver;
    if (!g_d3d) g_d3d = obj_new(vt_IDirect3D8, sizeof *g_d3d); else g_d3d->ref++;
    return (IDirect3D8 *)g_d3d;
}

/* ================= DirectMusic 8 =================
 * QueryInterface(.., NULL) = E_POINTER matters: FUN_00499200 passes the VALUE of its
 * segment-state slot (0) as the out pointer after PlaySegmentEx, so the slot stays 0.
 * The engine (docs/findings/audio-backend.md) creates a performance, three standard
 * audio paths and a loader, loads 21 BGM segments from files and 110 SE segments from
 * PE resources, and only then sets DAT_096430fc — the flag that gates its SE path, RNG
 * draws included. Every call succeeds here; nothing sounds. Playback state is never
 * reported as playing (retail under a real device at turbo speed cannot depend on it). */
SIMPLE_UNK(IDirectMusicPerformance8)
SIMPLE_UNK(IDirectMusicLoader8)
SIMPLE_UNK(IDirectMusicSegment8)
SIMPLE_UNK(IDirectMusicSegmentState8)
SIMPLE_UNK(IDirectMusicAudioPath)
static HRESULT WINAPI IDirectMusicPerformance8_x_InitAudio(IDirectMusicPerformance8 *This, IDirectMusic **dm, IDirectSound **ds, HWND w, DWORD path, DWORD ch, DWORD fl, DMUS_AUDIOPARAMS *ap)
{ (void)This; (void)w; (void)path; (void)ch; (void)fl; (void)ap; if (dm) *dm = NULL; if (ds) *ds = NULL; return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_CreateStandardAudioPath(IDirectMusicPerformance8 *This, DWORD type, DWORD ch, WINBOOL act, IDirectMusicAudioPath **out)
{ (void)This; (void)type; (void)ch; (void)act; *out = obj_new(vt_IDirectMusicAudioPath, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_PlaySegmentEx(IDirectMusicPerformance8 *This, IUnknown *src, WCHAR *name, IUnknown *trans, DWORD fl, __int64 start, IDirectMusicSegmentState **st, IUnknown *from, IUnknown *path)
{ (void)This; (void)src; (void)name; (void)trans; (void)fl; (void)start; (void)from; (void)path; if (st) *st = obj_new(vt_IDirectMusicSegmentState8, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_PlaySegment(IDirectMusicPerformance8 *This, IDirectMusicSegment *seg, DWORD fl, __int64 start, IDirectMusicSegmentState **st)
{ (void)This; (void)seg; (void)fl; (void)start; if (st) *st = obj_new(vt_IDirectMusicSegmentState8, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_IsPlaying(IDirectMusicPerformance8 *This, IDirectMusicSegment *seg, IDirectMusicSegmentState *st)
{ (void)This; (void)seg; (void)st; return S_FALSE; }
static HRESULT WINAPI IDirectMusicPerformance8_x_GetTime(IDirectMusicPerformance8 *This, REFERENCE_TIME *rt, MUSIC_TIME *mt)
{ (void)This; if (rt) *rt = (REFERENCE_TIME)g_frame * 166667; if (mt) *mt = (MUSIC_TIME)(g_frame * 26); return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_GetSegmentState(IDirectMusicPerformance8 *This, IDirectMusicSegmentState **st, MUSIC_TIME mt)
{ (void)This; (void)mt; if (st) *st = NULL; return DMUS_E_NOT_FOUND; }
static HRESULT WINAPI IDirectMusicPerformance8_x_GetDefaultAudioPath(IDirectMusicPerformance8 *This, IDirectMusicAudioPath **out)
{ (void)This; *out = obj_new(vt_IDirectMusicAudioPath, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicPerformance8_x_CreateAudioPath(IDirectMusicPerformance8 *This, IUnknown *cfg, WINBOOL act, IDirectMusicAudioPath **out)
{ (void)This; (void)cfg; (void)act; *out = obj_new(vt_IDirectMusicAudioPath, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicLoader8_x_GetObject(IDirectMusicLoader8 *This, LPDMUS_OBJECTDESC desc, REFIID riid, LPVOID *out)
{ (void)This; (void)desc; (void)riid; *out = obj_new(vt_IDirectMusicSegment8, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicLoader8_x_LoadObjectFromFile(IDirectMusicLoader8 *This, REFGUID cls, REFIID iid, WCHAR *path, void **out)
{ (void)This; (void)cls; (void)iid; (void)path; *out = obj_new(vt_IDirectMusicSegment8, sizeof(obj_t)); return S_OK; }
static HRESULT WINAPI IDirectMusicSegment8_x_GetLength(IDirectMusicSegment8 *This, MUSIC_TIME *mt) { (void)This; *mt = 768; return S_OK; }
static HRESULT WINAPI IDirectMusicSegment8_x_GetRepeats(IDirectMusicSegment8 *This, DWORD *n) { (void)This; *n = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicSegment8_x_GetStartPoint(IDirectMusicSegment8 *This, MUSIC_TIME *mt) { (void)This; *mt = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicSegment8_x_GetLoopPoints(IDirectMusicSegment8 *This, MUSIC_TIME *a, MUSIC_TIME *b) { (void)This; *a = 0; *b = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicSegmentState8_x_GetSeek(IDirectMusicSegmentState8 *This, MUSIC_TIME *mt) { (void)This; *mt = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicSegmentState8_x_GetStartTime(IDirectMusicSegmentState8 *This, MUSIC_TIME *mt) { (void)This; *mt = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicSegmentState8_x_GetRepeats(IDirectMusicSegmentState8 *This, DWORD *n) { (void)This; *n = 0; return S_OK; }
static HRESULT WINAPI IDirectMusicAudioPath_x_GetObjectInPath(IDirectMusicAudioPath *This, DWORD ch, DWORD stage, DWORD buf, REFGUID obj, DWORD idx, REFGUID iid, void **out)
{ (void)This; (void)ch; (void)stage; (void)buf; (void)obj; (void)idx; (void)iid; if (out) *out = NULL; return DMUS_E_NOT_FOUND; }

/* ================= DirectShow (the opening / attract movie) =================
 * FUN_0040cc87 / FUN_0040cf88 build a filter graph, RenderFile the .wmv into the game
 * window and poll IMediaEvent::WaitForCompletion(0) each tick (FUN_0040cea6). The fake
 * graph "plays" forever: the movie ends when input skips it, as in every recorded trace
 * (a real graph completes after the film's wall-clock length — not a function of ticks). */
typedef struct { obj_t o; } graph_t;
static const GUID k_IMediaControl = { 0x56a868b1, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_IBasicAudio   = { 0x56a868b3, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_IVideoWindow  = { 0x56a868b4, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_IMediaEvent   = { 0x56a868b6, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_IMediaEventEx = { 0x56a868c0, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_FilterGraph   = { 0xe436ebb3, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID k_DMPerformance = { 0xd2ac2881, 0xb39b, 0x11d1, { 0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd } };
static const GUID k_DMLoader      = { 0xd2ac2892, 0xb39b, 0x11d1, { 0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd } };
static HRESULT WINAPI IGraphBuilder_x_QueryInterface(IGraphBuilder *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    void *const *vt = !memcmp(riid, &k_IMediaControl, 16) ? vt_IMediaControl : !memcmp(riid, &k_IBasicAudio, 16) ? vt_IBasicAudio :
                      !memcmp(riid, &k_IVideoWindow, 16) ? vt_IVideoWindow :
                      (!memcmp(riid, &k_IMediaEvent, 16) || !memcmp(riid, &k_IMediaEventEx, 16)) ? vt_IMediaEventEx : NULL;
    if (!vt) {
        const uint32_t *g = (const uint32_t *)riid;
        rh_log("dev: IGraphBuilder::QueryInterface(%08x-%08x-%08x-%08x) refused (f%u)\n", g[0], g[1], g[2], g[3], g_frame);
        (void)This; *ppv = NULL; return E_NOINTERFACE;
    }
    *ppv = obj_new(vt, sizeof(obj_t));
    return S_OK;
}
static ULONG WINAPI IGraphBuilder_x_AddRef(IGraphBuilder *This) { return ++((obj_t *)This)->ref; }
static ULONG WINAPI IGraphBuilder_x_Release(IGraphBuilder *This) { obj_t *o = (obj_t *)This; uint32_t r = --o->ref; if (!r) dev_free(o); return r; }
SIMPLE_UNK(IMediaControl)
SIMPLE_UNK(IVideoWindow)
SIMPLE_UNK(IMediaEventEx)
SIMPLE_UNK(IBasicAudio)
static HRESULT WINAPI IMediaEventEx_x_WaitForCompletion(IMediaEventEx *This, LONG ms, LONG *code) { (void)This; (void)ms; if (code) *code = 0; return E_ABORT; }
static HRESULT WINAPI IMediaEventEx_x_GetEvent(IMediaEventEx *This, LONG *code, LONG_PTR *p1, LONG_PTR *p2, LONG ms)
{ (void)This; (void)ms; if (code) *code = 0; if (p1) *p1 = 0; if (p2) *p2 = 0; return E_ABORT; }
static HRESULT WINAPI IMediaControl_x_GetState(IMediaControl *This, LONG ms, OAFilterState *st) { (void)This; (void)ms; if (st) *st = 2 /* State_Running */; return S_OK; }
static HRESULT WINAPI IBasicAudio_x_get_Volume(IBasicAudio *This, LONG *v) { (void)This; *v = 0; return S_OK; }

static HRESULT WINAPI my_CoCreateInstance(REFCLSID cls, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out)
{
    void *const *vt = !memcmp(cls, &k_DMPerformance, 16) ? vt_IDirectMusicPerformance8 : !memcmp(cls, &k_DMLoader, 16) ? vt_IDirectMusicLoader8 :
                      !memcmp(cls, &k_FilterGraph, 16) ? vt_IGraphBuilder : NULL;
    if (!vt) {
        const uint32_t *g = (const uint32_t *)cls;
        rh_log("dev: CoCreateInstance(%08x-%08x-%08x-%08x) is not faked — passed to the system (f%u)\n", g[0], g[1], g[2], g[3], g_frame);
        return CoCreateInstance(cls, outer, ctx, iid, out);
    }
    *out = obj_new(vt, sizeof(graph_t));
    return S_OK;
}

/* ================= DirectInput 8 =================
 * Only the boot-time setup reaches the device (FUN_0047af52); the per-tick poll is replaced
 * by the scripted mask (rhost.c hook_input). Acquire answers what the real keyboard does
 * for rhost's never-activated window: E_ACCESSDENIED. */
SIMPLE_UNK(IDirectInput8A)
SIMPLE_UNK(IDirectInputDevice8A)
static HRESULT WINAPI IDirectInput8A_x_CreateDevice(IDirectInput8A *This, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN outer)
{ (void)This; (void)g; (void)outer; *out = obj_new(vt_IDirectInputDevice8A, sizeof(obj_t)); return DI_OK; }
static HRESULT WINAPI IDirectInputDevice8A_x_Acquire(IDirectInputDevice8A *This) { (void)This; return (HRESULT)0x80070005L; }
static HRESULT WINAPI IDirectInputDevice8A_x_GetDeviceState(IDirectInputDevice8A *This, DWORD n, LPVOID p) { (void)This; memset(p, 0, n); return DI_OK; }
static HRESULT WINAPI IDirectInputDevice8A_x_GetDeviceData(IDirectInputDevice8A *This, DWORD sz, LPDIDEVICEOBJECTDATA d, LPDWORD n, DWORD fl)
{ (void)This; (void)sz; (void)d; (void)fl; if (n) *n = 0; return DI_OK; }
static HRESULT WINAPI my_DirectInput8Create(HINSTANCE h, DWORD ver, REFIID iid, LPVOID *out, LPUNKNOWN outer)
{ (void)h; (void)ver; (void)iid; (void)outer; *out = obj_new(vt_IDirectInput8A, sizeof(obj_t)); return DI_OK; }

#include "rh_dev_gen.h"

static void dev_note(int i) { g_note_calls[i]++; }
void rh_dev_report(void)
{
    if (!O.headless) return;
    char b[2048]; size_t o = 0; int n = 0;
    for (int i = 0; i < DEV_NOTE_COUNT; i++)
        if (g_note_calls[i]) { n++; if (o < sizeof b - 96) o += snprintf(b + o, sizeof b - o, " %s×%u", g_note_names[i], g_note_calls[i]); }
    rh_log("dev: defaulted (S_OK stub) methods called: %d%s\n", n, n ? b : "");
    rh_log("dev: region top=+%uK live=%u blocks %uK allocs=%u frees=%u ref-misses=%ld\n", (DH->top - RH_DEV_LO) >> 10, DH->live, DH->bytes >> 10,
           DH->nalloc, DH->nfree, g_ref_miss);
}

void rh_dev_install(void)
{
    if (VirtualAlloc((void *)(uintptr_t)RH_DEV_LO, RH_DEV_HI - RH_DEV_LO, MEM_RESERVE, PAGE_READWRITE) != (void *)(uintptr_t)RH_DEV_LO)
        rh_fatal("rh_dev: cannot reserve the DEV region %08x..%08x (err %lu)", RH_DEV_LO, RH_DEV_HI, GetLastError());
    g_dev_committed = RH_DEV_LO;
    dev_commit(RH_DEV_LO + 0x1000);
    DH->magic = DEV_MAGIC; DH->top = RH_DEV_LO + 0x1000;
    iat_override("Direct3DCreate8", my_Direct3DCreate8);
    iat_override("CoCreateInstance", my_CoCreateInstance);
    iat_override("DirectInput8Create", my_DirectInput8Create);
    rh_log("dev: headless devices (D3D8 %ux%u@%u fmt %u, DirectMusic, DirectShow, DirectInput) — no GPU, no audio\n",
           REF_MODE.Width, REF_MODE.Height, REF_MODE.RefreshRate, (unsigned)REF_MODE.Format);
}
