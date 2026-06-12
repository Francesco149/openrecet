// Trace Studio v3 — native viewer.
//
// Dear ImGui + Win32 + Direct3D9, cross-built 32-bit with mingw (so the process
// matches the real 32-bit d3d8.dll the replay core loads). The d3d9 device hosts the
// UI; the d3d8 replay core (replay_core.{c,h}) renders kept frames on demand into a
// sysmem BGRA buffer that we upload to a d3d9 texture (~3 ms/frame resident) and show
// with ImGui::Image. No PNG bake, no stale intermediates: the container is the only
// artifact, frames are replayed live.
//
//   viewer.exe <container.bin>              interactive scrub of a captured window
//   viewer.exe --shot out.bmp <container>   headless: render one UI frame + save BMP
//
// (N1 shows ONE container's frames; N2 adds the port|retail|diff timeline + UX.)
// Build: nix develop --command make -C tools/trace_studio_v3/viewer
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "replay_core.h"
#include <d3d9.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static LPDIRECT3D9       g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp;

// the open container (N1: one; N2: port + retail)
static OrV3Replay       *g_rep = nullptr;
static LPDIRECT3DTEXTURE9 g_tex = nullptr;
static int g_count = 0, g_w = 0, g_h = 0, g_idx = 0;
static const char *g_path = nullptr;
static double g_last_upload_ms = 0;

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
    if (g_tex) { g_tex->Release(); g_tex = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_d3d) { g_d3d->Release(); g_d3d = nullptr; }
}

// replay kept frame `idx` (d3d8) → upload BGRA to the d3d9 panel texture (d3d9).
static void upload(int idx)
{
    LARGE_INTEGER fq, a, b; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&a);
    const uint8_t *buf = orv3_replay_render(g_rep, idx);
    if (!buf || !g_tex) return;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(g_tex->LockRect(0, &lr, nullptr, 0))) {
        for (int r = 0; r < g_h; r++)
            memcpy((uint8_t *)lr.pBits + (size_t)r * lr.Pitch,
                   buf + (size_t)r * g_w * 4, (size_t)g_w * 4);
        g_tex->UnlockRect(0);
    }
    QueryPerformanceCounter(&b);
    g_last_upload_ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;
}

static bool open_container(const char *path)
{
    char err[128] = {0};
    g_rep = orv3_replay_open(path, err, sizeof err);
    if (!g_rep) { fprintf(stderr, "open %s: %s\n", path, err); return false; }
    g_count = orv3_replay_count(g_rep);
    g_w = orv3_replay_width(g_rep);
    g_h = orv3_replay_height(g_rep);
    // X8R8G8B8 (not A8…): the replayed backbuffer has no alpha (X8 ⇒ A bytes = 0); an
    // X8 texture samples alpha as 1.0 so ImGui draws it OPAQUE (an A8 texture would draw
    // it fully transparent). MANAGED ⇒ survives device reset (resize); locked per scrub.
    if (FAILED(g_dev->CreateTexture(g_w, g_h, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &g_tex, nullptr)))
        return false;
    upload(0);
    return true;
}

static void draw_ui()
{
    if (!g_rep) {
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
        ImGui::Begin("trace studio v3");
        ImGui::TextColored(ImVec4(0.42f, 0.71f, 1.0f, 1.0f), "no container");
        ImGui::Text("usage: viewer.exe <container.bin>");
        ImGui::End();
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("trace studio v3", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::TextColored(ImVec4(0.42f, 0.71f, 1.0f, 1.0f), "trace studio v3");
    ImGui::SameLine();
    ImGui::Text(" · %dx%d · %d frames · replay %.2f ms · %.0f fps",
                g_w, g_h, g_count, g_last_upload_ms, ImGui::GetIO().Framerate);

    int prev = g_idx;
    if (ImGui::Button("|<")) g_idx = 0;
    ImGui::SameLine(); if (ImGui::Button("<") && g_idx > 0) g_idx--;
    ImGui::SameLine(); ImGui::SetNextItemWidth(-160);
    ImGui::SliderInt("##frame", &g_idx, 0, g_count - 1);
    ImGui::SameLine(); if (ImGui::Button(">") && g_idx < g_count - 1) g_idx++;
    ImGui::SameLine(); if (ImGui::Button(">|")) g_idx = g_count - 1;
    ImGui::SameLine(); ImGui::Text("%d/%d", g_idx, g_count - 1);
    if (g_idx != prev) upload(g_idx);

    ImGui::Text("frame %d: %d draws / %d calls", g_idx,
                orv3_replay_draws(g_rep, g_idx), orv3_replay_calls(g_rep, g_idx));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = avail.x / g_w;
    if (scale > 1.0f) scale = 1.0f;
    if (scale <= 0.0f) scale = 0.5f;
    ImGui::Image((ImTextureID)(intptr_t)g_tex, ImVec2(g_w * scale, g_h * scale));
    ImGui::End();
}

static bool save_bmp(const char *path, const uint8_t *bgra, int W, int H, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t img = (uint32_t)W * H * 4, off = 14 + 40;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)&hdr[2]  = off + img;
    *(uint32_t *)&hdr[10] = off;
    *(uint32_t *)&hdr[14] = 40;
    *(int32_t  *)&hdr[18] = W;
    *(int32_t  *)&hdr[22] = H;          // positive ⇒ bottom-up
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
    ImGui::GetIO().IniFilename = nullptr;
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
                 D3DCOLOR_XRGB(16, 18, 22), 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_dev->EndScene();
    }
}

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
    if (g_path && !open_container(g_path)) { fprintf(stderr, "open_container failed\n"); return 2; }
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
    if (g_rep) orv3_replay_close(g_rep);
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

// keyboard scrub: ,/. ±1 · ←/→ ±10 · Home/End
static void handle_keys()
{
    if (!g_rep) return;
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureKeyboard && ImGui::IsAnyItemActive()) return;
    int prev = g_idx;
    if (ImGui::IsKeyPressed(ImGuiKey_Comma)) g_idx--;
    if (ImGui::IsKeyPressed(ImGuiKey_Period)) g_idx++;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) g_idx -= 10;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) g_idx += 10;
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) g_idx = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End)) g_idx = g_count - 1;
    if (g_idx < 0) g_idx = 0;
    if (g_idx > g_count - 1) g_idx = g_count - 1;
    if (g_idx != prev) upload(g_idx);
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
                              80, 60, 1180, 920, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, 1180, 920)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (g_path && !open_container(g_path)) { fprintf(stderr, "open_container failed\n"); return 2; }
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
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        handle_keys();
        draw_ui();
        ImGui::EndFrame();
        ImGui::Render();
        g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(16, 18, 22), 1.0f, 0);
        if (SUCCEEDED(g_dev->BeginScene())) {
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_dev->EndScene();
        }
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    if (g_rep) orv3_replay_close(g_rep);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *shot = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else g_path = argv[i];
    }
    if (shot) return do_shot(shot, 1100, 900);
    return do_interactive();
}
