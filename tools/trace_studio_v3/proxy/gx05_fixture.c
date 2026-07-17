/* GX-05 acceptance fixture — proves the v3 proxy's resource dedup is BYTE-COMPARE, not
 * hash-trust: even when EVERY resource lands in the same FNV-64 bucket (forced via the
 * OPENRECET_V3_TEST_FORCE_COLLISION seam), distinct contents stay DISTINCT and identical
 * content still DEDUPS. This is the roadmap GX-05 acceptance "forced hash collision fixture
 * remains distinct" (parity-evidence-roadmap §9 GX-05).
 *
 * A real FNV-64 collision is infeasible to construct (2^64), so the proxy reads
 * OPENRECET_V3_TEST_FORCE_COLLISION (set below, in-process, before Direct3DCreate8) and
 * makes fnv1a return a constant ⇒ ALL content collides into one hash bucket. dedup_or_write
 * must then rely SOLELY on the byte-compare to tell contents apart.
 *
 * One kept frame (capframe=0), one VB, four binds of the SAME pointer with contents
 * A, B, A, C. A correct (byte-compare) capture stores exactly THREE distinct RES_VB and
 * binds resids [0, 1, 0, 2]:
 *   - split A|B  : bind-0 (A) and bind-1 (B) differ ⇒ two versions (byte-compare rejects the
 *                  hash "match" of B against A);
 *   - dedup A    : bind-2 (A again) byte-matches bind-0 ⇒ no new record (id 0);
 *   - split C    : bind-3 (C) differs from BOTH A and B (byte-compare walks past both
 *                  hash-equal entries) ⇒ a third version (id 2).
 * The OLD hash-only dedup under this forced collision would store ONE record for all four
 * (every content "matches" the first bucket entry) ⇒ ids [0,0,0,0], aliasing B and C onto A
 * — exactly the false dedup GX-05 makes impossible.
 *
 * argv[1] = output dir (a Windows path). Exit 0 on a clean render; the host test
 * (test_gx05_fixture.py) inspects <outdir>/v3cap.bin for the three RES_VB + resid sequence.
 * Build: `nix develop --command make gx05_fixture.exe` in this dir (the host test copies the
 * proxy d3d8.dll next to the exe so Direct3DCreate8 loads the proxy). */
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
typedef struct { float x, y, z, rhw; DWORD color; } Vtx;

/* one triangle; `k` (0=A,1=B,2=C) picks a distinct geometry+color so the three contents
 * differ byte-for-byte (same length ⇒ dedup must compare bytes, not just size). */
static void fill_triangle(Vtx *v, int k)
{
    float o = (float)(k * 40);
    DWORD c = k == 0 ? 0xffff0000u : k == 1 ? 0xff00ff00u : 0xff0000ffu;
    v[0] = (Vtx){ 10.0f + o, 10.0f, 0.5f, 1.0f, c };
    v[1] = (Vtx){ 50.0f + o, 10.0f, 0.5f, 1.0f, c };
    v[2] = (Vtx){ 30.0f + o, 50.0f, 0.5f, 1.0f, c };
}

static int fail(const char *msg) { fprintf(stderr, "gx05_fixture: FAIL %s\n", msg); return 2; }

int main(int argc, char **argv)
{
    if (argc > 1) _putenv_s("OPENRECET_V3_OUT", argv[1]);   /* proxy reads this in Direct3DCreate8 */
    _putenv_s("OPENRECET_V3_CAPFRAME", "0");                /* keep the very first present */
    _putenv_s("OPENRECET_V3_CAPCOUNT", "1");
    _putenv_s("OPENRECET_V3_TEST_FORCE_COLLISION", "1");    /* GX-05: all content into one bucket */

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = hinst;
    wc.lpszClassName = "gx05fixture"; RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("gx05fixture", "gx05", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
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

    /* four binds of the SAME vb, contents A, B, A, C — mutate between each via Lock/Unlock */
    static const int seq[4] = { 0, 1, 0, 2 };   /* A, B, A, C */
    for (int i = 0; i < 4; i++) {
        BYTE *p = NULL;
        if (FAILED(IDirect3DVertexBuffer8_Lock(vb, 0, 0, &p, 0)) || !p) return fail("Lock");
        fill_triangle((Vtx *)p, seq[i]);
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
    printf("gx05_fixture: OK (4 binds A,B,A,C rendered + presented, forced-collision)\n");
    return 0;
}
