/* GX-06 kitchen-sink fixture — a single controlled frame that issues EVERY non-RT
 * recorded D3D8 opcode the census tracks, so the corpus has a synthetic fixture that
 * exercises each one's record→replay plumbing in isolation (roadmap parity-evidence
 * §9 GX-06; the RT/CopyRects opcodes are gx06_rt_fixture's job).
 *
 * Opcodes emitted (container ids in orv3_format.h):
 *   DEV_PARAMS(1) RES_TEX(2) RES_VB(3) RES_IB(4)
 *   SetRenderState(10) SetTextureStageState(11) SetTransform(12) SetMaterial(13)
 *   SetTexture(14) SetStreamSource(15) SetIndices(16) SetVertexShader(17)
 *   DrawPrimitive(18) DrawIndexedPrimitive(19) DrawPrimitiveUP(20) DrawIndexedPrimitiveUP(21)
 *   Clear(22) SetLight(23) LightEnable(24) BeginScene(25) EndScene(26) Present(27)
 *
 * Two geometry regimes keep the frame fully-covered (deterministic backbuffer) while
 * making the 3D-state opcodes LOAD-BEARING, not decorative:
 *  - a LIT, TEXTURED, TRANSFORMED quad (FVF XYZ|NORMAL|TEX1) drawn from the VB (both
 *    DrawPrimitive and DrawIndexedPrimitive via the IB) — SetTransform/SetMaterial/
 *    SetLight/LightEnable/SetTexture all affect its pixels;
 *  - pretransformed colored triangles (FVF XYZRHW|DIFFUSE) via DrawPrimitiveUP and
 *    DrawIndexedPrimitiveUP — inline geometry, no transform/light dependence, so they
 *    are bit-exact by construction and cover the two UP draw paths.
 * Capture and replay run the SAME HAL on the SAME machine, so even the lit draw is
 * bit-exact (arrprobe's lit HOUSE verifies 80/80) — the corpus fixture test asserts it.
 *
 * Mirrors gx04_fixture's harness contract: argv[1] = Windows output dir, set in-process
 * via _putenv before Direct3DCreate8; keep present 0 (capframe=0/capcount=1). Exit 0 on
 * a clean render; the host test (test_gx06_sink_fixture.py) checks <outdir>/v3cap.bin for
 * every opcode + replays v3ref_000.raw bit-exact. Build:
 *   nix develop --command make gx06_sink_fixture.exe   (in this dir) */
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FVF_LIT (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)
#define FVF_RHW (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
typedef struct { float x, y, z, nx, ny, nz, u, v; } LitVtx;
typedef struct { float x, y, z, rhw; DWORD color; } RhwVtx;

static int fail(const char *msg) { fprintf(stderr, "gx06_sink_fixture: FAIL %s\n", msg); return 2; }

/* a column-major-agnostic identity/simple matrix helper (D3DMATRIX is row-major m[r][c]) */
static void mat_identity(D3DMATRIX *m)
{
    memset(m, 0, sizeof *m);
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}
/* orthographic-ish projection mapping a small world region into clip space; exact
 * geometry is irrelevant — we only need the transform pipeline to run deterministically. */
static void mat_ortho(D3DMATRIX *m)
{
    mat_identity(m);
    m->_11 = 1.2f; m->_22 = 1.2f; m->_33 = 0.5f; m->_43 = 0.25f;
}

int main(int argc, char **argv)
{
    if (argc > 1) _putenv_s("OPENRECET_V3_OUT", argv[1]);
    _putenv_s("OPENRECET_V3_CAPFRAME", "0");
    _putenv_s("OPENRECET_V3_CAPCOUNT", "1");

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = hinst;
    wc.lpszClassName = "gx06sink"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("gx06sink", "gx06sink", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                              NULL, NULL, hinst, NULL);
    if (!hwnd) return fail("CreateWindow");

    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) return fail("Direct3DCreate8");
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 128; pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D16;
    IDirect3DDevice8 *dev = NULL;
    if (FAILED(IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)) || !dev)
        return fail("CreateDevice");

    /* ── a 4x4 A8R8G8B8 texture with a fixed pattern (RES_TEX) ── */
    IDirect3DTexture8 *tex = NULL;
    if (FAILED(IDirect3DDevice8_CreateTexture(dev, 4, 4, 1, 0, D3DFMT_A8R8G8B8,
                                              D3DPOOL_MANAGED, &tex)) || !tex)
        return fail("CreateTexture");
    { D3DLOCKED_RECT lr;
      if (FAILED(IDirect3DTexture8_LockRect(tex, 0, &lr, NULL, 0))) return fail("tex Lock");
      for (int y = 0; y < 4; y++) {
          DWORD *row = (DWORD *)((BYTE *)lr.pBits + (size_t)y * lr.Pitch);
          for (int x = 0; x < 4; x++) row[x] = 0xff000000u | ((DWORD)(x * 60) << 16) | ((DWORD)(y * 60) << 8) | 0x40u;
      }
      IDirect3DTexture8_UnlockRect(tex, 0);
    }

    /* ── VB: a lit textured quad (TL,TR,BL,BR) (RES_VB) ── */
    IDirect3DVertexBuffer8 *vb = NULL;
    if (FAILED(IDirect3DDevice8_CreateVertexBuffer(dev, 4 * sizeof(LitVtx), 0, FVF_LIT,
                                                   D3DPOOL_MANAGED, &vb)) || !vb)
        return fail("CreateVertexBuffer");
    { BYTE *p = NULL;
      if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) || !p) return fail("VB Lock");
      LitVtx q[4] = {
          { -0.6f,  0.6f, 0.5f, 0, 0, -1, 0, 0 },
          {  0.6f,  0.6f, 0.5f, 0, 0, -1, 1, 0 },
          { -0.6f, -0.6f, 0.5f, 0, 0, -1, 0, 1 },
          {  0.6f, -0.6f, 0.5f, 0, 0, -1, 1, 1 },
      };
      memcpy(p, q, sizeof q);
      IDirect3DVertexBuffer8_Unlock(vb);
    }

    /* ── IB: two triangles over the quad (RES_IB) ── */
    IDirect3DIndexBuffer8 *ib = NULL;
    if (FAILED(IDirect3DDevice8_CreateIndexBuffer(dev, 6 * sizeof(WORD), 0, D3DFMT_INDEX16,
                                                  D3DPOOL_MANAGED, &ib)) || !ib)
        return fail("CreateIndexBuffer");
    { BYTE *p = NULL;
      if (FAILED(IDirect3DIndexBuffer8_Lock(ib, 0, 0, &p, 0)) || !p) return fail("IB Lock");
      WORD idx[6] = { 0, 1, 2, 2, 1, 3 };
      memcpy(p, idx, sizeof idx);
      IDirect3DIndexBuffer8_Unlock(ib);
    }

    /* ── fixed-function state (all recorded opcodes) ── */
    D3DMATRIX m;
    mat_identity(&m); IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, &m);
    mat_identity(&m); IDirect3DDevice8_SetTransform(dev, D3DTS_VIEW, &m);
    mat_ortho(&m);    IDirect3DDevice8_SetTransform(dev, D3DTS_PROJECTION, &m);

    D3DMATERIAL8 mtl = {0};
    mtl.Diffuse.r = mtl.Diffuse.g = mtl.Diffuse.b = mtl.Diffuse.a = 1.0f;
    mtl.Ambient.r = mtl.Ambient.g = mtl.Ambient.b = 0.4f; mtl.Ambient.a = 1.0f;
    IDirect3DDevice8_SetMaterial(dev, &mtl);

    D3DLIGHT8 lt = {0};
    lt.Type = D3DLIGHT_DIRECTIONAL;
    lt.Diffuse.r = lt.Diffuse.g = lt.Diffuse.b = 1.0f;
    lt.Direction.x = 0.0f; lt.Direction.y = 0.0f; lt.Direction.z = 1.0f;
    IDirect3DDevice8_SetLight(dev, 0, &lt);
    IDirect3DDevice8_LightEnable(dev, 0, TRUE);

    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff404040u);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SPECULARENABLE, FALSE);

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex);

    IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           0xff101828u, 1.0f, 0);
    IDirect3DDevice8_BeginScene(dev);

    /* lit textured quad — DrawPrimitive (strip) and DrawIndexedPrimitive (list) from the VB */
    IDirect3DDevice8_SetVertexShader(dev, FVF_LIT);
    IDirect3DDevice8_SetStreamSource(dev, 0, vb, sizeof(LitVtx));
    IDirect3DDevice8_DrawPrimitive(dev, D3DPT_TRIANGLESTRIP, 0, 2);
    IDirect3DDevice8_SetIndices(dev, ib, 0);
    IDirect3DDevice8_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0, 4, 0, 2);

    /* pretransformed colored tris — stage 0 disabled so they are pure gouraud (no
     * texcoord dependence), bit-exact by construction; covers the two UP draw paths */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice8_SetVertexShader(dev, FVF_RHW);
    RhwVtx tri[3] = {
        {  4.0f,  4.0f, 0.5f, 1.0f, 0xffff2020u },
        { 60.0f,  4.0f, 0.5f, 1.0f, 0xff20ff20u },
        {  4.0f, 60.0f, 0.5f, 1.0f, 0xff2020ffu },
    };
    IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, 1, tri, sizeof(RhwVtx));

    RhwVtx up2[4] = {
        {  70.0f,  70.0f, 0.5f, 1.0f, 0xffffff20u },
        { 124.0f,  70.0f, 0.5f, 1.0f, 0xff20ffffu },
        {  70.0f, 124.0f, 0.5f, 1.0f, 0xffff20ffu },
        { 124.0f, 124.0f, 0.5f, 1.0f, 0xffffffffu },
    };
    WORD upidx[6] = { 0, 1, 2, 2, 1, 3 };
    IDirect3DDevice8_DrawIndexedPrimitiveUP(dev, D3DPT_TRIANGLELIST, 0, 4, 2, upidx,
                                            D3DFMT_INDEX16, up2, sizeof(RhwVtx));

    IDirect3DDevice8_EndScene(dev);
    IDirect3DDevice8_Present(dev, NULL, NULL, NULL, NULL);

    IDirect3DTexture8_Release(tex);
    IDirect3DIndexBuffer8_Release(ib);
    IDirect3DVertexBuffer8_Release(vb);
    IDirect3DDevice8_Release(dev);
    IDirect3D8_Release(d3d);
    DestroyWindow(hwnd);
    printf("gx06_sink_fixture: OK (kitchen-sink frame rendered + presented)\n");
    return 0;
}
