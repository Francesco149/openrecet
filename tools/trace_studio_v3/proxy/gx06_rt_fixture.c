/* GX-06 render-target fixture — a single frame that exercises the render-target opcodes
 * the kitchen-sink fixture (gx06_sink) does NOT: RES_RT_TEX(5), SetRenderTarget(28),
 * CopyRects(29), and all four SURFREF kinds (NULL/BACKBUFFER/DEPTH/TEX). SetRenderTarget
 * and the SURFREF kinds are OBSERVED in the pause backdrop (a real proof exists); CopyRects
 * is UNOBSERVED in every cached scene (the sweep) so this synthetic frame is its ONLY
 * capture — the corpus records it as fixture-covered-but-no-observed-use.
 *
 * The frame renders into an RT texture, composites it onto the backbuffer, then CopyRects
 * a corner of the RT directly onto the backbuffer — so the final backbuffer depends on BOTH
 * the sampled-RT draw AND the CopyRects, and the bit-exact replay check genuinely exercises
 * each. Structure (D3D8: SetRenderTarget/CopyRects OUTSIDE BeginScene/EndScene):
 *   SetRenderTarget(rtSurf, NULL)          — SURF_TEX color, SURF_NULL depth
 *   BeginScene; Clear(rt); draw gradient; EndScene       (fill the RT)
 *   SetRenderTarget(backbuffer, depth)     — SURF_BACKBUFFER color, SURF_DEPTH depth
 *   BeginScene; Clear(bb); SetTexture(rt); draw textured quad; EndScene   (composite)
 *   CopyRects(rtSurf 32x32 corner -> backbuffer)         (SURF_TEX -> SURF_BACKBUFFER)
 *   Present
 *
 * Mirrors the gx04/gx06_sink harness contract: argv[1] = Windows output dir via in-process
 * _putenv; keep present 0. Exit 0 on a clean render; test_gx06_rt_fixture.py checks
 * v3cap.bin for the RT opcodes + every SURFREF kind + replays v3ref_000.raw bit-exact.
 * Build: nix develop --command make gx06_rt_fixture.exe (in this dir). */
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define FVF_RHW  (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
#define FVF_RHWT (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
typedef struct { float x, y, z, rhw; DWORD color; } RhwVtx;
typedef struct { float x, y, z, rhw; DWORD color; float u, v; } RhwTVtx;

static int fail(const char *msg) { fprintf(stderr, "gx06_rt_fixture: FAIL %s\n", msg); return 2; }

int main(int argc, char **argv)
{
    if (argc > 1) _putenv_s("OPENRECET_V3_OUT", argv[1]);
    _putenv_s("OPENRECET_V3_CAPFRAME", "0");
    _putenv_s("OPENRECET_V3_CAPCOUNT", "1");

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = hinst;
    wc.lpszClassName = "gx06rt"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("gx06rt", "gx06rt", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
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

    /* an RT texture matching the backbuffer format (X8R8G8B8), DEFAULT pool (RES_RT_TEX) */
    IDirect3DTexture8 *rt = NULL;
    if (FAILED(IDirect3DDevice8_CreateTexture(dev, 128, 128, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &rt)) || !rt)
        return fail("CreateTexture(RT)");
    IDirect3DSurface8 *rtSurf = NULL, *bbSurf = NULL, *dsSurf = NULL;
    if (FAILED(IDirect3DTexture8_GetSurfaceLevel(rt, 0, &rtSurf)) || !rtSurf) return fail("RT GetSurfaceLevel");
    if (FAILED(IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bbSurf)) || !bbSurf) return fail("GetBackBuffer");
    if (FAILED(IDirect3DDevice8_GetDepthStencilSurface(dev, &dsSurf)) || !dsSurf) return fail("GetDepthStencil");

    IDirect3DDevice8_SetVertexShader(dev, FVF_RHW);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* ── Phase A: render a gradient into the RT (color SURF_TEX, depth SURF_NULL) ── */
    IDirect3DDevice8_SetRenderTarget(dev, rtSurf, NULL);
    IDirect3DDevice8_BeginScene(dev);
    IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff203040u, 1.0f, 0);
    { RhwVtx grad[3] = {
          {   2.0f,   2.0f, 0.5f, 1.0f, 0xffff0000u },
          { 126.0f,   2.0f, 0.5f, 1.0f, 0xff00ff00u },
          {  64.0f, 126.0f, 0.5f, 1.0f, 0xff0000ffu },
      };
      IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, 1, grad, sizeof(RhwVtx));
    }
    IDirect3DDevice8_EndScene(dev);

    /* ── Phase B: restore backbuffer (SURF_BACKBUFFER + SURF_DEPTH) + composite the RT ── */
    IDirect3DDevice8_SetRenderTarget(dev, bbSurf, dsSurf);
    IDirect3DDevice8_BeginScene(dev);
    IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000u, 1.0f, 0);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)rt);
    IDirect3DDevice8_SetVertexShader(dev, FVF_RHWT);
    { RhwTVtx quad[4] = {
          {   0.0f,   0.0f, 0.5f, 1.0f, 0xffffffffu, 0.0f, 0.0f },
          { 128.0f,   0.0f, 0.5f, 1.0f, 0xffffffffu, 1.0f, 0.0f },
          {   0.0f, 128.0f, 0.5f, 1.0f, 0xffffffffu, 0.0f, 1.0f },
          { 128.0f, 128.0f, 0.5f, 1.0f, 0xffffffffu, 1.0f, 1.0f },
      };
      IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(RhwTVtx));
    }
    IDirect3DDevice8_SetTexture(dev, 0, NULL);
    IDirect3DDevice8_EndScene(dev);

    /* ── Phase C: CopyRects a 32x32 corner of the RT directly onto the backbuffer
     * (SURF_TEX src -> SURF_BACKBUFFER dst); overwrites a patch so the final frame
     * depends on the copy — outside BeginScene/EndScene per D3D8 rules ── */
    { RECT sr = { 0, 0, 32, 32 }; POINT dp = { 96, 0 };
      HRESULT hr = IDirect3DDevice8_CopyRects(dev, rtSurf, &sr, 1, bbSurf, &dp);
      if (FAILED(hr)) fprintf(stderr, "gx06_rt_fixture: WARN CopyRects hr=0x%08lx\n", (unsigned long)hr);
    }

    IDirect3DDevice8_Present(dev, NULL, NULL, NULL, NULL);

    IDirect3DSurface8_Release(dsSurf);
    IDirect3DSurface8_Release(bbSurf);
    IDirect3DSurface8_Release(rtSurf);
    IDirect3DTexture8_Release(rt);
    IDirect3DDevice8_Release(dev);
    IDirect3D8_Release(d3d);
    DestroyWindow(hwnd);
    printf("gx06_rt_fixture: OK (RT render + composite + CopyRects presented)\n");
    return 0;
}
