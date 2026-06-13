/* OpenRecet Trace Studio v3 — capture container format (shared: proxy + replay).
 *
 * A flat, streaming, self-describing record sequence. The proxy WRITES records
 * as calls happen; the replayer READS them sequentially and re-issues. A
 * resource (texture/VB/IB) is written the first time its CONTENT is seen
 * (assigned an id, content-hash dedup'd); later references cite the id. Calls
 * cite resources by id.
 *
 * MULTI-FRAME (v2): a container holds a WINDOW of frames, not just one. Each
 * kept frame is a section [new RES…][scalar-state preamble][this frame's calls]
 * [Present]; resources are dedup'd across the WHOLE window by content hash, so a
 * mesh/texture bound every frame is stored ONCE — the window's storage stays
 * ≈ one frame's resources + per-frame call deltas. Present records delimit
 * sections; the replayer renders any kept-frame INDEX (0-based) standalone (its
 * preamble supplies the inherited device state).
 *
 * Streaming (no seeking, no global tables) keeps both producer and consumer
 * trivial. Endianness = native (both ends are i686). Each record: [u32 type]
 * then a fixed payload per type.
 */
#ifndef ORV3_FORMAT_H
#define ORV3_FORMAT_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORV3_MAGIC   0x33565241u   /* "ARV3" */
#define ORV3_VERSION 3u            /* v3: render-target capture (RT textures + SetRenderTarget + CopyRects) */

enum {
    ORV3_DEV_PARAMS = 1,   /* w,h,bbfmt,depthfmt,windowed,bbcount,presentflags,behavior,interval,adapter,devtype */
    ORV3_RES_TEX    = 2,   /* id,levels, per level: w,h,fmt,datalen,data[] (tight rows) */
    ORV3_RES_VB     = 3,   /* id,size,fvf,datalen,data[] */
    ORV3_RES_IB     = 4,   /* id,size,fmt,datalen,data[] */
    ORV3_RES_RT_TEX = 5,   /* id,w,h,fmt,levels,usage — a render-target texture (CreateTexture
                            * usage&RENDERTARGET, D3DPOOL_DEFAULT). NO pixel data: an RT is
                            * identified by IDENTITY (creation), not content — content-hash dedup
                            * would alias two same-size RTs and never carry their (unlockable)
                            * pixels. The replayer recreates it with RENDERTARGET usage; its
                            * content is produced by the replayed SetRenderTarget/draw/CopyRects
                            * stream (history mode), not stored. */

    ORV3_SetRenderState        = 10,  /* state,value */
    ORV3_SetTextureStageState  = 11,  /* stage,type,value */
    ORV3_SetTransform          = 12,  /* state, 16 floats */
    ORV3_SetMaterial           = 13,  /* 17 floats */
    ORV3_SetTexture            = 14,  /* stage, resid (-1 = NULL) */
    ORV3_SetStreamSource       = 15,  /* stream, resid, stride */
    ORV3_SetIndices            = 16,  /* resid, basevertex */
    ORV3_SetVertexShader       = 17,  /* handle */
    ORV3_DrawPrimitive         = 18,  /* pt, startvertex, primcount */
    ORV3_DrawIndexedPrimitive  = 19,  /* pt, minidx, numv, starti, primcount */
    ORV3_DrawPrimitiveUP       = 20,  /* pt, primcount, stride, datalen, data[] */
    ORV3_DrawIndexedPrimitiveUP= 21,  /* pt, minvi, numvi, primcount, idxfmt, idxlen, idx[], stride, vlen, v[] */
    ORV3_Clear                 = 22,  /* count, flags, color, z(float bits as u32), stencil */
    ORV3_SetLight              = 23,  /* index, sizeof(D3DLIGHT8), bytes[] */
    ORV3_LightEnable           = 24,  /* index, enable */
    ORV3_BeginScene            = 25,
    ORV3_EndScene              = 26,
    ORV3_Present               = 27,  /* frame-end marker, payload: frame index */

    /* ── render-target ops (v3) ── A surface is cited by a SURFREF = two u32s
     * [kind][resid]: the replayer reconstructs the actual IDirect3DSurface8* from
     * the kind (the app's GetBackBuffer/GetDepthStencilSurface/GetSurfaceLevel
     * results are NOT replayed — the replayer holds those handles itself). */
    ORV3_SetRenderTarget       = 28,  /* color SURFREF, depth SURFREF */
    ORV3_CopyRects             = 29,  /* src SURFREF, dst SURFREF, count, rects[count*16B], points[count*8B] */

    ORV3_EOF                   = 99,
};

/* SURFREF kinds (the [kind] half of a citation; the [resid] half is the RT
 * texture's resource id for ORV3_SURF_TEX, else 0). The proxy classifies each
 * surface pointer at call time (backbuffer/depth refs + IDirect3DSurface8::
 * GetContainer for texture-backed RTs); level is assumed 0 (the pause RTs are
 * 1-level, the only RT shape we hit). */
enum {
    ORV3_SURF_NULL       = 0,   /* NULL surface (e.g. SetRenderTarget depth = NULL) */
    ORV3_SURF_BACKBUFFER = 1,   /* the swapchain backbuffer (replayer: GetBackBuffer) */
    ORV3_SURF_DEPTH      = 2,   /* the auto depth-stencil (replayer: GetDepthStencilSurface) */
    ORV3_SURF_TEX        = 3,   /* an RT texture's surface (replayer: GetSurfaceLevel(tex[resid],0)) */
};

static inline int orv3_fmt_bpp(int f)
{
    switch (f) {
    case 21: /* D3DFMT_A8R8G8B8 */
    case 22: /* D3DFMT_X8R8G8B8 */ return 4;
    case 23: /* R5G6B5 */
    case 24: /* X1R5G5B5 */
    case 25: /* A1R5G5B5 */
    case 26: /* A4R4G4B4 */
    case 40: /* A8L8 */            return 2;
    case 28: /* A8 */
    case 50: /* L8 */             return 1;
    default:                       return 0;   /* unknown / compressed */
    }
}

/* tiny write helpers */
static inline void orv3_wu(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static inline void orv3_wbytes(FILE *f, const void *p, uint32_t n) { fwrite(&n, 4, 1, f); if (n) fwrite(p, 1, n, f); }

/* tiny read helpers (replayer) */
static inline uint32_t orv3_ru(FILE *f) { uint32_t v = 0; if (fread(&v, 4, 1, f) != 1) return 0xffffffffu; return v; }

/* ── shared backbuffer readback (proxy reference + replayer output) ──
 * Returns a freshly malloc'd, tightly-packed w*h*4 BGRA buffer (caller frees) and
 * reports the backbuffer dims. The backbuffer may be NON-lockable — retail ships
 * its swapchain with present flags=0x0 (no D3DPRESENTFLAG_LOCKABLE_BACKBUFFER), so
 * a direct LockRect fails — so we bounce the backbuffer through a lockable sysmem
 * image surface via CopyRects (the exact path the v2 Frida agent uses to grab
 * retail frames). The port (flags=0x1, lockable) reads back identically through
 * this same path. Using ONE helper on both the capture (reference) and replay
 * (output) sides guarantees the two frames are read back bit-identically, so the
 * compare is fair. Only compiled where d3d8 + COBJMACROS are in scope. */
#ifdef IDirect3DDevice8_CopyRects
static inline uint8_t *orv3_readback_bgra(IDirect3DDevice8 *dev, uint32_t *ow, uint32_t *oh)
{
    IDirect3DSurface8 *bb = NULL, *sys = NULL; uint8_t *dst = NULL;
    if (FAILED(IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return NULL;
    D3DSURFACE_DESC d = {0}; IDirect3DSurface8_GetDesc(bb, &d);
    if (SUCCEEDED(IDirect3DDevice8_CreateImageSurface(dev, d.Width, d.Height, d.Format, &sys)) && sys
     && SUCCEEDED(IDirect3DDevice8_CopyRects(dev, bb, NULL, 0, sys, NULL))) {
        D3DLOCKED_RECT lr = {0};
        if (SUCCEEDED(IDirect3DSurface8_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
            uint32_t rb = d.Width * 4u;
            dst = (uint8_t*)malloc((size_t)rb * d.Height);
            if (dst) for (uint32_t r = 0; r < d.Height; r++)
                memcpy(dst + (size_t)r*rb, (const uint8_t*)lr.pBits + (size_t)r*lr.Pitch, rb);
            IDirect3DSurface8_UnlockRect(sys);
        }
    }
    if (sys) IDirect3DSurface8_Release(sys);
    IDirect3DSurface8_Release(bb);
    if (dst) { *ow = d.Width; *oh = d.Height; }
    return dst;
}
#endif

#endif
