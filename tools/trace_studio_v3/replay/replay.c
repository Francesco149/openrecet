/* OpenRecet Trace Studio v3 — replayer (P0c go/no-go).
 *
 * Reads an orv3 capture container, creates a real D3D8 device with the captured
 * params, recreates every resource, replays ONE kept frame's recorded call
 * stream, reads back the backbuffer, and compares it BYTE-FOR-BYTE to that
 * frame's proxy reference. Bit-exact == the v3 replay bet holds.
 *
 * MULTI-FRAME: a container holds a window of kept frames (delimited by Present
 * records). Pick which one with the optional 0-based <frame-index>. Single pass:
 * every RES is created as seen (so the target's resources — possibly introduced
 * in an earlier frame and dedup'd — always exist), but only the TARGET section's
 * preamble+calls are ISSUED; non-target sections are parsed-and-skipped. The
 * target's preamble supplies its inherited device state ⇒ it renders standalone.
 *
 * Build: i686-w64-mingw32-gcc (links the REAL d3d8). Run from a dir WITHOUT the
 * proxy d3d8.dll. argv: <cap.bin> <ref.raw> [frame-index] [out.raw]
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

#define MAXRES 32768   /* matches the proxy's ORV3_MAXRES — a busy HOUSE frame can bind many unique resources */
static IDirect3DTexture8      *g_tex[MAXRES];
static IDirect3DVertexBuffer8 *g_vb [MAXRES];
static IDirect3DIndexBuffer8  *g_ib [MAXRES];

static uint32_t ru(FILE *f) { uint32_t v = 0; if (fread(&v, 4, 1, f) != 1) return 0xffffffffu; return v; }
static void *rbytes(FILE *f, uint32_t *lenout)
{ uint32_t n = ru(f); *lenout = n; if (!n) return NULL; void *p = malloc(n); if (fread(p, 1, n, f) != n) { free(p); return NULL; } return p; }

#define CK(call, what) do { HRESULT _hr=(call); if(FAILED(_hr)){fprintf(stderr,"FAIL %s hr=0x%08lx\n",what,(unsigned long)_hr); return 2;} } while(0)

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: replay <cap.bin> <ref.raw> [frame-index] [out.raw]\n"); return 2; }
    const char *cappath = argv[1], *refpath = argv[2];
    int target = argc > 3 ? atoi(argv[3]) : 0;   /* 0-based kept-frame index to render */
    const char *outpath = argc > 4 ? argv[4] : "v3replay.raw";

    FILE *f = fopen(cappath, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", cappath); return 2; }
    if (ru(f) != ORV3_MAGIC) { fprintf(stderr, "bad magic\n"); return 2; }
    ru(f); /* version */

    /* first record must be DEV_PARAMS */
    if (ru(f) != ORV3_DEV_PARAMS) { fprintf(stderr, "expected DEV_PARAMS\n"); return 2; }
    uint32_t W = ru(f), H = ru(f), bbfmt = ru(f), depthfmt = ru(f), windowed = ru(f),
             bbcount = ru(f), presentflags = ru(f), behavior = ru(f), interval = ru(f),
             adapter = ru(f), devtype = ru(f), autods = ru(f);
    fprintf(stderr, "params: %ux%u bbfmt=%u depth=%u windowed=%u flags=0x%x behavior=0x%x autods=%u\n",
            W, H, bbfmt, depthfmt, windowed, presentflags, behavior, autods);

    /* hidden window + device */
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "orv3replay"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("orv3replay", "orv3", WS_OVERLAPPEDWINDOW, 0, 0, W, H, NULL, NULL, wc.hInstance, NULL);
    MSG m; while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }

    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) { fprintf(stderr, "Direct3DCreate8 NULL\n"); return 2; }
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = W; pp.BackBufferHeight = H; pp.BackBufferFormat = bbfmt;
    pp.BackBufferCount = bbcount ? bbcount : 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = windowed ? TRUE : FALSE; pp.Flags = presentflags;
    pp.EnableAutoDepthStencil = autods ? TRUE : FALSE; pp.AutoDepthStencilFormat = depthfmt;
    pp.hDeviceWindow = hwnd;
    IDirect3DDevice8 *dev = NULL;
    CK(IDirect3D8_CreateDevice(d3d, adapter, (D3DDEVTYPE)devtype, hwnd, behavior, &pp, &dev), "CreateDevice");

    /* replay loop — single pass. Resources are created as seen (regardless of
     * section, so the target's dedup'd resources always exist); state/draw calls
     * are ISSUED only while in the target section (sect == target). Sections are
     * delimited by Present records; we stop at the target's Present. */
    unsigned ncalls = 0, ndraws = 0, nres = 0, sect = 0;
    int did_target = 0;
    for (;;) {
        uint32_t op = ru(f);
        if (op == 0xffffffffu || op == ORV3_EOF) break;
        int exec = (sect == (unsigned)target);   /* issue this record's call? */
        switch (op) {
        case ORV3_RES_TEX: {
            uint32_t id = ru(f), levels = ru(f);
            IDirect3DTexture8 *tex = NULL;
            /* read level 0 dims to create; then fill all levels */
            long after_levels = ftell(f); (void)after_levels;
            /* peek first level dims */
            uint32_t w0 = ru(f), h0 = ru(f), fmt0 = ru(f), rb0 = ru(f), dl0 = ru(f);
            CK(IDirect3DDevice8_CreateTexture(dev, w0, h0, levels, 0, (D3DFORMAT)fmt0, D3DPOOL_MANAGED, &tex), "CreateTexture");
            /* fill level 0 (already read its header; data follows) */
            for (uint32_t lvl = 0; lvl < levels; lvl++) {
                uint32_t lw, lh, lf, lrb, ld;
                if (lvl == 0) { lw=w0; lh=h0; lf=fmt0; lrb=rb0; ld=dl0; }
                else { lw=ru(f); lh=ru(f); lf=ru(f); lrb=ru(f); ld=ru(f); }
                unsigned char *buf = ld ? malloc(ld) : NULL;
                if (ld && fread(buf, 1, ld, f) != ld) { fprintf(stderr,"tex read short\n"); return 2; }
                if (ld && lrb) {
                    D3DLOCKED_RECT lr = {0};
                    if (SUCCEEDED(IDirect3DTexture8_LockRect(tex, lvl, &lr, NULL, 0))) {
                        for (uint32_t r = 0; r < lh; r++)
                            memcpy((unsigned char*)lr.pBits + (size_t)r*lr.Pitch, buf + (size_t)r*lrb, lrb);
                        IDirect3DTexture8_UnlockRect(tex, lvl);
                    }
                }
                free(buf);
            }
            if (id < MAXRES) g_tex[id] = tex; nres++;
            break; }
        case ORV3_RES_VB: {
            uint32_t id = ru(f), size = ru(f), fvf = ru(f), dl; void *data = rbytes(f, &dl);
            IDirect3DVertexBuffer8 *vb = NULL;
            CK(IDirect3DDevice8_CreateVertexBuffer(dev, size, 0, fvf, D3DPOOL_MANAGED, &vb), "CreateVertexBuffer");
            BYTE *p = NULL; if (SUCCEEDED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) && p) { if (data) memcpy(p, data, dl); IDirect3DVertexBuffer8_Unlock(vb); }
            free(data); if (id < MAXRES) g_vb[id] = vb; nres++;
            break; }
        case ORV3_RES_IB: {
            uint32_t id = ru(f), size = ru(f), fmt = ru(f), dl; void *data = rbytes(f, &dl);
            IDirect3DIndexBuffer8 *ib = NULL;
            CK(IDirect3DDevice8_CreateIndexBuffer(dev, size, 0, (D3DFORMAT)fmt, D3DPOOL_MANAGED, &ib), "CreateIndexBuffer");
            BYTE *p = NULL; if (SUCCEEDED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, 0)) && p) { if (data) memcpy(p, data, dl); IDirect3DIndexBuffer8_Unlock(ib); }
            free(data); if (id < MAXRES) g_ib[id] = ib; nres++;
            break; }
        case ORV3_SetRenderState: { uint32_t s=ru(f),v=ru(f); if(exec){IDirect3DDevice8_SetRenderState(dev,(D3DRENDERSTATETYPE)s,v); ncalls++;} break; }
        case ORV3_SetTextureStageState: { uint32_t st=ru(f),t=ru(f),v=ru(f); if(exec){IDirect3DDevice8_SetTextureStageState(dev,st,(D3DTEXTURESTAGESTATETYPE)t,v); ncalls++;} break; }
        case ORV3_SetTransform: { uint32_t s=ru(f); D3DMATRIX mx; fread(&mx,sizeof(float),16,f); if(exec){IDirect3DDevice8_SetTransform(dev,(D3DTRANSFORMSTATETYPE)s,&mx); ncalls++;} break; }
        case ORV3_SetMaterial: { D3DMATERIAL8 mt; fread(&mt,sizeof(float),17,f); if(exec){IDirect3DDevice8_SetMaterial(dev,&mt); ncalls++;} break; }
        case ORV3_SetTexture: { uint32_t stage=ru(f); int32_t id=(int32_t)ru(f); if(exec){IDirect3DDevice8_SetTexture(dev,stage,(id>=0&&id<MAXRES)?(IDirect3DBaseTexture8*)g_tex[id]:NULL); ncalls++;} break; }
        case ORV3_SetStreamSource: { uint32_t stream=ru(f); int32_t id=(int32_t)ru(f); uint32_t stride=ru(f); if(exec){IDirect3DDevice8_SetStreamSource(dev,stream,(id>=0&&id<MAXRES)?g_vb[id]:NULL,stride); ncalls++;} break; }
        case ORV3_SetIndices: { int32_t id=(int32_t)ru(f); uint32_t base=ru(f); if(exec){IDirect3DDevice8_SetIndices(dev,(id>=0&&id<MAXRES)?g_ib[id]:NULL,base); ncalls++;} break; }
        case ORV3_SetVertexShader: { uint32_t h=ru(f); if(exec){IDirect3DDevice8_SetVertexShader(dev,h); ncalls++;} break; }
        case ORV3_DrawPrimitive: { uint32_t pt=ru(f),sv=ru(f),pc=ru(f); if(exec){IDirect3DDevice8_DrawPrimitive(dev,(D3DPRIMITIVETYPE)pt,sv,pc); ncalls++; ndraws++;} break; }
        case ORV3_DrawIndexedPrimitive: { uint32_t pt=ru(f),mi=ru(f),nv=ru(f),si=ru(f),pc=ru(f); if(exec){IDirect3DDevice8_DrawIndexedPrimitive(dev,(D3DPRIMITIVETYPE)pt,mi,nv,si,pc); ncalls++; ndraws++;} break; }
        case ORV3_DrawPrimitiveUP: { uint32_t pt=ru(f),pc=ru(f),stride=ru(f),dl; void*v=rbytes(f,&dl); if(exec){IDirect3DDevice8_DrawPrimitiveUP(dev,(D3DPRIMITIVETYPE)pt,pc,v,stride); ncalls++; ndraws++;} free(v); break; }
        case ORV3_DrawIndexedPrimitiveUP: { uint32_t pt=ru(f),mvi=ru(f),nvi=ru(f),pc=ru(f),ifmt=ru(f),il; void*idx=rbytes(f,&il); uint32_t stride=ru(f),vl; void*v=rbytes(f,&vl);
            if(exec){IDirect3DDevice8_DrawIndexedPrimitiveUP(dev,(D3DPRIMITIVETYPE)pt,mvi,nvi,pc,idx,(D3DFORMAT)ifmt,v,stride); ncalls++; ndraws++;} free(idx); free(v); break; }
        case ORV3_Clear: { uint32_t count=ru(f); D3DRECT *rects=NULL; if(count){rects=malloc(count*sizeof(D3DRECT)); fread(rects,sizeof(D3DRECT),count,f);} uint32_t flags=ru(f),color=ru(f),zb=ru(f),stencil=ru(f); float z; memcpy(&z,&zb,4); if(exec){IDirect3DDevice8_Clear(dev,count,rects,flags,color,z,stencil); ncalls++;} free(rects); break; }
        case ORV3_SetLight: { uint32_t index=ru(f),dl; void*L=rbytes(f,&dl); if(exec&&L){IDirect3DDevice8_SetLight(dev,index,(D3DLIGHT8*)L); ncalls++;} free(L); break; }
        case ORV3_LightEnable: { uint32_t index=ru(f),en=ru(f); if(exec){IDirect3DDevice8_LightEnable(dev,index,en); ncalls++;} break; }
        case ORV3_BeginScene: if(exec){IDirect3DDevice8_BeginScene(dev); ncalls++;} break;
        case ORV3_EndScene: if(exec){IDirect3DDevice8_EndScene(dev); ncalls++;} break;
        case ORV3_Present: { ru(f); /* present-count payload (info) — do NOT flip; keep backbuffer for readback */
            if (sect == (unsigned)target) { did_target = 1; goto done_replay; }
            sect++; break; }
        default: fprintf(stderr, "unknown op %u @ %ld\n", op, ftell(f)); return 2;
        }
    }
done_replay:
    fclose(f);
    if (!did_target) { fprintf(stderr, "frame index %d not found (container has %u kept frame(s))\n", target, sect); return 2; }
    fprintf(stderr, "replayed frame index %d: %u resources created, %u calls (%u draws) issued\n", target, nres, ncalls, ndraws);

    /* read back the backbuffer via the SAME shared helper the proxy used for the
     * reference (CopyRects through a lockable sysmem surface) — so both frames are
     * read back bit-identically and the compare is fair, and so a non-lockable
     * replay backbuffer (retail params: flags=0x0) reads back at all. */
    uint32_t rb = W * 4, gw = 0, gh = 0;
    unsigned char *mine = orv3_readback_bgra(dev, &gw, &gh);
    if (!mine) { fprintf(stderr, "FAIL readback (GetBackBuffer/CopyRects/Lock)\n"); return 2; }
    if (gw != W || gh != H) { fprintf(stderr, "readback dims %ux%u != %ux%u\n", gw, gh, W, H); return 2; }

    /* write out.raw for visual inspection */
    FILE *of = fopen(outpath, "wb"); if (of) { fwrite(&W,4,1,of); fwrite(&H,4,1,of); fwrite(mine,1,(size_t)rb*H,of); fclose(of); }

    /* compare to reference */
    FILE *rf = fopen(refpath, "rb"); if (!rf) { fprintf(stderr, "no ref %s\n", refpath); return 2; }
    uint32_t rw = ru(rf), rh = ru(rf);
    if (rw != W || rh != H) { fprintf(stderr, "ref dims %ux%u != %ux%u\n", rw, rh, W, H); return 2; }
    unsigned char *ref = malloc((size_t)rb * H);
    if (fread(ref, 1, (size_t)rb*H, rf) != (size_t)rb*H) { fprintf(stderr, "ref short\n"); return 2; }
    fclose(rf);

    size_t total = (size_t)rb * H, ndiff = 0; unsigned maxd = 0; size_t difpx = 0;
    for (size_t i = 0; i < total; i++) { int d = abs((int)mine[i]-(int)ref[i]); if (d) { ndiff++; if((unsigned)d>maxd)maxd=d; } }
    for (size_t px = 0; px < (size_t)W*H; px++) { const unsigned char *a=mine+px*4,*b=ref+px*4; if(a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2]||a[3]!=b[3]) difpx++; }

    printf("\n==== P0c REPLAY BIT-EXACT CHECK ====\n");
    printf("  frame: %ux%u  bytes: %zu\n", W, H, total);
    printf("  differing bytes : %zu (%.4f%%)\n", ndiff, 100.0*ndiff/total);
    printf("  differing pixels: %zu / %u (%.4f%%)\n", difpx, W*H, 100.0*difpx/((double)W*H));
    printf("  max byte delta  : %u\n", maxd);
    printf("  VERDICT: %s\n", ndiff==0 ? "BIT-EXACT  *** GO ***" : (difpx*100.0/((double)W*H) < 0.5 ? "near-exact (investigate residual)" : "DIVERGENT"));
    return ndiff==0 ? 0 : 1;
}
