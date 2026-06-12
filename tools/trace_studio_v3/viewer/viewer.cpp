// Trace Studio v3 — native viewer (N0 toolchain spike).
//
// Proves the native-UI stack: Dear ImGui + Win32 + Direct3D9, cross-built 32-bit
// with mingw (i686-w64-mingw32-g++). 32-bit because the replay core (next step)
// loads the REAL 32-bit d3d8.dll the game used — the viewer process must match.
// The d3d9 device hosts the UI; the d3d8 replay (N1) renders frames into textures
// it uploads. d3d8 and d3d9 are independent COM objects ⇒ both live in one process.
//
// Two modes:
//   viewer.exe                 interactive window (the real viewer; N2 fills it in)
//   viewer.exe --shot out.bmp  headless: render ONE UI frame offscreen + read back +
//                              save a BMP — lets the build loop self-verify the UI
//                              renders (push the BMP to the feed) with no display.
//
// Build: nix develop --command make -C tools/trace_studio_v3/viewer
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include <d3d9.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static LPDIRECT3D9       g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp;

static bool create_device(HWND hwnd, UINT W, UINT H)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;
    ZeroMemory(&g_pp, sizeof(g_pp));
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_pp.BackBufferWidth = W;
    g_pp.BackBufferHeight = H;
    g_pp.EnableAutoDepthStencil = TRUE;
    g_pp.AutoDepthStencilFormat = D3DFMT_D16;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // no vsync — scrub at replay speed
    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev));
}

static void destroy_device()
{
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_d3d) { g_d3d->Release(); g_d3d = nullptr; }
}

// the spike's placeholder UI (N2 replaces this with the real 3-panel viewer)
static void draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("trace studio v3 — native viewer");
    ImGui::TextColored(ImVec4(0.42f, 0.71f, 1.0f, 1.0f), "N0 toolchain spike");
    ImGui::Separator();
    ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
    ImGui::Text("renderer : Direct3D9 (host)  ·  d3d8 replay core lands in N1");
    ImGui::Text("build    : mingw i686 (32-bit, matches the real d3d8.dll)");
    ImGui::Spacing();
    ImGui::BulletText("no PNG bake, no stale intermediates");
    ImGui::BulletText("frames replayed on demand from the container");
    ImGui::BulletText("scrub at replay speed (faster than realtime)");
    ImGui::Spacing();
    ImGui::Text("FPS %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
}

// write a 32-bit BGRA top-down surface as a (bottom-up) BMP — self-contained, no deps,
// universally viewable + feed-pushable. `pitch` is the locked-surface row stride.
static bool save_bmp(const char *path, const uint8_t *bgra, int W, int H, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t img = (uint32_t)W * H * 4;
    const uint32_t off = 14 + 40;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)&hdr[2]  = off + img;
    *(uint32_t *)&hdr[10] = off;
    *(uint32_t *)&hdr[14] = 40;
    *(int32_t  *)&hdr[18] = W;
    *(int32_t  *)&hdr[22] = H;            // positive ⇒ bottom-up
    *(uint16_t *)&hdr[26] = 1;
    *(uint16_t *)&hdr[28] = 32;
    *(uint32_t *)&hdr[34] = img;
    fwrite(hdr, 1, 54, f);
    for (int r = H - 1; r >= 0; r--)
        fwrite(bgra + (size_t)r * pitch, 1, (size_t)W * 4, f);
    fclose(f);
    return true;
}

static void imgui_init(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;        // no imgui.ini sidecar
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_dev);
}

static void render_frame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_ui();
    ImGui::EndFrame();
    ImGui::Render();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                 D3DCOLOR_XRGB(20, 22, 26), 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_dev->EndScene();
    }
}

// ── headless: render one UI frame to an offscreen device + save a BMP ──
static int do_shot(const char *out, int W, int H)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer_shot";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "orv3", WS_OVERLAPPEDWINDOW,
                              0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, W, H)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    render_frame();   // one frame

    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    int rc = 2;
    if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb
     && SUCCEEDED(g_dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8,
            D3DPOOL_SYSTEMMEM, &sys, nullptr)) && sys
     && SUCCEEDED(g_dev->GetRenderTargetData(bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            rc = save_bmp(out, (const uint8_t *)lr.pBits, W, H, lr.Pitch) ? 0 : 2;
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release();
    if (bb) bb->Release();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    DestroyWindow(hwnd);
    fprintf(stderr, rc == 0 ? "shot -> %s (%dx%d)\n" : "shot FAILED\n", out, W, H);
    return rc;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, msg, wp, lp);
}

static int do_interactive()
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "orv3viewer";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "trace studio v3", WS_OVERLAPPEDWINDOW,
                              100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, 1280, 800)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    bool running = true;
    while (running) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
            if (m.message == WM_QUIT) running = false;
        }
        if (!running) break;
        render_frame();
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--shot") == 0)
        return do_shot(argv[2], 720, 460);
    return do_interactive();
}
