/* GX-04 acceptance fixture — proves the v3 proxy's freeze-at-bind versioning SPLITS a
 * same-frame VB mutation into two distinct captured versions (and DEDUPS a re-bind of
 * identical content). The arrprobe re-drive proves the wrapper is TRANSPARENT on static
 * buffers (80/80 bit-exact) but its buffers never mutate mid-frame (RES_VB stays 5); this
 * synthetic frame exercises the POSITIVE path the roadmap GX-03 acceptance names.
 *
 * One kept frame (capframe=0), one VB, three binds of the SAME pointer with contents
 * A, B, A. A correct capture stores exactly TWO distinct RES_VB records:
 *   - split on mutation: bind-1 (A) and bind-2 (B) differ ⇒ two versions;
 *   - dedup on identity: bind-3 (A again) matches bind-1 ⇒ no third record.
 * The old frame-end snapshot would have stored ONE record (the buffer's end-of-frame
 * content) for all three binds — the bug GX-04 fixes.
 *
 * The proxy's output dir + capture window are set IN-PROCESS via _putenv before
 * Direct3DCreate8 (the proxy reads them there), so the harness needs no env crossing.
 * argv[1] = output dir (a Windows path). Exit 0 on a clean render; the host test
 * (test_gx04_fixture.py) inspects <outdir>/v3cap.bin for the two RES_VB. Build:
 * `nix develop --command make gx04_fixture.exe` in this dir (stages nothing — the host
 * test copies the proxy d3d8.dll next to the exe so Direct3DCreate8 loads the proxy). */
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
typedef struct { float x, y, z, rhw; DWORD color; } Vtx;

/* one triangle; `k` picks a distinct geometry+color so content A(k=0) != B(k=1) */
static void fill_triangle(Vtx *v, int k)
{
    float o = k ? 40.0f : 0.0f;
    DWORD c = k ? 0xff00ff00u : 0xffff0000u;
    v[0] = (Vtx){ 10.0f + o, 10.0f, 0.5f, 1.0f, c };
    v[1] = (Vtx){ 50.0f + o, 10.0f, 0.5f, 1.0f, c };
    v[2] = (Vtx){ 30.0f + o, 50.0f, 0.5f, 1.0f, c };
}

static int fail(const char *msg) { fprintf(stderr, "gx04_fixture: FAIL %s\n", msg); return 2; }

int main(int argc, char **argv)
{
    if (argc > 1) _putenv_s("OPENRECET_V3_OUT", argv[1]);   /* proxy reads this in Direct3DCreate8 */
    _putenv_s("OPENRECET_V3_CAPFRAME", "0");                /* keep the very first present */
    _putenv_s("OPENRECET_V3_CAPCOUNT", "1");

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = hinst;
    wc.lpszClassName = "gx04fixture"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("gx04fixture", "gx04", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                              NULL, NULL, hinst, NULL);
    if (!hwnd) return fail("CreateWindow");

    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) return fail("Direct3DCreate8");
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 128; pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = hwnd;
    IDirect3DDevice8 *dev = NULL;
    if (FAILED(IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)) || !dev)
        return fail("CreateDevice");

    IDirect3DVertexBuffer8 *vb = NULL;
    if (FAILED(IDirect3DDevice8_CreateVertexBuffer(dev, 3 * sizeof(Vtx), 0, FVF,
                                                   D3DPOOL_MANAGED, &vb)) || !vb)
        return fail("CreateVertexBuffer");

    IDirect3DDevice8_SetVertexShader(dev, FVF);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000u, 1.0f, 0);
    IDirect3DDevice8_BeginScene(dev);

    /* three binds of the SAME vb, contents A, B, A — mutate between each via Lock/Unlock */
    for (int k = 0; k < 3; k++) {
        BYTE *p = NULL;
        if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) || !p) return fail("Lock");
        fill_triangle((Vtx *)p, k == 1 ? 1 : 0);   /* A, B, A */
        IDirect3DVertexBuffer8_Unlock(vb);
        IDirect3DDevice8_SetStreamSource(dev, 0, vb, sizeof(Vtx));
        IDirect3DDevice8_DrawPrimitive(dev, D3DPT_TRIANGLELIST, 0, 1);
    }

    IDirect3DDevice8_EndScene(dev);
    IDirect3DDevice8_Present(dev, NULL, NULL, NULL, NULL);   /* present 0 → proxy keeps this frame */

    IDirect3DVertexBuffer8_Release(vb);
    IDirect3DDevice8_Release(dev);
    IDirect3D8_Release(d3d);
    DestroyWindow(hwnd);
    printf("gx04_fixture: OK (3 binds A,B,A rendered + presented)\n");
    return 0;
}
